# Копание сквозь стены Half-Life 2 — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ломать клетки стен, пола и потолка карт Half-Life 2 как блоки Minecraft (время по материалу и инструменту, трещины, дроп) и ходить по дырам: в туннели в камне за картой и насквозь в соседние комнаты, видимые с обеих сторон.

**Architecture:** Дырами владеет Minecraft (вложение чанка `halfcraft:dug`): сервер открывает клетку и вскрывает соседей из «камня» (браши | пустота `kColVoid`), клиент рисует стенки и шлёт маски `kRenDug`. client.dll кладёт их в текстуру для `game_shader_generic_halfcraft.dll` (hl2dm, `LightmappedGeneric` с `clip`) и мостом `HalfCraft_DugSince` отдаёт server.dll, который вырезает их из брашей мира в коллизии для Minecraft, метит грани `kTriDiggable` + `DigMaterial`, считает пустоту по дереву BSP и кладёт дыры в PVS.

**Tech Stack:** C++17 (MSVC, Source SDK 2013: hl2dm 64-bit в `source-sdk-2013/`, hl2 32-bit в `source-sdk-2013-sp/`, HLSL ps_2_b), Java 25, Minecraft 26.3, Fabric, JUnit 5, Python 3.13, ruff, clang-format/clang-tidy, GNU make.

**Spec:** `docs/superpowers/specs/2026-10-17-dig-through-walls-design.md`. Задача 1 дописывает «## Результаты этапа 1», задача 14 — «## Результаты проверки»; недостающие уточнения планирования вносят задачи 3, 6, 7, 8, 13 своим шагом.

## Global Constraints

- `dig-prototype` — локально, без push и PR; `dig-spike` не сливается.
- В игре — только `hl2dm`, `d1_trainstation_02`, тестовая сессия (`make test-start`, `make mc-test`, мир `HalfCraftTest`); `hl2` собирается (`make build ENGINE=hl2`) и запускается только коротко, без DLL.
- Протокол — только `protocol/schema/*.toml` + `make proto`, генерируемое не править; связываются только игры из одного коммита.
- Предупреждение валит сборку (`build.py` в `source/`, hl2 `/WX`; `core_tests.py` `/W4 /WX`; Java `-Xlint:all -Werror`).
- Код Valve — только в дереве SDK, в репо через `make patches` (`source/sdk/halfcraft-<engine>.patch`); новые файлы — после `git -C source-sdk-2013 add -N` (потом `Tree.added`); перед правкой сверить дерево с патчем.
- `source/src/core` — без SDK, C++17, заголовки с `inline`, тесты зовёт `run_tests()`; `shared/hc_dig_abi.h` — без `inline`-переменных (его включает шейдерная DLL); server.dll без исключений: в векторы по одному `push_back`.
- 1 блок = 40 юнитов, слоты карт вдоль x через 1024 блока (`MapSlot::x_blocks()`); бит клетки секции `x + 16z + 256y`, субвокселя `z*8 + x` слоя `y`; блок региона `bx + 8bz + 64by`.
- Маска 128×64×128 (x, y, z) в текстуре 1024×1024, перецентровка дальше 32 блоков по горизонтали или 16 по вертикали; `s15`, `c20 = (X0, Y1, Z0, 1/40)`, `c21 = (1/1024, 128, 8, enabled)`, пиксель на 0.5 юнита глубже по лучу взгляда. PVS: дыры в 24 блоках, ближайшие первыми, ≤ 64. Вскрытая клетка — камень при ≥ 256 субвокселях пустоты.
- Своих пакетов нет (клиент Minecraft зовёт `getSingleplayerServer().execute(...)`); вскрытые блоки — мимо Rollback (`Rollback.untracked`); сейвы HL дыр не откатывают.
- `hc_dig 0` запрещает только новые клетки; без DLL client.dll ставит `kHostDigOff` (одна строка лога). Вне дыр картинка стоковая (`tga_diff` в шуме N этапа 1): только пиксельные шейдеры 2.0b, без `psconst_patch.cpp`.
- Код, комментарии (строчными, табы) и коммиты — по-английски в стиле репо, записи в спеку — по-русски; коммит `feat:|fix:|chore:|docs: <строчными>` и `-m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`.
- Команды — PowerShell 5.1 из корня, gradle через `tools/build/gradle.py`; долгие (`make setup|build|tidy|mc-test`, ShaderCompile2, msbuild) — в фоне, лог в `build/logs/`. `make build` — при закрытых играх, `make test-start|test-stop` — и при закрытом Minecraft.
- `dig_base` брать только из `build/dig-baseline/save`: `<engine>-last` затирает каждый `make test-stop`.
- Тестовый Minecraft (скрыт, `quitWithHost=false`) останавливать только так и при запущенном Half-Life; `/F` теряет несохранённые чанки с дырами:
  ```powershell
  $mcpid = [int]((python tools/debug/hc_state.py | Select-String 'mc +pid (\d+)').Matches[0].Groups[1].Value)
  $m = python tools/game/wait_log.py mc-test --mark
  taskkill /PID $mcpid
  python tools/game/wait_log.py mc-test --after $m --pattern "Stopping!" --timeout 120
  Wait-Process -Id $mcpid -Timeout 180
  ```
  Прошло: `SUCCESS: Sent termination signal ...`, `Stopping!`, процесс вышел. Отказал `taskkill` — `hc_key 41`, по снимку `jpeg` `hc_cursor` + `hc_click 1` на «Save and Quit to Title», затем «Quit Game»; то же ожидание.

## Порядок задач

Задача 1 (spike) — первой: выбирает A или B, а если не вышли оба или не прошла проверка 3, 4 или 5, задачи 2–14 не начинаются. Дальше 2 → 3 → 4, затем параллельно линии C++ 5 → 6 → 7 → 8 → 9 (5–7 правят одни функции `WorldCollision`) и Java 10 → 11 → 12 → 13 (проверке 12 в игре нужны ещё 6, 7, 9); 14 последней.

## Review Focus

Случаи вне тестов задач, по убыванию вероятности:
1. **Сейв, загрузка, перезапуск в туннеле**: игрок остаётся там в обеих играх (`liftOutOfGeometry` и Source не двигают), стенки целы — задача 14, шаги 9a, 14a, 16a.
2. **Копание вниз**: игрок падает в выкопанную клетку пола в обеих играх, яма глубже блока вырезана сверху и снизу, стенки закрыты, выход есть — задача 14, шаг 12a.
3. **Вход в дыру не пешком (`/tp`, жемчуг, хорус)**: Source принимает клетку, как шаг в неё: `HalfCraftFits`/`HalfCraftRoomAt` (им же проверяет место `HalfCraftLandNear`) пропускают браши мира в выкопанных клетках (`box_dug`, решение задачи 7) — задача 9, шаг 14a.
4. **Уход с карты с дырами и возвращение**: на чужой нет дыр и рамок, на своей те же дыры видны и проходимы, снова `r_novis 1` — задача 14, шаг 17a.
5. **`hl2` на мире, где копали в `hl2dm`** (мир общий): видимая стена твёрдая и для Minecraft, без DLL коллизия не режется (решение задачи 7) — задача 14, шаг 18a.


---

### Task 1: Окружение и проверка шейдерного пути (spike)

Ставим инструменты, собираем нетронутый HalfCraft, снимаем эталонный кадр; на выбрасываемой ветке `dig-spike` DLL с копией `LightmappedGeneric` вырезает заданный в коде бокс из стены перед сейвом `dig_base`. Не вышли ни A, ни B или не прошла проверка 3, 4 или 5 — стоп с отчётом: задачи 2–14 не начинаются, план правится.

Обозначения: `cmd 'a' 'b'` = `make cmd C="'a' 'b'"`; `tga_diff` = `python tools/debug/tga_diff.py`; кадр `stock` = `build/dig-baseline/dig_base_stock.tga`, прочие — `build/dig-spike/<имя>.tga`; «ждать `<p>` N с [в <лог>]» = `python tools/game/wait_log.py <лог> --after $m --pattern "<p>" --timeout N`, где `$m = python tools/game/wait_log.py <лог> --mark` взят до команды (лог `hl2dm`, если не назван); `stdshaders/` = `source-sdk-2013/src/materialsystem/stdshaders/`.

**Files:**
- Create: `tools/debug/tga_diff.py`. Modify: `docs/superpowers/specs/2026-10-17-dig-through-walls-design.md`. Вне git: `build/dig-baseline/`, `build/dig-spike/`, `build/shaders-spike/`
- Только в `dig-spike`: `source/src/server/hc_world_collision.cpp`, `source/src/server/hc_server.cpp`, `source/src/client/hc_client.cpp`, `source/halfcraft_client.vpc`, `source/sdk/halfcraft-hl2dm.patch`; в SDK `src/vpc_scripts/projects.vgc` и новые файлы шага 8
- Test: логи, скриншоты, `tga_diff`

**Interfaces:**
- Consumes: —
- Produces:
  - `tga_diff <a.tga> <b.tga> [--tolerance 8] [--max <percent>] [--out <diff.tga>]` → `<a> vs <b>: <share>% of <n> pixels differ by more than <tolerance> (bounds x <x0>-<x1>, y <y0>-<y1>)` или `(none)`; код 1 при share > `--max`.
  - `build/dig-baseline/save/dig_base.*`, `stock`.
  - `## Результаты этапа 1` после `## Порядок работ` (в скобках — задачи): вариант A|B, папка DLL `bin/x64`|`bin` (9, 14); определения, файлы `.vpc`, что убрано в копиях, кортеж `.vcs` `(6, combos, dynamic)`, папка `.inc`, минуты компиляции (против `COMPILE_SECONDS = 600`), набор полный|урезанный, ошибки комбинаций (9); проверка 3: снимок с комнатой, держится ли значение из client.dll сразу, через 10 с, после `load` (8); проверка 5: origin в боксе, нет `source moved the player` (7); `setpos`, `hc_look P0 Y0`, S, G, шум N %, процедура «кадр dig_base» (9, 14); `W2` и `W1`: `pos` перед стеной (`python tools/debug/hc_state.py`), yaw `Y2`, `Y1`, толщина, строки зонда `W1` (14).

- [ ] **Step 1: Окружение**
`winget install --id ezwinports.make -e`, `winget install --id Python.Python.3.13 -e`; в новой оболочке `$env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')`; `python -m pip install ruff==0.16.9 clang-format==22.1.3 clang-tidy==22.1.8`; `python --version` → `3.13.x` (иначе 3.13 и её `Scripts` первыми в PATH). `make setup`, `make build`, `make mc`, `make lint`, `make tidy` — код 0 (`isn't clang-format 22` про clang из VS — норма); сбой — чинить окружение и записать, код не трогать.

- [ ] **Step 2: Сессия, сейв `dig_base`, процедура кадра**
`make test-start`, `make mc-test` (до шага 16). «Запуск hl2dm»: `make run ENGINE=hl2dm MAP=d1_trainstation_02`, ждать `puppet on` 600 с; S и G — из `(slot S)` и `grid offset for d1_trainstation_02: G units` в `build/game-hl2dm/console.log`. По `cmd 'hc_look 0 <yaw>' 'jpeg'` (yaw 0, 90, 180, −90) — ближайшая стена вдоль оси с комнатой за ней, без NPC; встать в 3–4 блоках (`cmd 'sv_cheats 1' 'setpos <x> <y> <z>'`), `Y0` (= `Y2`) кратен 90, `P0` чуть вниз (низ стены у пола в кадре: там бокс); `cmd 'sv_cheats 0' 'hc_look <P0> <Y0>' 'save dig_base'`, `build/game-hl2dm/save/dig_base.*` → `build/dig-baseline/save/`. «Кадр dig_base → <файл> [команды]»: `cmd 'load dig_base'`, ждать `puppet on` 120 с, `cmd 'hc_look <P0> <Y0>' [команды] 'hc_key 58' 'setpause' 'screenshot' 'unpause' 'hc_key 58'` (F1 прячет руку и HUD Minecraft), новейший `build/game-hl2dm/screenshots/*.tga` → <файл>.

- [ ] **Step 3: `tools/debug/tga_diff.py`, эталонный кадр, коммит**
Stdlib, как `tools/debug/collision_check.py`; TGA 2 и 10 (RLE), 24/32 бита; отличие — R, G или B больше `--tolerance`; маска `--out` — TGA, отличия белые. «Кадр dig_base» → `stock`; `tga_diff stock stock` → `… 0.000% of 921600 pixels differ by more than 8 (none)`, код 0; кадр с `'hc_look <P0> <Y0 + 10>'` → `turned`, `tga_diff stock turned --max 1 --out turned-diff` → код 1, маска не чёрная; `make lint`. `git add tools/debug/tga_diff.py`; `git commit -m "chore: tools/debug/tga_diff.py, the share of pixels two tga screenshots differ in, for the dig prototype's in-game checks" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`.

- [ ] **Step 4: Ветка `dig-spike`, бокс вырезан из коллизии**
`git switch -c dig-spike` (код на выброс, без format и tidy). `constexpr float SPIKE_BOX[6] = { 0, 0, 0, 0, 0, 0 };` (min, max в юнитах Source; нули — бокса нет), одинаковый в `hc_world_collision.cpp`, `hc_server.cpp` и хелпере шага 8. `add_brushes`: браш не неба (и player clip), пересекающий бокс (`to_mc_box`), вместо `push_back` даёт слои вне бокса: x < lo.x, x > hi.x; в x бокса y < lo.y, y > hi.y; в x, y бокса z < lo.z, z > hi.z — копии `cvx` с осевыми плоскостями (`n.p + d <= 0`) и подрезанными lo/hi, пустые отбросить.

- [ ] **Step 5: Зонд и клиент спайка**
`CON_COMMAND(hc_spike_probe, ...)` (админ, как `hc_debug_voxels`) → `HalfCraftServerSystem::probe()`: луч из `EyePosition()` по `EyeVectors()` на 4096, `MASK_PLAYERSOLID`, `CTraceFilterWorldOnly`; строки `[halfcraft] spike probe: ` и
- `hit X Y Z normal NX NY NZ, material M, minecraft MX MY MZ (slot S, grid_z G)` (M — `tr.surface.name`);
- `+d: contents 0x%x cluster C area A, brush yes|no` в hit + forward·d для d = 1, 8, 20, 40, 60, 80, 120, 160, 240, 400 (`GetPointContents`, `GetClusterForOrigin`, `GetArea`; `brush` — браш `SOLID | WINDOW | GRATE` с точкой, как шаг 8 задачи 6);
- `back face X Y Z, minecraft MX MY MZ` (трасса от первой `brush no` к глазу);
- `player X Y Z, minecraft MX MY MZ, in the box: yes|no`.

client.dll: `core/hc_module.{h,cpp}` в Core `halfcraft_client.vpc` (`.cpp` — к файлам без PCH, как в `halfcraft_server.vpc`); `LevelInitPostEntity` → `[halfcraft] spike: game_shader_generic_halfcraft.dll loaded|not loaded` (`find_export(<dll>, "CreateInterface")`); ConVar `hc_spike_vis` (0) ставит своё значение в `r_novis` и `r_portalsopenall` (`g_pCVar->FindVar(...)->SetValue`) и пишет `[halfcraft] spike: r_novis V, r_portalsopenall V from client.dll`.

- [ ] **Step 6: Спайк без DLL, шум кадра N**
`cmd 'quit'`, `make build`, «запуск hl2dm» → `spike: game_shader_generic_halfcraft.dll not loaded`. Дважды «кадр dig_base» → `stock_b`, `stock_c`; `tga_diff stock stock_b --out noise-diff`, `tga_diff stock stock_c`: N — больший share.

- [ ] **Step 7: Стены `W2`, `W1` (проверка 4), бокс**
- `W2`: `cmd 'load dig_base'`, `cmd 'hc_look <P0> <Y0>' 'hc_spike_probe'`, `hc_state.py`; годится при `contents 0x0` с `cluster >= 0` до +80; толщина |back face − hit| (≤ 2 блоков), M из `hit`. Иначе соседняя стена: новый сейв, `stock`, N (шаги 2, 3, 6).
- `W1`: фасад вдоль оси, за которым кончается карта, ≤ 30 блоков от `W2`: `cmd 'sv_cheats 1' 'setpos …' 'hc_look 0 <Y1>' 'hc_spike_probe'`, `hc_state.py`; `Y1` кратен 90; годится, если за последней `brush yes` — `contents 0x1 cluster -1, brush no`, толщина ≤ 40; годного нет — проверка 4 не прошла.
- Бокс (клетки Minecraft, виден в кадре `dig_base`): колонка клетки `hit` у `W2`, 2 клетки вверх от `floor(y ног)`, вглубь до первой клетки с центром за `back face`. В Source (`hc_units.h:48-53`; hi = последняя + 1): min = ((lo.x − 1024·S)·40, −hi.z·40, lo.y·40 + G), max = ((hi.x − 1024·S)·40, −lo.z·40, hi.y·40 + G).

В обе копии `SPIKE_BOX`, `cmd 'quit'`, `make build ENGINE=hl2dm NOPROJECTS=1`. Смена бокса позже — все три копии, при закрытой игре та же сборка и DLL шага 9.

- [ ] **Step 8: Файлы DLL в `stdshaders/`**
Копии Valve под `hc_`-именами, подключают друг друга:
- `hc_lightmappedgeneric_dx9.cpp`: шейдер `LightmappedGeneric` (A), в `SHADER_INIT_PARAMS` `Msg( "[halfcraft] spike: LightmappedGeneric from game_shader_generic_halfcraft.dll: %s\n", pMaterialName );`;
- хелпер `hc_lightmappedgeneric_dx9_helper.{h,cpp}`: только `hc_lightmappedgeneric_ps20b` (без ветки `ps20` и `r_flashlight_version2`), третья `SPIKE_BOX`, ConVar `hc_spike_clip` (1); перед `DynamicCmdsOut.End()` пиксельные константы 20 = (`SPIKE_BOX[0..2]`, 0) и 21 = (`SPIKE_BOX[3..5]`, `hc_spike_clip`); `DrawFlashlight_dx90` не трогать;
- `hc_lightmappedgeneric_ps2x.fxc` подключает `hc_lightmappedgeneric_ps2_3_x.h`, там первым в `main()` — `clip`, если `i.worldPos_projPosZ.xyz + normalize( i.worldPos_projPosZ.xyz - g_EyePos ) * 0.5` (полъюнита за поверхностью, как `HostRay.surfaceCell`) строго внутри `c20.xyz`…`c21.xyz` и `c21.w` = 1;
- `game_shader_generic_halfcraft.vpc` из `game_shader_generic_example_base.vpc` с макросами `game_shader_generic_example_episodic.vpc`: `OUTBINNAME "game_shader_generic_halfcraft"`, `GAMENAME "mod_ep1"` (→ `source-sdk-2013/game/mod_ep1/bin/x64`), папки `Release_halfcraft`, `Debug_halfcraft`, `$PreprocessorDefinitions "$BASE;STDSHADER_DX9_DLL_EXPORT;FAST_MATERIALVAR_ACCESS"` (без `GAME_SHADER_DLL`: с ним `CBaseVSShader` теряет `DrawFlashlight_dx90`); `BaseVSShader.{h,cpp}` и копии, mathlib, shaderlib, tier2; без примеров, `$Shaders` и `psconst_patch.cpp`;
- `projects.vgc` после `game_shader_generic_std`: `$Project "game_shader_generic_halfcraft" { "materialsystem\stdshaders\game_shader_generic_halfcraft.vpc" [$EPISODIC] }`.

- [ ] **Step 9: Шейдер и DLL**
`stdshaders/*.h` и `hc_lightmappedgeneric_ps2x.fxc` — в `build/shaders-spike`, там `source-sdk-2013/src/devtools/bin/ShaderCompile2.exe -ver 20b -shaderpath build/shaders-spike hc_lightmappedgeneric_ps2x.fxc`; записать минуты, папку `.inc`, `struct.unpack_from('<iIi', …)` начала `shaders/fxc/hc_lightmappedgeneric_ps20b.vcs`, ошибки комбинаций (число, первая). Дольше 60 минут — прервать, урезать (`LIGHTING_PREVIEW "0..0" [PC]`, `BICUBIC_LIGHTMAP "0..0" [ps20b]` в `.fxc`, обе 0 в хелпере), снова, оба замера. `.inc` — в `stdshaders/include/`; в `source-sdk-2013/src` `devtools\bin\vpc.exe /episodic /define:SOURCESDK +game_shader_generic_halfcraft /mksln hc_shaders.sln`, затем `python -c "import sys; sys.path.insert(0, 'tools'); from halfcraft import build, sdk; build.msbuild(build.toolchain('Release'), 'hc_shaders.sln', 'win64', sdk.TREES['hl2dm'].source())"` без ошибок (не хватает символов `BaseVSShader.cpp` — их файл из `stdshaders/` в `.vpc`). DLL — в `build/game-hl2dm/bin/x64`, `.vcs` — в `build/game-hl2dm/shaders/fxc`.

- [ ] **Step 10: Проверки 6 (папка DLL) и 1 (кадр вне бокса)**
«Запуск hl2dm» → `spike: game_shader_generic_halfcraft.dll loaded`; иначе `cmd 'quit'`, DLL в `build/game-hl2dm/bin`, снова; нигде — строки console.log со словом `shader` в отчёт, стоп. «Кадр dig_base» с `'hc_spike_clip 0'` → `c0`, с `'hc_spike_clip 1'` → `c1`. `tga_diff stock c0 --max <N + 0.1> --out c0-diff` → код 0: проверка 1 пройдена (код 1 с отличиями там же, где в `noise-diff`, — шум); `tga_diff c0 c1 --out cut-diff` — bounds на месте бокса; в console.log — `game_shader_generic_halfcraft.dll: <M>`.

- [ ] **Step 11: Решение A/B**
- **A**: строка для M, проверка 1, бокс в `cut-diff`.
- Строки не для M: шаг 7, другая стена.
- Для M, но c0 = c1: чинить `clip` (c20/c21, знак, `Unknown command "hc_spike_clip"`), шаг 9; к B не переходить.
- Проверка 1 не прошла сверх шума: share и маски в отчёт, стоп.
- **B** (DLL загружена, строк нет; игра закрыта): шейдер `HC_LightmappedGeneric` (и в логе), DLL шага 9; `LevelInitPostEntity` от `materials->FirstMaterial()` через `NextMaterial()` до `InvalidMaterial()` ставит материалам `TEXTURE_GROUP_WORLD` с шейдером `LightmappedGeneric` `SetShader("HC_LightmappedGeneric")`, лог `spike: <n> of <m> world materials switched to HC_LightmappedGeneric`; `make build ENGINE=hl2dm NOPROJECTS=1`, шаг 10: n > 0 и условия A, иначе отчёт, стоп.

- [ ] **Step 12: Проверки 2 (вырез и факел) и 3 (видимость)**
`cmd 'load dig_base'`, `jpeg`: в боксе нет стены (комната, пустота или HOM — записать). `cmd 'hc_mc setblock <x> <y> <z> minecraft:torch'` на полу перед колонкой бокса, `jpeg`: освещена ли стена вокруг, есть ли в боксе освещённый «призрак»; потом `… minecraft:air`. `jpeg` через бокс: как есть; после `'sv_cheats 1' 'r_novis 1' 'r_portalsopenall 1'`; после `'r_novis 0' 'r_portalsopenall 0' 'sv_cheats 0' 'hc_spike_vis 1'` (ждать `spike: r_novis 1, r_portalsopenall 1 from client.dll`). На втором комнаты нет — бокс не насквозь: шаг 7. Держится ли значение (сразу, через 10 с, после `load dig_base`): `cmd 'r_novis' 'r_portalsopenall'`, ждать 10 с `r_novis\W+=\W+1\b` и `r_portalsopenall\W+=\W+1\b`.

- [ ] **Step 13: Проверки 5 (игрок в браше) и 6 (hl2 без DLL)**
`setpos` в 1 блоке перед колонкой бокса, `hc_look` на неё; метка; `cmd 'hc_key 26 1 <с>'` (W; с = блоки до середины стены / 4.3); отпустило — `cmd 'hc_spike_probe' 'hc_spike_probe' 'hc_spike_probe'`; 10 с `--absent` `source moved the player`; `cmd 'hc_mc data get entity @s Pos'`, ждать 20 с в `mc-test` `has the following entity data` → три `in the box: yes`, origin в пределах 1 юнита, Pos = `minecraft …` зонда. Затем `cmd 'quit'`, `make run ENGINE=hl2 MAP=d1_trainstation_02`, ждать в `hl2` 600 с `dll not loaded` и `puppet on`, `cmd 'quit'`.

- [ ] **Step 14: Сохранить спайк**
`git -C source-sdk-2013/src/materialsystem/stdshaders add -N 'hc_lightmappedgeneric_*' game_shader_generic_halfcraft.vpc`; `make patches ENGINE=hl2dm` → `…halfcraft-hl2dm.patch (34 files)`; `git add source/src source/halfcraft_client.vpc source/sdk/halfcraft-hl2dm.patch`; `git commit -m "chore: the dig prototype's shader spike, throwaway and never merged: a LightmappedGeneric copy in game_shader_generic_halfcraft.dll that cuts one hard-coded box, the box cut out of minecraft's collision, hc_spike_probe and hc_spike_vis" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`.

- [ ] **Step 15: Вернуть `dig-prototype`, проверить эталон**
`git switch dig-prototype`; `git -C source-sdk-2013 reset -q -- src`, `git -C source-sdk-2013 checkout -- src/vpc_scripts/projects.vgc`; удалить файлы спайка: `stdshaders/` `hc_*`, `include/hc_*`, `game_shader_generic_halfcraft.*`, `Release_halfcraft`, `Debug_halfcraft`; `source-sdk-2013/src/hc_shaders.*`, `source-sdk-2013/game/mod_ep1/bin/x64/game_shader_generic_halfcraft.*`; в `build/game-hl2dm` — `shaders/fxc/hc_*` и DLL. `make setup ENGINE=hl2dm` → `hl2dm sdk already patched at …`; `git -C source-sdk-2013 status --short -- src/materialsystem src/vpc_scripts` — только ` M` у `flashlight_ps2x.fxc` и `lightmappedgeneric_flashlight_vs20.fxc`. `make build`, «запуск hl2dm», «кадр dig_base» → `stock_final`; `tga_diff stock stock_final --max <N>` → код 0.

- [ ] **Step 16: Результаты в спеку, конец сессии**
`## Результаты этапа 1` по-русски, с цифрами и строками логов: поля Produces, окружение (версии, что чинили), почему выбран вариант, проверки 1–6, клетки бокса и `SPIKE_BOX`. Не прошла 3, 4 или 5 — ещё вывод: задачи 2–14 не начинаются, правится план задач 3, 5, 7, 8 (для 5 — запасной вариант из рисков: без столкновений тени с миром в выкопанных клетках), и после коммита стоп. Minecraft — процедурой из шапки (Half-Life запущен с шага 15), затем `cmd 'quit'`, `Wait-Process -Name hl2mp_win64 -Timeout 120 -ErrorAction SilentlyContinue`; `python tools/game/test_session.py status` → `half-life: not running`, `minecraft: none`; `make test-stop` → `test session off` для обоих движков; `git add docs/superpowers/specs/2026-10-17-dig-through-walls-design.md`, `git commit -m "docs: the dig prototype's stage 1 results: how the wall shader reaches the map, and what the spike showed" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`.


---

### Task 2: Ядро C++: тесты и вырезание выкопанных кубов из брашей

**Files:**
- Create: `source/src/core/hc_dig_material.h`, `source/src/core/hc_dig_clip.h`, `tools/debug/core_tests.cpp`, `tools/debug/core_tests.py`
- Modify: `source/src/core/hc_collision_shapes.h`, `Makefile`
- Test: `tools/debug/core_tests.cpp`

**Interfaces** (`Convex` = `ColPrimitives::Convex`):
- Consumes: `ColPrimitives`, `triangulate`, `voxelize_region` (`hc_collision_shapes.h`); из Task 1 — инструменты и `.vcxproj` обоих движков (`tidy.py` читает все четыре и при списке файлов).
- Produces (namespace `halfcraft`, константы `inline constexpr`):
  - `hc_collision_shapes.h`: `std::uint32_t PRIM_DIGGABLE = 1u << 2; // proto::kTriDiggable`, `int PRIM_MATERIAL_SHIFT = 8;`, `std::uint32_t PRIM_MATERIAL_MASK = 0xFFu << PRIM_MATERIAL_SHIFT;`; последнее поле `std::vector<std::uint32_t> Convex::plane_flags` (пуст или по одному на плоскость); `bool convex_face(const Convex& cvx, std::size_t plane, std::vector<std::array<float, 3>>& poly)`; `std::uint32_t face_flags(const Convex& cvx, std::size_t plane)` (материал плоскости вместо материала куска, прочие биты OR: уточнение R7, иначе 1 | 4 = `DIG_DIRT`).
  - `hc_dig_material.h`: `enum DigMaterialId : std::uint8_t { DIG_NONE, DIG_STONE, DIG_STONE_BRICKS, DIG_IRON, DIG_PLANKS, DIG_DIRT, DIG_GLASS };` (0–6), `std::uint8_t dig_material(char game_material)`, `std::uint32_t dig_flags(std::uint8_t material)`.
  - `hc_dig_clip.h`: `struct DigBox { float lo[3], hi[3]; };`, `float DIG_SLIVER = 1.0f / 1024;`, `void merge_cells(std::vector<std::array<int, 3>> cells, std::vector<DigBox>& out)` (заменяет `out`), `void subtract_boxes(const Convex& in, const std::vector<DigBox>& boxes, std::uint32_t cut_flags, std::vector<Convex>& out)` (дописывает).
  - `core_tests.cpp`: `CHECK(condition)`, `int run_tests()`.

«Прогнать» = `make core-test`, проходит: `core_tests: <n> checks passed`, код 0. «Линтеры» = `python tools/lint/format.py`, `python tools/lint/tidy.py tools/debug/core_tests.cpp` → `clang-tidy: no findings`.

- [ ] **Step 1: Эталон collision_check**: `Remove-Item build/collision_check/collision_check.exe -ErrorAction SilentlyContinue` (чтобы строки cl совпали с шагом 6), `python tools/debug/collision_check.py --map d1_trainstation_02 --grid-z 0 | Out-File -Encoding utf8 build/collision_check-before.txt`; код 1 (дыры карты) не ошибка.

- [ ] **Step 2: Каркас, `core_tests.py`, цель make, падающие тесты граней**

`CHECK` считает проверки, провал печатает `FAILED <file>:<line>: <expression>` и не останавливает; `run_tests()`: `core_tests: <n> checks passed` → 0, `core_tests: <m> of <n> checks failed` → 1; `main()` как `collision_check.cpp:475-484` (`core_tests: <what>`, 2). `cube(lo, hi)`: плоскости +x, −x, +y, −y, +z, −z, границы [lo, hi]³; `D` = `PRIM_DIGGABLE`, `M(k)` = `k << PRIM_MATERIAL_SHIFT`, ≈ — 1e-4:
- `test_convex_face_gives_a_cube_its_squares`: грань `cube(0, 1)` — 4 угла на плоскости (1e-5) в 0/1, площадь ≈ 1, обход против часовой снаружи; плоскости 6 `{1,0,0,-2}` и 7 `{0,0,0,0}` → false.
- `test_face_flags_take_the_planes_material`: `flags = D|M(1)`: без `plane_flags` → `flags`; при `plane_flags = {D|M(6), 0, 0, 0, 0, 0}` грань 0 → `D|M(6)`, 1 → `flags`, а с `flags = PRIM_SKY` грань 0 → `PRIM_SKY|D|M(6)`.
- `test_triangulate_gives_each_face_its_flags`: tri `{0,0,0, 1,0,0, 0,0,1}` (flags 1) и `cube(0, 1)` (`PRIM_SKY`, `plane_flags[i] = D|M(i+1)`) → 13 треугольников: первый с flags 1, у грани `i` (по нормали обхода) 2 с `PRIM_SKY|D|M(i+1)`.

`core_tests.py` как `collision_check.py:23-49` (`build/core_tests`), та же строка cl + `/WX`, `what="core_tests build"`, `main()` возвращает код exe. `Makefile`: после `:18` `#   make core-test                 the c++ core's own tests (tools\debug\core_tests.cpp): no sdk, no game`, `core-test` в `.PHONY`, `-First 23`, после `tidy` цель `core-test:` → `python tools/debug/core_tests.py`.

- [ ] **Step 3: Прогнать: падает**: `error C3861: 'convex_face': identifier not found`, `core_tests build failed (1)`.

- [ ] **Step 4: Реализовать в `hc_collision_shapes.h`**: константы после `PRIM_SKY`; `brush_to_convex` очищает `plane_flags`; грань из цикла `triangulate` строит `voxel_detail::face_polygon(cvx, plane, poly, scratch) -> bool` с прежней арифметикой; `convex_face` даёт ему локальный scratch, `triangulate` — свои `poly`/`clipped` (без выделений на грань) и флаги `face_flags(cvx, i)`.

- [ ] **Step 5: Прогнать: проходит.**

- [ ] **Step 6: Сравнить с эталоном**: шаг 1 без `Remove-Item` в `build/collision_check-after.txt`, `Compare-Object (Get-Content build/collision_check-before.txt) (Get-Content build/collision_check-after.txt)`: разница только в `worker: ...` (время); иначе, и при `warning C<nnnn>` (collision_check без `/WX`), исправить.

- [ ] **Step 7: Линтеры** (tidy ещё и `tools/debug/collision_check.cpp source/src/core/hc_collision.cpp`) **и `make build NOPROJECTS=1`** (заголовок собирает server.dll, hl2 — с `/WX`): без `halfcraft's code has <n> warnings`.

- [ ] **Step 8: Коммит**: `git add source/src/core/hc_collision_shapes.h tools/debug/core_tests.cpp tools/debug/core_tests.py Makefile`; `git commit -m "feat: convexes carry per-face flags (plane_flags) and give their face polygons (convex_face), and make core-test runs plain checks over the pure c++ core" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`.

- [ ] **Step 9: Падающие тесты материалов, слияния и вырезания** (`#include "core/hc_dig_material.h"`, `"core/hc_dig_clip.h"`). `cut_matches(brush, cells)`: `merge_cells`, `subtract_boxes(brush, boxes, 0, pieces)`, `voxelize_region` кусков (регион 0,0,0) = вокселям браша без блоков `v / 8` из `cells`; `xyz` — клетка или угол `(x, y, z)`:
- `test_dig_material_maps_surface_materials` (`dig_material`, без `DIG_`): `'C'`→STONE, `'T'`→STONE_BRICKS, `'M' 'G' 'V' 'P'`→IRON, `'W' 'U'`→PLANKS, `'D'`→DIRT, `'Y'`→GLASS, `'F' 'S' 'N' 'Z' '\0'`→STONE; `dig_flags(DIG_STONE) == 0x104u`, `dig_flags(DIG_GLASS) == 0x604u`.
- `test_merge_cells_makes_boxes` (боксы точно): `{000, 100}` → `000–211`; L `{001, 100, 000}` → `000–211`, `001–112` по порядку; 8 клеток `[0,2)³`, одна дважды → `000–222`; `{(-3,5,-1)}` → `(-3,5,-1)–(-2,6,0)`; `{}` → пусто.
- `test_subtract_boxes_cuts_the_cells_voxels`: `cut_matches` у `cube(2, 4)` с `{222}`, `{333}`, `{222, 322, 223}`, `{422}` и у `cube(2.25f, 3.75f)` с `{333}`, `{232}`.
- `test_subtract_boxes_flags_the_cut_faces`: `cube(2, 4)` с `flags = dig_flags(DIG_STONE)` и 6 × `dig_flags(DIG_STONE_BRICKS)` в `plane_flags`, клетка `222`, `cut_flags = dig_flags(DIG_PLANKS)` → 3 куска (x > 3; y > 3 при x 2..3; z > 3 при x, y 2..3) с 7, 8, 9 плоскостями, `plane_flags` по плоскости, `flags` браша; в их `triangulate` (без вырожденных) по каждой оси площадь в плоскости 2 с обходом к −оси ≈ 3, в 4 к +оси ≈ 4, флаги в 2 и 4 — `dig_flags(DIG_STONE_BRICKS)`, в 3 — `dig_flags(DIG_PLANKS)`.
- `test_subtract_boxes_leaves_what_it_doesnt_cut`: `cube(2, 4)` с `PRIM_SKY`, в `out` уже 1 элемент, боксы `(10,10,10)–(11,11,11)` и касающийся `422–533`, `cut_flags = 7u` → `out.size() == 2`, `out[1]` = браш (плоскости, lo/hi, flags, пустой `plane_flags`); после `out.clear()` бокс `111–555` → `out.empty()`.

- [ ] **Step 10: Прогнать: падает**: `fatal error C1083: Cannot open include file: 'core/hc_dig_material.h'`.

- [ ] **Step 11: Написать `hc_dig_material.h`** (включает `hc_collision_shapes.h`): switch, `default` → `DIG_STONE`; в комментарии `CHAR_TEX_*` (`game/shared/decals.h`) → блок спеки, значения = `proto::DigMaterial` (сверит Task 4).

- [ ] **Step 12: Написать `hc_dig_clip.h`** (в шапке: из SkyCraft `skse/src/Clip.h`, MIT)
- `merge_cells` — порт `Clip::Merge` (`Clip.h:85-134`) на C++17 (`std::sort`, `count`): сортирует сам `cells` (копия `order` — `performance-unnecessary-value-param`), бокс точно до последней клетки + 1, без `kSlop` и `a_up`.
- `subtract_boxes`: каждый бокс по очереди режет все текущие куски (сначала `in`): кусок, чьих `lo`/`hi` он не пересекает (касание не в счёт), остаётся как есть, иначе даёт слои x < lo.x, x > hi.x, из середины по x — по y, из середины по x и y — по z (границы подрезаны), а бокс пропадает. Плоскость, которая у куска уже есть (та же нормаль, `|d − d'| < DIG_SLIVER`), не дублируется; новые с `cut_flags`, старые со своими (0 при пустом `in.plane_flags`); слой тоньше `DIG_SLIVER` выбрасывается.

- [ ] **Step 13: Прогнать: проходит.**

- [ ] **Step 14: Линтеры и `make lint`** (`c++: <n> files formatted`, ruff чист); DLL не пересобирать: они новых заголовков не включают.

- [ ] **Step 15: Коммит**: `git add source/src/core/hc_dig_material.h source/src/core/hc_dig_clip.h tools/debug/core_tests.cpp`; `git commit -m "feat: half-life's surface materials map to dig materials (core/hc_dig_material.h), and dug cells merge into boxes that cut out of brush convexes (core/hc_dig_clip.h)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`.


---

### Task 3: Ядро C++: дерево BSP, пустота за картой, раскладка маски

Три чистых заголовка ядра без SDK; тела `RegionVoid::any()` и членов `DigMaskLayout` пишутся в теле структуры.

**Files:**
- Create: `source/src/core/hc_bsp_tree.h`, `source/src/core/hc_dig_void.h`, `source/src/core/hc_dig_mask.h`
- Modify: `tools/debug/core_tests.cpp`, `docs/superpowers/specs/2026-10-17-dig-through-walls-design.md` (шаг 13)
- Test: `tools/debug/core_tests.cpp` (`make core-test`)

**Interfaces:**
- Consumes: `PRIM_DIGGABLE` (задача 2), `PRIM_SKY`, `COLLISION_REGION_SIZE`, `ColPrimitives` (`hc_collision_shapes.h`); `MapSlot`, `mc_to_source(double x, double y, double z, MapSlot slot, float out[3])` (`hc_units.h`); `CHECK`, `run_tests()`, `make core-test` (задача 2).
- Produces (namespace `halfcraft`; константы `inline constexpr`, функции `inline`):
  - `hc_bsp_tree.h`: `int BSP_CONTENTS_SOLID = 0x1;`, `struct BspTree { std::vector<std::array<float, 4>> planes; std::vector<std::array<int, 3>> nodes; std::vector<int> leaf_contents; int headnode = 0; };`, `int point_contents(const BspTree& tree, const float p[3]);`
  - `hc_dig_void.h`: `std::uint8_t VOID_NONE = 0, VOID_ALL = 1, VOID_SOME = 2;`, `struct RegionVoid { std::array<std::uint8_t, 512> kind{}; std::vector<std::array<std::uint64_t, 8>> layers; bool any() const; };`, `void void_region(const ColPrimitives& prims, const BspTree& tree, MapSlot slot, int rx, int ry, int rz, RegionVoid& out);`
  - `hc_dig_mask.h`: `struct DigMaskLayout { static constexpr int SIZE_XZ = 128, SIZE_Y = 64, TILES = 8, TEXTURE = 1024; int ox = 0, oy = 0, oz = 0; static DigMaskLayout around(double x, double y, double z); bool needs_recentre(double x, double y, double z) const; bool texel(int x, int y, int z, int& u, int& v) const; void shader_constants(MapSlot slot, float origin[4], float layout[4]) const; };`

- [ ] **Step 1: Написать падающие тесты дерева и пустоты**

Include `core/hc_bsp_tree.h`, `core/hc_dig_void.h`, вызовы — в `run_tests()`. Помощники: `ColPrimitives::Convex box_convex(float x0, float y0, float z0, float x1, float y1, float z1, std::uint32_t flags)` — осевой бокс (`{1,0,0,-x1}, {-1,0,0,x0}` и так же по y, z; пустой `plane_flags`); `BspTree one_plane(float nx, float ny, float nz, float dist)` — `planes {{nx, ny, nz, dist}}`, `nodes {{0, -1, -2}}`, `leaf_contents {0, BSP_CONTENTS_SOLID}`.
```cpp
void test_bsp_tree()  // at(z) = { 0, 0, z }; "p -> c": CHECK(point_contents(tree, p) == c)
	t = one_plane(0, 0, 1, 106): at(120) -> 0, at(80) -> BSP_CONTENTS_SOLID, at(106) -> 0 (on the plane: front); BspTree{}: at(80) -> 0
	copies of t, each -> 0: nodes[0][2] = -5 at(80); nodes[0][1] = 3 at(120); nodes[0][0] = 7 at(80); nodes[0] = { 0, 0, 0 } at(80) (a loop)
void test_void_region()  // void_region(prims, tree, slot{ 1, 16.0f }, 128, 0, 0, out), one out: source x 0..320, z 16..336
	prims: box_convex (1024, 0, 0)-(1024.25f, 1, 1) PRIM_DIGGABLE (block 0, sub-voxels x 0, 1); (x0, 0, 4)-(x0 + 1, 1, 5) for
	    x0 = 1027, 1029, 1030 (blocks 35, 37, 38) with flags PRIM_DIGGABLE, PRIM_SKY, 0
	prims, one_plane(0, 0, 1, 106) (solid below minecraft y 2.25): any(); kind [0] SOME, [1] [64] [38] ALL, [35] [37] NONE, [128..191] SOME,
	    [192] [511] NONE; layers.size() == 65u before indexing; each u64 of layers[0] == 0xFCFCFCFCFCFCFCFCull, layers[1] == { ~0ull, ~0ull, 0 x 6 }
	ColPrimitives{}, one_plane(1, 0, 0, 100) (solid west of x 1026.5): kind [0] [1] ALL, [2] SOME, [3] NONE; 64 layers, each u64 of [0] == 0x0F0F0F0F0F0F0F0Full
	prims, BspTree{}: !out.any() && out.layers.empty()  // out is reset
```

- [ ] **Step 2: Убедиться, что тесты падают**

`make core-test`: `C1083: Cannot open include file: 'core/hc_bsp_tree.h'`, make `Error 1`, код 2.

- [ ] **Step 3: Написать `hc_bsp_tree.h`**

`#pragma once`, только `<array>`, `<cstddef>`, `<vector>`; в комментарии: дерево `.bsp` (`models[0].headnode`; читает server.dll, задача 5) в единицах Source, `BSP_CONTENTS_SOLID` = `CONTENTS_SOLID`, ребёнок узла `< 0` — лист `-1 - child`. Обход как vbsp `PointLeafnum_r` (`src/utils/vbsp/writebsp.cpp:1370`): вперёд при `n.p - dist >= 0`. 0 при пустом дереве, индексе вне диапазона и после `nodes.size()` шагов: битый файл не займёт рабочий поток.

- [ ] **Step 4: Написать `hc_dig_void.h`**

Включает `hc_bsp_tree.h`, `hc_collision_shapes.h`, `hc_units.h`. В комментарии: листья снаружи карты твёрдые (vbsp `FillOutside`), браши мира и неба исключаются флагами; выкопанная в структурном браше клетка после вырезания (задача 7) читается пустотой. `VOID_*` = `proto::VoidKind` (сверит задача 4); `layers` — по блоку `VOID_SOME` в порядке `kind`. Точка пуста (R9), если `point_contents(...) & BSP_CONTENTS_SOLID` и она вне всех выпуклых с `PRIM_DIGGABLE | PRIM_SKY` (внутри — все `n.p + d <= 0`). Блок: 9 точек (центр, углы на 1/16 и 15/16), при смеси — 512 центров `(s + 0.5) / 8`; все пустые → `VOID_ALL`, ни одной → `VOID_NONE`, иначе `VOID_SOME` с битами. Выпуклые с этими флагами собираются раз за вызов и проверяются (сначала по `lo`/`hi`) только у твёрдых точек; `out` сбрасывается.

- [ ] **Step 5: Убедиться, что тесты проходят**

`make core-test` → `core_tests: <n> checks passed`, код 0; провал — `FAILED <file>:<line>: <expression>`, код 2.

- [ ] **Step 6: Отформатировать и проверить линтерами**

`python tools/lint/format.py`, затем с `--check` → `c++: N files formatted`; `python tools/lint/tidy.py --engine hl2dm tools/debug/core_tests.cpp` → `clang-tidy: no findings` (заголовки tidy видит через этот файл).

- [ ] **Step 7: Коммит**

`git add source/src/core/hc_bsp_tree.h source/src/core/hc_dig_void.h tools/debug/core_tests.cpp`
`git commit -m "feat: core walks a .bsp's tree and finds the void behind a map's walls, per block and per sub-voxel, for minecraft to make rock of" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`

- [ ] **Step 8: Написать падающий тест раскладки маски**

Include `core/hc_dig_mask.h`:
```cpp
void test_dig_mask_layout()
	a = DigMaskLayout::around(0.5, 70.2, -3.0): ox, oy, oz == -64, 32, -80 (centre 0, 64, -16); !a.needs_recentre(0.5, 70.2, -3.0)
	needs_recentre false | true: (±32.0 | ±32.5, 64, -16), (0, 64, 16.0 | 16.5), (0, 80.0 | 80.5, -16), (0, 48.0 | 47.5, -16)
	texel: (-64, 32, -80) -> (0, 0), (-59, 41, -73) -> (133, 135), (63, 95, 47) -> (1023, 1023); false at x -65 | 64, y 31 | 96, z -81 | 48 of (-64, 32, -80)
	slot{ 1, 16.0f }, b = around(1030.5, 70.2, -3.0): 960, 32, -80; shader_constants: origin[0..2] == -2560.0f, 3200.0f, 1296.0f,
	    std::fabs(origin[3] - 0.025f) < 1e-7f; layout == 1.0f / 1024.0f, 128.0f, 8.0f, 1.0f
	// R4's arithmetic: mc_to_source(965.5, 41.5, -72.5, slot, s); i, j, k = static_cast<int>(std::floor(d * origin[3])) for
	//   d = s[0] - origin[0], origin[1] - s[1], s[2] - origin[2]: 5, 7, 9; b.texel(965, 41, -73): u == (k % 8) * 128 + i, v == (k / 8) * 128 + j
```

- [ ] **Step 9: Убедиться, что тест падает**

Как шаг 2, с `'core/hc_dig_mask.h'`.

- [ ] **Step 10: Написать `hc_dig_mask.h`**

Включает только `hc_units.h` и `<cmath>`; в client.dll — после заголовков SDK и `tier0/valve_minmax_off.h` (как `source/src/client/hc_blocks.cpp:11`). Doc-комментарии:
- слои по y плиткой 8×8, R = 255 у выкопанной клетки (читает шейдер задачи 9); `ox, oy, oz` — блок текселя (0, 0);
- `around`: `ox, oy, oz = floor16(x, y, z) - (64, 32, 64)`, `floor16` — вниз до кратного 16;
- `needs_recentre`: от центра `(ox + 64, oy + 32, oz + 64)` дальше 32 по x или z, или 16 по y;
- `texel`: `i, j, k = x - ox, z - oz, y - oy`, `u = (k % 8) * 128 + i`, `v = (k / 8) * 128 + j`;
- `shader_constants` (`c20`, `c21`): `origin = {X0 = (ox - x_blocks) * 40, Y1 = -oz * 40, Z0 = oy * 40 + grid_z, 1 / 40}`, клетка точки Source p — `(floor((p.x - X0) / 40), floor((Y1 - p.y) / 40), floor((p.z - Z0) / 40))`; `layout = {1 / 1024, 128, 8, 1}`, `[3] = 0` выключает маску.

- [ ] **Step 11: Убедиться, что тест проходит**

Как шаг 5, вместе с тестами шагов 1–5.

- [ ] **Step 12: Отформатировать и проверить линтерами**

Как шаг 6.

- [ ] **Step 13: Внести уточнение в спеку**

«## Сторона Source: client.dll и шейдеры», «Маска для шейдера»: «когда игрок уходит больше чем на 32 блока от центра области» (перенос после «чем») → «когда игрок уходит от центра области больше чем на 32 блока по горизонтали или на 16 по вертикали (уточнено при планировании)», переносы по ширине файла.

- [ ] **Step 14: Коммит**

`git add source/src/core/hc_dig_mask.h tools/debug/core_tests.cpp docs/superpowers/specs/2026-10-17-dig-through-walls-design.md`
`git commit -m "feat: the dig mask's layout in core: 128x64x128 cells around the player in a 1024x1024 texture, recentred past 32 blocks across or 16 up or down, and the pixel shader's constants that find a cell" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`


---

### Task 4: Протокол

Схема общей памяти получает всё, что нужно копанию (R6). Новых сообщений пока никто не шлёт, а неизвестные типы обе игры пропускают.

**Files:**
- Modify: `protocol/schema/{render,collision,host_state}.toml`; `make proto` перепишет `protocol/halfcraft_protocol.h` и `minecraft/src/main/java/dev/halfcraft/link/Proto.java`
- Modify: `source/src/core/hc_collision.cpp`, `source/halfcraft_server.vpc`, `tools/halfcraft/link.py`; при предупреждении сборки или tidy — `source/src/core/hc_dig_material.h`, `hc_dig_void.h`, `hc_bsp_tree.h`
- Test: скрипт кольца (шаги 5, 7), сборка и линтеры (шаги 10-13)

**Interfaces:**
- Consumes: Task 2 — `PRIM_DIGGABLE`, `DIG_NONE` … `DIG_GLASS` (`hc_dig_material.h`); Task 3 — `VOID_NONE/ALL/SOME` (`hc_dig_void.h`, включает `hc_bsp_tree.h`).
- Produces: пункты шагов 1-3 в `halfcraft::proto` (`protocol/halfcraft_protocol.h`) и они же в `dev.halfcraft.link.Proto` как `int` (`REN_DUG`, `COL_VOID`, `TRI_DIGGABLE`, `DIG_NONE` … `DIG_STONE_BRICKS` … `DIG_GLASS`, `VOID_NONE/ALL/SOME`, `HOST_DIG_OFF`), плюс `long REN_DUG_SX/SY/SZ/COUNT` = 0x00/0x04/0x08/0x0C и `int REN_DUG_BYTES = 0x10`; `CollisionRing.void` (`tools/halfcraft/link.py`).

- [ ] **Step 1: `render.toml`**

Строки шагов 1-3 — до правок: вставлять снизу вверх в порядке шага, таблицы разделять пустой строкой. После `[structs.RenSolids]` (78-81):
```toml
[structs.RenDug]
[structs.RenDug.fields]
"sx, sy, sz" = { type = "i32", doc = "section coords, as in RenSection" }
count = { type = "u32", doc = "dug blocks (0: none, and no bitset follows)" }
```
После `kRenSolids` (42-46), как и он без `java_class`:
```toml
kRenDug = { value = 11, doc = """
RenDug + a 512-byte bitset (bit x + 16z + 256y): which blocks of a section
Minecraft has dug out of the host's map (its halfcraft:dug attachment): the host
stops drawing its walls there and cuts them out of the collision it sends.
Sent after its kRenSection and whenever they change; count 0: none, and no
bitset follows. kRenClearAll drops them too""" }
```

- [ ] **Step 2: `collision.toml`**

После `kColBlockSky` (37):
```toml
[enums.VoidKind]
type = "u8"
doc = "how much of a block is void (kColVoid): in a solid leaf of the map's BSP tree and in no world brush, out of the map"

[enums.VoidKind.items]
kVoidNone = 0
kVoidAll = { value = 1, doc = "all of it" }
kVoidSome = { value = 2, doc = "some of it: its sub-voxels follow" }
```
После `kTriSky` (31):
```toml
kTriDiggable = { bit = 2, doc = "a face of a world brush (not the sky's) that Minecraft can dig out" }

[enums.DigMaterial]
type = "u8"
doc = "the Minecraft block a diggable face (kTriDiggable) breaks like and drops, from its surface's game material"

[enums.DigMaterial.items]
kDigNone = { value = 0, doc = "none given: a diggable face with it digs as kDigStone" }
kDigStone = { value = 1, doc = "minecraft:stone: concrete (C) and every other game material" }
kDigStoneBricks = { value = 2, doc = "minecraft:stone_bricks: tile (T)" }
kDigIron = { value = 3, doc = "minecraft:iron_block: metal, grate, vent, computer (M G V P)" }
kDigPlanks = { value = 4, doc = "minecraft:oak_planks: wood (W; U too, which Half-Life 2 doesn't use)" }
kDigDirt = { value = 5, doc = "minecraft:dirt: dirt (D), which Half-Life 2's cardboard and paper surfaces are too" }
kDigGlass = { value = 6, doc = "minecraft:glass: glass (Y)" }
```
В `[enums.ColTriFlags]` после `type = "u32"` (27): `note = "bits 8-15: the face's DigMaterial, with kTriDiggable"`. После `kColTris` (24):
```toml
kColVoid = { value = 4, doc = """
payload: ColRegion (count = its kVoidSome blocks) + u8 kind[512], the VoidKind of block
bx + 8 * bz + 64 * by of the region, then u64 bits[8] for each kVoidSome block in that order
(bits[y] bit z * 8 + x, as in ColBlock). Sent before the region's kColRegion when any of its
blocks has void; a kColRegion without one before it means none""" }
```

- [ ] **Step 3: `host_state.toml`**: между `kHostSeated` (25) и `kHostTakeover`
```toml
kHostDigOff = { bit = 4, doc = "hc_dig 0, or no shader DLL: Minecraft digs no new holes in the host's map (dug ones stay)" }
```

- [ ] **Step 4: Запустить `make proto`**: `protocol: layout 0x9064AE03, 37 structs, 191 MB mapped` (было `0x0E992381, 36 structs`; в хэш не входят только doc и имена Java, поэтому другой хэш значит, что шаги 1-3 выполнены не дословно). `git status --short`: изменены три `.toml`, заголовок и `Proto.java`.

- [ ] **Step 5: Проверка кольца: должна упасть** (Git Bash, корень репозитория)
```bash
python - <<'EOF'
import struct, sys
sys.path.insert(0, "tools")
from halfcraft import protocol
from halfcraft.link import CollisionRing
s = protocol.load(); r = s.regions["CollisionRing"]; v = lambda name: int(s.value(name))
p = struct.pack("<6iII", 0, 0, 0, 7, 7, 7, 1, 1) + bytes([2]) + bytes(511) + struct.pack("<8Q", *range(8))  # one kVoidSome block
m = struct.pack("<II", v("kColVoid"), len(p)) + p
link, d = bytearray(r.end), r.at + v("kColRingDataOff")
link[d : d + len(m)] = m
struct.pack_into("<Q", link, r.at + v("kColRingHeadOff"), len(m))
print([(x.type, x.size) for x in CollisionRing(link, s).messages()])
EOF
```
Код выхода 1, `the collision ring doesn't read from its lap's start (0) to its head (616): ...`.

- [ ] **Step 6: `CollisionRing` читает `kColVoid` (`tools/halfcraft/link.py`)**, иначе после Task 5 падают `dump_collision.py` и `probe_feet.py`. В `__init__` (56-59) — `self.void = int(schema.value("kColVoid"))` и в `self.item_bytes` `self.void: schema.structs["ColBlock"].field("bits").size` (64); в `plausible` (73-75) для `kColVoid` — `size == region_bytes + 512 + count * 64` (512 байт `kind`), для остальных типов правило прежнее.

- [ ] **Step 7: Проверка кольца: должна пройти**: скрипт шага 5 печатает `[(4, 608)]`.

- [ ] **Step 8: Константы ядра = протокол (`source/src/core/hc_collision.cpp`)**: `#include "hc_dig_material.h"` и `"hc_dig_void.h"` перед `"hc_log.h"` (8). После `static_assert(PRIM_SKY == proto::kTriSky);` (302) — `static_assert` на `PRIM_DIGGABLE == proto::kTriDiggable`, на `static_cast<int>(DIG_X) == proto::kDigX` для семи `DIG_*` (без каста будет `-Wenum-compare`) и на `VOID_X == proto::kVoidX` для трёх `VOID_*`.

- [ ] **Step 9: `source/halfcraft_server.vpc`**: в папку Core строки `$File	"$HALFCRAFTDIR\src\core\<имя>"`: `hc_bsp_tree.h` после `hc_atomic.h` (66), `hc_dig_material.h` и `hc_dig_void.h` после `hc_collision_shapes.h` (69).

- [ ] **Step 10: Запустить `make build`** (игры закрыты; без `NOPROJECTS=1`, потому что изменился `.vpc`): `built hl2dm with ...`, `built hl2 with ...`, без `halfcraft's code has N warnings`. server.dll собирает заголовки ядра впервые, в том числе в 32-битной hl2: предупреждение в них исправить, прогнать `make core-test` (код 0) и повторить сборку.

- [ ] **Step 11: Запустить `python tools/lint/tidy.py source/src/core/hc_collision.cpp`**: `clang-tidy: 2 checks of 1 files with ...`, `clang-tidy: no findings`; находки в заголовках ядра — как в шаге 10.

- [ ] **Step 12: Запустить `make lint`**: `protocol: layout 0x9064AE03, ...`, `c++: N files formatted`, ruff и `spotlessCheck` без замечаний.

- [ ] **Step 13: Запустить `make mc`**: `BUILD SUCCESSFUL`.

- [ ] **Step 14: Коммит** (с заголовками ядра, если их правили в шагах 10-11)
```
git add protocol/schema protocol/halfcraft_protocol.h minecraft/src/main/java/dev/halfcraft/link/Proto.java source/src/core/hc_collision.cpp source/halfcraft_server.vpc tools/halfcraft/link.py
git commit -m "feat: the protocol has what digging needs (kRenDug, kTriDiggable and DigMaterial, kColVoid, kHostDigOff), and the debug tools read kColVoid" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```


---

### Task 5: server.dll: дерево BSP карты и пустота в потоке коллизии

До Task 10 Minecraft пишет на каждое `kColVoid` `HalfCraft: unknown collision message 4` (`HostCollision.java:288`), так и должно быть.

**Files:**
- Modify: `source/src/core/{hc_bsp_tree.h,hc_collision.h,hc_collision.cpp}`, `source/src/shared/hc_bsp.h`, `source/src/server/hc_world_collision.{h,cpp}`, `tools/debug/{dump_collision.py,collision_check.cpp,collision_check.py}`; `source/src/core/hc_dig_void.h` — при нужде (шаг 11)
- Test: `tools/debug/core_tests.cpp`

**Interfaces:**
- Consumes: Task 2 — `PRIM_DIGGABLE`, `CHECK`, `run_tests()`; Task 3 — `BspTree`, `BSP_CONTENTS_SOLID`, `point_contents`, `RegionVoid`, `void_region`; Task 4 — `proto::kColVoid`, `proto::ColRegion`, `hc_dig_void.h` уже в `hc_collision.cpp` и `halfcraft_server.vpc`, `CollisionRing.void`, `kVoidAll`/`kVoidSome`.
- Produces:
  - `hc_bsp_tree.h`: `inline constexpr std::size_t BSP_PLANE_BYTES = 20, BSP_NODE_BYTES = 32, BSP_LEAF_BYTES_V0 = 56, BSP_LEAF_BYTES_V1 = 32;`, `inline bool read_bsp_tree(const std::vector<std::uint8_t>& planes, const std::vector<std::uint8_t>& nodes, const std::vector<std::uint8_t>& leaves, int leaf_version, int headnode, BspTree& out);`
  - `hc_bsp.h`: `bool bsp::BspFile::lump_bytes(int index, std::vector<std::uint8_t>& out, int& version);`
  - `hc_collision.h`: `struct VoidQuery { std::shared_ptr<const BspTree> tree; MapSlot slot; };`, `virtual VoidQuery CollisionSource::void_query() const { return {}; }`, `VoidQuery CollisionStreamer::Job::voids;`, private `void CollisionStreamer::send_voids(const Job& job);`
  - `WorldCollision`: `VoidQuery void_query() const override;`, член `std::shared_ptr<const BspTree> bsp_;`
  - `kColVoid`: заголовок `ColRegion` (как у `kColRegion`, `count = layers.size()`) + `u8 kind[512]` + `u64[8] × count`, перед `kColRegion` региона, только при `any()`.
  - Лог (ждёт Task 14): `void: maps/<map>.bsp's tree: <N> nodes, <M> leaves (<K> solid); the engine (<call>) agrees at <A> of 512 points` (`<call>`: `world collideable` | `point contents`); `void: can't read maps/<map>.bsp's tree: nothing behind its walls is rock there`.
  - `dump_collision.py`: `void: <R> regions, <A> blocks wholly void, <S> partly`.

- [ ] **Step 1: Базовое время**: `python tools/debug/collision_check.py --map d1_trainstation_02 --grid-z 0`, записать среднее из `worker: ...` (код 1 — дыры под полом, не к задаче).

- [ ] **Step 2: Падающий тест `test_read_bsp_tree`** (вызов в `run_tests()`; `<cstring>`, если нет)
```cpp
void test_read_bsp_tree()  // put(bytes, at, v): memcpy of v; "p -> c": point_contents(tree, p) == c
	// planes(BSP_PLANE_BYTES): normal.z (at 8) 1.0f; nodes(BSP_NODE_BYTES): plane 0, front (at 4) -1, back (at 8) -2
	// leaves(2 * BSP_LEAF_BYTES_V1), leaves_v0(2 * BSP_LEAF_BYTES_V0): leaf 1 BSP_CONTENTS_SOLID; leaves_v0 at 32: 7
	CHECK(read_bsp_tree(planes, nodes, leaves, 1, 0, tree)); planes/nodes/leaf_contents 1/1/2, headnode 0; (0,0,10) -> 0, (0,0,-10) -> SOLID
	CHECK(read_bsp_tree(planes, nodes, leaves_v0, 0, 0, tree)); (0,0,-10) -> SOLID
	refused (false; out, given headnode 5, left BspTree{}): leaf version 2; leaves of BSP_LEAF_BYTES_V1 + 1 bytes;
	    headnode 1; nodes with plane 1 | back -3 (no leaf 2) | back 0 (a child comes after its node)
```

- [ ] **Step 3: `make core-test` падает**: `C2065`/`C3861` на `BSP_PLANE_BYTES`, `read_bsp_tree`, `make: *** [Makefile:<n>: core-test] Error 1`, код 2.

- [ ] **Step 4: `read_bsp_tree` в `hc_bsp_tree.h`**: поля через `std::memcpy` по раскладке `dplane_t`, `dnode_t`, `dleaf_t` (`dleaf_version_0_t` при версии 0) из `public/bspfile.h`. Отказ (`out = BspTree{}`) — в случаях теста и без узлов или листьев; ребёнок-узел `c` узла `n` годен только при `n < c < nodes`: vbsp пишет узлы в прямом порядке (у `d1_trainstation_02` — все 3598), так обход конечен.

- [ ] **Step 5: `make core-test` проходит**: код 0, `core_tests: <n> checks passed`.

- [ ] **Step 6: `lump_bytes` в `hc_bsp.h`** после `lump<T>`: `version = header_.lumps[index].version; return lump(index, out);`, `#include <cstdint>`.

- [ ] **Step 7: Дерево карты в `WorldCollision::reset`**
- `std::shared_ptr<const BspTree> read_void_tree(const char* map_name)` после `read_sky_brushes`, открывает файл как она: `lump_bytes` `LUMP_PLANES`, `LUMP_NODES`, `LUMP_LEAFS` (его версия — `leaf_version`), `lump(LUMP_MODELS, models)` (`dmodel_t`, не пуст), `read_bsp_tree(..., models[0].headnode, *tree)`; отказ — предупреждение (Produces), `nullptr`. `reset`: `bsp_ = map_name ? read_void_tree(map_name) : nullptr;`.
- Сверка только для лога: 512 центров сетки 8×8×8 в границах `models[0]` ± 64 юнита; бит `BSP_CONTENTS_SOLID` у `point_contents(*tree, p)` против `CONTENTS_SOLID` у `enginetrace->GetPointContents_Collideable(GetWorldEntity()->GetCollideable(), v)` (`world collideable`), а если тот нигде не дал `CONTENTS_SOLID` — у `enginetrace->GetPointContents(v)` (`point contents`).
- `static_assert` размеров `dplane_t`, `dnode_t`, `dleaf_t`, `dleaf_version_0_t` против `BSP_*_BYTES`; `#include "world.h"` до `tier0/valve_minmax_off.h`.

- [ ] **Step 8: Пустота в стримере**
- `harvest`: `job.voids = source.void_query();`.
- `send_voids`: при дереве — `void_region` региона задачи в `RegionVoid rv`, `kColVoid` только при `rv.any()` (`kColRegion` без него — «пустоты нет»); пустые `prims` не пропускать: регион за картой — сплошная пустота.
- `proto::ColRegion region_header(int rx, int ry, int rz, std::uint32_t epoch, std::uint32_t count)` в анонимном пространстве имён — для `send_triangles`, `voxelize`, `send_voids` (`const Job&` нельзя: `Job` в `private:`, C2248).
- `worker_loop`: `send_voids(job);  // before the region's kColRegion` между `send_triangles` и `voxelize`.

- [ ] **Step 9: `dump_collision.py`**: на регион — последний `ring.void` (ключ как у `kColRegion`), в 512 байтах `kind` после заголовка — байты `kVoidAll` и `kVoidSome`; строка из Produces — после числа регионов.

- [ ] **Step 10: `collision_check` с пустотой** (`core/hc_bsp_tree.h`, `core/hc_dig_void.h` после `core/hc_collision_shapes.h`; `void_region` — в шапку `.cpp` и docstring `.py`)
- `World::brush_flags` (на браш): `PRIM_DIGGABLE` при `contents & (0x1 | 0x2 | 0x8)`, иначе 0 (как `WorldCollision` с Task 6; что небо тут тоже копаемое, пустоту не меняет); `gather`: `cvx.flags = world.brush_flags[b]`.
- `check()` после `read_world`: `read_bsp_tree` по `bsp.lump<std::uint8_t>` трёх лампов, `bsp.version(LUMP_LEAFS)` и `head_node` модели 0; отказ — `can't read the .bsp's tree` в stderr, код 2; иначе `tree: %zu nodes, %zu leaves (%zu solid)`.
- `void_region(prims[ry], tree, slot, rx, ry, rz, voids)` в замеряемом участке после `voxelize_region`, у каждого региона, и без геометрии; после `worker:` — `void: %llu regions with void, %llu blocks wholly void, %llu partly`.

- [ ] **Step 11: Проверить на карте**: команда шага 1 → `tree: 3598 nodes, 3715 leaves (1127 solid)`, в `void:` три числа > 0, среднее `worker:` ≤ 2× шага 1. Иначе в `void_region` отсечь точки вне `lo`/`hi` выпуклой до её плоскостей (если Task 3 не сделала), `make core-test`, повтор; снова больше — оба средних отдельным `-m` в коммит и продолжать.

- [ ] **Step 12: `make format`, затем `make lint`**: обе с кодом 0, `protocol: layout 0x...`, `c++: <N> files formatted`; провал — любая ошибка, находка ruff или формата.

- [ ] **Step 13: `make build`** (игры закрыты): `built hl2dm with ...`, `built hl2 with ...`; провал — `halfcraft's code has <N> warnings (above): fix them`.

- [ ] **Step 14: Commit**
```
git add source/src tools/debug
git commit -m "feat: server.dll streams the void behind the map's walls (kColVoid) from its bsp tree; collision_check and dump_collision read it" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```


---

### Task 6: server.dll: материалы брашей и флаг копания

Копаемые браши мира (не небо, contents `SOLID | WINDOW | GRATE`) шлют в Minecraft треугольники с `kTriDiggable` и `DigMaterial` своей стороны; уточнения R7 задача вносит в спеку (шаг 12).

**Files** (задача 5 правит те же функции: место искать по символам):
- Modify: `source/src/core/hc_dig_material.h`, `source/src/server/hc_world_collision.{h,cpp}`, `source/src/server/hc_server.cpp`, `README.md`, `docs/superpowers/specs/2026-10-17-dig-through-walls-design.md`
- Test: `tools/debug/core_tests.cpp`

**Interfaces:**
- Consumes: задача 2 — `PRIM_DIGGABLE`, `PRIM_MATERIAL_SHIFT`, `Convex::plane_flags`, `convex_face(cvx, plane, poly)`, `DIG_NONE` … `DIG_GLASS`, `dig_material`, `dig_flags`, `CHECK`, `run_tests()`; в коде — `brush_to_convex`, `mc_to_source`.
- Produces:
  - `inline std::uint8_t brush_material(const std::vector<std::uint8_t>& sides);` (`hc_dig_material.h`);
  - `WorldCollision` private: `struct BrushDig { std::vector<std::uint8_t> sides; std::uint8_t material = DIG_STONE; };` (`sides[p]` — плоскости `p` по `GetBrushInfo`, не `DIG_NONE`), `std::unordered_map<int, BrushDig> brush_dig_;`, `const BrushDig& brush_dig(int brush, const CUtlVector<Vector4D>& planes);` (только при `diggable()`), `bool diggable(int brush, int contents) const;`; public `void log_dig_material(const Vector& eye, const Vector& forward);`;
  - копаемый кусок: `cvx.flags |= PRIM_DIGGABLE` без битов материала, `cvx.plane_flags[p] = dig_flags(sides[p])`;
  - строки лога шага 8 ровно в этом виде (их ждут задачи 12 и 14).

- [ ] **Step 1: Написать падающий тест `test_brush_material`** (`core_tests.cpp`, вызов из `run_tests()`)
```cpp
void test_brush_material()
{
	using namespace halfcraft;
	CHECK(brush_material({}) == DIG_STONE);
	CHECK(brush_material({ DIG_NONE, DIG_NONE }) == DIG_STONE);
	CHECK(brush_material({ DIG_NONE, DIG_DIRT }) == DIG_DIRT);
	CHECK(brush_material({ DIG_PLANKS, DIG_NONE, DIG_STONE, DIG_PLANKS }) == DIG_PLANKS);
	CHECK(brush_material({ DIG_GLASS, DIG_IRON, DIG_IRON, DIG_GLASS }) == DIG_GLASS);  // a tie: the first
	CHECK(brush_material({ DIG_NONE, DIG_IRON, DIG_GLASS, DIG_GLASS, DIG_IRON }) == DIG_IRON);
}
```

- [ ] **Step 2: `make core-test`: падает** на `error C3861: 'brush_material': identifier not found`, затем `core_tests build failed (1)`; make пишет `Error 1` и выходит с кодом 2.

- [ ] **Step 3: Реализовать `brush_material` в `hc_dig_material.h`** (+ `#include <vector>`).

- [ ] **Step 4: `make core-test`: проходит**: код 0, `core_tests: <n> checks passed`, без `FAILED`.

- [ ] **Step 5: Закоммитить**
```
git add source/src/core/hc_dig_material.h tools/debug/core_tests.cpp
git commit -m "feat: brush_material picks a brush's dig material from its sides' votes, the first on a tie, stone without any" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 6: Объявить всё в `hc_world_collision.h`**: `#include "core/hc_dig_material.h"` после `core/hc_collision.h`; `BrushDig` сразу после `using Hull = ...`, до методов (тип возврата ищется в точке объявления: ниже будет C2143/C4430); `add_brushes` без `const`, рядом `brush_dig` и `diggable`; `brush_dig_` после `sky_brushes_`; `log_dig_material` после `track_movers`; в шапке у world brushes — копаемые браши и материалы их сторон.

- [ ] **Step 7: Реализовать материалы и флаги в `hc_world_collision.cpp`**. После `PLAYER_SOLID_BRUSHES`:
```cpp
constexpr int   DIGGABLE_BRUSHES = CONTENTS_SOLID | CONTENTS_WINDOW | CONTENTS_GRATE;  // what minecraft may dig (not player clips)
constexpr int   UNSEEN_SURFACES = SURF_NODRAW | SURF_SKY | SURF_SKY2D;
constexpr float SIDE_PROBE_UNITS = 1.0f;
constexpr float SAME_NORMAL = 0.99f;
```
- `std::uint8_t side_material(const Vector& centre, const Vector& normal)` рядом с `read_sky_brushes`: `TraceRay` от `centre + normal * SIDE_PROBE_UNITS` до `centre - normal * SIDE_PROBE_UNITS`, `DIGGABLE_BRUSHES`, `CTraceFilterWorldOnly`; `DIG_NONE` при `startsolid`, `fraction >= 1`, `DotProduct(tr.plane.normal, normal) < SAME_NORMAL`, `surface.flags & UNSEEN_SURFACES`, `surfaceProps < 0` или null из `physprops->GetSurfaceData`, иначе `dig_material(static_cast<char>(data->game.material))`.
- `brush_dig` при промахе кэша: пробный кусок `brush_to_convex(<planes as {x, y, z, w}>, count, {MIN_COORD_FLOAT x3}, {MAX_COORD_FLOAT x3}, MapSlot{}, probe)` (полные грани при любом регионе сбора); `votes[p] = convex_face(probe, p, poly) ? side_material(src, planes[p].AsVector3D()) : DIG_NONE`, `src` — среднее углов `poly` через `mc_to_source(..., MapSlot{}, ...)`; `material = brush_material(votes)`, `sides[p] = votes[p] != DIG_NONE ? votes[p] : material`.
- `diggable`: `(contents & DIGGABLE_BRUSHES) != 0` и браш не в `sky_brushes_` (как в `add_brushes`); `reset` ещё и вызывает `brush_dig_.clear()`.
- `add_brushes`: ветка `PRIM_SKY` как есть, иначе при `diggable(brushes[i], contents)` — `dig = brush_dig(brushes[i], planes)`, `cvx.flags |= PRIM_DIGGABLE` и `plane_flags[p] = dig_flags(dig.sides[p])` (размеры не совпали — всем `dig_flags(dig.material)`).

- [ ] **Step 8: Реализовать `WorldCollision::log_dig_material`**: трасса `eye` → `eye + forward * 8192` (`DIGGABLE_BRUSHES`, `CTraceFilterWorldOnly`); `p = tr.endpos - tr.plane.normal * 0.5f`, браш — первый из `GetBrushesInAABB(p - h, p + h, &list, DIGGABLE_BRUSHES)` (`h = Vector(0.25f, 0.25f, 0.25f)`), у которого `GetBrushInfo` дал `contents & DIGGABLE_BRUSHES` и все плоскости `n.p - w <= 0.01`; сторона — плоскость с `DotProduct(n, tr.plane.normal) >= SAME_NORMAL` и наименьшим `|n.endpos - w|`, иначе -1. Строки `log_info`:
- промах или нет браша: `hc_debug_dig_material: no world brush under the crosshair`;
- `!diggable` (только небо): `hc_debug_dig_material: brush %d isn't diggable (contents 0x%x%s)`, `%s` = `", sky"` у неба;
- иначе `hc_debug_dig_material: brush %d side %d of %d: %s (%s, '%c') -> side %d, brush %d`: браш, сторона, число плоскостей, `tr.surface.name`, `physprops->GetPropName(tr.surface.surfaceProps)`, `game.material`, `brush_dig(...).sides[side]` (0 при -1), `.material`.

- [ ] **Step 9: Добавить команду в `hc_server.cpp` и строку в README**: public `void log_dig_material()` у `HalfCraftServerSystem` после `check_voxels`, как он: без `UTIL_GetLocalPlayer()` — `hc_debug_dig_material: no player`, иначе `world_.log_dig_material(player->EyePosition(), forward)` с `forward` из `player->EyeVectors`. После `CON_COMMAND(hc_debug_voxels, ...)` — `CON_COMMAND(hc_debug_dig_material, "halfcraft: log the dig material of the world brush under the crosshair: its side's surface, game material and DigMaterial")`: `UTIL_IsCommandIssuedByServerAdmin()`, затем `halfcraft::g_server_system.log_dig_material()`. `README.md`: после `` `hc_debug_voxels [regions]` (...) `` через запятую `` `hc_debug_dig_material` (the dig material of the world brush you look at) ``.

- [ ] **Step 10: Собрать оба движка**: `make build NOPROJECTS=1` (игры закрыты, `.vpc` не меняется) → `built hl2dm with ...`, `built hl2 with ...`, `launcher: ...\\HalfCraft.exe`; провал — ошибка MSBuild или `halfcraft's code has N warnings (above): fix them`.

- [ ] **Step 11: Проверить формат**: `make lint` — код 0, `c++: N files formatted`; при `not formatted: <path>` — `make format` и снова `make lint`.

- [ ] **Step 12: Внести уточнение в спеку**: в `## Сторона Source: server.dll`, пункт «**Материалы**», после «по умолчанию камень.» вставить (переносы по ширине файла): «Сторона без чистого попадания трассы (`startsolid`, промах, другая плоскость, `NODRAW`/`SKY`/`SKY2D`) не голосует и берёт материал браша; трассы идут с маской `SOLID | WINDOW | GRATE`, грани — с пробного куска на весь браш; материал лежит только в `plane_flags` (уточнено при планировании).»

- [ ] **Step 13: Закоммитить**
```
git add source/src tools/debug/core_tests.cpp README.md docs/superpowers/specs/2026-10-17-dig-through-walls-design.md
git commit -m "feat: world brushes minecraft may dig send their triangles with the dig material of their side, traced once per brush from its surfaceprop's game material, and hc_debug_dig_material logs it for the brush under the crosshair" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```


---

### Task 7: Мост дыр и вырезание коллизии

**Files** (`core/`, `shared/`, `client/`, `server/` — в `source/src/`):
- Create: `core/hc_dug_cells.h`, `shared/hc_dig_abi.h`, `client/hc_dig.{h,cpp}`, `server/hc_dig.{h,cpp}`
- Modify: `core/hc_collision_shapes.h`, `core/hc_collision.cpp`, `core/hc_module.{h,cpp}`, `shared/hc_bridge.h`, `shared/hc_hooks.h`, `shared/hc_movement.cpp`, `client/hc_client.cpp`, `server/hc_server.cpp`, `server/hc_world_collision.{h,cpp}`, `source/halfcraft_{client,server}.vpc`, `docs/superpowers/specs/2026-10-17-dig-through-walls-design.md`
- Test: `tools/debug/core_tests.cpp`

**Interfaces:**
- Consumes: задача 2 — `DigBox`, `merge_cells`, `subtract_boxes`, `dig_flags`, `CHECK`, `run_tests()`; задача 4 — `proto::kRenDug`, `proto::RenDug`, `proto::kRenClearAll`, `proto::kHostDigOff`; задача 6 — private `WorldCollision::diggable(int brush, int contents)`, `brush_dig(int brush, planes)`, `add_brushes` без `const`.
- Produces:
  - `DugCells` (шаг 3), `clip_triangle` (шаг 4); `DugSection`, `DugSinceFn`, `HC_DUG_EXPORT` и весь `shared/hc_dig_abi.h` (шаг 7, задача 9 только подключает); `bool module_loaded(const char* module);` (`core/hc_module.h`); `bool box_dug(const Vector& mins, const Vector& maxs);` (`shared/hc_hooks.h`, обе DLL).
  - `client/hc_dig.h`: `void dig_on_message(std::uint32_t type, const std::uint8_t* payload, std::uint32_t bytes); void dig_set_slot(MapSlot slot); bool dig_shader_ready(); bool dig_enabled(); void dig_shutdown(); int dug_since(std::uint32_t since, DugSection* out, int max, std::uint32_t* now);`; экспорт `HalfCraft_DugSince`, ConVar `hc_dig`, команда `hc_debug_dig_cell`, внутренний `DigStore` (расширяют задачи 8, 9).
  - `server/hc_dig.h`: `DigState`, `outline_mc_box` (шаг 10), ConVar `hc_debug_dig`; `WorldCollision::set_dig(const DigState* dig)`.
  - Лог: `dig: no game_shader_generic_halfcraft.dll in this engine: digging is off`, `dig: game_shader_generic_halfcraft.dll is loaded`, `dig: <n> section(s) changed, <m> cell(s) dug in this map`, `hc_debug_dig_cell: cell <x> <y> <z> dug|filled`, `hc_debug_dig_cell: no wall under the crosshair`, `hc_debug_voxels: <n> voxels inside dug cells (brushes leave none; props and displacements aren't cut)`.

- [ ] **Step 1: Падающие тесты `test_dug_cells` и `test_triangle_clip`** (`#include "core/hc_dug_cells.h"`, вызовы в `run_tests()`)
```cpp
void test_dug_cells()  // bits[512] = {}; bits[0] = 1u; bits[70] = 1u << 1
	set(-1, 0, 2, bits, changed): changed.size() == 2, count() == 2, dug(-16, 0, 32), dug(-15, 2, 35), !dug(-16, 0, 33), !dug(0, 0, 32)
	changed.clear(), same bits: changed empty; bits[70] = 0: changed == {{-15, 2, 35}}, count() == 1
	cells_in({-15, 0, 32}, {-14, 1, 33}) empty; cells_in({-15.5f, .5f, 32.5f}, {-15.4f, .6f, 32.6f}) == {{-16, 0, 32}}
	changed.clear(), set(-1, 0, 2, nullptr): changed.size() == 1, count() == 0
	around: (0, 0, 0) bits[0] = 1u << 5 | 1u << 1, bits[20] = 1u; (2, 0, 0) bits[0] = 1u
	nearest({.5, .5, .5}, 24.0, 2) == {{1, 0, 0}, {5, 0, 0}}; nearest(.., 24.0, 64): size 3, [2] == {0, 0, 10}
void test_triangle_clip()  // box 0..1
	{ -4, .5f, -4,  -4, .5f, 8,  8, .5f, -4 } flags 0x305u: not empty; each: flags 0x305u, v in box +-1e-5, (e1 x e2).y > 1e-6; areas sum 1 +-1e-4
	{ .25f, .25f, .25f,  .25f, .25f, .75f,  .75f, .25f, .25f }: itself; { 2, .5f, 0,  2, .5f, 1,  3, .5f, 0 }: none
```

- [ ] **Step 2: `make core-test` падает:** `Cannot open include file: 'core/hc_dug_cells.h'`.

- [ ] **Step 3: Написать `core/hc_dug_cells.h`**
```cpp
class DugCells  // bit x + 16z + 256y of a section's 512 bytes
{
public:
	using Cell = std::array<int, 3>;
	void set(int sx, int sy, int sz, const std::uint8_t* bits, std::vector<Cell>& changed);  // nullptr: none; appends
	void clear();
	bool dug(int x, int y, int z) const;
	std::size_t count() const;
	void cells_in(const float lo[3], const float hi[3], std::vector<Cell>& out) const;  // x + 1 > lo && x < hi
	void nearest(const McVec& p, double radius, std::size_t max, std::vector<Cell>& out) const;  // by centre, nearest first
};
```
`out` заменяется; `cells_in` и `nearest` обходят только секции внутри своего бокса.

- [ ] **Step 4: `clip_triangle` после `triangulate`:** `inline void clip_triangle(const ColPrimitives::Tri& tri, const float lo[3], const float hi[3], std::vector<ColPrimitives::Tri>& out);` дописывает часть внутри бокса с обходом и флагами `tri`: Sutherland–Hodgman по 6 плоскостям, угол на плоскости — внутри, новый угол только при строгом пересечении ребра (как `Clip::Split` в SkyCraft), веер от первого угла без треугольников с `|e1 × e2| < 1e-6`.

- [ ] **Step 5: `make core-test`:** код 0, ни одного `FAILED`.

- [ ] **Step 6: Коммит**
```
git add source/src/core/hc_dug_cells.h source/src/core/hc_collision_shapes.h tools/debug/core_tests.cpp
git commit -m "feat: the cells dug out of half-life's walls by section (hc_dug_cells.h) and a triangle cut down to a box (clip_triangle), pure in core, with core tests" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 7: Мост и ABI.** В `hc_bridge.h` за `HC_SOLIDS_EXPORT`: `struct DugSection { std::int32_t sx, sy, sz; std::uint32_t count; std::uint8_t bits[512]; };` (0 — дыр нет), `DugSinceFn` как `SolidsSinceFn` с `DugSection*`, `inline constexpr char HC_DUG_EXPORT[] = "HalfCraft_DugSince";`, в шапке — строка о мосте `dug`. `shared/hc_dig_abi.h` (без SDK): `class ITexture;`, в `halfcraft` — `struct DigMaskState { ITexture* texture; float origin[4]; float layout[4]; };` (`origin` — `c20`, `layout` — `c21`), `using DigMaskFn = const DigMaskState* (*)();`, `constexpr char HC_DIG_MASK_EXPORT[] = "HalfCraft_DigMask";`, `constexpr char HC_SHADER_DLL[] = "game_shader_generic_halfcraft.dll";`. `module_loaded` = `::GetModuleHandleA(module) != nullptr`.

- [ ] **Step 8: Написать `client/hc_dig.{h,cpp}`** (include как в `hc_blocks.cpp`)
- `struct DigStore { std::mutex lock; std::map<SectionKey, std::pair<DugSection, std::uint32_t>> sections; std::uint32_t version = 0; MapSlot slot; }; DigStore& store();` в анонимном namespace (`SectionKey` — свой `std::tuple<int, int, int>`: тот, что в `hc_blocks.cpp`, локален): секция с версией изменения, опустевшие остаются, как в `Blocks::solids`; `set_dug(sx, sy, sz, bits)` (null — пусто) при той же маске версию не двигает: `kRenDug` идёт с каждой `kRenSection`.
- `dig_on_message`: `kRenDug` короче `sizeof(proto::RenDug)` — мимо, биты при `count > 0 && sizeof(proto::RenDug) + 512 <= bytes`, иначе пусто; `kRenClearAll` опустошает все. `dig_set_slot` — как `blocks_set_slot` (`KEEP_SLOT_BLOCKS = 600.0f`).
- `dug_since` — как `solids_since`, но при `!dig_shader_ready()` — 0, `*now` не трогает: что рисует Half-Life, твёрдо и для Minecraft. Его зовёт `extern "C" __declspec(dllexport)` `HalfCraft_DugSince`.
- `ConVar hc_dig("hc_dig", "1", FCVAR_ARCHIVE, ...)`: 0 — без новых дыр, хранилище и мост не трогает. `dig_shader_ready()` = `module_loaded(HC_SHADER_DLL)` на каждый вызов, строка лога — при первом и при смене; `dig_enabled()` = `hc_dig.GetBool() && dig_shader_ready()`; `dig_shutdown()` очищает `sections`.
- `hc_debug_dig_cell [<x> <y> <z> [1 dig | 0 fill]]`: бит в маске секции → `set_dug`. Без клетки — `UTIL_TraceLine(MainViewOrigin(), MainViewOrigin() + MainViewForward() * 180.0f, MASK_SOLID_BRUSHONLY, &filter, &tr)`, `CTraceFilterWorldOnly` (без player clip: клип перед стеной не перехватит луч), клетка — `floor` от `source_to_mc((tr.endpos - tr.plane.normal * 0.5f).Base(), store().slot)`; при `tr.fraction == 1.0f` — `no wall under the crosshair`.

- [ ] **Step 9: Подключить в client.dll.** `hc_client.cpp`: `dig_shutdown()` в `Shutdown`, `dig_set_slot(s.slot)` сразу за `blocks_set_slot(s.slot)` (до drain: иначе сообщения нового слота пропадут), `dig_on_message` в drain, `if (!dig_enabled()) { host.flags |= proto::kHostDigOff; }`. `halfcraft_client.vpc`: `shared\hc_dig_abi.h`, `client\hc_dig.{h,cpp}`, `core\hc_module.h`, а `core\hc_module.cpp` — в список без PCH (`:75-79`).

- [ ] **Step 10: Написать `server/hc_dig.{h,cpp}`** (`include after the sdk headers and tier0/valve_minmax_off.h`): `class DigState { public: void reset(MapSlot slot); void update(CollisionStreamer* streamer); bool dug(int x, int y, int z) const; void boxes_in(const float lo[3], const float hi[3], std::vector<DigBox>& out) const; void pvs_points(const McVec& player, std::vector<Vector>& out) const; std::size_t count() const; bool outlined() const; };`, `void outline_mc_box(const float lo[3], const float hi[3], MapSlot slot, int r, int g, int b);`.
- `update` (раз в кадр, `streamer` может быть nullptr) опрашивает client.dll как `BlockSolids::update` (`hc_block_solids.cpp:363-386`); секция с `count == 0` или дальше 600 блоков от слота — `DugCells::set(..., nullptr, ...)`; каждая изменившаяся клетка — `streamer->invalidate(клетка − 0.5, клетка + 1.5)` (до 8 регионов; бокс секции — 64, весь предел `invalidate`); при изменениях — строка `dig: ... section(s) changed`.
- `boxes_in` — `cells_in` через `merge_cells`; `pvs_points` = `nearest(player, 24.0, 64)`, центры `mc_to_source(x + 0.5, y + 0.5, z + 0.5, slot_)`. Без блокировок: всё на главном потоке.
- `ConVar hc_debug_dig("hc_debug_dig", "0", 0, ...)`: при 1 `update` раз в 0.2 с (`gpGlobals->curtime`) обводит клетки `nearest(ноги UTIL_GetLocalPlayer() в Minecraft, 16.0, 512)` — `outline_mc_box(c, c + 1, slot_, 255, 80, 0)`, 12 рёбер `NDebugOverlay::Line(..., true, 0.25f)` (`debugoverlay_shared.h`) без проверки глубины — и `outlined()` истинно до следующего `update`.

- [ ] **Step 11: Подключить в server.dll.** `hc_server.cpp`: член `DigState dig_;`, `world_.set_dig(&dig_)` в `Init()`, `dig_.reset(slot_)` за `solids_.reset(slot_)`, `dig_.update(streamer_.get());` за `solids_.update()`, затем при `dig_.outlined()` и игроке — `outline_mc_box(p.lo, p.hi, slot_, 0, 160, 255)` по `world_.cut_pieces_near(ноги в Minecraft, 16.0, pieces)`. `halfcraft_server.vpc`: `server\hc_dig.{h,cpp}`, `core\hc_dug_cells.h`, `core\hc_dig_clip.h`.

- [ ] **Step 12: Вырезать выкопанное из брашей мира, треугольники региона — по его боксу**
- `hc_world_collision.h` (включает `core/hc_dig_clip.h`; `.cpp` — `server/hc_dig.h`): `class DigState;`, public `set_dig` и `void cut_pieces_near(const McVec& p, double radius, std::vector<DigBox>& out) const` (куски ближе `radius` по каждой оси), private `const DigState* dig_ = nullptr;` (`reset` не трогает) и `std::unordered_map<long long, std::vector<DigBox>> cut_pieces_;` (`reset` очищает).
- `gather` очищает список своего региона (ключ `floor((lo[k] + 0.5) / COLLISION_REGION_SIZE)`, 21 бит на ось, как `region_key`) и передаёт его в `add_brushes(mins, maxs, out, std::vector<DigBox>& cut)`; пустой удаляется.
- `add_brushes` вместо `out.convexes.push_back(std::move(cvx));`: кусок без `PRIM_SKY`, для которого при `dig_ && dig_->count() > 0` `dig_->boxes_in(cvx.lo, cvx.hi, boxes)` не пуст, режется `subtract_boxes(cvx, boxes, cut_flags, pieces)` с `cut_flags = diggable(brushes[i], contents) ? dig_flags(brush_dig(brushes[i], planes).material) : 0` (клип и `MOVEABLE` — без флага); куски — в `out.convexes`, их `lo/hi` — в `cut`.
- `send_triangles` (`hc_collision.cpp`): треугольник, задевающий бокс региона, уходит кусками `clip_triangle(tri, lo, hi, pieces)`, иначе грань соседнего региона легла бы поверх дыры.

- [ ] **Step 13: `hc_debug_voxels` пропускает выкопанное.** В `check_voxels` воксель выкопанного блока `(rx * R + x / 8, ry * R + y / 8, rz * R + z / 8)` до `++voxels` уходит в `in_holes` без сверки; после итога при `dig_ && dig_->count() > 0` — строка `... voxels inside dug cells` из Produces.

- [ ] **Step 14: Принять прыжок Minecraft в выкопанное (`box_dug`).** `hc_hooks.h`, перед `#ifdef CLIENT_DLL`: истина, если выкопана каждая клетка, которую бокс задевает объёмом (углы — `source_to_mc(v.Base(), slot)`, min/max по осям, z Minecraft = −y Source, клетки `floor(lo)`…`ceil(hi) − 1`). client.dll (`hc_dig.cpp`): без DLL false, иначе `DigStore` под `lock`; server.dll (`hc_server.cpp`, за анонимным namespace): `return g_server_system.box_dug(mins, maxs);` — публичный `const` метод по `slot_` и `dig_.dug`. `hc_movement.cpp`, `HalfCraftFits` и `HalfCraftRoomAt` (там `vecFeet`): свободно при `!pm.startsolid || (pm.DidHitWorld() && halfcraft::box_dug(vecAt + vecMins, vecAt + vecMaxs))`; `HalfCraftClear`, пули и NPC не меняются. Проверка — задача 9, шаг 14a.

- [ ] **Step 15: `make build`** (без `NOPROJECTS`: .vpc изменились): вывод как в задаче 6, шаг 10, у обоих движков; `make core-test` проходит.

- [ ] **Step 16: Проверить в игре без шейдерной DLL (hl2dm).** Вырез и прыжок без DLL проверить нельзя: их проверяет задача 9, шаг 14a (пробы `probe_feet.py` и `hc_debug_voxels: 0 voxels inside dug cells`), PVS — задача 14, шаг 12. Перед запуском: движку с `<engine>: no test session` в `python tools/game/test_session.py status` — `make test-start ENGINE=<engine>`; копии DLL задачи 1 (`Get-ChildItem build\game-hl2dm -Recurse -Filter game_shader_generic_halfcraft.dll`) удалить; `make mc-test` в фоне. `<x> <y> <z>` — `floor` от `pos` строки `host` у `hc_state.py`.
```powershell
$m = python tools/game/wait_log.py hl2dm --mark
make run ENGINE=hl2dm MAP=d1_trainstation_02
python tools/game/wait_log.py hl2dm --after $m --pattern "digging is off" --timeout 120
python tools/game/wait_log.py hl2dm --after $m --pattern "puppet on" --timeout 300
python tools/debug/hc_state.py
$m = python tools/game/wait_log.py hl2dm --mark
make cmd C="'hc_debug_dig_cell <x> <y> <z>'"
python tools/game/wait_log.py hl2dm --after $m --pattern "hc_debug_dig_cell: cell -?\d+ -?\d+ -?\d+ dug" --timeout 15
python tools/game/wait_log.py hl2dm --after $m --pattern "dig: \d+ section\(s\) changed" --absent --timeout 15
```
Прошло: все `wait_log` с кодом 0, во `flags` строки `host` бит `0x10`. Затем Minecraft и Half-Life закрыть процедурой из шапки; сессия остаётся.

- [ ] **Step 17: `make format`, затем `make lint`:** код 0, формат и протокол без отличий.

- [ ] **Step 18: Уточнение в спеке** (переносы по ширине файла). «Сторона Source: server.dll», «Вырезание»: «При изменении дыр `CollisionStreamer::invalidate` отправляет затронутые регионы заново.» → «При изменении дыр `CollisionStreamer::invalidate` отправляет заново регионы вокруг каждой изменившейся клетки (не бокс секции); треугольники региона режутся по его боксу, чтобы грань соседнего региона не легла поверх дыры; без шейдерной DLL дыры в server.dll не уходят, а прыжок Minecraft в выкопанную клетку Source принимает (`box_dug`) (уточнено при планировании).»

- [ ] **Step 19: Коммит** (файлы шага 6 снова: их мог поправить `make format`)
```
git add source/src/core/hc_dug_cells.h source/src/core/hc_collision_shapes.h source/src/core/hc_collision.cpp source/src/core/hc_module.h source/src/core/hc_module.cpp source/src/shared/hc_bridge.h source/src/shared/hc_dig_abi.h source/src/shared/hc_hooks.h source/src/shared/hc_movement.cpp source/src/client/hc_dig.h source/src/client/hc_dig.cpp source/src/client/hc_client.cpp source/src/server/hc_dig.h source/src/server/hc_dig.cpp source/src/server/hc_server.cpp source/src/server/hc_world_collision.h source/src/server/hc_world_collision.cpp source/halfcraft_client.vpc source/halfcraft_server.vpc tools/debug/core_tests.cpp docs/superpowers/specs/2026-10-17-dig-through-walls-design.md
git commit -m "feat: with the shader dll, minecraft's dug cells reach server.dll (HalfCraft_DugSince) and come out of half-life's world brushes in minecraft's collision, region triangles cut to their box, and source takes minecraft's jumps into them; hc_dig, kHostDigOff, hc_debug_dig and hc_debug_dig_cell" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```


---

### Task 8: Видимость сквозь дыры

Через дыру видно соседнюю комнату, её NPC и предметы приходят на клиент. До задачи 9 DLL нет (client.dll ничего не включает): здесь проверка — сборка, в игре — задача 14, проверка 3.

**Files:**
- Modify: `source/src/shared/hc_hooks.h`, `source/src/server/hc_server.cpp`, `source/src/client/hc_dig.{h,cpp}`, `source/src/client/hc_client.cpp`, `docs/superpowers/specs/2026-10-17-dig-through-walls-design.md`
- Modify (деревья SDK, в git — через `make patches`): `source-sdk-2013/src/game/server/player.cpp`, `source-sdk-2013-sp/sp/src/game/server/player.cpp`; `source/sdk/halfcraft-{hl2dm,hl2}.patch`
- Test: нет (связка с движком)

**Interfaces:**
- Consumes: задача 7 — `DigState::count()`, `void DigState::pvs_points(const McVec& player, std::vector<Vector>& out) const` (центры в юнитах Source) у члена `dig_`; `dig_shader_ready()`, `dig_shutdown()`, `store()` (`client/hc_dig.cpp`). Есть: `McVec source_to_mc(const float p[3], MapSlot slot)`, `engine->GetClusterForOrigin` и `engine->AddOriginToPVS` (`const Vector&`).
- Produces:
  - `void halfcraft::server_add_dig_pvs(CBasePlayer* player);` (`shared/hc_hooks.h`, `GAME_DLL`)
  - `void halfcraft::dig_update(const McVec& player);` (`client/hc_dig.h`), раз в кадр; задача 9 добавит маску
  - ConVar `hc_debug_dig_pvs` (server.dll, `"0"`); лог для задачи 14: `dig pvs: <n> holes near the player add <m> clusters` (при `hc_debug_dig_pvs 1`), `dig: this map has holes: r_novis 1, r_portalsopenall 1 (they were <a>, <b>)` (при каждом включении: метку брать до первой дыры), `dig: no holes here: r_novis <a>, r_portalsopenall <b> again`.

- [ ] **Step 1: Сверить деревья SDK с патчами** (деревья вне git: могли остаться правки `dig-spike`)

Команда: `make patches`, затем `git diff --stat -- source/sdk`.
Прошло: `…halfcraft-hl2.patch (N files)`, `…halfcraft-hl2dm.patch (M files)` (запомнить N, M), `--stat` пуст. Провал:
1. Выписать `<path>` из строк `diff --git a/<path>` в `git diff -- source/sdk`; `git checkout -- source/sdk`.
2. Для каждого `<path>` в его дереве `<tree>` (`source-sdk-2013` — hl2dm, `source-sdk-2013-sp` — hl2): при `new file mode` — `git -C <tree> rm --cached --quiet -- <path>`; иначе `git -C <tree> checkout -- <path>`, а если `<path>` есть в закоммиченном патче, ещё `git -C <tree> apply --whitespace=nowarn --include=<path> ../source/sdk/halfcraft-<engine>.patch`.
3. Повторить шаг.

- [ ] **Step 2: Объявить хук** в `hc_hooks.h` после `server_minecraft_picks_weapon`: `void server_add_dig_pvs(CBasePlayer* player);` с `///` (из `SetupVisibility`, когда глаз уже в PVS).

- [ ] **Step 3: Написать `add_dig_pvs` в `hc_server.cpp`**
- `#include <vector>`; у `hc_debug_use` — `ConVar hc_debug_dig_pvs("hc_debug_dig_pvs", "0", 0, "halfcraft: log what the holes near the player add to its pvs");`; у `PUPPET_GRACE_SECONDS` — `constexpr float PVS_PROBE_INSIDE = static_cast<float>(UNITS_PER_BLOCK) / 2.0f - 1.0f;` (19), `PVS_PROBE_OUTSIDE` — с `+ 1.0f` (21).
- `HalfCraftServerSystem`: публичный `void add_dig_pvs(CBasePlayer* player);`, члены `std::vector<Vector> pvs_points_`, `std::vector<int> pvs_clusters_` и два `std::size_t` — числа последней строки лога. `server_add_dig_pvs` (после `server_player_puppeted`) при `player != nullptr` зовёт `g_server_system.add_dig_pvs(player)`.
- Алгоритм: `dig_.count() == 0` → выход (зовётся каждый снапшот). `eye = player->EyePosition()`, `at[3] = { eye.x, eye.y, eye.z }`; `dig_.pvs_points(source_to_mc(at, slot_), pvs_points_)`; `pvs_clusters_.assign(1, engine->GetClusterForOrigin(eye))` (кластер глаза уже в PVS). На клетку 13 точек: центр и `centre[axis] + offset`, `axis` 0..2, `offset` из `{-OUTSIDE, -INSIDE, INSIDE, OUTSIDE}` (центр часто в браше, а стена может кончаться на грани клетки). Кластер < 0 или уже в `pvs_clusters_` — пропуск, иначе `push_back` и `engine->AddOriginToPVS(point)`. При `hc_debug_dig_pvs` и новых числах — `log_info` строки `dig pvs` (`%zu`: `pvs_points_.size()`, `pvs_clusters_.size() - 1`).

- [ ] **Step 4: Вызвать хук в обоих деревьях SDK**: в конец `CBasePlayer::SetupVisibility` обоих `player.cpp` (include `shared/hc_hooks.h` уже есть):
```cpp
	engine->AddOriginToPVS( org );

	// halfcraft: holes dug near the player show the rooms behind them: their npcs and props come to the client too
	halfcraft::server_add_dig_pvs( this );
```

- [ ] **Step 5: Написать `dig_update` и видимость в client.dll**

`hc_dig.h`: `void dig_update(const McVec& player);` с `///` (`player` — ноги в Minecraft).

`hc_dig.cpp`, безымянный namespace:
- `bool any_dug()`: в `store()` под `lock` есть секция с `count > 0` (чужие слоты `dig_set_slot` уже опустошил).
- `bool forced`, два запомненных `int`, `void force_vis(bool want)` со `static ConVarRef novis("r_novis"), portals("r_portalsopenall");`, ссылки с `!IsValid()` пропускать. `want`: при `!forced` запомнить `GetInt()`, `forced = true`, лог `this map has holes`; всегда `1`, где не 1 (движок откатывает читы при смене `sv_cheats`). `!want && forced`: вернуть запомненные, где отличаются, `forced = false`, лог `no holes here`. Флаг чита не снимать: `RemoveFlags` нет, а `ConVar::SetValue` `FCVAR_CHEAT` не проверяет.
- `void dig_update(const McVec& /*player*/)` (иначе C4100): `force_vis(in_game && dig_shader_ready() && any_dug())`, `in_game` — то же выражение, что в `HalfCraftClientSystem::update`. `hc_dig` не учитывать (при `hc_dig 0` дыры остаются).
- `dig_shutdown()`: первой строкой `force_vis(false)`.

- [ ] **Step 6: Вызвать `dig_update`** в `HalfCraftClientSystem::update` сразу после блока, заполняющего `origin`: `dig_update(source_to_mc(origin, s.slot));`.

- [ ] **Step 7: Собрать оба движка**
Команда: `make build ENGINE=hl2dm`, затем `make build ENGINE=hl2`.
Прошло: `built hl2dm with …` и `built hl2 with …`, последней строкой `launcher: …\HalfCraft.exe`. Провал: ошибка компиляции или `halfcraft's code has N warnings (above): fix them`.

- [ ] **Step 8: Записать патчи**
Команда: `make patches`, затем `git diff -- source/sdk`.
Прошло: N и M как в шаге 1; в диффе каждого патча только новый hunk `SetupVisibility` с тремя `+`, новая строка `index` у player.cpp и у пяти следующих hunk-ов player.cpp второй номер `@@` на 3 больше. Провал: иное — правка вне `SetupVisibility`: убрать в дереве, снова `make patches`.

- [ ] **Step 9: Линтеры**
Команда: `make lint`, затем `make tidy`. Прошло: нет `aren't clean: run make format`, `clang-tidy: no findings`.

- [ ] **Step 10: Уточнения в спеку** (переносы по ширине файла; старая фраза разбита после «(флаг»)
- «Сторона Source: client.dll и шейдеры», «Видимость»: `(флаг чита снимается)` → «(флаг чита не снимается: `ConVar::SetValue` не проверяет `FCVAR_CHEAT`, значения ставятся снова каждый кадр; уточнено при планировании)».
- «Сторона Source: server.dll», «PVS»: после «сначала ближайших, не больше 64» — «; у клетки центр и по точке на 1 юнит внутри и снаружи каждой грани, кластеры без повторов (уточнено при планировании)».

- [ ] **Step 11: Коммит**
```
git add source/src source/sdk docs/superpowers/specs/2026-10-17-dig-through-walls-design.md
git commit -m "feat: holes dug near the player put the rooms behind them in its pvs, and client.dll turns r_novis and r_portalsopenall on while the map has holes" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```


---

### Task 9: Шейдерная DLL и маска дыр

`game_shader_generic_halfcraft.dll` (только hl2dm): `LightmappedGeneric` и его проход фонарика (свет факелов) с `clip` по маске дыр — текстуре client.dll 1024×1024. Шаги — для варианта A; **[только B]** — только при B.

**Files:**
- Create в `source-sdk-2013/src/materialsystem/stdshaders/` (в репозиторий — патчем): `game_shader_generic_halfcraft.vpc`, `hc_lightmappedgeneric_{dx9.cpp,dx9_helper.cpp,dx9_helper.h,ps2x.fxc,ps2_3_x.h}`, `hc_flashlight_{dx9.cpp,dx9.h,ps2x.fxc}`, `hc_dig_clip_fxc.h`; Modify `source-sdk-2013/src/vpc_scripts/projects.vgc`
- Create: `source/halfcraft_shaders.vpc`, `source/src/shaders/hc_shader_dig.{h,cpp}`
- Modify: `source/src/core/hc_dig_mask.h`, `source/src/client/hc_dig.{h,cpp}`, `source/halfcraft_client.vpc`, [только B] `source/src/shared/hc_dig_abi.h`, `tools/halfcraft/{shaders,engines,game_folder,build,sdk}.py`, `tools/build/update_patches.py`, `source/sdk/halfcraft-hl2dm.patch`
- Test: `tools/debug/core_tests.cpp`

**Interfaces:**
- Consumes: «## Результаты этапа 1», `build/dig-baseline/`, «кадр dig_base» (шаг 3), `tga_diff.py` (задача 1); `CHECK`, `run_tests()` (2); `DigMaskLayout` (3); `DugSection`, `dug_since`, `dig_shader_ready`, `store().slot`, `dig_shutdown`, `hc_debug_dig_cell`, `box_dug`, `hc_dig_abi.h` (7); `dig_update` (8); `McTexture` (`client/hc_texture.h:17-49`), `find_export` (`core/hc_module.h:8`).
- Produces: `hc_dig_mask.h`: `inline bool shader_texel(const float origin[4], const float layout[4], const float p[3], int& u, int& v);` (`p` уже сдвинута внутрь), `inline bool write_section(const DigMaskLayout& layout, int sx, int sy, int sz, const std::uint8_t* bits, std::uint8_t* rgba);` (R 255/0, G = B = A = 0, `nullptr` — пусто); вне области — false без записи. client.dll: `const DigMaskState* dig_mask_state();`, экспорт `HalfCraft_DigMask` (`DigMaskFn`). Лог `[halfcraft] the world shader reads client.dll's dig mask`. [только B] `constexpr char HC_WORLD_SHADER[] = "HC_LightmappedGeneric";` (`hc_dig_abi.h`). `Engine.shader_dll`, `Shader.dll`, `Tree.added`.

- [ ] **Step 1: Падающие тесты** (`core_tests.cpp`, из `run_tests()`); `L = DigMaskLayout::around(3200.5, 70.2, -40.7)` (ox 3136, oy 32, oz -112), `slot{ 3, -152.5f }`, `L.shader_constants(slot, o, l)`:
```
test_dig_mask_shader_texel   p(x, y, z) = mc_to_source(x + .5, y + .5, z + .5, slot): shader_texel(o, l, p(3141, 41, -105)) -> (133, 135)  // i 5, j 7, k 9
  p(3263, 95, 15) -> (1023, 1023); false at p(3135, 32, -112), p(3136, 96, -112), p(3136, 32, 16)
  each 3rd cell of x 3134..3266, y 30..97, z -114..18: result == L.texel(x, y, z, tu, tv), (u, v) == (tu, tv) when true
test_dig_mask_write_section  rgba(1024 * 1024 * 4, 0); bits[512] with bit 3 + 16 * 5 + 256 * 9; red(u, v) = rgba[(v * 1024 + u) * 4]
  write_section(L, 196, 2, -7, bits, rgba) true; red(131, 133) == 255 (cell 3139, 41, -107), red(132, 133), red(131, 134), red(3, 5) == 0
  (L, 196, 2, -7, nullptr, rgba) true, red(131, 133) == 0; (L, 195, 2, -7, bits, rgba) false, every byte of rgba 0
```
- [ ] **Step 2: `make core-test` падает**: `error C3861: 'shader_texel': identifier not found`.
- [ ] **Step 3: Реализовать в `hc_dig_mask.h`**: `shader_texel` — арифметика `DigClip` (шаг 6) на float, без сдвига и `layout[3]`; `write_section` — угол каждого из 16 слоёв через `texel`, биты `x + 16z + 256y`.
- [ ] **Step 4: `make core-test` проходит.**

- [ ] **Step 5: `source/src/shaders/hc_shader_dig.{h,cpp}`, `source/halfcraft_shaders.vpc`** (`namespace halfcraft`): `const DigMaskState* shader_dig_mask()` — `find_export("client.dll", HC_DIG_MASK_EXPORT)` до первой находки (тогда лог из Produces через `Msg`), в `std::atomic`: рисуют и из потока материальной системы; `void shader_dig_shadow(IShaderShadow*)` — `EnableTexture(SHADER_SAMPLER15, true)`; `void shader_dig_dynamic(CBaseShader*, IShaderDynamicAPI*)` — `BindTexture(SHADER_SAMPLER15, state->texture)`, `SetPixelShaderConstant(20, state->origin, 1)`, `(21, state->layout, 1)`, без маски `BindStandardTexture(SHADER_SAMPLER15, TEXTURE_BLACK)` и нули. `.vpc` как `halfcraft_client.vpc`: include `$HALFCRAFTROOT\build\shaders\<папка .inc этапа 1>`, `$HALFCRAFTDIR\src`; файлы `shaders\hc_shader_dig.h/.cpp`, `shared\hc_dig_abi.h`, `core\hc_module.h/.cpp`; без `/std:c++17` — он лёг бы на код Valve в DLL (C++14), поэтому и без `inline`-переменных, `[[nodiscard]]`.

- [ ] **Step 6: HLSL в дереве SDK**. Сначала `git -C source-sdk-2013 status --short -- src/materialsystem src/vpc_scripts`: только ` M` у двух `flashlight` `.fxc`, остатки `dig-spike` удалить (`include/hc_*.inc` перекрыл бы новый; `projects.vgc` — `git checkout`). `s15`, `c20`, `c21` в обоих шейдерах свободны. `hc_dig_clip_fxc.h`:
```hlsl
sampler DigMaskSampler : register( s15 );	// halfcraft: client.dll's mask of dug cells (client/hc_dig.cpp)
const float4 g_DigOrigin : register( c20 );
const float4 g_DigLayout : register( c21 );
void DigClip( float3 worldPos, float3 eyePos )
{
	float3 p = worldPos + normalize( worldPos - eyePos ) * 0.5f;	// a face belongs to the cell behind it (HostRay.surfaceCell)
	float3 cell = floor( float3( p.x - g_DigOrigin.x, g_DigOrigin.y - p.y, p.z - g_DigOrigin.z ) * g_DigOrigin.w );	// i, j, k
	float inside = all( cell >= 0 ) && all( cell < float3( g_DigLayout.y, g_DigLayout.y, g_DigLayout.y * 0.5f ) );
	float row = floor( cell.z / g_DigLayout.z );
	float2 uv = ( float2( cell.z - row * g_DigLayout.z, row ) * g_DigLayout.y + cell.xy + 0.5f ) * g_DigLayout.x;
	clip( 0.5f - tex2D( DigMaskSampler, uv ).r * inside * g_DigLayout.w );
}
```
Копии с правками `// halfcraft: <why>` (у строк комбинаций — строкой выше): `hc_lightmappedgeneric_ps2x.fxc` включает `hc_lightmappedgeneric_ps2_3_x.h` (урезанный набор: `LIGHTING_PREVIEW "0..0" [PC]`, `BICUBIC_LIGHTMAP "0..0" [ps20b]`); `hc_lightmappedgeneric_ps2_3_x.h` и `hc_flashlight_ps2x.fxc` (из пропатченного) включают `hc_dig_clip_fxc.h`, первая строка `main` — `DigClip( i.worldPos_projPosZ.xyz, g_EyePos.xyz );`, у фонарика `i.worldPos_worldTransition.xyz`.

- [ ] **Step 7: C++ шейдера в дереве SDK** (копии Valve, правки `// halfcraft:`): `hc_lightmappedgeneric_dx9.cpp` — шейдер `LightmappedGeneric` с include `hc_lightmappedgeneric_dx9_helper.h`. В helper сверх убранного в этапе 1: только `hc_lightmappedgeneric_ps20b`; без `r_flashlight_version2`; `DrawHcFlashlight` вместо `DrawFlashlight_dx90` (тот биндит стоковый `flashlight_ps20b`); `shader_dig_shadow` у `EnableTexture`, `shader_dig_dynamic` перед `DynamicCmdsOut.End()`; при урезанном наборе `LIGHTING_PREVIEW`, `BICUBIC_LIGHTMAP` = 0, как шаг 9 задачи 1. `hc_flashlight_dx9.{h,cpp}`: `struct HcFlashlightVars` (поля `DrawFlashlight_dx90_Vars_t` ветки LightmappedGeneric), `void DrawHcFlashlight( CBaseVSShader*, IMaterialVar**, IShaderDynamicAPI*, IShaderShadow*, HcFlashlightVars& )` — `BaseVSShader.cpp:1810-2095` с веткой `m_bLightmappedGeneric` и ps20b, пиксельным `hc_flashlight_ps20b`, `shader_dig_*`; рядом копия `SetFlashlightVertexShaderConstants` (`:1610-1649`).

- [ ] **Step 8: Проект DLL**: `game_shader_generic_halfcraft.vpc` — файл этапа 1 (`git show dig-spike:source/sdk/halfcraft-hl2dm.patch`; без `GAME_SHADER_DLL`, вывод `game/mod_ep1/bin/x64`) + `hc_flashlight_dx9.cpp/.h`, в конце `$Macro HALFCRAFTROOT "$SRCDIR\..\.."`, `$Include "$HALFCRAFTROOT\source\halfcraft_shaders.vpc"`; без `psconst_patch.cpp` (меняет число пиксельных констант всем шейдерам), `$Shaders`, `.fxc`. `projects.vgc:45`, после `game_shader_generic_std`, с `// halfcraft:`: `$Project "game_shader_generic_halfcraft" { "materialsystem\stdshaders\game_shader_generic_halfcraft.vpc" [$EPISODIC] }`.

- [ ] **Step 9: Инструменты**
  - `shaders.py`: `Shader` + `combos: int | None = None`, `dynamic: int | None = None` (`None` — без проверки раскладки), `dll: bool = False`; `SHADERS` + `Shader("hc_lightmappedgeneric_ps2x.fxc", "hc_lightmappedgeneric_ps20b", dll=True)`, `Shader("hc_flashlight_ps2x.fxc", "hc_flashlight_ps20b", dll=True)`.
  - `engines.py`: `Engine.shader_dll: str | None = None` (hl2dm `"game_shader_generic_halfcraft"`), `dlls()` + `REPO / self.bin / f"{shader_dll}.dll"`; при `bin` в проверке 6 — `shader_bin = "bin"`.
  - `game_folder.py:203-208`: шейдеры `dll=True` — только движку с `shader_dll`; DLL — в `shader_bin`, если задан.
  - `build.py`: `build_shaders()` до движков (DLL включает `.inc`); vpc hl2dm (`:92`) + `"+game_shader_generic_halfcraft"`; `build/shaders` не стирать (ShaderCompile2 пропускает шейдер с прежним CRC); `SHADER_SECONDS` = max(3600, 2 × время набора этапа 1); раскладка — при `combos is not None`.
  - `sdk.py`: `Tree.added: tuple[str, ...] = ()`, у hl2dm `("src/materialsystem/stdshaders/hc_*", "src/materialsystem/stdshaders/game_shader_generic_halfcraft.vpc")`; `update_patches.py` перед `git diff` при непустом `added` (пустой pathspec — все неотслеживаемые): `git ls-files --others --exclude-standard -z -- <added>` → `git add --intent-to-add -- <files>`.

- [ ] **Step 10: Маска в `hc_dig.cpp`** (`core\hc_dig_mask.h` — в Core `halfcraft_client.vpc`): `McTexture texture{"dig_mask"}`, RGBA 1024×1024, `layout`, `built`, `version`, `DigMaskState state`. `dig_update` (имя `player` раскомментировать): без `dig_shader_ready()` — `state = {}`; иначе `dug_since(version, ...)` дважды, как `hc_block_solids.cpp:374-380`, `version = now`; при `!built || needs_recentre(player)` — `around(player)`, картинку обнулить, `write_section` всех из `dug_since(0, ...)`, `texture.set(rgba, 1024, 1024)`; иначе `write_section` изменившихся (`count == 0` → `nullptr`) и по `update_region(u, v, 16, 16, block)` на каждый из 16 слоёв, `block` — плотные 16×16×4 байт (строк по 1024 он не берёт); затем `state.texture`, `shader_constants(store().slot, state.origin, state.layout)`, `state.layout[3] = 1`. `dig_shutdown` освобождает текстуру, сбрасывает `state` и `built`; `dig_mask_state()` = `&state`; экспорт — внизу, как `HalfCraft_SolidsSince` (`hc_blocks.cpp:495`).

- [ ] **Step 11: [только B]** Шейдер `HC_LightmappedGeneric`; `CAutoGameSystem` в `hc_dig.cpp` (как `hc_blocks.cpp:242-259`): `LevelInitPostEntity` при `dig_shader_ready()` переключает материалы на `HC_WORLD_SHADER`, как ветка B шага 11 задачи 1 (прежде переключённые — тоже в n); логи `dig: <n> of <m> world materials switched to HC_LightmappedGeneric`, `dig: other world shaders: <shader> <count>, ...`.

- [ ] **Step 12: Сборка и линт** (игры закрыты): `make build ENGINE=hl2dm` (компиляция — как в этапе 1) → `built hl2dm with v14...`, DLL с `.pdb` в `build\game-hl2dm\bin\x64` (или `bin`), `hc_lightmappedgeneric_ps20b.vcs` и `hc_flashlight_ps20b.vcs` в `shaders\fxc`; провал — `halfcraft's code has N warnings`, `ShaderCompile2 failed on ...`, ошибка vpc/msbuild. Повтор с `NOPROJECTS=1` — минуты, `build\shaders\shaders\fxc\hc_lightmappedgeneric_ps20b.vcs` не перезаписан. `make build ENGINE=hl2` → `built hl2 with ...`, DLL нет, в `shaders\fxc` только `flashlight_ps20b.vcs`, `lightmappedgeneric_flashlight_vs20.vcs`. `make format`, `make lint` — чисто.

- [ ] **Step 13: В игре: кадр без дыр не изменился**. Ниже `cmd 'a'` = `make cmd C="'a'"`; «ждать `<p>` N с» — `$m = python tools/game/wait_log.py hl2dm --mark` до команды, `python tools/game/wait_log.py hl2dm --after $m --pattern "<p>" --timeout N` после, код 0 («той же меткой» — без новой); `diff a b` = `python tools/debug/tga_diff.py build/dig-t9/a.tga build/dig-t9/b.tga --max <N + 0.1>`.
  1. `python tools/game/test_session.py status`; движку с `no test session` — `make test-start ENGINE=<engine>`; `New-Item -ItemType Directory -Force build/game-hl2dm/save, build/dig-t9 | Out-Null; Copy-Item build/dig-baseline/save/dig_base.* build/game-hl2dm/save/; Copy-Item build/dig-baseline/dig_base_stock.tga build/dig-t9/`; без `minecraft: test` — `make mc-test` в фоне.
  2. `make run ENGINE=hl2dm MAP=d1_trainstation_02`, ждать `minecraft connected` 300 с, той же меткой `reads client.dll's dig mask` 180 с.
  3. «Кадр dig_base → `build/dig-t9/base.tga`», `diff dig_base_stock base` — код 0.

- [ ] **Step 14: В игре: вырез и факел** («кадр без загрузки» — «кадр dig_base» без `load` и ожидания `puppet on`)
  1. `cmd 'hc_debug_dig_cell'`, ждать `hc_debug_dig_cell: cell -?\d+ -?\d+ -?\d+ dug` 10 с — это `<x> <y> <z>` (при `no wall under the crosshair` — `cmd 'hc_debug_dig_cell <x> <y> <z>'` с клеткой бокса этапа 1 у лицевой грани). Кадр без загрузки → `hole.tga`, `cmd 'jpeg'`: `diff base hole` — код 1, на `.jpg` у прицела квадратная дыра 40×40 юнитов.
  2. `cmd 'hc_debug_torch' 'jpeg'`: стена вокруг дыры освещена, в дыре светлого пятна нет.
  3. `cmd 'hc_debug_torch 0' 'hc_debug_dig_cell <x> <y> <z> 0'` → `hc_debug_dig_cell: cell <x> <y> <z> filled`; кадр без загрузки → `after.tga`, `diff base after` — код 0.

- [ ] **Step 14a: В игре: вырез коллизии и прыжок Minecraft в клетки** (их передали сюда задача 7 и Review Focus 3: без DLL `dug_since` молчит)
  1. `cmd 'hc_look <P0> <Y0>' 'hc_key 26 1 2'`, `python tools/debug/probe_feet.py` («до»).
  2. `cmd 'hc_debug_dig_cell'`, ждать `hc_debug_dig_cell: cell .* dug` 15 с (`<x> <y> <z>`); `cmd 'hc_debug_dig_cell <x> <y-1> <z>' 'hc_debug_voxels 1'`, той же меткой ждать `dig: \d+ section\(s\) changed, 2 cell\(s\) dug in this map` 15 с и `hc_debug_voxels: 0 voxels inside dug cells` 30 с; `probe_feet.py` («после»). Прошло: в `host flags` нет `0x10`; у клетки номер `(dz + 1) * 3 + (dx + 1)` (`dx = x − floor(posX)`, `dz = z − floor(posZ)`, `pos` из первой строки) в строках `y=<y>`, `y=<y-1>` «до» > 0, «после» 0, прочие числа прежние. `|dx|` или `|dz|` > 1 — заполнить обе клетки, `hc_key 26 1 2`, повторить.
  3. `cmd 'hc_mc tp @s <x+0.5> <y-1> <z+0.5>'`, ждать `minecraft moved its player by itself` 15 с, той же меткой с `--absent` `source (refused|landed) minecraft's jump` 5 с; `python tools/debug/hc_state.py`: `host ... pos` и `mc ... pos` совпадают до 0.05 на дне клетки `(x, y-1, z)` (`/tp`, жемчуг и хорус идут одним путём `HC_CMD_JUMP`). Провал — `box_dug` задачи 7 не учтён в `HalfCraftFits`/`HalfCraftRoomAt`.
  4. `cmd 'hc_mc tp @s <mc pos «до»>' 'hc_debug_dig_cell <x> <y> <z> 0' 'hc_debug_dig_cell <x> <y-1> <z> 0'`.
  5. Minecraft — процедурой остановки из шапки, пока Half-Life запущен; затем `cmd 'quit'`, `Wait-Process -Name hl2mp_win64 -Timeout 120 -ErrorAction SilentlyContinue`; `test_session.py status`: `half-life: not running`, `minecraft: none`; `make test-stop` → `hl2dm: test session off; ...`, `hl2: test session off; ...`.

- [ ] **Step 15: `make patches ENGINE=hl2dm`** (после проверок и шага 11) → `...halfcraft-hl2dm.patch (38 files)` (прежние 27, 10 новых, `projects.vgc`), в нём 10 строк `^new file mode 100644`; `python tools/build/setup_sdk.py --engine hl2dm` → `hl2dm sdk already patched`.

- [ ] **Step 16: Коммит**
```
git add tools/halfcraft tools/build/update_patches.py tools/debug/core_tests.cpp source/src source/halfcraft_client.vpc source/halfcraft_shaders.vpc source/sdk/halfcraft-hl2dm.patch
git commit -m "feat: half-life's walls leave out the cells dug in minecraft: game_shader_generic_halfcraft.dll (hl2dm) clips LightmappedGeneric and its torchlight pass by client.dll's mask texture of 128x64x128 blocks around the player; make build builds it, make patches carries new sdk files" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```


---

### Task 10: Minecraft: связь с Half-Life для копания

**Files** (от `minecraft/src/`):
- Modify: `main/java/dev/halfcraft/world/{HostTri,HostCollision}.java`, `main/java/dev/halfcraft/link/HostLink.java:266-269` (после `seated()`)
- Test (новые): `test/java/dev/halfcraft/world/{HostTriTest,HostCollisionTest}.java`

**Interfaces:**
- Consumes: из `Proto` (Task 4) `TRI_DIGGABLE`, `COL_VOID`, `VOID_NONE/ALL/SOME`, `DIG_NONE`..`DIG_GLASS`, `HOST_DIG_OFF`; уже есть `COL_REGION_*`. Раскладка `kColVoid` (шлёт Task 5) и его место перед `kColRegion` региона — doc `kColVoid` в `protocol/schema/collision.toml` (Task 4). Биты 8–15 грани с `TRI_DIGGABLE` — одно `DigMaterial` (не OR, Task 2, 6), 0 — без материала.
- Produces:
  - `HostTri`: `public static final int MATERIAL_SHIFT = 8;` (хостовый `PRIM_MATERIAL_SHIFT`), `public final int digMaterial;` (`Proto.DIG_*`, 0 — нельзя копать), `public boolean diggable()`.
  - `HostCollision`: `public static long @Nullable [] voidLayersAt(int x, int y, int z)` (null: пустоты нет или регион неизвестен) и `rockLayersAt` с той же сигнатурой (null, пока регион неизвестен, иначе occupancy | void); `static long[] rock(long @Nullable [] occupancy, long @Nullable [] voids)`; package-private для тестов: `record RegionVoid(byte[] kinds, long[][] layers)` с `long @Nullable [] at(int bx, int by, int bz)`, `static RegionVoid parseVoid(MemorySegment s, long payload)`, `static void readVoid(MemorySegment s, long p)`, а `readRegion(MemorySegment, long)` и `clear(int)` перестают быть private.
  - `HostLink.HostState`: `public boolean digOff()`.
  - `latest.log`, раз за эпоху коллизии: `HalfCraft: first void region (rx, ry, rz) in epoch N (K blocks partly void)` (ждёт Task 14).

- [ ] **Step 1: Написать падающий тест `HostTriTest`**

Package-private, как `RegionEdgeTest`; `WALL = { 0, 0, 0, 0, 1, 0, 0, 1, 1 }`, `t(f)` = `new HostTri(WALL, 0, f)`, `S` = `HostTri.MATERIAL_SHIFT`, константы из `Proto`:
- `shouldReadTheDigMaterialFromBits8To15`: `t(TRI_DIGGABLE | DIG_IRON << S)` → `diggable()`, `digMaterial == DIG_IRON`;
- `shouldDigAsStoneWhenNoMaterialCame`: `t(TRI_DIGGABLE)` → `diggable()`, `digMaterial == DIG_STONE`;
- `shouldNotDigWithoutTheFlag`: `t(DIG_GLASS << S)` → `!diggable()`, `digMaterial == 0`; `!diggable()` и у `t(TRI_SKY | TRI_STAIR_HELPER)`, `new HostTri(WALL, 0, false)`;
- `shouldKeepTheStairHelperBesideTheDigFlags`: `t(TRI_STAIR_HELPER | TRI_DIGGABLE | DIG_PLANKS << S)` → `stairHelper`, `diggable()`, `digMaterial == DIG_PLANKS`.

- [ ] **Step 2: Запустить тест: падает**: `python tools/build/gradle.py test --tests dev.halfcraft.world.HostTriTest` → `error: cannot find symbol` (новые имена), `BUILD FAILED`.

- [ ] **Step 3: Реализовать флаги копания в `HostTri`**: конструктор с `int flags`: `digMaterial` = 0 без `TRI_DIGGABLE`, иначе `flags >>> MATERIAL_SHIFT & 0xFF`, 0 → `DIG_STONE`, неизвестные как есть (камнем их делает `DigMaterials`, Task 11).

- [ ] **Step 4: Запустить тест: проходит**: та же команда, 4 × `PASSED`, `BUILD SUCCESSFUL`.

- [ ] **Step 5: Написать падающий тест `HostCollisionTest`**

`FULL` = 8 × `-1L`; `{a, b}` = `long[8]`, хвост нулями; `B` = `COL_REGION_HEADER_BYTES`; `P` = (8, 0, 16); сегменты в `Arena.ofConfined()`. `header(arena, count, extra)`: `arena.allocate(B + extra, 8)`, `JAVA_INT` `MIN_X/MIN_Z/MAX_X/MAX_Y/MAX_Z` = 8/16/15/7/23 (регион (1, 0, 2)), `EPOCH` 7, `COUNT` = `count`.
- `shouldJoinOccupancyAndVoidLayerByLayer`: `rock({0xFF}, {0xFF00, -1})` = `{0xFFFF, -1}`;
- `shouldTakeEitherAloneAndGiveZerosForNeither`: `h` = 4 × `0`, 4 × `-1L`: `rock(null, h)` = `rock(h, null)` = `h`, `rock(null, null)` = `new long[8]`;
- `shouldNeverChangeOrHandBackItsInputs`: `o = {1}`, `v = {2}`: `assertNotSame(o, rock(o, v))`, `assertNotSame(v, rock(null, v))`, `o`, `v` прежние;
- `shouldReadAColVoidPayloadByBlockIndex`: `s = header(arena, 1, 512 + 64)`, байт `B` = `VOID_ALL` (блок 0,0,0), байт `B + 1 + 8*2 + 64*3` = `VOID_SOME` (блок 1,3,2), `JAVA_LONG` в `B + 512 + y*8L` = `-1` при `y < 4`; `r = parseVoid(s, 0)`: `r.at(0,0,0)` = `FULL`, `r.at(1,3,2)` = `{-1, -1, -1, -1}`, `r.at(2,3,1)` и `r.at(7,7,7)` null;
- `shouldShowARegionsVoidOnceItsRegionArrivesAndForgetItWithTheRegion`: `voids = header(arena, 0, 512)` с `VOID_ALL` в `B` (блок `P`), `region = header(arena, 0, 0)`. `clear(7)`, `readVoid(voids, 0)` → `voidLayersAt(P)`, `rockLayersAt(P)` null; `readRegion(region, 0)` → оба `FULL`, `rockLayersAt(9, 0, 16)` = `new long[8]`; снова `readRegion` → `voidLayersAt(P)` null; `readVoid`, `readRegion`, `clear(8)` → оба null.

- [ ] **Step 6: Запустить тест: падает**: `python tools/build/gradle.py test --tests dev.halfcraft.world.HostCollisionTest` → `error: cannot find symbol` (новые имена), `has private access in HostCollision` (`readRegion`, `clear`), `BUILD FAILED`.

- [ ] **Step 7: Написать хранилище пустоты и запросы в `HostCollision`**

Поля после `LAYERS` (строка 35): `ConcurrentHashMap<Long, RegionVoid> VOIDS` (известные регионы по `regionKey`), `java.util.HashMap<Long, RegionVoid> PENDING_VOIDS` (ждут своего `kColRegion`) и `boolean voidLogged` (оба только для потока-потребителя).
- `at`: индекс `bx + 8*bz + 64*by`; `VOID_ALL` отдаёт общий `ALL_VOID` (8 × `-1L`).
- `parseVoid` чистый (без лога и хранилищ): `kinds` — 512 байт с `payload + COL_REGION_HEADER_BYTES`, слои n-го `VOID_SOME` — существующий `readLayers` (строка 430) с `payload + COL_REGION_HEADER_BYTES + 512 + n * 64L`, пока `n < COL_REGION_COUNT`; лишние `VOID_SOME` остаются null.
- `voidLayersAt`: пустой `VOIDS` → null, иначе регион `floorDiv(·, REGION_SIZE)`, `at(floorMod(·, 8))` по осям; `rockLayersAt` — через `isKnown`.
- Javadoc `voidLayersAt` как у `layersAt` (пустота за картой без браша мира; never change them); у него и `rockLayersAt`: `A dug cell inside a world brush reads as void once its region comes again (Half-Life finds void after the cut): callers skip dug cells.`

- [ ] **Step 8: Подключить `COL_VOID` к чтению кольца**

Package-private методы — с `// package-private for HostCollisionTest`.
- `drainOnce` (строки 284-289): `case COL_VOID -> readVoid(s, payload);`.
- `readVoid` проверяет эпоху как `readTris`, затем `PENDING_VOIDS.put(regionKey(floorDiv(minX, REGION_SIZE), …), parseVoid(s, p))`; при `!voidLogged` — строка из Produces (`HalfCraft.LOG.info`: `rx, ry, rz, msgEpoch, count`), `voidLogged = true`.
- `readRegion`, цикл регионов (строки 386-392), перед `KNOWN_REGIONS.add(key)` (вместе с занятостью): `voids = PENDING_VOIDS.remove(key)`; не null → `VOIDS.put(key, voids)`, иначе `VOIDS.remove(key); // sent again without kColVoid: no void left in it`.
- `clear` (строки 322-332): после `KNOWN_REGIONS.clear()` — `VOIDS.clear()`, `PENDING_VOIDS.clear()`, `voidLogged = false`.

- [ ] **Step 9: Запустить тест: проходит**: команда шага 6, 5 × `PASSED`, `BUILD SUCCESSFUL`.

- [ ] **Step 10: Добавить `HostState.digOff()`**: `(this.flags & HOST_DIG_OFF) != 0`; Javadoc: Half-Life не даёт копать новое (`hc_dig 0` или нет шейдерной DLL HalfCraft), выкопанное остаётся.

- [ ] **Step 11: Отформатировать, собрать мод, прогнать линт**: `python tools/build/gradle.py spotlessApply`; `make mc` → `BUILD SUCCESSFUL`, тесты `PASSED`, ни одного `warning:` (`-Werror`); `make lint` → код 0. В игре — Task 14.

- [ ] **Step 12: Коммит**

```
git add minecraft/src
git commit -m "feat: minecraft reads which half-life faces can be dug and their material, the void behind the map per region (kColVoid), and whether half-life has digging off" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```


---

### Task 11: Minecraft: хранение выкопанного и вскрытие

**Files** (от `minecraft/src/{main,test}/java/dev/halfcraft/`):
- Create: `world/{DugBits,DugColumn,DigMaterials,DigRules,HostDig}.java`
- Modify: `world/Rollback.java` (поля `:96-101`, `afterChange` `:154-159`, `tracks()` `:174-177`), `mixin/LevelChunkMixin.java:12-23`, `HalfCraft.java:29`
- Test: `world/{DugBits,DigMaterials,DigRules}Test.java`

**Interfaces:**
- Consumes: `Proto.DIG_NONE`…`DIG_GLASS` = 0…6 (задача 4); `HostCollision.voidLayersAt(int, int, int)` → `long @Nullable []`, `HostLink.HostState.digOff()` (задача 10).
- Produces (`dev.halfcraft.world`, `public`, методы классов `static`):
  - `final class DugBits`: `int index(int x, int y, int z)` = `(x & 15) + 16 * (z & 15) + 256 * (y & 15)`, `boolean get(long[] bits, int index)`, `void set(long[] bits, int index)`, `int count(long[] bits)`, `byte[] toBytes(long[] bits)` (512 байт: бит `i` — байт `i >> 3`, бит `i & 7`).
  - `record DugColumn(List<DugSection> sections)` с `record DugSection(int sectionY, long[] bits)` (`long[64]`): `EMPTY`, `CODEC`, `STREAM_CODEC`, `DugColumn with(int x, int y, int z)`, `boolean has(int x, int y, int z)`, `long @Nullable [] bits(int sectionY)`.
  - `final class HostDig`: `AttachmentType<DugColumn> DUG`, `long @Nullable [] sectionBits(LevelChunk, int sectionY)`, `void open(ServerLevel, ServerPlayer, BlockPos, int material)`, `BlockState stateFor(int material)`; `DigMaterials`, `DigRules` (шаг 1); `void Rollback.untracked(Runnable change)`.
  - Лог (ждут задачи 12, 14): `HalfCraft: digging into Half-Life's walls registered (halfcraft:dug)`, `HalfCraft: dug <x, y, z> out of Half-Life's walls as <blockId>`, `HalfCraft: <n> cells around <x, y, z> revealed`.

- [ ] **Step 1: Написать падающие тесты** (стиль `RegionEdgeTest`, `import static dev.halfcraft.link.Proto.*;`, `@Nullable` из `org.jspecify.annotations`)
```
DugBitsTest
  shouldIndexXThenZThenY: (0,0,0) 0, (1,0,0) 1, (0,0,1) 16, (0,1,0) 256, (15,15,15) 4095, (-1,-1,-1) 4095, (17,33,-15) 273
  shouldSetGetAndCountBits: set 0, 4095, 4095 in long[64]: get 0, 4095 true, 1 false; count 2
  shouldWriteBytesAsLittleEndianWords: set 0, 9, 4095: toBytes 512 bytes, [0] 1, [1] 2, [511] (byte) 0x80, 3 nonzero
  shouldMarkCellsInANewColumnAndLeaveTheOldOne: one = EMPTY.with(1,2,3): has(1,2,3), !has(1,2,4), !EMPTY.has(1,2,3);
    two = one.with(1,-5,3).with(2,2,3): 1 and 2 sections, two.bits(-1) != null, two.bits(5) == null, count(two.bits(0)) 2
  shouldKeepCellsThroughBothCodecs: EMPTY.with(1,2,3).with(4,-20,5) there and back by CODEC (JsonOps.INSTANCE, getOrThrow())
    and by STREAM_CODEC (Unpooled.buffer()): has(1,2,3), has(4,-20,5), !has(1,2,4)
DigMaterialsTest  // String blockId(int material), int material(String blockId)
  shouldNameTheBlockOfEachMaterial: blockId(DIG_STONE..DIG_GLASS) = minecraft:stone, stone_bricks, iron_block, oak_planks, dirt, glass
  shouldTakeStoneForNoneOrAnUnknownMaterial: blockId(DIG_NONE), blockId(99) = minecraft:stone
  shouldFindTheMaterialOfABlockBack: material(blockId(m)) == m, m = DIG_STONE..DIG_GLASS; material("minecraft:cobblestone") DIG_STONE
DigRulesTest  // Cells: boolean known/dug/air(x, y, z), long @Nullable [] occupancy/voids(x, y, z); record Reveal(x, y, z, material)
  // World implements Cells: Set<Pos> unknown, dug, built (not air), Map<Pos, long[]> occupancy, voids; other cells known, not dug,
  // air, rockless. FULL 8 x -1L; LOWER 4 x -1L, 4 x 0L; UPPER reversed; X' = X.clone() with [7] = 0x7FFFFFFFFFFFFFFFL
  shouldRevealAWhollyRockNeighbourWithTheMiningMaterial: occupancy (1,0,0) FULL: reveals(w, 0,0,0, DIG_IRON) = [(1,0,0, DIG_IRON)]
  shouldRevealAMostlyVoidNeighbourAsStone: (0,-1,0) occupancy LOWER, voids UPPER: reveals(w, 0,0,0, DIG_PLANKS) = [(0,-1,0, DIG_STONE)]
  shouldLeaveAPartlyRockNeighbour: occupancy (0,0,1) FULL': reveals(w, 0,0,0, DIG_STONE) = []
  shouldLeaveDugBuiltAndUnknownNeighbours: FULL in (-1,0,0) dug, (0,0,-1) built, (0,1,0) unknown: reveals(w, 0,0,0, DIG_STONE) = []
  shouldRevealEverySideDownUpNorthSouthWestEast: FULL round (5,6,7): reveals(w, 5,6,7, DIG_DIRT) at (5,5,7) (5,7,7) (5,6,6) (5,6,8) (4,6,7) (6,6,7)
  shouldCountOccupancyAndVoidTogetherAsRock: whollyRock (FULL, null) (null, FULL) (LOWER, UPPER) true; (LOWER, null) (null, null) false
  shouldTakeStoneFromHalfVoidUpAndTheOpeningsMaterialBelow: revealMaterial (UPPER, DIG_GLASS) DIG_STONE; (UPPER', DIG_GLASS) DIG_GLASS;
    (null, DIG_IRON) DIG_IRON; (null, DIG_NONE), (null, 99) DIG_STONE
```

- [ ] **Step 2: Запустить** `python tools/build/gradle.py test --tests "dev.halfcraft.world.D*Test"` → `error: cannot find symbol` (`DugBits`, `DugColumn`, `DigMaterials`, `DigRules`), `BUILD FAILED`.

- [ ] **Step 3: Написать четыре класса**: `DugColumn` — порт `SkyDig.DugSection`/`DugColumn` (SkyCraft `SkyDig.java:59-139`) без `world` (слоты карт разнесены в пространстве), `isDug` → `has`, биты через `DugBits` (у него приватный конструктор); `DigMaterials` — `switch` по таблице «Материалы» спеки; `DigRules` без классов Minecraft (и без `Direction`): сосед вскрывается при `known`, `whollyRock(occupancy, voids)`, `!dug`, `air` (проверки в этом порядке) с `revealMaterial(voids соседа, material)`.

- [ ] **Step 4: Запустить** ту же команду → 5 + 3 + 7 `PASSED`, `BUILD SUCCESSFUL`.

- [ ] **Step 5: Отформатировать** `python tools/build/gradle.py spotlessApply` → `BUILD SUCCESSFUL`.

- [ ] **Step 6: Коммит**
```
git add minecraft/src/main/java/dev/halfcraft/world/D*.java minecraft/src/test/java/dev/halfcraft/world/D*Test.java
git commit -m "feat: minecraft keeps the cells dug out of half-life's walls per chunk section (DugBits, DugColumn) and works out which rock around a hole turns into blocks (DigRules, DigMaterials)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 7: `Rollback.untracked`** (после `afterChange`): `change` между `untracked++` и `untracked--` в `finally` (`private static int untracked`, вложенные вызовы работают); при `untracked > 0` `tracks()` — false. Javadoc: вскрытые блоки сейв не откатывает, как и дыры; только серверный поток.

- [ ] **Step 8: `HostDig`: вложение и материалы**
```java
public static final AttachmentType<DugColumn> DUG = AttachmentRegistry.<DugColumn>create(Identifier.fromNamespaceAndPath(HalfCraft.MOD_ID, "dug"),
	builder -> builder.persistent(DugColumn.CODEC).syncWith(DugColumn.STREAM_CODEC, AttachmentSyncPredicate.all()));
```
Не `builder()` (`SkyDig.java:141-144`): в `fabric-data-attachment-api-v1` 2.2.30 он `@Deprecated`, а сборка с `-Werror`. `void init()`, приватный `column` и `boolean isDug(Level, BlockPos)` — как `SkyDig.java:146-158`; `init()` ещё регистрирует `ServerTickEvents.END_SERVER_TICK` → `serverTick` и `SERVER_STOPPING` → очистить очередь. `sectionBits` = `column(chunk).bits(sectionY)`; `stateFor(m)` = `BuiltInRegistries.BLOCK.getValue(Identifier.parse(DigMaterials.blockId(m))).defaultBlockState()`, `int materialOf(BlockState)` — обратно через `BLOCK.getKey`; приватный `digOff()` = `HostLink.active() && HostLink.readHostState(HOST) && HOST.digOff()` со своим `HostState` (как `PlayerSupportMixin.java:34,65`).

- [ ] **Step 9: `HostDig.open`** (серверный поток; задача 12 зовёт через `getSingleplayerServer().execute`): выход при `!HostNav.inMirror(level)`, `player.isSpectator() || !player.mayBuild()`, `!level.isLoaded(pos)`, центре клетки дальше `REACH = 8.0` от глаз, `digOff()`, выкопанной клетке или `!level.getBlockState(pos).canBeReplaced()`. Иначе `SkyDig.open` (`:177-193`) в его порядке (`used`, `harvest` до `mineBlock`: инструмент может в нём сломаться) с `stateFor(material)`; затем `(pos, material)` в очередь вскрытия и вторая строка лога (`pos.toShortString()`, `blockId`).

- [ ] **Step 10: `afterChange` и вскрытие** (очередь серверного потока из `private record Pending(ServerLevel level, BlockPos pos, int material, int tries)`, `REVEAL_TRIES = 60` тиков)
- `void afterChange(LevelChunk, BlockPos, @Nullable BlockState previous, BlockState now)` зовётся изнутри `setBlockState` и мир не трогает: ставит `Pending(level, pos.immutable(), materialOf(previous), REVEAL_TRIES)`, если по порядку `previous != null && !previous.canBeReplaced() && now.canBeReplaced()`; `chunk.getLevel() instanceof ServerLevel level`, `level.getServer().isSameThread()`, `HostNav.inMirror(level)`; клетка в `column(chunk)`.
- `serverTick(MinecraftServer)` забирает очередь: регион одного из 6 соседей неизвестен (`!HostCollision.isKnown`) и `tries > 1` → обратно с `tries - 1`; иначе каждый `Reveal` из `DigRules.reveals(cells(level), x, y, z, material)` пометить выкопанным в его чанке, затем `Rollback.untracked(() -> level.setBlock(p, stateFor(r.material()), Block.UPDATE_ALL))`; вскрыто > 0 — третья строка лога. `digOff()` не нужен: дыра уже выкопана.
- `cells(level)`: `HostCollision.isKnown`/`layersAt`/`voidLayersAt`; чанк не загружать: `dug` = `!level.isLoaded(p) || isDug(level, p)`, `air` = `level.isLoaded(p) && level.getBlockState(p).isAir()`.

- [ ] **Step 11: Подключить `HostDig`**: в `LevelChunkMixin.halfcraft$after` после `Rollback.afterChange` — `HostDig.afterChange((LevelChunk) (Object) this, pos, cir.getReturnValue(), state);` (`setBlockState` возвращает прежнее состояние), в Javadoc класса — `and the reveals of dug cells (HostDig)`; в `HalfCraft.onInitialize` после строки 29 — `// half-life's walls dig like blocks: the dug cells live in minecraft's chunks` и `dev.halfcraft.world.HostDig.init();`.

- [ ] **Step 12: Собрать** `make mc` → `BUILD SUCCESSFUL`, новые тесты `PASSED`, ни одного `warning:` (и `[deprecation]`).

- [ ] **Step 13: Формат и линт** `python tools/build/gradle.py spotlessApply`, затем `make lint` → код 0.

- [ ] **Step 14: Проверить с Half-Life**
Мир клиента (и `LevelChunk`, цель миксина) открывается только после связи (`HostClient.java:105-112`): Half-Life со схемой задачи 4 стартует после клиента. Идёт сессия (`python tools/game/test_session.py status`) — без `make test-start`/`make test-stop`; запущенные игры сначала закрыть, как в конце блока. `wait <re> <с>` = `python tools/game/wait_log.py mc-test --after 0 --pattern "<re>" --timeout <с>`.
```
make build ENGINE=hl2dm
make test-start
python tools/game/wait_log.py mc-test --mark          # -> <mark>
make mc-test                                           # in the background
python tools/game/wait_log.py mc-test --after <mark> --pattern "digging into Half-Life's walls registered \(halfcraft:dug\)" --timeout 600
make run ENGINE=hl2dm MAP=d1_trainstation_02
wait "Half-Life link up" 300
wait "mirror world configured" 300
wait "Mixin apply failed|InvalidInjectionException|halfcraft:dug.*(error|fail)" 5 --absent
# minecraft: the header's stop procedure, half-life still running
make cmd C="'quit'"
Wait-Process -Name hl2mp_win64 -Timeout 120 -ErrorAction SilentlyContinue
python tools/game/test_session.py status               # half-life: not running, minecraft: none
make test-stop
```
`--after <mark>` — только у первого ожидания: новый latest.log короче метки, и `wait_log.py:47-48` читает его с начала. Прошло: три `found in mc-test: ...`, `not in mc-test within 5 s, as expected: ...`, остановка по шапке, `test session off` для обоих движков.

- [ ] **Step 15: Коммит**
```
git add minecraft/src/main/java/dev/halfcraft/world/HostDig.java minecraft/src/main/java/dev/halfcraft/world/Rollback.java minecraft/src/main/java/dev/halfcraft/mixin/LevelChunkMixin.java minecraft/src/main/java/dev/halfcraft/HalfCraft.java
git commit -m "feat: cells dug out of half-life's walls live in minecraft's chunks (halfcraft:dug, saved and synced); opening one drops its block's loot, and the wholly rock cells around a hole become blocks the rollback leaves alone" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```


---

### Task 12: Minecraft клиент: ломание стен Half-Life

Игрок Minecraft ломает клетку стены Half-Life под прицелом (`TRI_DIGGABLE`) как блок её материала, с трещинами, звуком, частицами и рамкой; открывает клетку встроенный сервер (`HostDig.open`). Стенки и `kRenDug` — Task 13.

**Files** (от `minecraft/src/`):
- Create: `main/java/dev/halfcraft/world/DigProgress.java` (без классов Minecraft: тест не грузит игру), `client/java/dev/halfcraft/client/{HostDigClient,mixin/MinecraftDigMixin}.java`
- Modify: `client/resources/halfcraft.client.mixins.json:16-17`, `client/java/dev/halfcraft/client/{HalfCraftClient.java:15,render/WorldExporter.java:414-467}`
- Test: `test/java/dev/halfcraft/world/DigProgressTest.java`

**Interfaces:**
- Consumes: Task 10 — `HostTri.digMaterial`, `diggable()`, `HostLink.HostState.digOff()`; Task 11 — `HostDig` (`DUG`, `sectionBits`, `open`, `stateFor`), `DugColumn` (`EMPTY`, `sections()`, `bits(int)`), `DugBits.index/get`; Task 6 (шаги 8–9) — `hc_debug_dig_material`.
- Produces:
  - `dev.halfcraft.world.DigProgress`, `public`: `enum Step { NONE, MINE, KNOCK, OPEN }`, `static final int STALE_TICKS = 2, DELAY_TICKS = 5`, `Step hit(long cell, int tick, float delta)`, `void tick(int tick)`, `boolean mining()`, `long cell()`, `int stage()`, `void clear()`.
  - `dev.halfcraft.client.HostDigClient`, `public static`: `boolean attack(Minecraft)`, `void tick(Minecraft)`, `@Nullable BlockPos target(Minecraft)`, `int @Nullable [] crack()` (`{x, y, z, stage 0-9}`), `long @Nullable [] dugBits(ClientLevel, int sx, int sy, int sz)`, `boolean dug(ClientLevel, int x, int y, int z)`.
  - `dev.halfcraft.client.mixin.MinecraftDigMixin`; лог `HalfCraft: dug Half-Life's wall at <x>, <y>, <z> (minecraft:<block>)`.

- [ ] **Step 1: Написать падающий тест `DigProgressTest`**

Как `RegionEdgeTest`; `CELL = 7L`, `OTHER = 8L`, новый `DigProgress` в каждом тесте, `=` — `assertEquals`, дельты `float`:
```
shouldTakeOneHitPerTick  hit(CELL,1,.25)=KNOCK; hit(CELL,1,.25)=NONE; stage()=2
shouldOpenWhenProgressReachesOne  hit(CELL,t,.25) t=1..4 = KNOCK,MINE,MINE,OPEN; !mining(); stage()=-1
shouldStartOverOnAnotherCell  hit(CELL,1,.5); hit(OTHER,2,.25)=KNOCK; cell()=OTHER; stage()=2
shouldKeepProgressOverAPauseOfTwoTicks  hit(CELL,1,.5); hit(CELL,3,.25)=MINE; stage()=7
shouldStartOverAfterALongerPause  hit(CELL,1,.5); hit(CELL,4,.25)=KNOCK; stage()=2
shouldDropTheCrackWhenHitsStop  hit(CELL,1,.5); tick(3): mining(), stage()=5; tick(4): !mining(), stage()=-1
shouldWaitFiveTicksAfterOpening  hit(CELL,1,1)=OPEN; hit(OTHER,6,1)=NONE; hit(OTHER,7,1)=OPEN
shouldKnockOnEveryFourthHit  t=1..9: hit(CELL,t,.01) = t%4==1 ? KNOCK : MINE, message "tick " + t
```

- [ ] **Step 2: Запустить тест: падает** — `python tools/build/gradle.py test --tests dev.halfcraft.world.DigProgressTest` → `error: cannot find symbol ... DigProgress`, `BUILD FAILED`.

- [ ] **Step 3: Реализовать `DigProgress`**

`public final class`, клетка — `BlockPos.asLong`. `hit`: `tick < readyAt` (вначале `Integer.MIN_VALUE`) или второй удар по той же клетке в том же тике → `NONE` без прогресса; нет копания, клетка другая или `tick - lastTick > STALE_TICKS` → прогресс и удары с нуля; затем `progress += delta`, `hits++`, `lastTick = tick`; `progress >= 1` → копание сброшено, `readyAt = tick + DELAY_TICKS + 1`, `OPEN` (как ванильный `destroyDelay = 5`: следующий удар на `tick + 6`); иначе `(hits - 1) % 4 == 0` → `KNOCK`, прочее `MINE`. `tick(now)`: копание и `now - lastTick > STALE_TICKS` → `clear()`. `stage()`: при копании и `progress > 0` — `Math.min(9, (int) (progress * 10.0F))`, иначе `-1`.

- [ ] **Step 4: Запустить тест: проходит** — та же команда → 8 × `PASSED`, `BUILD SUCCESSFUL`.

- [ ] **Step 5: Написать `HostDigClient`**

Порт `SkyDigClient` (SkyCraft) на `DigProgress`, только render thread; поля `PROGRESS`, `int ticks`, `Long2ObjectOpenHashMap<DugColumn> SEEN`, `@Nullable ClientLevel seenLevel`.
- `target` — `HostRay.surfaceCell(result.hit)` или `null`: нет `HostClip.HostHitResult result`, `level`, `player`, `gameMode`, `getSingleplayerServer()` (открывает только встроенный сервер); `!HostClient.linked()`, `host().digOff()`, `!HostNav.inMirror(level)`, в руке `HostWeapons.isStandIn`, `isBlockPlacingRestricted()`, `!tri().diggable()`, `dug(...)`; блок клетки не воздух и не `canBeReplaced()`.
- `attack`: без `target` — `false`; иначе `material = tri().digMaterial`, `PROGRESS.hit(pos.asLong(), ticks, instabuild ? 1 : HostDig.stateFor(material).getDestroyProgress(player, level, pos))`, `true`; `KNOCK` — звук и частицы `SkyDigClient.java:100-105`; `OPEN` — лог и `HostDig.open(sp.level(), sp, pos, material)` в `getSingleplayerServer().execute` (игрок по `UUID`, в `HostNav.inMirror`).
- `crack()` — клетка и `stage()`, пока `mining()` и `stage() >= 0`.
- `tick`: `ticks++`; без `level`, `player` или связи — `PROGRESS`, `SEEN`, `seenLevel` сбросить; иначе `PROGRESS.tick(ticks)` и опрос `SkyDigClient.pollDug` каждый тик, без мира Skyrim: новый `level` → `SEEN.clear()`; `markDirtyNow`/`markDirty` только у `sectionY` с `!Arrays.equals(old.bits(sy), now.bits(sy))`.
- `dugBits` — `HostDig.sectionBits` чанка `getChunk(sx, sz, ChunkStatus.FULL, false)` или null; `dug` — его бит `DugBits.index(x, y, z)`.

- [ ] **Step 6: Написать `MinecraftDigMixin` и подключить его**

`@Mixin(Minecraft.class)`, `@Inject(at = @At("HEAD"), cancellable = true)` в `startAttack` (`halfcraft$digStart`, `cir.setReturnValue(true)`) и `continueAttack` (`halfcraft$digHold(boolean down, …)`, ещё `down && !player.isUsingItem()`, `ci.cancel()`): при `HostDigClient.attack(self)` — `player.swing(InteractionHand.MAIN_HAND, player.getMainHandItem().getAttackAnimation(), false)` и отмена ванили (та ставит `missTime = 10` и не машет). `WeaponInputMixin` не мешает: с оружием Half-Life `target` отказывает. В `halfcraft.client.mixins.json` — `"MinecraftDigMixin",` перед `MinecraftMixin`.

- [ ] **Step 7: Зарегистрировать тик** — `HalfCraftClient.java` после строки 15: `ClientTickEvents.END_CLIENT_TICK.register(HostDigClient::tick);`.

- [ ] **Step 8: Трещина и рамка в `WorldExporter`**

`addCracks`: тело цикла — в `private static void addCrack(BlockPos pos, AABB box, int stage)`, после цикла при `HostDigClient.crack() != null` и `ENTITIES.size() < Proto.MAX_WORLD_ENTITIES` — `addCrack` по `new AABB(pos).inflate(0.004)` (R11). `selection`: в ветке `HostHitResult` без `BlockItem` в руке — куб `HostDigClient.target(minecraft)` или null.

- [ ] **Step 9: Форматирование, сборка и линтер** — `python tools/build/gradle.py spotlessApply`, `make mc` → `BUILD SUCCESSFUL`, без `warning:` (`-Werror`); `make lint` → код 0.

- [ ] **Step 10: Проверка в игре**

Нужны Task 6, 7, 9, иначе `kHostDigOff`; без них шаг — в конце Task 13 без пункта 5 (стена уже вырезана). При закрытых играх: `make build ENGINE=hl2dm`, `make test-start ENGINE=<engine>` движкам без сессии (`python tools/game/test_session.py status`), `dig_base` по шапке (шаг 14 Task 11 закрыл сессию). `cmd` — `python tools/game/hl2_command.py`; «ждать/нет `<re>` N» — `python tools/game/wait_log.py mc-test --after <mark перед командой> --pattern "<re>" --timeout N [--absent]`.
1. `make mc-test` в фоне, `make run ENGINE=hl2dm MAP=d1_trainstation_02`, ждать `Half-Life link up` 180, `teleported to` 120; `cmd "load dig_base"`, ждать `rolled back to checkpoint|no checkpoint`, `teleported to` по 90.
2. `cmd "hc_look P0 Y0" "hc_debug_dig_material"` (углы этапа 1): в логе `hl2dm` DigMaterial 1–3 (рукой дольше 5 с, киркой до 1,25 с; `<drop>`: `cobblestone`, `stone_bricks`, `iron_block`), иначе `hc_look` к бетону `C`.
3. `cmd "hc_mc item replace entity @s weapon.mainhand with air" "hc_hold 1 1 5" "hc_screenshot"`: ждать `frame saved as` 10, нет `dug Half-Life's wall` 8.
4. То же с `iron_pickaxe` и `hc_hold 1 1 2`: ждать `dug Half-Life's wall at` 20; `cmd "hc_mc execute if entity @e[type=item,distance=..6]" "hc_mc clear @s <drop> 0"`: ждать `hc_mc .* -> (Test passed|Found)` 20.
5. `cmd "hc_hold 1 1 2"`: нет `dug Half-Life's wall` 6 (до Task 13 удар идёт в выкопанную, ещё твёрдую клетку).

Прошло: всё с кодом 0; на PNG из `frame saved as` (`minecraft/run/test/screenshots`) трещина и рамка у перекрестья. Нет `dug` — материал и `digging is off` в `build/game-hl2dm/console.log`. Затем обе игры закрыть процедурой шапки; сессия остаётся. Повтор: закрыть игры, `Remove-Item -Recurse -Force minecraft/run/test/saves/HalfCraftTest` (`load dig_base` дыры не откатывает).

- [ ] **Step 11: Коммит**

```
git add minecraft/src
git commit -m "feat: minecraft's player mines half-life's walls cell by cell, as long as a block of their material takes, with cracks, hit sounds and an outline; the integrated server opens the cell" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```


---

### Task 13: Minecraft клиент: стенки дыры и отправка дыр в Half-Life

**Files:**
- Create: `minecraft/src/client/java/dev/halfcraft/client/render/DigWalls.java`
- Test: `minecraft/src/test/java/dev/halfcraft/client/render/DigWallsTest.java` (тот же пакет: Loom даёт тестам вывод `client`)
- Modify: `WorldExporter.java` там же (строки — до Task 12), `docs/superpowers/specs/2026-10-17-dig-through-walls-design.md`

**Interfaces:**
- Consumes: задачи 4 (`Proto.REN_DUG`, `REN_DUG_BYTES`, `DIG_*`), 10 (`HostCollision.rockLayersAt`, `HostTri.digMaterial`, `diggable()`), 11 (`DugBits`, `HostDig.stateFor`), 12 (`HostDigClient.dugBits`, `dug`; смену вложения его `tick` помечает сам).
- Produces:
  - package-private `final class DigWalls`: `interface Cells` (`boolean dug`, `boolean occludes`, `long @Nullable [] rock`, `int material`, все от `(int x, int y, int z)`), `interface Out { void quad(float[] corners, int face, int material, float[] uv); }` (`face` — ordinal `Direction`), `static int add(int sx, int sy, int sz, long[] dug, Cells cells, Out out)` → число квадратов.
  - Лог `HalfCraft: sent <n> dug cells of section <sx> <sy> <sz> to Half-Life (<q> wall quads)` (Task 14).

- [ ] **Step 1: Написать падающий тест `DigWallsTest`**

```
// Fake DigWalls.Cells (rock Map<Pos, long[]>, dug and occluding Set<Pos>, material DIG_IRON); walls(...) marks the cell dug,
//   runs add, copies the quads, count == size. area = sum |(c1-c0) x (c3-c0)|, normal = (c1-c0) x (c2-c0), FULL = 8 x -1L.
// Dug (1, 2, 3) of section (0, 0, 0), rock at (2, 2, 3) unless named; UP = 1, WEST = 4:
shouldCoverTheWholeFaceTowardAFullyRockEastNeighbour: FULL -> 8 quads, area 1.0; each face WEST, material DIG_IRON,
  normal.x < 0, corners x == 2.0f, y in [2, 3], z in [3, 4], uv in [0, 1]
shouldCoverTheLowerHalfTowardANeighbourRockInItsLowerHalf: {-1L x 4, 0L x 4} -> 4, area 0.5, y in [2.0, 2.5], uv v >= 0.5
shouldUseTheNeighboursSliceThatTouchesTheFace: 8 x 0x8080808080808080L (x = 7) -> 0; 8 x 0x0101010101010101L (x = 0) -> 8, area 1.0
shouldSplitARowIntoItsRuns: 8 x 0x0000000100000101L (x = 0 at z 0, 1, 4) -> 16, area 0.375, z in [3.0, 3.25] or [3.5, 3.625]
shouldFloorTheCellWithTheTopSliceOfTheNeighbourBelow: rock (1, 1, 3) only layers[7] = -1L -> 8, face UP, normal.y > 0, y == 2.0f, area 1.0
shouldDrawNoWallTowardADugOccludingOrRocklessNeighbour: 0 for FULL + in dug; FULL + in occluding; no rock (null); new long[8]
shouldDrawNoWallsInADugCellAnOccludingBlockFills: FULL, (1, 2, 3) in occluding -> 0
shouldGiveSectionRelativeCornersAcrossASectionEdge: dug (31, -14, 35) of section (1, -1, 2), rock (32, -14, 35) FULL -> 8,
  x == 16.0f, y in [2, 3], z in [3, 4]
```

- [ ] **Step 2: Запустить тест: падает.** `python tools/build/gradle.py test --tests dev.halfcraft.client.render.DigWallsTest` → `error: cannot find symbol` (`DigWalls`), `BUILD FAILED`.

- [ ] **Step 3: Написать `DigWalls`**

Перенос SkyCraft `DigWalls.java` только по слоям скалы (1/8 блока), без `net.minecraft`: тест не грузит Minecraft. `dug` не менять: этот массив отдаёт вложение. У грани к соседу `n = клетка + STEP[d]` ячейка `(i, j)` (0..7) занята, если занят субвоксель `n` `floor(8·(p − 15/16·STEP[d]))`, `p = frame(d, (i + .5)/8, (j + .5)/8)`. Каждый максимальный отрезок занятых `i0..i1` строки `j` — квадрат с углами `frame` в `(i0, j), (i1+1, j), (i1+1, j+1), (i0, j+1)` / 8 + `клетка − 16·секция`, uv угла `(u, 1 − v)`, `face = d ^ 1`; `material(n)` — раз на грань с квадратами.

```java
private static final int[][] STEP = { { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 }, { -1, 0, 0 }, { 1, 0, 0 } }; // by Direction ordinal
// frame(d, u, v) from the dug cell's min corner, u right and v up seen from inside:
// DOWN (u, 0, 1 - v) | UP (u, 1, v) | NORTH (u, v, 0) | SOUTH (1 - u, v, 1) | WEST (0, v, 1 - u) | EAST (1, v, u)
```

- [ ] **Step 4: Запустить тест: проходит.** Та же команда: 8 × `PASSED`, `BUILD SUCCESSFUL`.

- [ ] **Step 5: Коммит**

`git add minecraft/src`
`git commit -m "feat: the walls of a dug hole come from the rock around it, an eighth of a block at a time (DigWalls)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`

- [ ] **Step 6: Добавить `MeshBuilder.quad(float[] corners, float[] uv, int light, Direction normal)`** после `flags`: `ensure(6 * Proto.REN_VERTEX_BYTES)`, `vertex` по углам `0,1,2, 0,2,3`, цвет `0xFFFFFFFF`, `flags(false, normal)`.

- [ ] **Step 7: Добавить адаптер `WallCells`.** `private static final class WallCells implements DigWalls.Cells` у `MeshBuilder`, экземпляр `WALLS` и `float[8] WALL_UV` — у `MESH`. `dug` → `HostDigClient.dug`; `occludes` → `canOcclude()` блока; `rock` → `rockLayersAt`, null → `unknown = true`, `unknownCell = BlockPos.asLong(x, y, z)`; `material` → `tris.clear()` (`HostCollision.trianglesNear` только добавляет), `trianglesNear(new AABB(x, y, z, x + 1, y + 1, z + 1), tris)`, `digMaterial` первого `diggable()`, иначе `DIG_STONE`.

- [ ] **Step 8: Добавить стенки в `meshSection`**

До проверки пустоты (`:200-204`): `dug = HostDigClient.dugBits(level, sx, sy, sz)`, `dugCount` (`DugBits.count`, 0 при null), `walls = 0`, `WALLS.level = level`, `WALLS.unknown = false`; пропуск на `:202` — ещё `&& dugCount == 0 && !DUG.contains(key)`. После `if (!empty) {…}` (`:260`) при `dugCount > 0` — `walls = DigWalls.add(sx, sy, sz, dug, WALLS, out)`, где `out`:
- `normal = Direction.from3DDataValue(face)`; `rects = cubeFaces(Minecraft.getInstance(), HostDig.stateFor(material))`, null → для `DIG_STONE`, снова null → без квадрата;
- `o` = 4 (верх) у `UP`, 8 (низ) у `DOWN`, иначе 0; `WALL_UV` угла = `rects[o, o+1] + uv · (rects[o+2, o+3] − rects[o, o+1])`;
- `light = LightCoordsUtil.pack` (`net.minecraft.util`) блочного и небесного `level.getBrightness` в `BlockPos.containing(16·s + центр квадрата + 0.5·normal)`;
- `MESH.quad(corners, WALL_UV, light, normal)`.

Затем `WALLS.unknown` → `DUG_WAITING.put(key, WALLS.unknownCell)`, иначе `remove(key)`. Стенки `DIG_GLASS` с вырезом просвечивают — оставить (шаг 14).

- [ ] **Step 9: Отправлять `kRenDug` после `REN_SECTION`.** Поле `LongOpenHashSet DUG` и запись — как у `SOLID` и `REN_SOLIDS` (ранний выход `:261`, после `REN_SOLIDS`, `markDirty` при полном кольце), но условие `dugCount > 0 || DUG.contains(key)`, заголовок `sx, sy, sz, dugCount` (`REN_DUG_BYTES`), тело `DugBits.toBytes(dug)` или пустое; при успехе ещё лог из Produces.

- [ ] **Step 10: Учесть дыры в `resendEverything`.** У `SOLID.clear()` — `DUG.clear()`, `DUG_WAITING.clear()`; в цикле чанков секцию с `HostDigClient.dugBits(…) != null` — через `markDirtyNow`, даже из одного воздуха: иначе после `kRenClearAll` туннель долго остаётся скалой.

- [ ] **Step 11: Перепроверять `DUG_WAITING`.** Поле `Long2LongOpenHashMap DUG_WAITING` (секция → клетка без коллизии). В `frame()` перед `meshDirtySections` записи с `HostCollision.isKnown` клетки удалить, их секции → `markDirty` (приход региона сам не перемешивает).

- [ ] **Step 12: Собрать мод с тестами.** `make mc` → `BUILD SUCCESSFUL`, тесты `PASSED`.

- [ ] **Step 13: Отформатировать и прогнать линтеры.** `python tools/build/gradle.py spotlessApply`, затем `make lint` → код 0.

- [ ] **Step 13a: [если шаг 10 задачи 12 был отложен] Проверить в игре.** Когда готовы задачи 6, 7 и 9 — шаг 10 задачи 12 без последней группы (`hc_hold 1 1 2`, `--absent --timeout 6`): стена уже вырезана. Во второй группе после `dug Half-Life's wall at` ещё `python tools/game/wait_log.py mc-test --after $mark --pattern "dug cells of section" --timeout 10`.

- [ ] **Step 14: Записать артефакт стекла в спеку.** В таблицу «## Риски и запасные варианты»: `| Стенка дыры у стекла HL2 рисуется стеклом с вырезом и просвечивает | Известный артефакт прототипа |`.

- [ ] **Step 15: Коммит**

`git add minecraft/src docs/superpowers/specs/2026-10-17-dig-through-walls-design.md`
`git commit -m "feat: minecraft meshes the walls of holes in half-life's world and sends half-life each section's dug cells (kRenDug)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`


---

### Task 14: Проверка в игре

Проверки 1–6 спеки одним прогоном на `hl2dm`, `d1_trainstation_02`, в тестовой сессии; короткий запуск `hl2`; линтеры; результаты в спеку. Кода нет. Не прошла проверка — чинится задача из Consumes, прогон заново с шага 2.

**Files:**
- Modify: `docs/superpowers/specs/2026-10-17-dig-through-walls-design.md:5` (статус) и конец файла (`## Результаты проверки`)
- Вне git: `build/dig-check/` (снимки, TGA, тестовые миры)
- Test: прогон в игре

**Interfaces:**
- Consumes:
  - из `## Результаты этапа 1` — `build/dig-baseline/save/dig_base.*` с углами `P0 Y0`, `build/dig-baseline/dig_base_stock.tga`, процедура «кадр dig_base», шум `N` %, стены `W2` (место, `Y2`, ≤ 2 блоков, за ней комната B) и `W1` (место, `Y1`, ≤ 1 блока, за ним пустота, ≤ 30 блоков от `W2`);
  - строки логов — из Produces задач 5–8 и 10–13; DLL только у `hl2dm` (задача 9);
  - поведение: грань браша несёт `DigMaterial` своей стороны, срез — материал браша (2, 6); вскрытые соседи помечены выкопанными (в `n` и рамках `hc_debug_dig`), ставятся мимо Rollback, а выломанные сейв возвращает (7, 11); пока у карты есть дыры, `r_novis` и `r_portalsopenall` равны 1 (8).
- Produces: `## Результаты проверки` и строка статуса.

**Соглашения.** PowerShell из корня, блок — один вызов; `wait`, `absent`, `air`, `solid`, `diff` выходят с кодом 0.
- Команды игре — `python tools/game/hl2_command.py "<к1>" "<к2>" …`; `hc_look <pitch> <yaw>` — градусы Minecraft, `hc_key 26|22 1 <с>` — W|S, `hc_hold 1 1 <с>` — ЛКМ; `hand <id>` = `hc_mc item replace entity @s weapon.mainhand with minecraft:<id>`.
- `state` = `python tools/debug/hc_state.py`: `host … flags 0x.. … pos (x y z)` (ноги Source в координатах Minecraft) и `mc … pos`; `same` — они совпадают до 0.05.
- `wait m|h "<re>" <с>` = `python tools/game/wait_log.py mc-test|hl2dm --after <метка> --pattern "<re>" --timeout <с>` (в шаге 18 `hl2`), `absent` — с `--absent`; метка — `--mark` того же лога перед командой, вызвавшей строку (`@<x>` — метка из другого вызова); кавычки в `<re>` — `\W+`.
- `save`/`load <s>` ждут `wait m "HalfCraft: checkpoint [0-9a-f]+ " 60` / `wait m "HalfCraft: rolled back to checkpoint" 300`; `air <c>` — `hc_mc execute if block <c> minecraft:air`, `wait m "finished: success" 20`; `solid <c>` — с `unless`.
- `dug(<re>)` = `dig: \d+ section\(s\) changed, <re> cell\(s\) dug in this map`; `n` — число последней такой строки `build/game-hl2dm/console.log`; `moved` = `source (moved the player|refused minecraft's jump|landed minecraft's jump)`.
- Кадр `<имя>`: процедура этапа 1 со `screenshot`, новейший `.tga` из `build/game-hl2dm/screenshots` → `build/dig-check/<имя>.tga`; `diff a b` = `python tools/debug/tga_diff.py build/dig-check/a.tga build/dig-check/b.tga --max <N + 0.1>`; снимок `<имя>` — `jpeg <имя> 90` в `build/game-<engine>/screenshots/`, смотреть (Read).
- `mat`: `hc_debug_dig_material`, `wait h "hc_debug_dig_material: brush \d+ side \d+ of \d+: .* -> side [1-6]" 20`; число после `-> side` — строка таблицы; нет строки — другая клетка той же стены.
- `stop-mc` — остановка Minecraft из шапки (пока Half-Life запущен); `close-hl` — её `quit` и `Wait-Process` (`hl2`: `-Name hl2`).
- Клетки: yaw стены кратен 90 (−90 смотрит на +x, 90 на −x, 0 на +z, 180 на −z), `s` = +1 при −90 и 0, иначе −1; `(k, y, t)` — блок `A0 + k·s` по оси стены (x при ±90, иначе z), высоты `y`, поперёк `T + t`; `A0`, `T` — клетка у головы первого столбца, `F` — её y − 1. Туннель `W1` — `(k, F|F+1, 0)`, `k` = 0..2; стенки столбца — `(k, F+2, 0)`, `(k, F−1, 0)`, `(k, F|F+1, ±1)`; торец — `(3, F|F+1, 0)`.

Удержания — секунды `hc_hold`, трещина — 4 с.

| `DigMaterial` | блок → дроп | инструмент | ломается за | короткое | длинное | трещина: в руке, усталость | алмазная |
|---|---|---|---|---|---|---|---|
| 1 камень | `stone` → `cobblestone` | `wooden_pickaxe` | 1.15 с | 0.6 | 1.5 | `air`, — | 0.45 |
| 2 кирпичи | `stone_bricks` → `stone_bricks` | `wooden_pickaxe` | 1.15 с | 0.6 | 1.5 | `air`, — | 0.45 |
| 3 железо | `iron_block` → `iron_block` | `stone_pickaxe` | 1.9 с | 1.0 | 2.3 | `stone_pickaxe`, 0 | 1.1 |
| 4 доски | `oak_planks` → `oak_planks` | `wooden_pickaxe` | 3 с | 1.5 | 3.5 | `air`, 0 | 3.15 |
| 5 земля | `dirt` → `dirt` | `wooden_pickaxe` | 0.75 с | 0.35 | 1.2 | `air`, 1 | 0.9 |
| 6 стекло | `glass` → ничего | `wooden_pickaxe` | 0.5 с | 0.2 | 0.9 | `air`, 1 | 0.65 |

- [ ] **Step 1: Собрать и прогнать тесты**

`make build; make mc; make core-test`: код 0 (`built hl2dm`, `built hl2`, JUnit задач 10–13).

- [ ] **Step 2: Сессия, чистый мир, сейв этапа 1**

`python tools/game/test_session.py status` (сессия или игры после сбоя — `stop-mc`, `close-hl`, `make test-stop`); `make test-start` без `ENGINE` (иначе откажет `make mc-test`) → `test session on` у обоих. Нет `build/dig-check/HalfCraftTest-before` — создать и перенести в неё `minecraft/run/test/saves/HalfCraftTest` (если есть), есть (повтор) — этот мир удалить: Minecraft создаст свежий. `dig_base.*` из `build/dig-baseline/save` — в `build/game-hl2dm/save`.

- [ ] **Step 3: Запустить Minecraft, затем hl2dm**

`make mc-test` в фоне, `make run ENGINE=hl2dm MAP=d1_trainstation_02`; `wait m "HalfCraft: Half-Life link up" 600`, `wait h "d1_trainstation_02.bsp's tree: 3598 nodes, 3715 leaves \(1127 solid\)" 300`, `wait h "dig: game_shader_generic_halfcraft.dll is loaded" 60`, `wait m "HalfCraft: first void region" 120`; `python tools/debug/dump_collision.py` печатает `void: <R ≥ 1> regions`. Прошло: строка дерева кончается `(world collideable) agrees at 512 of 512 points` (с `point contents` — расхождения у брашевых сущностей).

- [ ] **Step 4: Сейв до копания**

`load dig_base` с `wait m "HalfCraft: no checkpoint" 120`, `hc_look P0 Y0`, `save dig_start`.

- [ ] **Step 5: Без дыр: кадры и замер**

Кадры `base` (`hc_look P0 Y0`) и `u0` (`P0 <Y0+180>`, без `W1`, `W2`, иначе другие углы — записать); `python tools/debug/tga_diff.py build/dig-check/base.tga build/dig-baseline/dig_base_stock.tga --max <N + 0.1>`. «Замер» без процедуры кадра (`hc_perf` считает кадры с оверлеем): `hc_look P0 Y0`, 10 с, `hc_perf`, `r_novis`, `hc_debug_voxels 1`; `wait h` по 60 с `halfcraft perf: [0-9.]+ fps`, `r_novis\W+=\W+0`, `hc_debug_voxels: \d+ voxels, \d+ with nothing` → `F0`, `V0`.

- [ ] **Step 6: Проверка 1: трещина, время, дроп**

`hc_look P0 Y0`, `mat` (дальше 4.5 блока — подойти W).
1. `hc_mc kill @e[type=minecraft:item]`, `hc_mc clear @s minecraft:<дроп или glass>`; рука и усталость по таблице (`hc_mc effect give @s minecraft:mining_fatigue 30 <уровень> true`, потом `effect clear`); `hc_hold 1 1 4`, снимок `c1-crack`, `absent m "HalfCraft: dug " 6`. Прошло: на снимке трещина.
2. `hand <инструмент>`, `hc_hold 1 1 <короткое>`, `absent m "HalfCraft: dug " 4`.
3. `hc_hold 1 1 <длинное>`; по 10 с — строки задач 12 и 11 с `minecraft:<блок>`, 13 с `sent [1-6]`, `wait h dug([1-6])`, `wait h "dig: this map has holes: r_novis 1, r_portalsopenall 1"`. Клетка — `C1`, через 3 с `n1 = n` (1 + вскрытые соседи, ≤ 6).
4. `hc_mc execute as @e[type=minecraft:item,distance=..8] run data get entity @s Item.id`, `hc_mc clear @s minecraft:<дроп> 0`, `wait m "entity data: \W+minecraft:<дроп>|Found 1 matching item" 10` (у стекла — `absent` с `glass`).

- [ ] **Step 7: Проверка 1: дыра на снимках**

`hc_debug_dig 1`, снимок `c1-debug`, `hc_debug_dig 0`, снимок `c1-hole`; факел на блок позади (у ног бывает клетка стены): `hc_look 0 Y0`, `hc_mc setblock ^ ^ ^-1 minecraft:torch`, `hc_look P0 Y0`, 3 с, снимок `c1-torch`, снова `hc_look 0 Y0` и тот же `setblock` с `minecraft:air`. Прошло: `c1-debug` — оранжевые рамки клеток на `C1` и только на её соседях (вскрытые, `n1 − 1`); синие — куски брашей вокруг (задача 7); `c1-hole` — дыра в блок, края — срез стены или вскрытые блоки (не пустота, не небо), в глубине вскрытый блок или комната за `W2`; `c1-torch` — стена освещена, в дыре пятна нет.

- [ ] **Step 8: Проверка 2: туннель в W1**

`hc_mc tp @s <место перед W1>`, `hc_look 0 Y1`, `hand diamond_pickaxe`, метка mc-test `<mt>`. Пока ноги (`state`) не в `k = 2`: `hc_key 26 1 1.5`, затем клетка `<c>` у головы (`p` = 0) и у ног (`p` = 70), открытую пропускать: `hc_look <p> Y1`, `hc_debug_dig_material`, `hc_mc execute if block <c> minecraft:air`, ждать `hc_debug_dig_material: ` и `finished: (success|failure)`; `hc_hold 1 1 <t>`, `<t>` — «алмазная»: при `success` и у первой клетки — по `-> side`, иначе (вскрытый блок) камня, если `if block <c> minecraft:stone`, или блока последней строки задачи 11. По строкам задачи 11 после `<mt>` записать `A0`, `T`, `F` (первая) и клетки, открытые с поверхности. Прошло: после каждого W игрок входит в новый столбец.

- [ ] **Step 9: Проверка 2: в туннеле, стенки из пустоты**

`state`, `absent m@<mt> "HalfCraft: (teleported to|lifted player)" 2`; одним `hc_mc execute if block <c> minecraft:stone if block …` по 8 клеткам (стенки столбца 2, торец) — `finished: success` (при `failure` — по одной). Факел `hc_mc setblock ~ ~ ~ minecraft:torch`, снимки `t-ahead`, `t-side`, `t-back` (yaw `Y1`, `Y1+90`, `Y1+180`), факел убрать до сейвов. Прошло: `same` в `k = 2`; стенки сплошные (камень из `kColVoid`), назад видна комната.

9a: `hc_look 0 Y1`, `save dig_inside`, `state`: `same` в `k = 2` — это `P_in`.

- [ ] **Step 10: Сейв с дырами**

`hc_key 22 1 1.2`, `hc_look 0 Y1`, `save dig_holes`; игрок в комнате.

- [ ] **Step 11: `hc_dig 0` выключает копание**

`hc_dig 0`, снимок `dig-off`, `state`; `hc_mc tp @s <centre of (-1, F, 2)>`, `hc_look 0 Y1`, `mat`, `air (0, F+1, 2)` (через клетку от туннеля: вскрытые блоки ломаются ванильно и при `hc_dig 0`); `hc_hold 1 1 4`, `absent m "HalfCraft: dug " 6`; `hc_dig 1`, `state`. Прошло: при `hc_dig 0` в `host flags` бит `0x10`, туннель вырезан; после `hc_dig 1` бита нет.

- [ ] **Step 12: Проверка 3: сквозь W2**

1. `hc_mc tp @s <место перед W2>`, `hc_look 0 Y2`, `<mt>`; столбцы циклом шага 8 с `W2` (начат с `C1` — `A0`, `T`, `F` по ней), пока клетки у головы и ног не откроются в комнату B (видно на снимке).
2. Снова `tp` и `hc_look` пункта 1; `hc_debug_dig_pvs 1`, снимок `w2-from-a`, `wait h "dig pvs: \d+ holes near the player add [1-9]\d* clusters" 20`; `hc_debug_dig_pvs 0`, `hc_key 26 1 2`.
3. `state`; снимок `w2-from-b` при `Y2+180`; `r_novis`, `r_portalsopenall` — обе `\W+=\W+1`.

Прошло: `same` в комнате B; на обоих снимках сквозь дыру комната (стены, пол, свет, NPC), без чёрного, неба, «зеркал»; обе грани `W2` вырезаны.

- [ ] **Step 12a: Копание вниз**

Пол перед `W1`, `K = −3`, `t = 0`: `hc_mc tp @s <centre of (K, F, 0)>`, `hc_look 90 Y1`, `mat` (нет строки — `K = −4`, `−5`, записать). Для `d = 1, 2, 3` — клетка под ногами, как в шаге 8 при `p` = 90; через 2 с `air (K, F−d, 0)`, `state`, `absent h moved 2`, `absent m "HalfCraft: lifted player" 2` (первой выкопалась `(K, F, 0)` — сдвинуть счёт). Снимки `pit-side`, `pit-side2`, `pit-up` (`hc_look 0 Y1`, `0 <Y1+90>`, `-90 Y1`), `pit-above` с `(K−2, F, 0)` при `50 Y1`, `state`. Прошло: после клетки `same`, y ног `F − d`; стенки сплошные; на `pit-up` сквозь пол комната, на `pit-above` яма; в конце обе позиции в `(K − 2, F, 0)`.

- [ ] **Step 13: Откат к сейву до копания**

Записать `n`, `load dig_start`; через 5 с `n` тот же; `solid` стенок столбцов 1, 2 и торца (вскрытие мимо Rollback), `air` клеток с поверхности (шаг 8), `solid` прочих клеток туннеля (выломанные вскрытые блоки сейв вернул; записать). Кадр `u1` с углами `u0`, `diff u0 u1`; «замер» с `r_novis\W+=\W+1` → `F1`, `V1`. Прошло: `V1 ≤ V0`. Если `F1 < 0.7 · F0`, отметить в результатах риск «`r_novis` сильно роняет FPS» (мера из спеки — после прототипа).

- [ ] **Step 14: Проверка 4: до перезапуска**

`load dig_holes`, `air` шести клеток туннеля, кадр `t1`, `n`.

14a: `load dig_inside`, `wait h "puppet on" 120`, 5 с, `state`, `absent m "HalfCraft: lifted player" 2`, `absent h moved 2`, снимок `in-load`, `hc_key 22 1 2.5`, 3 с, `state`. Прошло: первый `state` — обе позиции = `P_in` до 0.05, стенки сплошные; второй — игрок в комнате (`k` ног < 0).

- [ ] **Step 15: Проверка 4: выход**

`stop-mc`, `close-hl`, `python tools/game/test_session.py status`: `half-life: not running`, `minecraft: none`; записать, чем остановлен Minecraft.

- [ ] **Step 16: Проверка 4: после перезапуска**

Шаг 3 заново (его метка hl2dm — `<h3>`). 16a: 14a и `wait h dug(\d+) 120` (client.dll ещё ждёт `kRenDug`, а server.dll уже шлёт коллизию). `load dig_holes`, `wait h@<h3> dug(<n шага 14>) 120`, кадр `t2`, `diff t1 t2`, `air` клеток шага 14.

- [ ] **Step 17: Карта без дыр и возвращение**

`map d1_trainstation_03`, `wait h "dig: no holes here: r_novis 0, r_portalsopenall 0 again" 300`; `r_novis`, `r_portalsopenall` — `\W+=\W+0`; `hc_debug_dig 1`, снимок `m3-debug`, `hc_debug_dig 0`, `hc_debug_voxels 1`, `wait h "hc_debug_voxels: \d+ voxels, \d+ with nothing" 60`, `absent h "voxels inside dug cells" 2`.

17a: `load dig_holes`, `wait h dug(<n>) 120` (`<n>` последней строки до шага 17), `wait h "dig: this map has holes: r_novis 1, r_portalsopenall 1" 120`; кадр `t3`, `diff t1 t3`; `hc_key 26 1 1.5`, `state`; `close-hl` (Minecraft нужен шагу 18). Прошло: на `m3-debug` рамок нет; после W `same` в туннеле (`k` ног 0–2).

- [ ] **Step 18: hl2 без шейдерной DLL**

`Test-Path build/game-hl2/bin/game_shader_generic_halfcraft.dll` — `False`; `make run ENGINE=hl2 MAP=d1_trainstation_02`, `wait m "HalfCraft: Half-Life link up" 300`, `wait h "dig: no game_shader_generic_halfcraft.dll in this engine: digging is off" 300`, `state`: бит `0x10`.

18a (мир общий, туннель есть): `hc_mc tp @s <место перед W1>`, `hc_look 0 Y1`, 3 с, снимок `hl2-w1`, `hc_key 26 1 2`, 3 с, `state`, снимок `hl2-w1-after`. Прошло: `W1` целая; после W обе позиции перед стеной (`k` ног < 0), камера не в стене. Затем `stop-mc`, `close-hl`.

- [ ] **Step 19: Закрыть сессию, вернуть мир**

`make test-stop` (`test session off` у обоих); `minecraft/run/test/saves/HalfCraftTest` — в `build/dig-check/HalfCraftTest-dug` (прежний удалить), мир из `build/dig-check/HalfCraftTest-before` (если был) — обратно, папку удалить.

- [ ] **Step 20: Линтеры**

`make lint; make tidy`: код 0.

- [ ] **Step 21: Результаты в спеку**

`:5` → `- Статус: прототип проверен в игре на hl2dm (см. «Результаты проверки»)`. В конец `## Результаты проверки`: дата, коммит (`git rev-parse --short HEAD`), вариант шейдера; по пунктам 1–7 «Прототип готов, когда» — результат, шаг и записанное (материал, удержания, дроп, `C1`, `n1`; туннель: позиции, стенки, клетки с поверхности и вскрытые; `W2`; доли `tga_diff` и `D`; `F0`, `F1`; `n` до и после перезапуска, способ остановки Minecraft; итог `load dig_start`; `hc_dig 0`; hl2; линтеры и тесты); Review Focus — позиции, логи и снимки 9a, 12a (`K`), 14a, 16a, 17a, 18a; «Известные ограничения» — что не прошло (с шагом) и что ранний сейв возвращает выломанные вскрытые блоки (шаг 13); список снимков `build/dig-check`.

- [ ] **Step 22: Коммит**

```powershell
git add docs/superpowers/specs/2026-10-17-dig-through-walls-design.md
git commit -m "docs: the dig prototype checked in game on hl2dm, what it showed in the spec" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git status --short
```
Прошло: `git status --short` пуст (снимки и миры в игнорируемом `/build/`).

