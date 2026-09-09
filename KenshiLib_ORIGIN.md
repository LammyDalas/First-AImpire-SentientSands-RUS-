> Заголовки лежат рядом, в файле `KenshiLib.zip` (854 файла, 2 МБ) —
> распаковать его нужно так, чтобы получилась папка `KenshiLib`.
> Этот файл и текст лицензии продублированы здесь, чтобы их было
> видно без скачивания.
>
> The headers ship as `KenshiLib.zip` next to this file. This notice
> and the licence are duplicated here so they can be read without
> downloading the archive.

# KenshiLib — origin of this copy / происхождение копии

## English (for reviewers)

This folder contains the **headers only** of
[KenshiLib](https://github.com/BFrizzleFoShizzle/KenshiLib) by
BFrizzleFoShizzle, the library used by RE_Kenshi. KenshiLib is released under
the **GNU GPL v3**, the same licence as this mod; its licence text is in
`KenshiLib_LICENSE.txt` next to this file. No source files or binaries of KenshiLib are
redistributed here — only the headers needed to compile
`SentientSands.dll`.

The copy is included so that the DLL can be rebuilt exactly as distributed.
It is **not identical to upstream master**: 26 of 852 headers differ.

- **24 files differ because this is an older snapshot** of KenshiLib. Upstream
  has since moved declarations between headers (for example `taskPriority`
  moved from `Tasker.h` to `AI/AITaskSystem.h`) and added functions
  (`QueueHook`, `ApplyQueuedHooks` in `core/Functions.h`). Replacing this copy
  with current upstream headers breaks the build.
- **2 files carry a local fix**, marked below. `Building/Building.h` and
  `Platoon.h` both declare `enum BuildingDesignation`; when both headers reach
  the same translation unit the compiler reports a redefinition, so an include
  guard `KENSHILIB_BUILDINGDESIGNATION_DEFINED` was added around the second
  declaration. Nothing else was changed.

As required by GPL v3 section 5(a), the modified files are listed below.

## По-русски

Здесь лежат **только заголовки** библиотеки KenshiLib — те описания
внутренностей игры, по которым собирается `SentientSands.dll`. Библиотеку
написал BFrizzleFoShizzle, она под той же лицензией GPL v3, что и мод; текст
лицензии — в файле `KenshiLib_LICENSE.txt` рядом.

Копия лежит здесь по одной причине: **без неё DLL не собрать**. И это не
свежая версия с гитхаба, а именно та, на которой мод собирается сегодня.

Из 852 заголовков 26 отличаются от нынешних официальных:

- **24 — просто разница версий.** С тех пор автор библиотеки переносил
  объявления между файлами и добавлял новые функции.
- **2 — наша правка**, отмечена в таблице. В `Building/Building.h` и
  `Platoon.h` один и тот же перечень объявлен дважды, и компилятор ругается на
  повторное объявление; добавлен сторож, пропускающий только первое.

**Важно для сборки:** не заменяй эту папку свежей с гитхаба — сборка
сломается.

## Список отличий / list of differing files

| Файл | Строк +/− | Причина |
|---|---|---|
| `core/Functions.h` | +0 / −15 | различие версий |
| `kenshi/AI/AI.h` | +5 / −4 | различие версий |
| `kenshi/AI/AITaskSystem.h` | +11 / −30 | различие версий |
| `kenshi/Building/Building.h` | +22 / −0 | локальная правка (сторож повторного объявления) |
| `kenshi/Building/CraftingBuilding.h` | +6 / −26 | различие версий |
| `kenshi/Building/FarmBuilding.h` | +0 / −3 | различие версий |
| `kenshi/Building/UseableStuff.h` | +0 / −2 | различие версий |
| `kenshi/CharMovement.h` | +1 / −24 | различие версий |
| `kenshi/Character.h` | +5 / −0 | различие версий |
| `kenshi/Enums.h` | +0 / −31 | различие версий |
| `kenshi/GunClass.h` | +0 / −1 | различие версий |
| `kenshi/Havok.h` | +2 / −1 | различие версий |
| `kenshi/NavMesh.h` | +4 / −4 | различие версий |
| `kenshi/Platoon.h` | +23 / −0 | локальная правка (сторож повторного объявления) |
| `kenshi/PlayerInterface.h` | +2 / −1 | различие версий |
| `kenshi/Renderer.h` | +0 / −1 | различие версий |
| `kenshi/Tasker.h` | +18 / −5 | различие версий |
| `kenshi/Weather.h` | +0 / −5 | различие версий |
| `kenshi/ZoneManager.h` | +2 / −1 | различие версий |
| `kenshi/combat/CombatClass.h` | +4 / −4 | различие версий |
| `kenshi/gui/DataPanelLine.h` | +0 / −2 | различие версий |
| `kenshi/gui/DatapanelGUI.h` | +4 / −3 | различие версий |
| `kenshi/gui/MainBarGUI.h` | +0 / −1 | различие версий |
| `kenshi/gui/ScreenLabel.h` | +0 / −1 | различие версий |
| `kenshi/util/PerfTimer.h` | +3 / −3 | различие версий |
| `mygui/common/baselayout/BaseLayout.h` | +1 / −1 | различие версий |
