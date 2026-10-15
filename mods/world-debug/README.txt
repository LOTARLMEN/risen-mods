Risen World Debug 0.1.1

РУССКИЙ
Инструменты разработчика для Steam Risen x64 Rev.2045. Нужен загрузчик Risen Mods;
для настроек F10 и индикатора нужен menu.dll. Установка: world_debug.dll в bin\mods.
Отладка по умолчанию выключена: F10 → Отладка мира → Включить отладку мира.

NUMPAD7 — выбрать лежащий под прицелом предмет с физическим телом.
NUMPAD5 — заморозить выбранный предмет / включить его физику и отпустить.
NUMPAD9 — поднять выбранный предмет на 100 см и заморозить. NUMPAD5 запускает падение.
NUMPAD0 — штатная свободная камера / возврат к герою. Управление камеры — игровое.
Клавиши, высота подъёма и скорость камеры меняются в F10 или world_debug.ini [Debug].
Скорость применяется сразу: 0.1–1000 см/с, по умолчанию 30 см/с.
Для точного ввода скорости: Ctrl+клик по числу в F10. Ускорение — штатной клавишей игры.
Для цифровых клавиш включите Num Lock.

Выключите AutoLoot перед выбором. Выбор сохраняется при отводе камеры, но предмет
должен оставаться в пределах 20 м от героя. Инвентарь, экипировка, NPC и статические
объекты без rigid body не изменяются. Индикатор DEBUG показывает выбранное имя
и frozen/physics. При отключении инструментов камера возвращается к герою.
Положение и физика предметов могут сохраниться в обычном сохранении игры.
Выбор, подъём, физика и freecam проверены пользователем; настройка скорости исправлена в 0.1.1.
Удаление: закройте игру и удалите world_debug.dll и world_debug.ini.

ENGLISH
Developer tools for Steam Risen x64 Rev.2045. Requires the Risen Mods loader;
menu.dll provides the F10 settings and status. Copy world_debug.dll into bin\mods.
Disabled by default: F10 → World debug → Enable world debugging.

NUMPAD7 selects a focused loose rigid-body item. NUMPAD5 freezes/releases it.
NUMPAD9 raises the selection by 100 cm and freezes it; NUMPAD5 starts the drop.
NUMPAD0 toggles the native free camera. Movement uses the game's camera controls.
Enable Num Lock. Keys, lift height and camera speed are configurable in F10 or
world_debug.ini [Debug]. Turn AutoLoot off before selecting an item.
Speed applies immediately: 0.1–1000 cm/s, default 30 cm/s. Ctrl+click the F10 number
to enter an exact speed. The game's native acceleration key still applies.

Selection survives camera movement, using a fresh GUID lookup for every command.
Items must remain within 20 m of the hero. Inventory/equipped items, NPCs and
static objects without a rigid body are refused. The DEBUG indicator shows the
selection and frozen/physics state. Disabling the tools returns the camera.
Normal game saves can retain object position and physics changes.
Selection, lift, physics and freecam were user-tested; speed adjustment was fixed in 0.1.1.
Uninstall with the game closed: remove world_debug.dll and world_debug.ini.
