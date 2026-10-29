// HalfCraft.exe, the one click. finds half-life 2 in steam (and half-life 2: deathmatch, whose 64-bit
// engine it runs instead of half-life 2's own 32-bit one whenever it's installed: digging through
// half-life's walls works only there), starts the bundled minecraft (the first time it waits until the
// player has signed in to it), then the engine on its mod folder beside it. extra arguments go to the
// engine:
//
//   HalfCraft.exe -windowed -w 1280 -h 720 +map d1_canals_01
//
// with both games installed it asks nothing. holding shift while starting it asks which engine to run,
// deathmatch's preselected, for that start only (the engine= an older HalfCraft.exe kept in HalfCraft.ini
// is ignored). without deathmatch it offers to open steam for it (steam://install/320: steam installs an
// owned app and shows the store page of one that isn't), and starts nothing then; a player who says no
// plays on half-life 2's engine and isn't asked again (HalfCraft.ini's deathmatch_declined) until they
// hold shift while starting it. for tests only, HALFCRAFT_TEST_NO_DEATHMATCH=1 makes it take deathmatch
// for not installed, so that offer can be tried without uninstalling anything.
//
// -hc_link <id> among them (or HALFCRAFT_LINK=<id>, which -hc_link wins over) puts the game and the minecraft
// it starts on a link of their own (core/hc_link_names.h): the engine reads it as well, and minecraft gets
// HALFCRAFT_LINK from prism, which gets it from here.
//
// it speaks english or russian (halfcraft_texts.h): HALFCRAFT_LANG=en or ru says which (the release's
// "HalfCraft EN.cmd" and "HalfCraft RU.cmd" set it), else windows' display language does (russian for
// russian, english for the rest). two arguments are its own, not the engine's:
//
//   HalfCraft.exe --engine hl2dm|hl2  that engine, with no question even with shift: hl2dm is deathmatch's,
//                                     hl2 half-life 2's own (which never offers deathmatch). hl2dm without
//                                     deathmatch is half-life 2's after the offer. the last one given wins:
//                                     the .cmd files give --engine hl2dm, and their own arguments come after
//                                     it. any other value says so and starts nothing
//   HalfCraft.exe --strings <file>    every text it can show, in its language, into <file> (utf-8); starts nothing
//
// it expects the release layout (core/hc_prism.h): game-hl2/, game-hl2dm/ and minecraft/ next to it.

#include <algorithm>
#include <chrono>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <tlhelp32.h>

#include "core/hc_prism.h"
#include "halfcraft_texts.h"

namespace
{
	namespace fs = std::filesystem;
	namespace texts = halfcraft::launcher::texts;
	using halfcraft::launcher::Text;
	using namespace std::chrono_literals;

	constexpr wchar_t TITLE[] = L"HalfCraft";
	constexpr wchar_t SETTINGS_FILE[] = L"HalfCraft.ini";
	constexpr wchar_t SETTINGS_SECTION[] = L"launcher";
	constexpr wchar_t ENGINE_ARGUMENT[] = L"--engine";
	constexpr wchar_t STRINGS_ARGUMENT[] = L"--strings";
	constexpr wchar_t NO_DEATHMATCH_VARIABLE[] = L"HALFCRAFT_TEST_NO_DEATHMATCH";  // tests only: deathmatch counts as not installed
	constexpr wchar_t LANGUAGE_VARIABLE[] = L"HALFCRAFT_LANG";
	constexpr wchar_t LINK_VARIABLE[] = L"HALFCRAFT_LINK";  // halfcraft::LINK_ID_VARIABLE
	constexpr wchar_t STEAM_KEY[] = L"Software\\Valve\\Steam";
	constexpr wchar_t STEAM_PROCESS_KEY[] = L"Software\\Valve\\Steam\\ActiveProcess";
	constexpr auto    STEAM_SIGN_IN_WAIT = 3min;
	constexpr auto    OLD_MINECRAFT_WAIT = 30s;  // the last game's minecraft saves and quits within a few seconds

	struct SteamApp
	{
		int            id;
		const wchar_t* name;
	};
	constexpr SteamApp HALF_LIFE_2{ 220, L"Half-Life 2" };
	constexpr SteamApp DEATHMATCH{ 320, L"Half-Life 2: Deathmatch" };
	// they come with half-life 2 and install into its folder; their chapters need them
	constexpr SteamApp EPISODES[]{ { 380, L"Half-Life 2: Episode One" }, { 420, L"Half-Life 2: Episode Two" } };
	constexpr wchar_t  SETTINGS_EPISODES_DECLINED[] = L"episodes_declined";
	constexpr wchar_t  SETTINGS_DEATHMATCH_DECLINED[] = L"deathmatch_declined";  // steam's install of deathmatch, said no to

	/// a source engine halfcraft runs on. the release has a mod folder for each: game-<id>.
	struct Engine
	{
		const wchar_t* id;      // its name after --engine and in its mod folder's
		SteamApp       app;     // the game it comes with
		const wchar_t* exe;     // in that game's folder
		Text           choice;  // what the player picks it by
	};
	// the engine question (shift) lists them in this order and preselects the first. deathmatch's goes first and
	// runs whenever it's installed: it's the one with the shader dll that digs half-life's walls (and 64-bit, more
	// memory). half-life 2's is always there: half-life 2 is the one game halfcraft always needs (its content)
	constexpr Engine ENGINES[]{
		{ L"hl2dm", DEATHMATCH, L"hl2mp_win64.exe", texts::HL2DM_CHOICE },
		{ L"hl2", HALF_LIFE_2, L"hl2.exe", texts::HL2_CHOICE },
	};
	constexpr const Engine* DEATHMATCH_ENGINE = &ENGINES[0];

	bool russian = false;  // the player's language (speaks_russian), set before anything is said

	/// HALFCRAFT_LANG as set, or empty (unset, or too long to be a language).
	std::wstring language_variable()
	{
		wchar_t     value[64]{};
		const DWORD length = ::GetEnvironmentVariableW(LANGUAGE_VARIABLE, value, static_cast<DWORD>(std::size(value)));
		return length > 0 && length < std::size(value) ? std::wstring(value, length) : std::wstring();
	}

	/// HALFCRAFT_LANG's ru or en, else windows' display language: russian for russian, english for the rest.
	bool speaks_russian()
	{
		const std::wstring chosen = language_variable();
		if (_wcsicmp(chosen.c_str(), L"ru") == 0) {
			return true;
		}
		if (_wcsicmp(chosen.c_str(), L"en") == 0) {
			return false;
		}
		return PRIMARYLANGID(::GetUserDefaultUILanguage()) == LANG_RUSSIAN;
	}

	/// a text in the player's language, its {0}, {1}, ... filled in with values (not themselves searched for more).
	std::wstring say(const Text& what, std::initializer_list<std::wstring> values = {})
	{
		std::wstring said;
		for (const wchar_t* at = russian ? what.russian : what.english; *at; ++at) {
			if (at[0] == L'{' && at[1] >= L'0' && at[1] <= L'9' && at[2] == L'}' && static_cast<std::size_t>(at[1] - L'0') < values.size()) {
				said += values.begin()[at[1] - L'0'];
				at += 2;
			} else {
				said += *at;
			}
		}
		return said;
	}

	void tell(const std::wstring& text, UINT icon = MB_ICONINFORMATION)
	{
		::MessageBoxW(nullptr, text.c_str(), TITLE, MB_OK | icon);
	}

	bool ask(const std::wstring& text)
	{
		return ::MessageBoxW(nullptr, text.c_str(), TITLE, MB_YESNO | MB_ICONWARNING) == IDYES;
	}

	int fail(const std::wstring& text)
	{
		tell(text, MB_ICONERROR);
		return 1;
	}

	std::string narrow(const std::wstring& wide)
	{
		if (wide.empty()) {
			return {};
		}
		const int   length = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
		std::string utf8(static_cast<std::size_t>(length), '\0');
		::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), length, nullptr, nullptr);
		return utf8;
	}

	std::wstring widen(const std::string& utf8)
	{
		if (utf8.empty()) {
			return {};
		}
		const int    length = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
		std::wstring wide(static_cast<std::size_t>(length), L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), length);
		return wide;
	}

	/// HalfCraft.exe's arguments, its own --engine <id>s taken out of the ones that go on to the engine.
	struct Arguments
	{
		std::vector<std::wstring> engines;     // each --engine's id in order, "" for one with nothing after it
		std::vector<std::wstring> others;      // the rest, their quotes taken out
		std::wstring              for_engine;  // the rest as written
	};

	/// the command line's arguments (PathGetArgsW's), split at spaces and tabs outside "quotes". what goes on to
	/// the engine is the line itself with each --engine and the argument after it cut out.
	Arguments read_arguments()
	{
		const std::wstring line = ::PathGetArgsW(::GetCommandLineW());
		const auto         blank = [&](std::size_t at) { return line[at] == L' ' || line[at] == L'\t'; };
		Arguments          arguments;
		std::size_t        kept = 0;  // where the line's next stretch for the engine starts
		bool               engine_next = false;
		for (std::size_t at = 0; at < line.size();) {
			if (blank(at)) {
				++at;
				continue;
			}
			const std::size_t begin = at;
			std::wstring      text;
			for (bool quoted = false; at < line.size() && (quoted || !blank(at)); ++at) {
				if (line[at] == L'"') {
					quoted = !quoted;
				} else {
					text += line[at];
				}
			}
			if (engine_next) {
				arguments.engines.back() = text;
				engine_next = false;
				kept = at;
			} else if (text == ENGINE_ARGUMENT) {
				arguments.for_engine += line.substr(kept, begin - kept);
				arguments.engines.emplace_back();
				engine_next = true;
				kept = at;
			} else {
				arguments.others.push_back(text);
			}
		}
		arguments.for_engine += line.substr(kept);
		const auto first = arguments.for_engine.find_first_not_of(L" \t");
		const auto last = arguments.for_engine.find_last_not_of(L" \t");
		arguments.for_engine = first == std::wstring::npos ? std::wstring() : arguments.for_engine.substr(first, last - first + 1);
		return arguments;
	}

	/// -hc_link's value among the arguments (they go on to the engine), the way source reads it: nullopt
	/// without -hc_link, empty when nothing follows it or another option does.
	std::optional<std::string> link_parameter()
	{
		int                        count = 0;
		LPWSTR*                    args = ::CommandLineToArgvW(::GetCommandLineW(), &count);
		std::optional<std::string> value;
		for (int i = 1; args && i < count && !value; ++i) {
			if (_wcsicmp(args[i], L"-hc_link") == 0) {
				const bool given = i + 1 < count && args[i + 1][0] != L'-' && args[i + 1][0] != L'+';
				value = given ? narrow(args[i + 1]) : std::string();
			}
		}
		::LocalFree(static_cast<HLOCAL>(args));
		return value;
	}

	/// the link the game and its minecraft take (core/hc_link_names.h): -hc_link's id over HALFCRAFT_LINK's.
	/// nullopt, said, for an id that isn't one: neither starts then, rather than take another pair's link.
	std::optional<halfcraft::LinkNames> chosen_link()
	{
		const auto   parameter = link_parameter();
		const DWORD  size = ::GetEnvironmentVariableW(LINK_VARIABLE, nullptr, 0);
		std::wstring variable(size, L'\0');
		const DWORD  length = size ? ::GetEnvironmentVariableW(LINK_VARIABLE, variable.data(), size) : 0;
		variable.resize(length < size ? length : 0);
		const std::string variable_text = narrow(variable);
		const auto        choice = halfcraft::choose_link_id(parameter ? parameter->c_str() : nullptr, variable_text.c_str());
		auto              link = halfcraft::chosen_link_names(choice);
		if (!link) {
			fail(say(texts::BAD_LINK_ID, { widen(choice.from), widen(choice.id) }));
		}
		return link;
	}

	/// HALFCRAFT_LINK set to the link's id (taken out for the default link), for prism and the minecraft it
	/// starts to inherit. steam, which this may start too, never gets it: set only after steam is up.
	void pass_link_on(const halfcraft::LinkNames& link)
	{
		::SetEnvironmentVariableW(LINK_VARIABLE, link.id.empty() ? nullptr : widen(link.id).c_str());
	}

	std::wstring registry_string(const wchar_t* key, const wchar_t* value)
	{
		wchar_t buffer[MAX_PATH * 2];
		DWORD   size = sizeof(buffer);
		if (::RegGetValueW(HKEY_CURRENT_USER, key, value, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) {
			return {};
		}
		return buffer;
	}

	DWORD registry_dword(const wchar_t* key, const wchar_t* value)
	{
		DWORD result = 0;
		DWORD size = sizeof(result);
		return ::RegGetValueW(HKEY_CURRENT_USER, key, value, RRF_RT_REG_DWORD, nullptr, &result, &size) == ERROR_SUCCESS ? result : 0;
	}

	/// the values of every `"key"  "value"` line with this key in a steam .vdf or .acf file.
	std::vector<std::wstring> vdf_values(const fs::path& file, const std::string& key)
	{
		std::vector<std::wstring> values;
		std::ifstream             in(file);
		const std::string         quoted = "\"" + key + "\"";
		for (std::string line; std::getline(in, line);) {
			const auto at = line.find(quoted);
			const auto open = at == std::string::npos ? std::string::npos : line.find('"', at + quoted.size());
			const auto close = open == std::string::npos ? std::string::npos : line.find('"', open + 1);
			if (close == std::string::npos) {
				continue;
			}
			std::string value;
			for (auto i = open + 1; i < close; ++i) {
				if (line[i] == '\\' && i + 1 < close) {
					++i;  // vdf escapes backslashes
				}
				value += line[i];
			}
			values.push_back(widen(value));
		}
		return values;
	}

	std::vector<fs::path> steam_libraries(const fs::path& steam)
	{
		std::vector<fs::path> libraries{ steam };
		for (const auto& path : vdf_values(steam / L"steamapps" / L"libraryfolders.vdf", "path")) {
			libraries.emplace_back(path);
		}
		return libraries;
	}

	/// where steam has an app fully installed, or empty.
	fs::path app_dir(const std::vector<fs::path>& libraries, const SteamApp& app)
	{
		for (const auto& library : libraries) {
			const auto manifest = library / L"steamapps" / (L"appmanifest_" + std::to_wstring(app.id) + L".acf");
			const auto names = vdf_values(manifest, "installdir");
			const auto flags = vdf_values(manifest, "StateFlags");
			if (names.empty() || flags.empty() || !(std::wcstol(flags.front().c_str(), nullptr, 10) & 4)) {
				continue;  // not in this library, or still downloading
			}
			std::error_code ec;
			const auto      dir = library / L"steamapps" / L"common" / names.front();
			if (fs::is_directory(dir, ec)) {
				return dir;
			}
		}
		return {};
	}

	bool process_running(const wchar_t* exe)
	{
		HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snapshot == INVALID_HANDLE_VALUE) {
			return false;
		}
		bool            found = false;
		PROCESSENTRY32W entry{ sizeof(entry) };
		for (BOOL more = ::Process32FirstW(snapshot, &entry); more && !found; more = ::Process32NextW(snapshot, &entry)) {
			found = _wcsicmp(entry.szExeFile, exe) == 0;
		}
		::CloseHandle(snapshot);
		return found;
	}

	bool steam_signed_in()
	{
		const DWORD pid = registry_dword(STEAM_PROCESS_KEY, L"pid");
		if (!pid || !registry_dword(STEAM_PROCESS_KEY, L"ActiveUser")) {
			return false;
		}
		HANDLE process = ::OpenProcess(SYNCHRONIZE, FALSE, pid);  // the values outlive a steam that crashed
		if (!process) {
			return false;
		}
		const bool alive = ::WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
		::CloseHandle(process);
		return alive;
	}

	/// the engine started without a signed-in steam would leave for steam's own launch of it, without
	/// the mod folder. so steam goes first.
	bool ensure_steam()
	{
		if (steam_signed_in()) {
			return true;
		}
		const std::wstring steam_exe = registry_string(STEAM_KEY, L"SteamExe");
		if (steam_exe.empty() || reinterpret_cast<INT_PTR>(::ShellExecuteW(nullptr, L"open", steam_exe.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32) {
			fail(say(texts::STEAM_WONT_START));
			return false;
		}
		const auto give_up = std::chrono::steady_clock::now() + STEAM_SIGN_IN_WAIT;
		while (!steam_signed_in()) {
			if (std::chrono::steady_clock::now() > give_up) {
				fail(say(texts::STEAM_SIGNED_OUT));
				return false;
			}
			std::this_thread::sleep_for(500ms);
		}
		return true;
	}

	/// minecraft running on the game's link, or on its way with an account to play with.
	bool ensure_minecraft(const fs::path& game, const halfcraft::LinkNames& link)
	{
		const auto bundle = halfcraft::prism::find_bundle(game);
		if (bundle.empty()) {
			fail(say(texts::MISSING_FOLDER, { L"minecraft" }));
			return false;
		}
		// one running now is most likely the last game's on its way out: wait for it, then start a new one
		const auto give_up = std::chrono::steady_clock::now() + OLD_MINECRAFT_WAIT;
		while (halfcraft::prism::minecraft_running(link) && std::chrono::steady_clock::now() < give_up) {
			std::this_thread::sleep_for(500ms);
		}
		if (halfcraft::prism::minecraft_running(link)) {
			return true;  // it stays: it connects to this game instead
		}
		if (!halfcraft::prism::launcher_running(bundle)) {
			if (!halfcraft::prism::signed_in(bundle)) {
				tell(say(texts::FIRST_START));
			}
			std::wstring error;
			if (!halfcraft::prism::start(bundle, error)) {
				fail(say(texts::MINECRAFT_WONT_START, { error }));
				return false;
			}
		}
		// prism quits if the player closes it without signing in (or plays its demo, which is fine too)
		while (!halfcraft::prism::signed_in(bundle) && !halfcraft::prism::minecraft_running(link)) {
			if (!halfcraft::prism::launcher_running(bundle) && !halfcraft::prism::minecraft_running(link)) {
				tell(say(texts::NO_ACCOUNT), MB_ICONWARNING);
				return false;
			}
			std::this_thread::sleep_for(500ms);
		}
		return true;
	}

	/// half-life's file system reads paths in the windows ("ansi") code page.
	bool fits_ansi_code_page(const std::wstring& text)
	{
		BOOL lossy = FALSE;
		return ::WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, text.c_str(), -1, nullptr, 0, nullptr, &lossy) > 0 && !lossy;
	}

	fs::path exe_dir()
	{
		wchar_t     path[MAX_PATH * 2];
		const DWORD length = ::GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
		return fs::path(std::wstring(path, length)).parent_path();
	}

	fs::path game_dir(const fs::path& here, const Engine& engine)
	{
		return here / (std::wstring(L"game-") + engine.id);
	}

	bool is_game_dir(const fs::path& dir)
	{
		std::error_code ec;
		return fs::exists(dir / L"gameinfo.txt", ec);
	}

	/// the engines whose game steam has installed, in ENGINES' order. HALFCRAFT_TEST_NO_DEATHMATCH=1 leaves
	/// deathmatch's out, for tests of the offer to get it.
	std::vector<const Engine*> installed_engines(const std::vector<fs::path>& libraries)
	{
		wchar_t    test[2]{};
		const bool no_deathmatch = ::GetEnvironmentVariableW(NO_DEATHMATCH_VARIABLE, test, static_cast<DWORD>(std::size(test))) == 1 && test[0] == L'1';

		std::vector<const Engine*> installed;
		for (const auto& engine : ENGINES) {
			if ((!no_deathmatch || &engine != DEATHMATCH_ENGINE) && !app_dir(libraries, engine.app).empty()) {
				installed.push_back(&engine);
			}
		}
		return installed;
	}

	/// the engine with this id, any case (--engine's), or nullptr.
	const Engine* engine_named(const std::wstring& id)
	{
		const auto found = std::find_if(std::begin(ENGINES), std::end(ENGINES), [&](const Engine& engine) { return _wcsicmp(id.c_str(), engine.id) == 0; });
		return found == std::end(ENGINES) ? nullptr : found;
	}

	/// asks the player which engine to run this time (shift held), ENGINES' first preselected.
	/// @param engines - the installed ones to pick from
	/// @return the choice, or nullptr when the player closed the question or it couldn't be asked
	const Engine* ask_engine(const std::vector<const Engine*>& engines)
	{
		constexpr int FIRST_BUTTON = 100;  // clear of the common buttons' ids (IDCANCEL, ...)

		// the dialog points into these
		std::vector<std::wstring> choices;
		choices.reserve(engines.size());
		for (const Engine* engine : engines) {
			choices.push_back(say(engine->choice));
		}
		const std::wstring question = say(texts::WHICH_ENGINE);
		const std::wstring saves = say(texts::ENGINE_SAVES);

		TASKDIALOGCONFIG               config{ sizeof(config) };
		std::vector<TASKDIALOG_BUTTON> buttons;
		buttons.reserve(engines.size());
		for (std::size_t i = 0; i < engines.size(); ++i) {
			buttons.push_back({ FIRST_BUTTON + static_cast<int>(i), choices[i].c_str() });
		}
		config.nDefaultButton = FIRST_BUTTON;  // ENGINES' first: deathmatch's when it's there
		config.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION;
		config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
		config.pszWindowTitle = TITLE;
		config.pszMainInstruction = question.c_str();
		config.pszContent = saves.c_str();
		config.cButtons = static_cast<UINT>(buttons.size());
		config.pButtons = buttons.data();

		int           button = 0;
		const HRESULT result = ::TaskDialogIndirect(&config, &button, nullptr, nullptr);
		if (FAILED(result)) {
			wchar_t code[16];
			std::swprintf(code, std::size(code), L"0x%08lX", static_cast<unsigned long>(result));
			fail(say(texts::ASK_ENGINE_FAILED, { code }));
			return nullptr;
		}
		const int picked = button - FIRST_BUTTON;
		return picked >= 0 && picked < static_cast<int>(engines.size()) ? engines[static_cast<std::size_t>(picked)] : nullptr;
	}

	/// offers steam's install of half-life 2: deathmatch, whose engine halfcraft runs on (only it digs through
	/// half-life's walls), unless the player said no before (HalfCraft.ini) and didn't hold shift this time.
	/// steam installs an owned app and shows the store page of one that isn't. a no is kept.
	/// @return whether steam was sent for it: then nothing starts, and the player starts HalfCraft again once it's in
	bool get_deathmatch(const fs::path& settings, bool ask_again)
	{
		const bool declined = ::GetPrivateProfileIntW(SETTINGS_SECTION, SETTINGS_DEATHMATCH_DECLINED, 0, settings.c_str()) != 0;
		if (declined && !ask_again) {
			return false;
		}
		if (!ask(say(texts::GET_DEATHMATCH))) {
			::WritePrivateProfileStringW(SETTINGS_SECTION, SETTINGS_DEATHMATCH_DECLINED, L"1", settings.c_str());
			return false;
		}
		if (declined) {
			// shift asked again and the player said yes: a start without it asks again too
			::WritePrivateProfileStringW(SETTINGS_SECTION, SETTINGS_DEATHMATCH_DECLINED, nullptr, settings.c_str());
		}
		::ShellExecuteW(nullptr, L"open", (L"steam://install/" + std::to_wstring(DEATHMATCH.id)).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		return true;
	}

	/// the engine to run:
	/// - --engine hl2's, half-life 2's, without a question or an offer;
	/// - deathmatch's whenever it's installed. shift held without --engine asks which, for this start only;
	/// - without deathmatch, half-life 2's once the player has said no to steam's install of it (get_deathmatch).
	/// @param installed - the installed engines, at least one
	/// @param wanted - --engine's, or nullptr
	/// @param settings - HalfCraft.ini
	/// @param shift_held - the player held shift while starting HalfCraft.exe
	/// @return nullptr when nothing should start: the question was closed, or steam is getting deathmatch
	const Engine* choose_engine(const std::vector<const Engine*>& installed, const Engine* wanted, const fs::path& settings, bool shift_held)
	{
		if (installed.empty()) {
			return nullptr;
		}
		const auto is_installed = [&](const Engine* engine) { return std::find(installed.begin(), installed.end(), engine) != installed.end(); };
		if (wanted && wanted != DEATHMATCH_ENGINE) {
			return is_installed(wanted) ? wanted : installed.front();
		}
		if (!is_installed(DEATHMATCH_ENGINE)) {
			return get_deathmatch(settings, shift_held) ? nullptr : installed.front();
		}
		return !wanted && shift_held && installed.size() > 1 ? ask_engine(installed) : DEATHMATCH_ENGINE;
	}

	/// offers steam's install of the episodes it hasn't installed: half-life 2's chapters play without them,
	/// theirs don't. a player who says no isn't asked again (HalfCraft.ini).
	void offer_episodes(const std::vector<fs::path>& libraries, const fs::path& settings)
	{
		std::wstring     names;
		std::vector<int> missing;
		for (const auto& episode : EPISODES) {
			if (app_dir(libraries, episode).empty()) {
				names += L"\n" + std::wstring(episode.name);
				missing.push_back(episode.id);
			}
		}
		if (missing.empty() || ::GetPrivateProfileIntW(SETTINGS_SECTION, SETTINGS_EPISODES_DECLINED, 0, settings.c_str()) != 0) {
			return;
		}
		if (ask(say(texts::MISSING_EPISODES, { names }))) {
			for (const int id : missing) {
				::ShellExecuteW(nullptr, L"open", (L"steam://install/" + std::to_wstring(id)).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
			}
		} else {
			::WritePrivateProfileStringW(SETTINGS_SECTION, SETTINGS_EPISODES_DECLINED, L"1", settings.c_str());
		}
	}

	int run(const Arguments& arguments)
	{
		// read first thing: the player lets go of shift once HalfCraft has started
		const bool shift_held = (::GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

		// the last --engine wins (the .cmd files' comes before the player's own). one that names no engine stops it
		const Engine* wanted = nullptr;
		for (const auto& id : arguments.engines) {
			wanted = engine_named(id);
			if (!wanted) {
				return fail(say(texts::UNKNOWN_ENGINE, { id }));
			}
		}

		const fs::path here = exe_dir();
		if (std::none_of(std::begin(ENGINES), std::end(ENGINES), [&](const Engine& engine) { return is_game_dir(game_dir(here, engine)); })) {
			return fail(say(texts::OUTSIDE_FOLDER));
		}
		if (!fits_ansi_code_page(here.wstring()) && !ask(say(texts::ODD_PATH, { here.wstring() }))) {
			return 1;
		}
		const auto link = chosen_link();
		if (!link) {
			return 1;
		}

		const std::wstring steam = registry_string(STEAM_KEY, L"SteamPath");
		if (steam.empty()) {
			return fail(say(texts::NO_STEAM));
		}
		const auto libraries = steam_libraries(steam);
		if (app_dir(libraries, HALF_LIFE_2).empty()) {
			if (ask(say(texts::NO_HALF_LIFE_2))) {
				::ShellExecuteW(nullptr, L"open", (L"steam://install/" + std::to_wstring(HALF_LIFE_2.id)).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
			}
			return 1;
		}
		offer_episodes(libraries, here / SETTINGS_FILE);
		// source allows one game at a time, whichever engine runs it
		for (const auto& running : ENGINES) {
			if (process_running(running.exe)) {
				return fail(say(texts::ALREADY_RUNNING, { running.app.name, running.exe }));
			}
		}

		const Engine* engine = choose_engine(installed_engines(libraries), wanted, here / SETTINGS_FILE, shift_held);
		if (!engine) {
			return 1;
		}
		const fs::path game = game_dir(here, *engine);
		if (!is_game_dir(game)) {
			return fail(say(texts::MISSING_FOLDER, { game.filename().wstring() }));
		}
		const fs::path  exe = app_dir(libraries, engine->app) / engine->exe;
		std::error_code ec;
		if (!fs::exists(exe, ec)) {
			return fail(say(texts::MISSING_ENGINE_EXE, { engine->app.name, engine->exe }));
		}

		if (!ensure_steam()) {
			return 1;
		}
		pass_link_on(*link);
		if (!ensure_minecraft(game, *link)) {
			return 1;
		}

		// -insecure: no vac. halfcraft's client.dll and server.dll are no game's own, so it never joins a vac-secured server
		std::wstring command = L"\"" + exe.wstring() + L"\" -game \"" + game.wstring() + L"\" -novid -condebug -insecure";
		if (!arguments.for_engine.empty()) {
			command += L" ";
			command += arguments.for_engine;
		}
		STARTUPINFOW        startup{ sizeof(startup) };
		PROCESS_INFORMATION process{};
		if (!::CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, exe.parent_path().c_str(), &startup, &process)) {
			const DWORD error = ::GetLastError();
			return fail(say(texts::ENGINE_WONT_START, { exe.wstring(), std::to_wstring(error) }));
		}
		::CloseHandle(process.hThread);
		::CloseHandle(process.hProcess);
		return 0;
	}

	/// --strings <file>: the language it picked and every text it can show (their {0}s unfilled), utf-8,
	/// for tests and translators. starts nothing.
	int write_strings(const fs::path& file)
	{
		const std::wstring chosen = language_variable();
		wchar_t            windows[16];
		std::swprintf(windows, std::size(windows), L"0x%04X", static_cast<unsigned>(::GetUserDefaultUILanguage()));
		std::wstring all = std::wstring(L"language: ") + (russian ? L"ru" : L"en") + L"\n" + LANGUAGE_VARIABLE + L": " +
			(chosen.empty() ? L"(not set)" : chosen) + L"\nwindows display language: " + windows + L"\n";
		for (const Text* each : texts::ALL) {
			all += L"---\n";
			all += say(*each);
			all += L'\n';
		}
		std::ofstream out(file, std::ios::binary);
		out << narrow(all);
		return out.good() ? 0 : 1;
	}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	russian = speaks_russian();
	const Arguments arguments = read_arguments();
	// --strings <file> alone, but for any --engine (a .cmd file's)
	if (arguments.others.size() == 2 && arguments.others[0] == STRINGS_ARGUMENT) {
		return write_strings(arguments.others[1]);
	}

	// a second click while the first one waits (for steam, or the first sign-in) does nothing
	HANDLE single = ::CreateMutexW(nullptr, TRUE, L"Local\\HalfCraft_launcher");
	if (single && ::GetLastError() == ERROR_ALREADY_EXISTS) {
		::CloseHandle(single);
		return 0;
	}
	const int result = run(arguments);
	if (single) {
		::CloseHandle(single);
	}
	return result;
}
