# HL2RPM — вход для AI-агента

Общие инструкции для любой модели находятся в `AI_instructions`.
Перед работой **прочитать полностью**:
1. [AI_instructions/PROJECT.md](AI_instructions/PROJECT.md) — проект, обязательные правила, сборка, оглавление.
2. [AI_instructions/CURRENT_STATUS.md](AI_instructions/CURRENT_STATUS.md) — последние изменения и незавершённая работа.
3. Нужные `topics/*.md` и `skills/<имя>/SKILL.md` по оглавлению. Skills — обычные файлы, их можно читать без специального инструмента.

Если локального junction `AI_instructions` нет, те же канонические файлы лежат в `source-sdk-2013-mapbase-v8.0/sp/src/AI_instructions`.

Ключевые ограничения: отвечать по-русски; git-корень — `source-sdk-2013-mapbase-v8.0/sp/src`; **не менять `.vpc` и не запускать VPC**; сборка Win32/episodic/Release; шейдеры только ShaderCompile235 и только нужные. Коммит/пуш — по просьбе пользователя. Не сбрасывать накопленные изменения. После работы обновлять общую базу, а не только личную память клиента.

Живая интеграция с **HL2RPM Creation Kit** (без пересборки BSP): когда пользователь просит изменить текстуру/UV/браш/энтити из 3D-вида, сначала прочитать `ck_ai_context/state.json` и `ck_ai_context/view.png`, подобрать материал или геометрию и править `.vmf` через `"E:\SourceModding\HL2RPMSOURCE\HL2RPM Creation Kit\ck_vmf.exe"` — редактор подхватывает изменения в реальном времени; компиляцию в BSP делать только по явной просьбе.
