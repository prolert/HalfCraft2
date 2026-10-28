// server.dll: each campaign's own characters (core/hc_campaign_models.h says why and which). as a level loads, before
// its entities precache anything, the game folder gets the versions the map's campaign plays (half-life 2's, episode
// one's or lost coast's), read from that campaign's own content, or none on episode two's maps, whose merged content is
// its own. only when that set changes: the model cache lets go of every model involved, then every file that has to go
// is checked to be free to go and the disk to have room, before anything changes; then the files go and come, each
// checked against what it was read from, and halfcraft-campaign-models.txt, written last, says what's there. a file the
// folder already has, byte for byte as the new set wants it, stays. should something fail halfway after all (a file
// another program grabbed in between, a full disk), every copy goes; a model with a file that won't go gets its other
// files back in that file's version, so it plays whole, and is named in the log as a mix, which the next level tries
// again. a level the client still has entities of (a changelevel across campaigns typed into the console) keeps
// the set it has: the client's models point into the cache's data. a menu background map writes nothing: it keeps the
// set when its files check out, else it takes the copies out. once the level is in, the log says which version of each
// character it plays, and hc_debug_model shows any entity's.

#include "cbase.h"
#include "datacache/imdlcache.h"
#include "engine/ivmodelinfo.h"
#include "filesystem.h"
#include "studio.h"
#include "tier0/threadtools.h"
#include "tier1/utlbuffer.h"

#include "tier0/valve_minmax_off.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "core/hc_campaign_models.h"
#include "core/hc_files.h"
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
	using State = std::optional<CampaignModelState>;

	constexpr const char* GAME_FOLDER = "DEFAULT_WRITE_PATH";  // the game folder: first on GAME
	constexpr const char* STATE_FILE = "halfcraft-campaign-models.txt";
	constexpr const char* MODEL_SOUNDS_CACHE = "modelsounds.cache";  // in the game folder (MOD)
	constexpr int         REMOVE_TRIES = 5;                          // an antivirus may hold a file it just saw written
	constexpr unsigned    REMOVE_RETRY_MS = 50;
	constexpr long long   SPARE_BYTES = 64ll << 20;  // what a swap leaves free on the disk, at least
	constexpr std::size_t LOG_LINE = 850;            // what one log line holds of a list (hc_log's lines are 1 kB)

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
	std::string short_name(const std::string& name)
	{
		std::string shown = halfcraft::normal_model_name(name);
		if (shown.rfind("models/", 0) == 0) {
			shown.erase(0, 7);
		}
		if (shown.size() > 4 && shown.compare(shown.size() - 4, 4, ".mdl") == 0) {
			shown.erase(shown.size() - 4);
		}
		return shown;
	}

	std::string set_text(ModelSet set)
	{
		return set == ModelSet::kEpisodeTwo ? std::string("the merged models (episode two's)") : std::string(halfcraft::model_set_name(set)) + "'s models";
	}

	std::string state_text(const State& state)
	{
		return state ? set_text(state->set) : std::string("what the game folder has");
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

	State read_state()
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

	long folder_size(const std::string& name)
	{
		return filesystem->FileExists(name.c_str(), GAME_FOLDER) ? static_cast<long>(filesystem->Size(name.c_str(), GAME_FOLDER)) : -1;
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

	/// writes a file into the game folder; true when it's all there
	bool write_file(const halfcraft::PlannedFile& file)
	{
		const std::size_t slash = file.name.rfind('/');
		if (slash != std::string::npos) {
			filesystem->CreateDirHierarchy(file.name.substr(0, slash).c_str(), GAME_FOLDER);
		}
		CUtlBuffer buffer;
		buffer.Put(file.data.data(), static_cast<int>(file.data.size()));
		return !file.data.empty() && filesystem->WriteFile(file.name.c_str(), GAME_FOLDER, buffer) && folder_size(file.name) == static_cast<long>(file.data.size());
	}

	/// every file of the lists' (and the state's) the game folder has
	std::vector<std::string> copies_present(const State& state)
	{
		std::vector<std::string> names = halfcraft::campaign_model_files();
		if (state) {
			for (const halfcraft::CampaignModelFile& file : state->files) {
				names.push_back(file.name);
			}
		}
		std::vector<std::string> present;
		for (const std::string& name : names) {
			if (folder_size(name) >= 0 && std::find(present.begin(), present.end(), name) == present.end()) {
				present.push_back(name);
			}
		}
		return present;
	}

	/// the folder's own path, the first of DEFAULT_WRITE_PATH's
	std::string game_folder_path()
	{
		char paths[1024] = {};
		filesystem->GetSearchPath(GAME_FOLDER, false, paths, static_cast<int>(sizeof(paths)));
		std::string       path = paths;
		const std::size_t end = path.find(';');
		return end == std::string::npos ? path : path.substr(0, end);
	}

	/// why the swap can't start without risking a half-changed folder: a file that has to go but couldn't now (another
	/// program has it open), or a disk without room for the files that come; nullopt: it can
	std::optional<std::string> blocker(const halfcraft::CampaignModelSwap& swap, const halfcraft::ModelSetPlan& plan)
	{
		long long needed = 0;
		for (const std::size_t index : swap.write) {
			needed += static_cast<long long>(plan.files[index].data.size());
		}
		const std::string folder = game_folder_path();
		const long long   free = halfcraft::free_bytes(folder.c_str());
		if (free < 0) {
			halfcraft::log_warning("campaign models: windows won't say how much room the disk under %s has: the copies go without that check", folder.c_str());
		} else if (needed && free < needed + SPARE_BYTES) {
			return "the disk has " + std::to_string(free >> 20) + " MB free, and the copies need " + std::to_string(needed >> 20) + " MB";
		}
		std::vector<std::string> going = swap.remove;
		going.emplace_back(STATE_FILE);
		std::vector<std::string> unchecked;  // files windows couldn't be asked about
		for (const std::string& name : going) {
			char path[1024] = {};
			if (!filesystem->FileExists(name.c_str(), GAME_FOLDER)) {
				continue;
			}
			if (!filesystem->RelativePathToFullPath(name.c_str(), GAME_FOLDER, path, static_cast<int>(sizeof(path))) || !path[0]) {
				unchecked.push_back(name + " (no path)");
				continue;
			}
			switch (halfcraft::file_deletable(path)) {
			case halfcraft::Deletable::kNo:
				return name + " is open in another program";
			case halfcraft::Deletable::kUnknown:
				unchecked.push_back(std::string(path) + " (windows finds nothing there)");
				break;
			case halfcraft::Deletable::kYes:
				break;
			}
		}
		if (!unchecked.empty()) {
			halfcraft::log_warning("campaign models: %d files go without the check that nothing has them open, the first: %s", static_cast<int>(unchecked.size()),
				unchecked.front().c_str());
		}
		return std::nullopt;
	}

	/// whether the game folder's file has exactly these bytes
	bool folder_holds(const std::string& name, const std::vector<std::uint8_t>& data)
	{
		CUtlBuffer buffer;
		if (!filesystem->ReadFile(name.c_str(), GAME_FOLDER, buffer) || static_cast<std::size_t>(buffer.TellPut()) != data.size()) {
			return false;
		}
		return data.empty() || std::memcmp(buffer.Base(), data.data(), data.size()) == 0;
	}

	const halfcraft::CampaignModelFolder GAME_FOLDER_FILES{ folder_size, folder_holds };

	/// the model cache lets go of every model the files change, and of those that include them, so whatever reads them
	/// next reads the game folder's, a model that failed to load before included; returns how many were loaded
	int flush_models(const State& state)
	{
		std::vector<std::string> names = halfcraft::campaign_models_to_flush();
		if (state) {
			for (const halfcraft::PlannedModel& model : state->models) {
				if (std::find(names.begin(), names.end(), model.name) == names.end()) {
					names.push_back(model.name);  // from older lists
				}
			}
		}
		mdlcache->FinishPendingLoads();
		filesystem->AsyncFinishAll();
		// the cache's lock (BeginLock), which valve's code takes around work on the cache's entries
		MDLCACHE_CRITICAL_SECTION();
		int loaded = 0;
		for (const std::string& name : names) {
			const MDLHandle_t handle = mdlcache->FindMDL(name.c_str());
			if (handle == MDLHANDLE_INVALID) {
				continue;
			}
			loaded += mdlcache->GetRef(handle) > 1 ? 1 : 0;
			mdlcache->Flush(handle, MDLCACHE_FLUSH_ALL);
			mdlcache->ResetErrorModelStatus(handle);
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

	/// what a model's checksum means here: "half-life 2's" when it's the version the game folder has of it
	std::string version_of(const State& state, const std::string& name, std::uint32_t checksum)
	{
		if (state) {
			const std::string model = halfcraft::normal_model_name(name);
			for (const halfcraft::PlannedModel& planned : state->models) {
				if (planned.name == model && planned.checksum == checksum) {
					return std::string(halfcraft::model_set_name(state->set)) + "'s";
				}
			}
		}
		return "";
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
			State          state = read_state();
			if (!state && remembered_) {  // a state that couldn't be written: the folder is as it says
				state = remembered_;
				store_state(*state);
			}
			if (gpGlobals->eLoadType == MapLoad_Background) {
				background(map, state);
				return;
			}
			if (state && current(*state, wanted)) {
				return;
			}
			if (!state && wanted == ModelSet::kEpisodeTwo && copies_present(state).empty()) {
				store_state(empty_state());  // a game folder that never had any
				return;
			}
			if (client_in_level()) {
				halfcraft::log_warning("campaign models: the client still has the last level's entities as %s (%s) loads: it keeps %s; "
									   "a new game or a load there gives it %s",
					map, load_type(), state_text(state).c_str(), set_text(wanted).c_str());
				return;
			}
			swap(map, wanted, state);
		}

		// what the level plays: each character's version, and its includes'
		void LevelInitPostEntity() override
		{
			const char*              map = STRING(gpGlobals->mapname);
			State                    state = read_state();
			std::vector<std::string> played, wrong;
			if (!state) {
				state = remembered_;
			}
			for (const char* root : halfcraft::CAMPAIGN_CHARACTERS) {
				const int          index = modelinfo->GetModelIndex(root);
				const studiohdr_t* header = index >= 0 ? modelinfo->GetStudiomodel(modelinfo->GetModel(index)) : nullptr;
				if (!header) {
					continue;
				}
				const auto checksum = static_cast<std::uint32_t>(header->checksum);
				check(state, root, checksum, wrong);
				std::vector<std::string> includes;
				for (int i = 0; i < header->numincludemodels; ++i) {
					const char*        include = header->pModelGroup(i)->pszName();
					const MDLHandle_t  handle = mdlcache->FindMDL(include);
					const studiohdr_t* included = handle != MDLHANDLE_INVALID ? mdlcache->GetStudioHdr(handle) : nullptr;
					if (included) {
						check(state, include, static_cast<std::uint32_t>(included->checksum), wrong);
					}
					includes.push_back(short_name(include) + " " + (included ? halfcraft::checksum_text(static_cast<std::uint32_t>(included->checksum)) : std::string("missing")));
					if (handle != MDLHANDLE_INVALID) {
						mdlcache->Release(handle);
					}
				}
				std::string text = short_name(root) + " " + halfcraft::checksum_text(checksum);
				if (!includes.empty()) {
					text += " (" + halfcraft::campaign_models_detail::joined(includes) + ")";
				}
				played.push_back(text);
			}
			if (!played.empty()) {
				log_list(std::string("campaign models: ") + map + " plays " + (state ? set_text(state->set) : "the merged models") + ": ", played);
			}
			for (const std::string& line : wrong) {
				// mostly a file ahead of the game folder on GAME: a steam workshop addon hl2.exe mounts (one replacing alyx
				// was seen), or custom/
				halfcraft::log_warning("campaign models: %s: %s (likely a file mounted ahead of the game folder: a workshop addon, custom/)", map, line.c_str());
			}
			if (!mixed_.empty()) {
				halfcraft::log_warning("campaign models: %s plays a mix: the game folder still has files of %s, the rest of them comes from the merged content",
					map, halfcraft::campaign_models_detail::joined(mixed_).c_str());
			}
		}

	private:
		static CampaignModelState empty_state()
		{
			CampaignModelState state;
			state.list_hash = halfcraft::campaign_model_list_hash();
			state.set = ModelSet::kEpisodeTwo;
			return state;
		}

		/// a model the game folder has a version of that the level doesn't play
		static void check(const State& state, const char* name, std::uint32_t checksum, std::vector<std::string>& wrong)
		{
			if (!state) {
				return;
			}
			const std::string model = halfcraft::normal_model_name(name);
			for (const halfcraft::PlannedModel& planned : state->models) {
				if (planned.name == model && planned.checksum != checksum) {
					wrong.push_back(model + " is " + halfcraft::checksum_text(checksum) + ", expected " + halfcraft::model_set_name(state->set) + "'s " +
						halfcraft::checksum_text(planned.checksum));
				}
			}
		}

		/// whether the game folder still holds every file the state says, as big as it says
		static bool intact(const CampaignModelState& state)
		{
			for (const halfcraft::CampaignModelFile& file : state.files) {
				if (folder_size(file.name) != file.size) {
					return false;
				}
			}
			return true;
		}

		/// a menu background map writes no copies: it keeps the set when its files check out, else takes them out
		void background(const char* map, const State& state)
		{
			if (state && intact(*state)) {
				halfcraft::log_info("campaign models: the menu's %s keeps %s", map, set_text(state->set).c_str());
				return;
			}
			if (copies_present(state).empty()) {
				halfcraft::log_info("campaign models: the menu's %s plays the merged models", map);
				return;
			}
			if (client_in_level()) {
				halfcraft::log_warning("campaign models: the client still has the last level's entities as the menu's %s loads: it keeps %s", map, state_text(state).c_str());
				return;
			}
			halfcraft::log_info("campaign models: the game folder's copies don't check out: the menu's %s takes them out", map);
			swap(map, ModelSet::kEpisodeTwo, state);
		}

		/// whether the game folder holds what the state says, for the set wanted, as the lists and rules are now; the
		/// first time a process asks, also whether its sources still give what was copied (a pak valve updated)
		bool current(const CampaignModelState& state, ModelSet wanted)
		{
			if (state.list_hash != halfcraft::campaign_model_list_hash() || state.set != wanted || !intact(state)) {
				return false;
			}
			if (!sources_checked_) {
				if (const auto stale = halfcraft::stale_campaign_models(state, GameFiles())) {
					halfcraft::log_info("campaign models: %s: copies again", stale->c_str());
					return false;
				}
				sources_checked_ = true;
			}
			mixed_.clear();
			return true;
		}

		/// a swap that stopped halfway: every copy goes, so the level plays the merged models, and what can't go is named
		/// and each model with a file that wouldn't go gets its other files back, in the version the leftover is, so it
		/// plays whole
		void undo(const char* map, const State& old, const halfcraft::ModelSetPlan& plan, const std::string& why)
		{
			remembered_.reset();
			for (const std::string& name : copies_present(old)) {
				remove_file(name);
			}
			const std::vector<std::string> leftovers = copies_present(old);
			int                            restored = 0, unrestored = 0;
			for (const halfcraft::CampaignModelRestore& file : halfcraft::plan_campaign_model_restore(leftovers, old, plan, GAME_FOLDER_FILES)) {
				halfcraft::PlannedFile back{ file.name, nullptr, {} };
				if (file.plan_file) {
					back.data = plan.files[*file.plan_file].data;
				} else if (!GameFiles().read(file.name, file.source.c_str(), back.data, 0) || static_cast<long>(back.data.size()) != file.size) {
					back.data.clear();
				}
				if (!back.data.empty() && write_file(back)) {
					++restored;
				} else {
					remove_file(file.name);
					++unrestored;
				}
			}
			forget_model_sounds();
			mixed_ = halfcraft::models_of_files(copies_present(old));
			if (mixed_.empty()) {
				halfcraft::log_warning("campaign models: %s: %s plays the merged models; the next level tries again", why.c_str(), map);
				return;
			}
			halfcraft::log_warning("campaign models: %s: %s plays a mix: the game folder still has %s (%d files put back so each is whole, %d couldn't be), "
								   "the rest comes from the merged content; the next level tries again",
				why.c_str(), map, halfcraft::campaign_models_detail::joined(mixed_).c_str(), restored, unrestored);
		}

		/// writes the state file; a state is either there or remembered, never an older one
		bool store_state(const CampaignModelState& state)
		{
			remembered_.reset();
			if (write_state(state)) {
				return true;
			}
			remembered_ = state;
			return false;
		}

		/// the game folder gets the set wanted: the cache lets go, everything that has to go is checked to be free to go
		/// and the disk to have room, then the state goes, the old files go, the new ones come, the state comes last
		void swap(const char* map, ModelSet wanted, const State& state)
		{
			const double            started = Plat_FloatTime();
			const GameFiles         files;
			halfcraft::ModelSetPlan plan = halfcraft::plan_model_set(wanted, files);
			for (const std::string& skipped : plan.skipped) {
				halfcraft::log_warning("campaign models: %s's models: skipped %s", halfcraft::model_set_name(wanted), skipped.c_str());
			}
			if (wanted != ModelSet::kEpisodeTwo && plan.models.empty()) {
				halfcraft::log_warning("campaign models: nothing of %s to copy: %s keeps %s", set_text(wanted).c_str(), map, state_text(state).c_str());
				return;
			}
			const auto steps = halfcraft::plan_campaign_model_swap(state, plan, GAME_FOLDER_FILES);
			const int  loaded = flush_models(state);
			if (const auto why = blocker(steps, plan)) {
				halfcraft::log_warning("campaign models: %s: %s keeps %s; the next level tries again", why->c_str(), map, state_text(state).c_str());
				return;
			}
			if (!remove_file(STATE_FILE)) {
				halfcraft::log_warning("campaign models: couldn't remove %s: %s keeps %s; the next level tries again", STATE_FILE, map, state_text(state).c_str());
				return;
			}
			remembered_.reset();  // the folder changes now: no older state may come back
			for (const std::string& name : steps.remove) {
				if (!remove_file(name)) {
					undo(map, state, plan, "couldn't remove " + name);
					return;
				}
			}
			for (const std::size_t index : steps.write) {
				if (!write_file(plan.files[index])) {
					undo(map, state, plan, "couldn't write " + plan.files[index].name);
					return;
				}
			}
			CampaignModelState written;
			written.list_hash = halfcraft::campaign_model_list_hash();
			written.set = wanted;
			for (const halfcraft::PlannedFile& file : plan.files) {
				written.files.push_back({ file.name, static_cast<long>(file.data.size()), file.source, files.size(file.name, file.source) });
			}
			written.models = plan.models;
			if (!store_state(written)) {
				halfcraft::log_warning("campaign models: couldn't write %s into the game folder: the next levels try again", STATE_FILE);
			}
			sources_checked_ = true;
			mixed_.clear();
			forget_model_sounds();
			std::string done = std::to_string(steps.write.size()) + " written, " + std::to_string(steps.keep.size()) + " kept, " + std::to_string(steps.remove.size()) +
				" removed (" + state_text(state) + ")";
			if (!plan.files.empty()) {
				std::vector<std::string> sources;
				for (const char* source : halfcraft::model_set_sources(wanted)) {
					sources.emplace_back(source);
				}
				char megabytes[32];
				V_snprintf(megabytes, sizeof(megabytes), "%.1f", static_cast<double>(plan.bytes()) / 1e6);
				done = std::to_string(plan.models.size()) + " models, " + std::to_string(plan.files.size()) + " files (" + megabytes + " MB) from " +
					halfcraft::campaign_models_detail::joined(sources) + ": " + done;
			}
			halfcraft::log_info("campaign models: %s on %s (%s): %s; %d loaded models flushed; %.0f ms", set_text(wanted).c_str(), map, load_type(), done.c_str(), loaded,
				(Plat_FloatTime() - started) * 1000.0);
		}

		bool                     sources_checked_ = false;  // this process has seen the state's sources still give the copies
		State                    remembered_;               // a state written to the folder's files but not to its file
		std::vector<std::string> mixed_;                    // the models a swap that stopped halfway left files of
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
	const State state = read_state();
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
		const std::string version = version_of(state, STRING(entity->GetModelName()), checksum);
		const std::string shown = halfcraft::checksum_text(checksum) + (version.empty() ? "" : " (" + version + ")");
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
