Risen World Debug 0.2.2

РУССКИЙ
Инструменты разработчика для Steam Risen x64 Rev.2045. Нужен загрузчик Risen Mods;
для настроек F10 и индикатора нужен menu.dll. Установка: world_debug.dll в bin\mods.
Отладка по умолчанию выключена: F10 → Отладка мира → Включить отладку мира.

NUMPAD7 — выбрать лежащий под прицелом предмет с физическим телом.
NUMPAD5 — заморозить выбранный предмет / включить его физику и отпустить.
NUMPAD9 — поднять выбранный предмет на 100 см и заморозить. NUMPAD5 запускает падение.
NUMPAD0 — штатная свободная камера / возврат к герою. Управление камеры — игровое.
- (_) — уменьшить скорость перемещения вдвое; = (+) — увеличить вдвое.
Это клавиши верхнего ряда возле Backspace; Shift не обязателен. Можно удерживать.
Клавиши скорости работают только в свободной камере, переназначаются в F10.
Клавиши, высота подъёма и скорость камеры меняются в F10 или world_debug.ini [Debug].
Скорость движения применяется сразу: 0.1–10000 см/с, по умолчанию 300 см/с (3 м/с).
Текущая скорость видна рядом с DEBUG CAM и сохраняется в INI.
Чувствительность вращения соответствует обычной камере героя и не зависит от скорости движения.
Для точного ввода скорости: Ctrl+клик по числу в F10. При обновлении существующий INI сохраняется.
Для цифровых клавиш включите Num Lock.

Время и погода (нужна включённая отладка и загруженная игра, вне диалога):
NUMPAD1 — установить выбранный в F10 час, по умолчанию 12:00.
NUMPAD2 — следующее время суток: 06:00 / 12:00 / 18:00 / 00:00.
NUMPAD6 — следующий день с сохранением текущего часа.
NUMPAD3 — ясная погода Sunny_Full / возврат предыдущего режима погоды.
Для осмотра моделей нажмите NUMPAD1 и NUMPAD3. Клавиши переназначаются в F10.
Час в настройке десятичный: 12.5 означает 12:30. Время продолжает идти обычным образом.
Изменение времени/даты влияет на распорядок NPC и запоминается сохранением игры.
Ясная погода не включается автоматически; отключение отладки/выход из мира возвращает погоду.

Выключите AutoLoot перед выбором. Выбор сохраняется при отводе камеры, но предмет
должен оставаться в пределах 20 м от героя. Инвентарь, экипировка, NPC и статические
объекты без rigid body не изменяются. Индикатор DEBUG показывает выбранное имя
и frozen/physics. При отключении инструментов камера возвращается к герою.
Положение и физика предметов могут сохраниться в обычном сохранении игры.
Удаление: закройте игру и удалите world_debug.dll и world_debug.ini.

ENGLISH
Developer tools for Steam Risen x64 Rev.2045. Requires the Risen Mods loader;
menu.dll provides the F10 settings and status. Copy world_debug.dll into bin\mods.
Disabled by default: F10 → World debug → Enable world debugging.

NUMPAD7 selects a focused loose rigid-body item. NUMPAD5 freezes/releases it.
NUMPAD9 raises the selection by 100 cm and freezes it; NUMPAD5 starts the drop.
NUMPAD0 toggles the native free camera. Movement uses the game's camera controls.
- (_) halves movement speed; = (+) doubles it. Use the top-row keys near Backspace;
Shift is optional. Hold to repeat. Speed keys work only in freecam and can be rebound in F10.
Enable Num Lock. Keys, lift height and camera speed are configurable in F10 or
world_debug.ini [Debug]. Turn AutoLoot off before selecting an item.
Movement speed applies immediately: 0.1–10000 cm/s, default 300 cm/s (3 m/s).
DEBUG CAM shows the current speed, saved in the INI. Mouse look matches the normal
player camera sensitivity and is independent of movement speed. Ctrl+click the F10
number for exact entry. Updates preserve the existing INI.

Time/weather (debug enabled, loaded world, outside dialogs): NUMPAD1 applies the
F10 hour (default 12:00); NUMPAD2 cycles 06:00 / 12:00 / 18:00 / 00:00; NUMPAD6
advances one day while keeping the hour; NUMPAD3 toggles Sunny_Full clear weather
and restores the previous native mode/weights. For inspection, press NUMPAD1 + NUMPAD3.
All keys can be rebound. Hour is decimal (12.5 = 12:30); the clock keeps running normally.
Time/date changes affect NPC schedules and persist in saves. Clear weather is never
enabled automatically; disabling debugging/leaving the world restores weather.

Selection survives camera movement, using a fresh GUID lookup for every command.
Items must remain within 20 m of the hero. Inventory/equipped items, NPCs and
static objects without a rigid body are refused. The DEBUG indicator shows the
selection and frozen/physics state. Disabling the tools returns the camera.
Normal game saves can retain object position and physics changes.
Uninstall with the game closed: remove world_debug.dll and world_debug.ini.
