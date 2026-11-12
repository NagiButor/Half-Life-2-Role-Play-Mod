"""One-off (2026-11-04): split the grown CLAUDE.md into topic files in sp/src/claude_docs/ - the line ranges below are
of CLAUDE.md as it was that day (345 lines). Checks that every non-empty line that leaves CLAUDE.md lands in exactly
one topic file. CLAUDE.md itself is rewritten by hand as the index."""
import os
import sys

sys.stdout.reconfigure(encoding="utf-8")
SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
raise SystemExit("Archived one-off migration from 2026-11-04. Canonical documentation is now in AI_instructions; do not split the CLAUDE.md entry point.")
lines = open(os.path.join(SRC, "CLAUDE.md"), encoding="utf-8").read().split("\n")
assert len(lines) in (345, 346), len(lines)

R = lambda a, b: list(range(a, b + 1))  # noqa: E731  (1-based, inclusive)
DOCS = [
    ("ck-editor.md", "HL2RPM Creation Kit: редактор, окно карты, превью, демо ck_test",
     R(22, 30) + [36, 43, 44]),
    ("world-ai.md", "Мир в игре: слой карты, распорядок NPC, навигация открытых карт", R(31, 35) + [49]),
    ("rpg-systems.md", "РПГ-системы: предметы, лут, замки, торговля, логика и задания, персонаж, терминалы, общие темы, кражи",
     [37, 38, 40, 42, 45]),
    ("ui-hud.md", "Интерфейс и HUD: окна M6, карточка предмета, оружие как в New Vegas, HUD костюмов, пульс без костюма",
     [41, 46, 48] + R(336, 345)),
    ("audio.md", "Звук: музыка, фразы NPC, субтитры, локации, звуковые сцены (озвучка - voice.md)", [39]),
    ("content.md", "Контент: главное меню, Steam, аддоны GMod, модели (Blender, пересборка), костюмы, руки, оружие, интро",
     [47] + R(51, 65)),
    ("animation.md", "Поведение NPC, библиотека анимаций, Kimodo / MotionBricks, вид от третьего лица", R(67, 80)),
    ("debugging.md", "Отладка крашей и порчи памяти", R(101, 110)),
    ("leaked-source.md", "Слитые исходники движка (только справочник)", R(112, 120)),
    ("mapping.md", "Маппинг: FGD / Hammer, компиляторы hl2rpm_vbsp / vvis / vrad", R(122, 141)),
    ("build.md", "Сборка C++ и шейдеров (ShaderCompile235)", R(143, 181)),
    ("renderer-core.md", "Deferred-рендер, часть 1: архитектура, тени, сглаживание, лампы, константы, вода, стекло, туман",
     R(183, 221)),
    ("renderer-gi-post.md", "Deferred-рендер, часть 2: GI-пробы, пост-обработка, SSR, частицы, PBR, вода, отражения",
     R(222, 252)),
    ("renderer-passes.md", "Deferred-рендер, часть 3: GI v3, POM, ветер, листва, occlusion culling, решения (DXVK, x64), "
     "финальные проходы графики", R(253, 313)),
    ("weather.md", "Погода, небо, время суток, фото-тур, настройки графики", R(315, 334)),
]
KEPT = R(1, 21) + R(82, 99)

used = {}
out_dir = os.path.join(SRC, "claude_docs")
os.makedirs(out_dir, exist_ok=True)
for fn, title, rng in DOCS:
    body = []
    for n in rng:
        if n in used:
            raise SystemExit("line %d twice: %s and %s" % (n, used[n], fn))
        used[n] = fn
        body.append(lines[n - 1])
    text = "# %s\n\n> Перенесено из CLAUDE.md 2026-11-04 (CLAUDE.md теперь только оглавление). Новые знания по этой теме - сюда.\n\n" % title
    text += "\n".join(body).strip("\n") + "\n"
    with open(os.path.join(out_dir, fn), "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("%-22s %4d lines %7d bytes" % (fn, len(rng), len(text.encode("utf-8"))))
lost = [n for n in range(1, len(lines) + 1) if lines[n - 1].strip() and n not in used and n not in KEPT]
print("lines not placed:", lost)
