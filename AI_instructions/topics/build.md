# Сборка C++ и шейдеров (ShaderCompile235)

> Общая техническая справка для любой AI-модели. Правила — `../PROJECT.md`, статус — `../CURRENT_STATUS.md`. Команды ниже запускать из git-корня `sp/src`; пути описывают установку пользователя.

## Сборка C++

Visual Studio 2022 Community: `E:\Programs\Microsoft Visual Studio\Community 2022`, тулсет v143, платформа Win32.

```powershell
$msb = "E:\Programs\Microsoft Visual Studio\Community 2022\MSBuild\Current\Bin\MSBuild.exe"
# client.dll / server.dll
& $msb "game\client\client_episodic.vcxproj" /p:Configuration=Release /p:Platform=Win32 /m
& $msb "game\server\server_episodic.vcxproj" /p:Configuration=Release /p:Platform=Win32 /m
# game_shader_dx9.dll (C++ часть шейдеров)
& $msb "materialsystem\stdshaders\game_shader_dx9_episodic.vcxproj" /p:Configuration=Release /p:Platform=Win32 /m
```
(пути относительно `sp\src`). Решения: `games.sln`, `shaders.sln`, `everything.sln`.

- PostBuild-события в `client_episodic`, `server_episodic` и `game_shader_dx9_episodic` уже копируют DLL в `E:\Steam\steamapps\sourcemods\hl2rpm\bin`. Если копирование падает — скорее всего запущена игра и DLL заблокирована.
- Предупреждение «This VCPROJ is out of sync with its VPC scripts» ожидаемо (vcxproj правятся вручную) — игнорировать, VPC не запускать.
- Используются только проекты `*_episodic` (не `*_hl2`). Выходные каталоги: `Release_mod_episodic` / `Release_dx9_mod_episodic`.
- В сообщениях компилятора кириллица из‑за локали MSVC может выводиться кракозябрами — ориентироваться на коды ошибок (C2065 и т.п.).

## Компиляция шейдеров (ShaderCompile235)

Исходники `.fxc` лежат в `sp\src\materialsystem\stdshaders`. Компилятор: `sp\src\game\ShaderCompile235\bin\ShaderCompile.exe` (standalone, без perl/DXSDK). Штатные `buildshaders.bat`/`buildepisodicshaders.bat` не использовать — они собирают всё.

```powershell
$sc = "E:\SourceModding\HL2RPMSOURCE\source-sdk-2013-mapbase-v8.0\sp\src\game\ShaderCompile235\bin\ShaderCompile.exe"
cd "E:\SourceModding\HL2RPMSOURCE\source-sdk-2013-mapbase-v8.0\sp\src\materialsystem\stdshaders"
# один шейдер: -ver 30 для *_ps30/*_vs30, -ver 20b для *_ps20b/*_vs20
& $sc -ver 30 -shaderpath . lightingpass_global_ps30.fxc
# только .inc-заголовок (без компиляции комбо): добавить -dynamic
```
Или списком через `process_shaders.ps1 -File <список.txt> -Version 30`.

- **Какой `.inc` видит C++:** в `game_shader_dx9_episodic.vcxproj` include-пути идут `fxctmp9;vshtmp9;include`, т.е. C++ берёт `.inc` из `fxctmp9\` (старый формат, классы `sdk_*`), а ShaderCompile235 пишет новые в `include\` (классы `SDK_*`). Если комбо шейдера меняются — обновить и `fxctmp9\<name>.inc` (копия из `include\` с заменой `SDK_`→`sdk_` в именах), иначе `.vcs` и C++ разойдутся → «Using invalid shader combo» → `Error()` → краш (так падали декали от пуль, 2026-09).
- **Проверка рассинхрона:** `python materialsystem\stdshaders\check_shader_combos.py` — сверяет каждый `.vcs` в `hl2rpm\shaders\fxc` с `.inc`, против которого собран C++ (число и порядок комбо). Запускать после любой компиляции шейдеров; должно быть `0 mismatched`.
- Новый fxc не принимает `register( D3DVERTEXTEXTURESAMPLER0, s0 )` (X3089) — писать `register( s0 )`.
- Hammer++ держит `hl2rpm\bin\game_shader_dx9.dll` открытой → PostBuild падает и удаляет собранную DLL. Либо закрыть Hammer, либо переименовать занятую DLL (`*.old_inuse`) и пересобрать.
- Выход: `.vcs` → `stdshaders\shaders\fxc\`, `.inc` → `stdshaders\include\`. После компиляции **скопировать нужные `.vcs` в `E:\Steam\steamapps\sourcemods\hl2rpm\shaders\fxc\`**.
- При изменении комбо/констант в `.fxc` сначала обновить `.inc`, затем пересобрать `game_shader_dx9_episodic` (C++ использует `.inc`; ошибки вида `vsh_forgot_to_set_dynamic_X` / необъявленные `iShadowMode` означают рассинхрон `.cpp` и `.inc`).
- Списки шейдеров мода: `stdshaders\hl2rpm_custom.txt` (используемые модом) и `stdshaders\deferred_shaders.txt` (весь deferred-набор). Добавляя новый шейдер, дописать его в `hl2rpm_custom.txt`.
