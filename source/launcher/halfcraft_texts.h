#pragma once

// what HalfCraft.exe tells the player, in english and russian (halfcraft_launcher.cpp picks one). {0}, {1}, ...
// are filled in where a text is said. the games keep steam's names, untranslated. tools/build/package.py checks
// the built exe's texts (HalfCraft.exe --strings): ALL lists every one, and each language has the same {N}s.

// utf-8: the launcher's cl line has /utf-8. without it msvc reads this file in the windows code page
static_assert(L"я"[0] == 0x044F, "build HalfCraft.exe with /utf-8: its russian texts are utf-8");

namespace halfcraft::launcher
{
	struct Text
	{
		const wchar_t* english;
		const wchar_t* russian;
	};

	namespace texts
	{
		constexpr Text OUTSIDE_FOLDER{
			L"HalfCraft.exe has to stay in the HalfCraft folder, next to its game-hl2, game-hl2dm and minecraft folders.",
			L"HalfCraft.exe должен лежать в папке HalfCraft, рядом с папками game-hl2, game-hl2dm и minecraft."
		};
		// {0}: the folder's name
		constexpr Text MISSING_FOLDER{
			L"The {0} folder next to HalfCraft.exe is missing. Unpack the whole HalfCraft zip again.",
			L"Рядом с HalfCraft.exe нет папки {0}. Распакуйте весь архив HalfCraft заново."
		};
		// {0}: HalfCraft's folder
		constexpr Text ODD_PATH{
			L"HalfCraft's folder has characters in its path that Half-Life 2 may not read:\n{0}\n\n"
			L"If the game doesn't start, move the HalfCraft folder somewhere with a plain English path, like C:\\Games\\HalfCraft.\n\nStart anyway?",
			L"В пути к папке HalfCraft есть символы, которые Half-Life 2 может не прочитать:\n{0}\n\n"
			L"Если игра не запустится, перенесите папку HalfCraft в место с путём из одних латинских букв, например C:\\Games\\HalfCraft.\n\nВсё равно запустить?"
		};
		// {0}: -hc_link or HALFCRAFT_LINK, {1}: the id it gave
		constexpr Text BAD_LINK_ID{
			L"{0} \"{1}\" isn't a link id: that's 1 to 32 of A-Z, a-z, 0-9, _ and -. Fix it, or leave it out for the default link.",
			L"{0} «{1}» — не идентификатор связи: это от 1 до 32 символов из A-Z, a-z, 0-9, _ и -. Исправьте его или уберите, чтобы играть на обычной связи."
		};

		constexpr Text NO_STEAM{
			L"Steam isn't installed. HalfCraft needs Half-Life 2 from Steam.",
			L"Не найден Steam. Для HalfCraft нужна Half-Life 2 из Steam."
		};
		constexpr Text NO_HALF_LIFE_2{
			L"Half-Life 2 isn't installed. HalfCraft needs it from Steam.\n\nOpen Steam to install it?",
			L"Half-Life 2 не установлена, а без неё HalfCraft не работает.\n\nОткрыть Steam, чтобы установить её?"
		};
		// {0}: the episodes' names, each on a line of its own
		constexpr Text MISSING_EPISODES{
			L"Steam hasn't installed:{0}\n\nThey come with Half-Life 2. Their chapters won't load without them; Half-Life 2's will.\n\nOpen Steam to install them?",
			L"В Steam не установлены:{0}\n\nОни идут в комплекте с Half-Life 2. Без них главы эпизодов не загрузятся, а главы Half-Life 2 будут работать.\n\n"
			L"Открыть Steam, чтобы установить их?"
		};
		// {0}: the game, {1}: its exe
		constexpr Text ALREADY_RUNNING{
			L"{0} ({1}) is already running, and only one Source game can run at a time. Close it, then start HalfCraft again.",
			L"{0} ({1}) уже запущена, а одновременно может работать только одна игра на Source. Закройте её и снова запустите HalfCraft."
		};
		// {0}: the game, {1}: the exe it lacks
		constexpr Text MISSING_ENGINE_EXE{
			L"{0} has no {1}. Let Steam update it, or check its files (Properties, Installed Files, Verify integrity).",
			L"В игре {0} нет файла {1}. Дайте Steam обновить игру или проверьте её файлы (Свойства → Установленные файлы → Проверить целостность файлов игры)."
		};
		constexpr Text STEAM_WONT_START{
			L"Couldn't start Steam. Start it, sign in, then start HalfCraft again.",
			L"Не удалось запустить Steam. Запустите его, войдите в аккаунт и снова запустите HalfCraft."
		};
		constexpr Text STEAM_SIGNED_OUT{
			L"Steam isn't signed in. Sign in, then start HalfCraft again.",
			L"Вход в Steam не выполнен. Войдите в аккаунт и снова запустите HalfCraft."
		};

		// {0}: what followed --engine
		constexpr Text UNKNOWN_ENGINE{
			L"--engine \"{0}\" isn't an engine HalfCraft runs on. Use --engine hl2dm (Half-Life 2: Deathmatch's 64-bit engine) "
			L"or --engine hl2 (Half-Life 2's own 32-bit engine).",
			L"--engine «{0}» — такого движка нет. Укажите --engine hl2dm (64-битный движок Half-Life 2: Deathmatch) "
			L"или --engine hl2 (32-битный движок самой Half-Life 2)."
		};
		constexpr Text GET_DEATHMATCH{
			L"HalfCraft runs on Half-Life 2: Deathmatch's engine (only there can you dig through Half-Life's walls), "
			L"and Half-Life 2: Deathmatch isn't installed.\n\n"
			L"Open Steam to get it? If you own it, Steam installs it; if not, Steam opens its store page. Then start HalfCraft again.\n\n"
			L"Otherwise HalfCraft starts on Half-Life 2's own engine, where you can't dig, and doesn't ask again. "
			L"Hold Shift while starting HalfCraft to be asked again.",
			L"HalfCraft работает на движке Half-Life 2: Deathmatch (только на нём можно копать стены Half-Life), "
			L"а Half-Life 2: Deathmatch не установлена.\n\n"
			L"Открыть Steam, чтобы получить её? Если игра у вас есть, Steam её установит, а если нет — откроет её страницу в магазине. "
			L"Потом снова запустите HalfCraft.\n\n"
			L"Иначе HalfCraft запустится на движке самой Half-Life 2, где копать нельзя, и больше не будет об этом спрашивать. "
			L"Чтобы он спросил снова, держите Shift при запуске HalfCraft."
		};

		// the engine question (shift): a command link for each (its first line, then a note under it)
		constexpr Text HL2DM_CHOICE{
			L"Half-Life 2: Deathmatch (64-bit engine, more memory)\nRecommended: digging through Half-Life's walls works only on this engine.",
			L"Half-Life 2: Deathmatch (64-битный движок, больше памяти)\nРекомендуется: копать стены Half-Life можно только на нём."
		};
		constexpr Text HL2_CHOICE{
			L"Half-Life 2 (32-bit engine)\nDigging through Half-Life's walls doesn't work on it.",
			L"Half-Life 2 (32-битный движок)\nНа нём нельзя копать стены Half-Life."
		};
		constexpr Text WHICH_ENGINE{
			L"Which engine should run HalfCraft?",
			L"На каком движке запустить HalfCraft?"
		};
		constexpr Text ENGINE_SAVES{
			L"Each engine keeps its own saves.",
			L"У каждого движка свои сохранения."
		};
		// {0}: the error code
		constexpr Text ASK_ENGINE_FAILED{
			L"Couldn't ask which engine to run (error {0}).",
			L"Не удалось показать выбор движка (ошибка {0})."
		};

		constexpr Text FIRST_START{
			L"First start: Prism Launcher opens and asks you to sign in with the Microsoft account that owns Minecraft: Java Edition. "
			L"Then it downloads Minecraft and Java by itself.\n\n"
			L"Half-Life 2 starts as soon as you're signed in. Minecraft joins it once it's ready (the first time takes a few minutes).",
			L"Первый запуск: откроется Prism Launcher и попросит войти в аккаунт Microsoft, на котором куплен Minecraft: Java Edition. "
			L"Затем он сам скачает Minecraft и Java.\n\n"
			L"Half-Life 2 запустится, как только вы войдёте. Minecraft подключится к ней, когда будет готов (в первый раз это займёт несколько минут)."
		};
		// {0}: prism's exe and why it didn't start
		constexpr Text MINECRAFT_WONT_START{
			L"Couldn't start Minecraft.\n\n{0}",
			L"Не удалось запустить Minecraft.\n\n{0}"
		};
		constexpr Text NO_ACCOUNT{
			L"HalfCraft needs Minecraft, and Minecraft needs an account. Start HalfCraft again to sign in.",
			L"Для HalfCraft нужен Minecraft, а для Minecraft — аккаунт. Запустите HalfCraft снова и войдите в аккаунт."
		};
		// {0}: the engine's exe, {1}: the error code
		constexpr Text ENGINE_WONT_START{
			L"Couldn't start {0} (error {1}).",
			L"Не удалось запустить {0} (ошибка {1})."
		};

		/// every text above, for HalfCraft.exe --strings
		constexpr const Text* ALL[]{
			&OUTSIDE_FOLDER,
			&MISSING_FOLDER,
			&ODD_PATH,
			&BAD_LINK_ID,
			&NO_STEAM,
			&NO_HALF_LIFE_2,
			&MISSING_EPISODES,
			&ALREADY_RUNNING,
			&MISSING_ENGINE_EXE,
			&STEAM_WONT_START,
			&STEAM_SIGNED_OUT,
			&UNKNOWN_ENGINE,
			&GET_DEATHMATCH,
			&HL2DM_CHOICE,
			&HL2_CHOICE,
			&WHICH_ENGINE,
			&ENGINE_SAVES,
			&ASK_ENGINE_FAILED,
			&FIRST_START,
			&MINECRAFT_WONT_START,
			&NO_ACCOUNT,
			&ENGINE_WONT_START,
		};
	}
}
