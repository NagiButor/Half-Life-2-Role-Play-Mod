# Adds the HL2RPM weather / graphics tokens to the mod's localization files
# (UTF-16LE with BOM). Existing tokens with the same name are replaced.
import re, sys, shutil, datetime

MOD = r"E:\Steam\steamapps\sourcemods\hl2rpm\resource"

TOKENS = [
    # weather presets
    ("HL2RPM_Weather_Clear", "Ясно", "Clear"),
    ("HL2RPM_Weather_Fair", "Малооблачно", "Fair"),
    ("HL2RPM_Weather_PartlyCloudy", "Переменная облачность", "Partly cloudy"),
    ("HL2RPM_Weather_Overcast", "Пасмурно", "Overcast"),
    ("HL2RPM_Weather_Fog", "Туман", "Fog"),
    ("HL2RPM_Weather_Drizzle", "Морось", "Drizzle"),
    ("HL2RPM_Weather_Rain", "Дождь", "Rain"),
    ("HL2RPM_Weather_Thunderstorm", "Гроза", "Thunderstorm"),
    ("HL2RPM_Weather_Misty", "Дымка", "Misty"),
    ("HL2RPM_Weather_Snow", "Снегопад", "Snowfall"),
    ("HL2RPM_Weather_Ash", "Пепел", "Falling ash"),
    ("HL2RPM_Weather_Cumulus", "Кучевые облака", "Cumulus"),
    ("HL2RPM_Weather_Stratocumulus", "Облачно с прояснениями", "Broken clouds"),
    ("HL2RPM_Weather_HighClouds", "Высокая облачность", "High clouds"),
    # F1 time of day / weather editor
    ("HL2RPM_TCE_Title", "Время суток и погода", "Time of day and weather"),
    ("HL2RPM_TCE_CurrentTime", "Текущее время", "Current time"),
    ("HL2RPM_TCE_TimeOfDay", "Время суток", "Time of day"),
    ("HL2RPM_TCE_TimeSpeed", "Скорость времени (x реального)", "Time speed (x real time)"),
    ("HL2RPM_TCE_WeatherNow", "Погода:", "Weather:"),
    ("HL2RPM_TCE_WeatherPreset", "Пресет погоды", "Weather preset"),
    ("HL2RPM_TCE_Transition", "Время перехода", "Transition time"),
    ("HL2RPM_TCE_Instant", "Мгновенно", "Instant"),
    ("HL2RPM_TCE_10s", "10 секунд", "10 seconds"),
    ("HL2RPM_TCE_30s", "30 секунд", "30 seconds"),
    ("HL2RPM_TCE_60s", "1 минута", "1 minute"),
    ("HL2RPM_TCE_180s", "3 минуты", "3 minutes"),
    ("HL2RPM_TCE_AutoWeather", "Погода меняется сама", "Automatic weather changes"),
    ("HL2RPM_TCE_NextWeather", "Следующая погода", "Next weather"),
    ("HL2RPM_TCE_Lightning", "Удар молнии", "Lightning strike"),
    ("HL2RPM_TCE_Close", "Закрыть", "Close"),
    # Options -> Graphics page
    ("HL2RPM_Options_Graphics", "Графика", "Graphics"),
    ("HL2RPM_Gfx_Preset", "Общее качество графики", "Overall graphics quality"),
    ("HL2RPM_Gfx_Renderer", "Режим рендера", "Renderer mode"),
    ("HL2RPM_Gfx_Rend_Deferred", "Deferred", "Deferred"),
    ("HL2RPM_Gfx_Rend_Forward", "Forward", "Forward"),
    ("HL2RPM_Gfx_SunShadows", "Тени от солнца и луны", "Sun and moon shadows"),
    ("HL2RPM_Gfx_LampShadows", "Тени от ламп и фонарей", "Lamp shadows"),
    ("HL2RPM_Gfx_Clouds", "Объёмные облака", "Volumetric clouds"),
    ("HL2RPM_Gfx_SunShafts", "Лучи света в воздухе", "Light shafts"),
    ("HL2RPM_Gfx_Rain", "Плотность дождя", "Rain density"),
    ("HL2RPM_Gfx_Wetness", "Мокрые поверхности и лужи", "Wet surfaces and puddles"),
    ("HL2RPM_Gfx_SSAO", "Затенение окружения (SSAO)", "Ambient occlusion (SSAO)"),
    ("HL2RPM_Gfx_SoftShadows", "Мягкие тени солнца", "Soft sun shadows"),
    ("HL2RPM_Gfx_Bounce", "Отражённый свет ламп", "Bounced lamp light"),
    ("HL2RPM_Gfx_WaterReflect", "Отражения в воде", "Water reflections"),
    ("HL2RPM_Gfx_PBR", "Блики и отражения (PBR)", "Highlights and reflections (PBR)"),
    ("HL2RPM_Gfx_RenderScale", "Масштаб рендера", "Render scale"),
    ("HL2RPM_Gfx_Textures", "Качество текстур", "Texture detail"),
    ("HL2RPM_Gfx_Models", "Детализация моделей", "Model detail"),
    ("HL2RPM_Gfx_Filtering", "Фильтрация текстур", "Texture filtering"),
    ("HL2RPM_Gfx_VSync", "Вертикальная синхронизация", "Vertical sync"),
    ("HL2RPM_Gfx_Multicore", "Многоядерный рендеринг", "Multicore rendering"),
    ("HL2RPM_Gfx_Filt_Bilinear", "Билинейная", "Bilinear"),
    ("HL2RPM_Gfx_Filt_Trilinear", "Трилинейная", "Trilinear"),
    ("HL2RPM_Gfx_Filt_Aniso2", "Анизотропная 2x", "Anisotropic 2x"),
    ("HL2RPM_Gfx_Filt_Aniso4", "Анизотропная 4x", "Anisotropic 4x"),
    ("HL2RPM_Gfx_Filt_Aniso8", "Анизотропная 8x", "Anisotropic 8x"),
    ("HL2RPM_Gfx_Filt_Aniso16", "Анизотропная 16x", "Anisotropic 16x"),
    ("HL2RPM_Gfx_Multi_Auto", "Авто", "Auto"),
    ("HL2RPM_Gfx_Q_VeryLow", "Очень низкое", "Very low"),
    ("HL2RPM_Gfx_Q_Low", "Низкое", "Low"),
    ("HL2RPM_Gfx_Q_Medium", "Среднее", "Medium"),
    ("HL2RPM_Gfx_Q_High", "Высокое", "High"),
    ("HL2RPM_Gfx_Q_VeryHigh", "Очень высокое", "Very high"),
    ("HL2RPM_Gfx_Q_Ultra", "Ультра", "Ultra"),
    ("HL2RPM_Gfx_Q_Custom", "Пользовательское", "Custom"),
    ("HL2RPM_Gfx_Off", "Выкл.", "Off"),
    ("HL2RPM_Gfx_On", "Вкл.", "On"),
    ("HL2RPM_Gfx_Rain_Low", "Низкая", "Low"),
    ("HL2RPM_Gfx_Rain_Medium", "Средняя", "Medium"),
    ("HL2RPM_Gfx_Rain_High", "Высокая", "High"),
    ("HL2RPM_Gfx_GPU", "Видеокарта:", "Graphics card:"),
    ("HL2RPM_Gfx_RestartNote", "* Изменения режима рендера и теней от ламп вступят в силу после перезапуска игры.", "* Renderer mode and lamp shadow changes apply after restarting the game."),
    ("HL2RPM_Gfx_Hint", "Разрешение экрана — во вкладке «Видео», угол обзора (FOV) — в «Звук и интерфейс». MSAA заменён на FXAA.", "Resolution is on the Video tab, field of view (FOV) is in Sound and interface. MSAA is replaced by FXAA."),
    ("HL2RPM_Gfx_FXAA", "Сглаживание (FXAA)", "Anti-aliasing (FXAA)"),
    ("HL2RPM_Gfx_AA", "Сглаживание", "Anti-aliasing"),
    ("HL2RPM_Gfx_AA_FXAA", "FXAA", "FXAA"),
    ("HL2RPM_Gfx_Desc_Renderer", "Deferred — динамический свет, тени, погода и PBR; Forward — лёгкий классический рендер (*)", "Deferred — dynamic lighting, shadows, weather and PBR; Forward — lightweight classic renderer (*)"),
    ("HL2RPM_Gfx_Desc_SunShadows", "Каскадные тени от солнца и луны на улице (сильно влияет на FPS)", "Sun and moon shadow cascade range and resolution (high FPS impact)"),
    ("HL2RPM_Gfx_Desc_LampShadows", "Разрешение карт теней от точечных ламп и прожекторов (*)", "Shadow map resolution for point and spot lights (*)"),
    ("HL2RPM_Gfx_Desc_Clouds", "Разрешение и детализация объёмных 3D-облаков и их теней", "Resolution and detail of 3D volumetric clouds and cloud shadows"),
    ("HL2RPM_Gfx_Desc_SunShafts", "Объёмные лучи солнца, луны и прожекторов в воздухе и дымке", "Volumetric light beams from sun, moon and spotlights in haze"),
    ("HL2RPM_Gfx_Desc_Rain", "Плотность и количество падающих капель дождя и осадков", "Density and amount of falling raindrops and weather particles"),
    ("HL2RPM_Gfx_Desc_Wetness", "Намокание стен и земли под открытым небом, лужи после дождя", "Wet sheen on exposed surfaces and puddles during rain"),
    ("HL2RPM_Gfx_Desc_SSAO", "Затенение углов, стыков, щелей и углублений от света неба", "Ambient shading in corners, crevices and occluded geometry"),
    ("HL2RPM_Gfx_Desc_SoftShadows", "Реалистичное размытие краёв солнечных теней с удалением (PCSS)", "Realistic distance-based softening of sun shadow edges (PCSS)"),
    ("HL2RPM_Gfx_Desc_Bounce", "Вторичный отражённый свет от поверхностей, освещённых лампами", "Secondary bounced light from surfaces lit by nearby lamps"),
    ("HL2RPM_Gfx_Desc_WaterReflect", "Отражение мира на поверхности воды и преломление под водой", "Real-time water surface reflections and underwater refraction"),
    ("HL2RPM_Gfx_Desc_AA", "Быстрое сглаживание FXAA лесенок на краях геометрии и листвы", "Fast FXAA smoothing of jagged edges on geometry and foliage"),
    ("HL2RPM_Gfx_Desc_PBR", "Физически корректные блики и отражения металлов и материалов", "Physically based metallic and roughness specular highlights"),
    ("HL2RPM_Gfx_Desc_RenderScale", "Внутреннее разрешение 3D-сцены (ниже — выше FPS, HUD чёткий)", "Internal 3D scene resolution (lower boosts FPS, HUD stays crisp)"),
    ("HL2RPM_Gfx_Desc_Textures", "Разрешение текстур и загрузка HD-текстур (влияет на видеопамять)", "Texture resolution and HD texture loading (affects VRAM)"),
    ("HL2RPM_Gfx_Desc_Models", "Уровень детализации (LOD) геометрии персонажей и пропов", "Geometric level of detail (LOD) for characters and props"),
    ("HL2RPM_Gfx_Desc_Filtering", "Чёткость текстур пола и стен под острыми углами и вдали", "Sharpness of ground and wall textures at grazing angles"),
    ("HL2RPM_Gfx_Desc_VSync", "Синхронизация FPS с частотой монитора (убирает разрывы кадра)", "Locks frame rate to monitor refresh to eliminate screen tearing"),
    ("HL2RPM_Gfx_Desc_Multicore", "Распараллеливает отрисовку по ядрам CPU для повышения FPS", "Spreads rendering work across CPU threads to boost FPS"),
    # Options -> Effects page (indirect light, post-processing)
    ("HL2RPM_Options_Effects", "Эффекты", "Effects"),
    ("HL2RPM_Fx_Preset", "Качество эффектов", "Effects quality"),
    ("HL2RPM_Gfx_GI", "Глобальное освещение (GI)", "Global illumination (GI)"),
    ("HL2RPM_Gfx_Contact", "Контактные тени", "Contact shadows"),
    ("HL2RPM_Gfx_SSR", "Отражения в лужах (SSR)", "Puddle reflections (SSR)"),
    ("HL2RPM_Gfx_Post", "Постобработка кадра", "Frame post-processing"),
    ("HL2RPM_Gfx_Exposure", "Адаптация глаза к свету", "Eye adaptation"),
    ("HL2RPM_Gfx_Bloom", "Свечение ярких мест (bloom)", "Bloom"),
    ("HL2RPM_Gfx_Grade", "Цветокоррекция", "Color grading"),
    ("HL2RPM_Gfx_Lens", "Эффекты объектива", "Lens effects"),
    ("HL2RPM_Gfx_SoftParticles", "Мягкие частицы (дым, пар, пыль)", "Soft particles (smoke, steam, dust)"),
    ("HL2RPM_Gfx_Motes", "Пылинки в лучах солнца", "Dust motes in sun beams"),
    ("HL2RPM_Gfx_DoF", "Глубина резкости", "Depth of field"),
    ("HL2RPM_Gfx_DoF_Zoom", "При зуме и в диалогах", "When zoomed and in dialogs"),
    ("HL2RPM_Gfx_DoF_Always", "Всегда", "Always"),
    ("HL2RPM_Gfx_Sharpen", "Резкость изображения", "Image sharpening"),
    ("HL2RPM_Gfx_MotionBlur", "Размытие в движении", "Motion blur"),
    ("HL2RPM_Gfx_FPS", "Счётчик FPS", "FPS counter"),
    ("HL2RPM_Gfx_Desc_Contact", "Мелкие чёткие тени вблизи объектов, травы, мебели и камней", "Fine crisp contact shadows near objects, grass and props"),
    ("HL2RPM_Gfx_Desc_SSR", "Экранные отражения неба, зданий и ламп в лужах после дождя", "Screen-space reflections of sky, buildings and lamps in puddles"),
    ("HL2RPM_Gfx_Desc_SoftParticles", "Плавное растворение дыма, пара и пыли на стыке со стенами", "Smoothly fades smoke, steam and dust where they meet walls"),
    ("HL2RPM_Gfx_Desc_Motes", "Мелкие пылинки в воздухе, светящиеся в лучах солнца", "Floating air dust motes illuminated by sunlight beams"),
    ("HL2RPM_Gfx_Desc_Post", "Общий конвейер постобработки (выключение отключает 4 пункта ниже)", "Master post-processing toggle (off disables the 4 items below)"),
    ("HL2RPM_Gfx_Desc_Exposure", "Плавная подстройка яркости при переходе из темноты на свет", "Smooth eye brightness adaptation between dark rooms and daylight"),
    ("HL2RPM_Gfx_Desc_Bloom", "Мягкое свечение вокруг ламп, огня, ярких бликов и неба", "Soft glow halo around lamps, fire, bright highlights and sky"),
    ("HL2RPM_Gfx_Desc_Grade", "Кинематографичный цветовой баланс, контраст и насыщенность", "Filmic tone curve, contrast and color saturation balance"),
    ("HL2RPM_Gfx_Desc_Lens", "Лёгкая виньетка, зерно плёнки, хроматика и блики от солнца", "Subtle vignette, film grain, chromatic aberration and sun flare"),
    ("HL2RPM_Gfx_Desc_DoF", "Размытие фона за объектом в фокусе (в прицеле, диалогах или всегда)", "Blurs out-of-focus background (zoom, dialogs or always)"),
    ("HL2RPM_Gfx_Desc_Sharpen", "Контрастное повышение резкости и чёткости деталей кадра", "Contrast-adaptive sharpening of fine details in the image"),
    ("HL2RPM_Gfx_Desc_MotionBlur", "Смазывание изображения при быстром повороте камеры", "Subtle screen blur during fast camera turns"),
    ("HL2RPM_Gfx_Desc_FPS", "Показывает счётчик FPS и время кадра (мс) в правом верхнем углу", "Shows real-time FPS and frame time (ms) in the top-right corner"),
    ("HL2RPM_Fx_Hint", "Выключение постобработки отключает адаптацию глаза, свечение, цветокоррекцию и эффекты объектива.", "Disabling frame post-processing turns off eye adaptation, bloom, color grading and lens effects."),
    # Options -> Sound and interface page
    ("HL2RPM_Options_Sound", "Звук и интерфейс", "Sound and interface"),
    ("HL2RPM_Snd_FOV", "Поле зрения (FOV)", "Field of view (FOV)"),
    ("HL2RPM_Snd_Music", "Музыка мода (поверх «Музыки»)", "Mod music (on top of Music)"),
    ("HL2RPM_Snd_Subtitles", "Субтитры фраз NPC", "Subtitles of NPC barks"),
    ("HL2RPM_Snd_StockSubtitles", "Субтитры речи HL2 в том же стиле", "HL2 speech captions in the same style"),
    ("HL2RPM_Snd_Breath", "Дыхание раненого", "Breathing when hurt"),
    ("HL2RPM_Snd_Heart", "Сердцебиение", "Heartbeat"),
    ("HL2RPM_Snd_HurtScreen", "Красные края экрана при ранении", "Red screen edges when hurt"),
    ("HL2RPM_Snd_HurtSway", "Покачивание при ранении", "Sway when badly hurt"),
    ("HL2RPM_Snd_Prompt", "Подсказка под прицелом", "Prompt under the crosshair"),
    ("HL2RPM_Snd_Markers", "Метки целей заданий", "Quest objective markers"),
    ("HL2RPM_Snd_SuitBoot", "Загрузка костюма", "Suit boot screen"),
    ("HL2RPM_Snd_CharPause", "Пауза в окне персонажа", "Pause in the character window"),
    ("HL2RPM_Snd_NewWindows", "Новые окна (меню, лут, торговля)", "New windows (menu, loot, trade)"),
    ("HL2RPM_Snd_Weak", "Слабое", "Weak"),
    ("HL2RPM_Snd_Strong", "Сильное", "Strong"),
    ("HL2RPM_Snd_Hint", "Язык меняется сразу, стандартные меню HL2 - после перезапуска игры.", "The language changes at once, the stock HL2 menus after a restart of the game."),
    ("HL2RPM_Snd_Language", "Язык игры (Language)", "Language (Язык игры)"),
    ("HL2RPM_Lang_Auto", "Как в Steam", "As in Steam"),
    ("HL2RPM_Lang_Russian", "Русский", "Русский"),
    ("HL2RPM_Key_Title", "Half-Life 2: Role-Playing Mod", "Half-Life 2: Role-Playing Mod"),
    ("HL2RPM_Key_Holster", "Достать / убрать оружие", "Draw / holster the weapon"),
    ("HL2RPM_Key_ThirdPerson", "Вид от первого / третьего лица", "First / third person view"),
    ("HL2RPM_Key_Menu", "Меню персонажа", "Character menu"),
]

def process(path, russian):
    raw = open(path, "rb").read()
    bom = raw[:2] == b"\xff\xfe"
    text = (raw[2:] if bom else raw).decode("utf-16-le")
    nl = "\r\n" if "\r\n" in text else "\n"
    shutil.copy2(path, path + ".bak_" + datetime.datetime.now().strftime("%Y%m%d_%H%M%S"))
    # drop existing lines of our tokens
    names = {t[0] for t in TOKENS}
    lines = text.split(nl)
    kept = []
    for ln in lines:
        m = re.match(r'\s*"(\[english\])?(\w+)"', ln)
        if m and m.group(2) in names:
            continue
        kept.append(ln)
    text = nl.join(kept)
    block = []
    for name, ru, en in TOKENS:
        if russian:
            block.append('\t\t"%s"\t"%s"' % (name, ru))
            block.append('\t\t"[english]%s"\t"%s"' % (name, en))
        else:
            block.append('\t\t"%s"\t"%s"' % (name, en))
    # insert before the closing brace of "Tokens" (the second to last '}')
    idx_last = text.rfind("}")
    idx_tokens_end = text.rfind("}", 0, idx_last)
    text = text[:idx_tokens_end].rstrip() + nl + nl.join(block) + nl + "\t" + text[idx_tokens_end:]
    # Source's localization loader needs the UTF-16LE byte order mark
    open(path, "wb").write(b"\xff\xfe" + text.encode("utf-16-le"))
    print("updated", path, len(TOKENS), "tokens")

process(MOD + r"\hl2rpm_russian.txt", True)
process(MOD + r"\hl2rpm_english.txt", False)
