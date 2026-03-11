**Обновленный план: Исправление артефактов теней light_deferred (Версия 2)**

Этот план учитывает ваши замечания: мы **не будем** переключать метод на `COLOR` (остаемся на аппаратных картах глубины `DEPTH_STENCIL`), но внедрим предложенный вами метод `fwidth` и дополнительные современные техники (как в AAA играх) для устранения швов, акне и черного круга.

### **Анализ багов**
1.  **Черный круг (Скрин 3):** Это происходит из-за того, что текущий bias на стороне приемника вычитает фиксированное значение из глубины. Вблизи источника глубина стремится к 0 (near plane), и вычитание делает её отрицательной или меньше, чем в карте теней. Зажим (`clamp`) этой глубины приводит к ложному срабатыванию тени.
2.  **Швы между гранями (Скрин 1):** Кубическая карта в Mapbase реализована через атлас. На стыке граней расчеты смещения (bias) и углов падения света меняются скачкообразно, что создает видимую линию.
3.  **Акне на расстоянии (Скрин 2):** Фиксированный bias не учитывает, что один тексель карты теней на расстоянии покрывает большую площадь мира. Это требует адаптивного смещения.

### **Предлагаемые изменения**

#### **1. Внедрение fwidth в аппаратную запись глубины**
Чтобы использовать `fwidth` без перехода на `COLOR`, мы модифицируем пиксельный шейдер записи теней, чтобы он переопределял глубину (`oDepth`).
*   **Файлы:** [shadowpass_vs30.fxc](file:///E:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/materialsystem/stdshaders/shadowpass_vs30.fxc), [shadowpass_ps30.fxc](file:///E:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/materialsystem/stdshaders/shadowpass_ps30.fxc).
*   **Что делаем:** Передаем глубину из VS в PS и в PS пишем `oDepth = depth + fwidth(depth) * multiplier`. Это создаст «идеальный» bias прямо в аппаратной карте глубины.

#### **2. Добавление Normal Offset Bias (как в CSM)**
Это «золотой стандарт» AAA игр. Мы смещаем точку выборки тени вдоль нормали поверхности.
*   **Файл:** [common_shadowmapping_fxc.h](file:///E:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/materialsystem/stdshaders/common_shadowmapping_fxc.h), функция `PerformDualParaboloidShadow`.
*   **Что делаем:** Добавляем смещение `worldPos += normal * bias`. Это убирает акне на пологих углах (стены), не отодвигая тень от объекта (решает питер-паннинг).

#### **3. Добавление Receiver Plane Depth Bias (RPDB) для Point Light**
Эта техника уже есть в Mapbase для Spot Light, но отсутствует для Point Light. Она вычисляет уклон поверхности относительно камеры тени.
*   **Файлы:** [lightingpass_point_ps30.fxc](file:///E:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/materialsystem/stdshaders/lightingpass_point_ps30.fxc), [common_shadowmapping_fxc.h](file:///E:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/materialsystem/stdshaders/common_shadowmapping_fxc.h).
*   **Что делаем:** Портируем логику `SpotReceiverPlaneDepthBias` для точечных источников. Это уберет акне на больших расстояниях.

#### **4. Исправление черного круга и швов**
*   **Логика:** Мы уберем агрессивный фиксированный вычет глубины в `PerformDualParaboloidShadow`. Вместо него будем полагаться на `fwidth` (из шага 1) и `Normal Offset`.
*   Это гарантирует, что вблизи источника (где `fwidth` минимален) мы не будем «проваливаться» под карту теней.

### **Проверка**
1.  Компиляция: `shadowpass_ps30.fxc`, `lightingpass_point_ps30.fxc`, `lightingpass_spot_ps30.fxc`.
2.  Результат: исчезновение черного пятна под лампой, отсутствие «зебры» на стенах вдали и бесшовные стыки граней куба.

### **Отчёт**
После завершения я предоставлю список изменений со ссылками на код и объяснением, как каждая AAA техника (Normal Offset, RPDB, fwidth-bias) помогла решить конкретный баг.
