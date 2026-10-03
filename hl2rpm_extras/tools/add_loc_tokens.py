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
    ("HL2RPM_Gfx_RestartNote", "* Тени от ламп обновятся после перезапуска игры.", "* Lamp shadows update after restarting the game."),
    ("HL2RPM_Gfx_Hint", "Разрешение, сглаживание и текстуры - на вкладке «Видео».", "Resolution, anti-aliasing and textures are on the Video tab."),
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
