// server.dll: each campaign's own characters (core/hc_campaign_models.h says why and which). as a level loads, before
// its entities precache anything, the game folder gets the versions the map's campaign plays (half-life 2's, episode
// one's or lost coast's), read from that campaign's own content, or none on episode two's maps, whose merged content is
// its own. only when that set changes: the model cache lets go of every model involved first, then the files go and
// come, each checked against what it was read from, and halfcraft-campaign-models.txt, written last, says what's
// there. a level the client still has entities of (a changelevel across campaigns typed into the console) keeps the
// set it has: the client's models point into the cache's data. once the level is in, the log says which version of
// each character it plays, and hc_debug_model shows any entity's.

#include "cbase.h"
#include "datacache/imdlcache.h"
#include "engine/ivmodelinfo.h"
#include "filesystem.h"
#include "studio.h"
#include "tier0/threadtools.h"
#include "tier1/utlbuffer.h"

#include "tier0/valve_minmax_off.h"
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/hc_campaign_models.h"
#include "core/hc_log.h"
#include "core/hc_module.h"
#include "shared/hc_bridge.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// baseentity.cpp: reloads modelsounds.cache, the sounds each model's animation events play, by its file name
void ClearModelSoundsCache();

namespace
{
	using halfcraft::CampaignModelState;
	using halfcraft::ModelSet;

	constexpr const char* GAME_FOLDER = "DEFAULT_WRITE_PATH";  // the game folder: first on GAME
	constexpr const char* STATE_FILE = "halfcraft-campaign-models.txt";
	constexpr const char* MODEL_SOUNDS_CACHE = "modelsounds.cache";  // in the game folder (MOD)
	constexpr int         REMOVE_TRIES = 5;                          // an antivirus may hold a file it just saw written
	constexpr unsigned    REMOVE_RETRY_MS = 50;
	constexpr std::size_t LOG_LINE = 850;  // what one log line holds of a list (hc_log's lines are 1 kB)

	/// the game's files, through the engine's file system.
	class GameFiles final : public halfcraft::ModelFileSource
	{
	public:
		bool exists(const std::string& name, const char* path_id) const override
		{
			return filesystem->FileExists(name.c_str(), path_id);
		}

		long size(const std::string& name, const char* path_id) const override
		{
			return exists(name, path_id) ? static_cast<long>(filesystem->Size(name.c_str(), path_id)) : -1;
		}

		bool read(const std::string& name, const char* path_id, std::vector<std::uint8_t>& out, std::size_t max_bytes) const override
		{
			CUtlBuffer buffer;
			if (!filesystem->ReadFile(name.c_str(), path_id, buffer, static_cast<int>(max_bytes))) {
				return false;
			}
			const auto* bytes = static_cast<const std::uint8_t*>(buffer.Base());
			out.assign(bytes, bytes + buffer.TellPut());
			return true;
		}
	};

	/// "models/alyx.mdl" as the log shows it: alyx
	std::string short_name(std::string name)
	{
		for (char& c : name) {
			c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		if (name.rfind("models/", 0) == 0) {
			name.erase(0, 7);
		}
		if (name.size() > 4 && name.compare(name.size() - 4, 4, ".mdl") == 0) {
			name.erase(name.size() - 4);
		}
		return name;
	}

	std::string hex(std::uint32_t value)
	{
		char text[16];
		V_snprintf(text, sizeof(text), "%08x", static_cast<unsigned>(value));
		return text;
	}

	const char* load_type()
	{
		switch (gpGlobals->eLoadType) {
		case MapLoad_LoadGame:
			return "a load";
		case MapLoad_Transition:
			return "a transition";
		case MapLoad_Background:
			return "a menu background";
		default:
			return "a new map";
		}
	}

	bool client_in_level()
	{
		static halfcraft::ClientInLevelFn in_level = nullptr;
		if (!in_level) {
			in_level = reinterpret_cast<halfcraft::ClientInLevelFn>(halfcraft::find_export("client.dll", halfcraft::HC_CLIENT_IN_LEVEL_EXPORT));
		}
		return in_level && in_level() != 0;
	}

	std::optional<CampaignModelState> read_state()
	{
		if (!filesystem->FileExists(STATE_FILE, GAME_FOLDER)) {
			return std::nullopt;
		}
		CUtlBuffer buffer;
		if (!filesystem->ReadFile(STATE_FILE, GAME_FOLDER, buffer)) {
			return std::nullopt;
		}
		return halfcraft::parse_campaign_model_state(std::string(static_cast<const char*>(buffer.Base()), buffer.TellPut()));
	}

	bool write_state(const CampaignModelState& state)
	{
		const std::string text = halfcraft::format_campaign_model_state(state);
		CUtlBuffer        buffer;
		buffer.Put(text.data(), static_cast<int>(text.size()));
		return filesystem->WriteFile(STATE_FILE, GAME_FOLDER, buffer) && filesystem->Size(STATE_FILE, GAME_FOLDER) == text.size();
	}

	/// takes a file out of the game folder; false when it's still there
	bool remove_file(const std::string& name)
	{
		for (int attempt = 0; attempt < REMOVE_TRIES && filesystem->FileExists(name.c_str(), GAME_FOLDER); ++attempt) {
			if (attempt) {
				ThreadSleep(REMOVE_RETRY_MS);
			}
			filesystem->RemoveFile(name.c_str(), GAME_FOLDER);
		}
		return !filesystem->FileExists(name.c_str(), GAME_FOLDER);
	}

	/// writes a file into the game folder and checks it's all there; a file that isn't goes again
	bool write_file(const halfcraft::PlannedFile& file)
	{
		const std::size_t slash = file.name.rfind('/');
		if (slash != std::string::npos) {
			filesystem->CreateDirHierarchy(file.name.substr(0, slash).c_str(), GAME_FOLDER);
		}
		CUtlBuffer buffer;
		buffer.Put(file.data.data(), static_cast<int>(file.data.size()));
		if (!file.data.empty() && filesystem->WriteFile(file.name.c_str(), GAME_FOLDER, buffer) &&
			filesystem->Size(file.name.c_str(), GAME_FOLDER) == file.data.size()) {
			return true;
		}
		remove_file(file.name);
		return false;
	}

	/// the model cache lets go of every model the files change, and of those that include them, so whatever reads them
	/// next reads the game folder's; returns how many were loaded
	int flush_models()
	{
		mdlcache->FinishPendingLoads();
		filesystem->AsyncFinishAll();
		MDLCACHE_CRITICAL_SECTION();
		int loaded = 0;
		for (const std::string& name : halfcraft::campaign_models_to_flush()) {
			const MDLHandle_t handle = mdlcache->FindMDL(name.c_str());
			if (handle == MDLHANDLE_INVALID) {
				continue;
			}
			loaded += mdlcache->GetRef(handle) > 1 ? 1 : 0;
			mdlcache->Flush(handle, MDLCACHE_FLUSH_ALL);
			mdlcache->Release(handle);
		}
		return loaded;
	}

	/// modelsounds.cache tells a model's sounds by its file's size, and the campaigns' versions of a character can have
	/// the same size (alyx.mdl has in all three): it starts over, and the map's models are read for their sounds again
	void forget_model_sounds()
	{
		ClearModelSoundsCache();  // saves what it has learned since
		if (filesystem->FileExists(MODEL_SOUNDS_CACHE, "MOD")) {
			filesystem->RemoveFile(MODEL_SOUNDS_CACHE, "MOD");
		}
		ClearModelSoundsCache();  // nothing on disk now: it starts empty
	}

	/// logs a list in as few lines as hold it
	void log_list(const std::string& lead, const std::vector<std::string>& items)
	{
		std::string line;
		for (const std::string& item : items) {
			if (!line.empty() && line.size() + item.size() > LOG_LINE) {
				halfcraft::log_info("%s%s", lead.c_str(), line.c_str());
				line.clear();
			}
			line += (line.empty() ? "" : "; ") + item;
		}
		if (!line.empty()) {
			halfcraft::log_info("%s%s", lead.c_str(), line.c_str());
		}
	}

	class CampaignModels final : public CAutoGameSystem
	{
	public:
		CampaignModels() : CAutoGameSystem("HalfCraftCampaignModels") {}

		// before the map's entities precache their models: new maps, loads and transitions all come through here
		void LevelInitPreEntity() override
		{
			const char*    map = STRING(gpGlobals->mapname);
			const ModelSet wanted = halfcraft::model_set_of(map);
			const auto     state = read_state();
			if (gpGlobals->eLoadType == MapLoad_Background) {
				halfcraft::log_info("campaign models: the menu's %s keeps %s", map, state ? set_text(state->set).c_str() : "what the game folder has");
				return;
			}
			if (state && current(*state, wanted)) {
				return;
			}
			if (!state && wanted == ModelSet::kEpisodeTwo && !any_copies()) {
				write_state(empty_state());  // a game folder that never had any
				return;
			}
			if (client_in_level()) {
				halfcraft::log_warning("campaign models: the client still has the last level's entities as %s (%s) loads: it keeps %s; "
									   "a new game or a load there gives it %s",
					map, load_type(), state ? set_text(state->set).c_str() : "what the game folder has", set_text(wanted).c_str());
				return;
			}
			swap(map, wanted, state);
		}

		// what the level plays: each character's version, and its includes'
		void LevelInitPostEntity() override
		{
			const char*              map = STRING(gpGlobals->mapname);
			const auto               state = read_state();
			std::vector<std::string> played, wrong;
			for (const char* root : halfcraft::CAMPAIGN_CHARACTERS) {
				const int          index = modelinfo->GetModelIndex(root);
				const studiohdr_t* header = index >= 0 ? modelinfo->GetStudiomodel(modelinfo->GetModel(index)) : nullptr;
				if (!header) {
					continue;
				}
				std::string text = short_name(root) + " " + hex(static_cast<std::uint32_t>(header->checksum));
				check(state, root, static_cast<std::uint32_t>(header->checksum), wrong);
				std::string includes;
				for (int i = 0; i < header->numincludemodels; ++i) {
					const char*         include = header->pModelGroup(i)->pszName();
					const MDLHandle_t   handle = mdlcache->FindMDL(include);
					const studiohdr_t*  included = handle != MDLHANDLE_INVALID ? mdlcache->GetStudioHdr(handle) : nullptr;
					const std::uint32_t checksum = included ? static_cast<std::uint32_t>(included->checksum) : 0;
					includes += (includes.empty() ? "" : ", ") + short_name(include) + " " + (included ? hex(checksum) : std::string("missing"));
					if (included) {
						check(state, include, checksum, wrong);
					}
					if (handle != MDLHANDLE_INVALID) {
						mdlcache->Release(handle);
					}
				}
				played.push_back(text + (includes.empty() ? "" : " (" + includes + ")"));
			}
			if (!played.empty()) {
				log_list(std::string("campaign models: ") + map + " plays " + (state ? set_text(state->set) : "the merged models") + ": ", played);
			}
			for (const std::string& line : wrong) {
				halfcraft::log_warning("campaign models: %s: %s", map, line.c_str());
			}
		}

		/// what a model's checksum means here: "half-life 2's" when it's the version the game folder has of it
		static std::string version_of(const std::optional<CampaignModelState>& state, const std::string& name, std::uint32_t checksum)
		{
			if (state) {
				for (const auto& [model, planned] : state->models) {
					if (model == "models/" + short_name(name) + ".mdl" && planned == checksum) {
						return std::string(halfcraft::model_set_name(state->set)) + "'s";
					}
				}
			}
			return "";
		}

	private:
		static std::string set_text(ModelSet set)
		{
			return set == ModelSet::kEpisodeTwo ? std::string("the merged models (episode two's)") : std::string(halfcraft::model_set_name(set)) + "'s models";
		}

		static CampaignModelState empty_state()
		{
			CampaignModelState state;
			state.list_hash = halfcraft::campaign_model_list_hash();
			state.set = ModelSet::kEpisodeTwo;
			return state;
		}

		static bool any_copies()
		{
			for (const std::string& name : halfcraft::campaign_model_files()) {
				if (filesystem->FileExists(name.c_str(), GAME_FOLDER)) {
					return true;
				}
			}
			return false;
		}

		/// a model the game folder has a version of that the level doesn't play
		static void check(const std::optional<CampaignModelState>& state, const char* name, std::uint32_t checksum, std::vector<std::string>& wrong)
		{
			if (!state) {
				return;
			}
			const std::string model = "models/" + short_name(name) + ".mdl";
			for (const auto& [planned_name, planned] : state->models) {
				if (planned_name == model && planned != checksum) {
					wrong.push_back(model + " is " + hex(checksum) + ", expected " + halfcraft::model_set_name(state->set) + "'s " + hex(planned));
				}
			}
		}

		/// whether the game folder holds what the state says, for the set wanted, as the lists are now; the first time
		/// a process asks, also whether its sources are still what they were (a pak valve updated)
		bool current(const CampaignModelState& state, ModelSet wanted)
		{
			if (state.list_hash != halfcraft::campaign_model_list_hash() || state.set != wanted) {
				return false;
			}
			for (const halfcraft::CampaignModelFile& file : state.files) {
				const char* name = file.name.c_str();
				if (!filesystem->FileExists(name, GAME_FOLDER) || static_cast<long>(filesystem->Size(name, GAME_FOLDER)) != file.size) {
					return false;
				}
				if (!sources_checked_ && (!filesystem->FileExists(name, file.source.c_str()) || static_cast<long>(filesystem->Size(name, file.source.c_str())) != file.source_size)) {
					return false;
				}
			}
			sources_checked_ = true;
			return true;
		}

		/// the game folder gets the set wanted: the cache lets go, what was there goes, the plan's files come, the state
		/// comes last. a file that can't go or come leaves no state, so the next level tries again
		void swap(const char* map, ModelSet wanted, const std::optional<CampaignModelState>& state)
		{
			const double            started = Plat_FloatTime();
			GameFiles               files;
			halfcraft::ModelSetPlan plan = halfcraft::plan_model_set(wanted, files);
			for (const std::string& skipped : plan.skipped) {
				halfcraft::log_warning("campaign models: %s's models: skipped %s", halfcraft::model_set_name(wanted), skipped.c_str());
			}
			const int                loaded = flush_models();
			std::vector<std::string> old = halfcraft::campaign_model_files();
			if (state) {
				for (const halfcraft::CampaignModelFile& file : state->files) {
					old.push_back(file.name);
				}
			}
			int removed = 0;
			remove_file(STATE_FILE);
			for (const std::string& name : old) {
				const bool there = filesystem->FileExists(name.c_str(), GAME_FOLDER);
				if (there && !remove_file(name)) {
					halfcraft::log_warning("campaign models: couldn't remove %s from the game folder: %s keeps a mix; the next level tries again", name.c_str(), map);
					return;
				}
				removed += there ? 1 : 0;
			}
			CampaignModelState written;
			written.list_hash = halfcraft::campaign_model_list_hash();
			written.set = wanted;
			for (const halfcraft::PlannedFile& file : plan.files) {
				if (!write_file(file)) {
					for (const halfcraft::CampaignModelFile& done : written.files) {
						remove_file(done.name);
					}
					halfcraft::log_warning("campaign models: couldn't write %s into the game folder: %s plays the merged models; the next level tries again", file.name.c_str(), map);
					return;
				}
				written.files.push_back({ file.name, static_cast<long>(file.data.size()), file.source, files.size(file.name, file.source) });
			}
			for (const halfcraft::PlannedModel& model : plan.models) {
				written.models.emplace_back(model.name, model.checksum);
			}
			if (!write_state(written)) {
				halfcraft::log_warning("campaign models: couldn't write %s into the game folder: the next level checks the models again", STATE_FILE);
			}
			sources_checked_ = true;
			forget_model_sounds();
			const std::string from = state ? set_text(state->set) : std::string("an earlier run's");
			if (plan.files.empty()) {
				halfcraft::log_info("campaign models: %s on %s (%s): removed %d files (%s); %d loaded models flushed; %.0f ms",
					set_text(wanted).c_str(), map, load_type(), removed, from.c_str(), loaded, (Plat_FloatTime() - started) * 1000.0);
				return;
			}
			std::string sources;
			for (const char* source : halfcraft::model_set_sources(wanted)) {
				sources += (sources.empty() ? "" : ", ") + std::string(source);
			}
			halfcraft::log_info("campaign models: %s on %s (%s): %d models, %d files (%.1f MB) from %s; removed %d files (%s); %d loaded models flushed; %.0f ms",
				set_text(wanted).c_str(), map, load_type(), static_cast<int>(plan.models.size()), static_cast<int>(plan.files.size()), plan.bytes() / 1e6,
				sources.c_str(), removed, from.c_str(), loaded, (Plat_FloatTime() - started) * 1000.0);
		}

		bool sources_checked_ = false;  // this process has seen the state's sources unchanged
	};

	CampaignModels g_campaign_models;
}

CON_COMMAND(hc_debug_model, "halfcraft: an entity's model, its version (checksum) and sequence count, and whether it has the sequences named: "
							"hc_debug_model <targetname or classname> [sequence ...]")
{
	if (args.ArgC() < 2) {
		halfcraft::log_info("model: hc_debug_model <targetname or classname> [sequence ...]");
		return;
	}
	const auto  state = read_state();
	const char* name = args[1];
	int         found = 0;
	const auto  report = [&](CBaseEntity* entity) {
		++found;
		CBaseAnimating* animating = entity->GetBaseAnimating();
		CStudioHdr*     studio = animating ? animating->GetModelPtr() : nullptr;
		if (!studio || !studio->IsValid()) {
			halfcraft::log_info("model: %s (%s #%d) %s has no studio model", name, entity->GetClassname(), entity->entindex(), STRING(entity->GetModelName()));
			return;
		}
		const auto        checksum = static_cast<std::uint32_t>(studio->GetRenderHdr()->checksum);
		const std::string version = CampaignModels::version_of(state, STRING(entity->GetModelName()), checksum);
		const std::string shown = hex(checksum) + (version.empty() ? "" : " (" + version + ")");
		std::string       sequences;
		for (int i = 2; i < args.ArgC(); ++i) {
			sequences += std::string(i > 2 ? ", " : "; ") + args[i] + (animating->LookupSequence(args[i]) >= 0 ? " yes" : " no");
		}
		halfcraft::log_info("model: %s (%s #%d) %s %s, %d sequences%s", name, entity->GetClassname(), entity->entindex(), STRING(entity->GetModelName()),
			shown.c_str(), studio->GetNumSeq(), sequences.c_str());
	};
	for (CBaseEntity* entity = gEntList.FindEntityByName(nullptr, name); entity; entity = gEntList.FindEntityByName(entity, name)) {
		report(entity);
	}
	if (!found) {
		for (CBaseEntity* entity = gEntList.FindEntityByClassname(nullptr, name); entity; entity = gEntList.FindEntityByClassname(entity, name)) {
			report(entity);
		}
	}
	if (!found) {
		halfcraft::log_info("model: no entity named %s", name);
	}
}
