# First AImpire (SentientSands RUS+)

Source code for the C++ plugin (`SentientSands.dll`) of the Kenshi mod
**First AImpire (SentientSands RUS+)** — a mod that lets NPCs hold real
conversations through a language model.

Licensed under the **GNU General Public License v3.0** (see the header of
`src/main.cpp`). This repository exists so that anyone who receives the
compiled DLL can obtain the corresponding source, as the licence requires.

---

## English summary (for reviewers)

The distributed mod archive contains **one binary built by the mod author**:
`SentientSands.dll`. Its complete source is in the `src/` folder of this
repository, and the exact KenshiLib headers it is compiled against are in
`KenshiLib.zip` (third-party, GPL v3, see `KenshiLib_ORIGIN.md`).

Everything else that looks like an executable in the archive is the **official
unmodified CPython 3.13 runtime for Windows**, bundled under
`server/python/` so that users do not have to install Python themselves.
That folder is upstream CPython plus packages installed with `pip`
(Flask, requests, pygame); none of it was written or modified by the mod
author. It is the source of the `.exe`, `.dll` and `.pyd` files, and of the
nested archives `python313.zip` (the CPython standard library, shipped this
way by python.org) and `pygame/docs/.../legacy_logos.zip`.

### How the mod works

Kenshi loads `SentientSands.dll` through RE_Kenshi. The DLL reads game state,
sends it over `http://127.0.0.1` to a local Flask server written in Python,
and applies the returned actions back to the game. All network traffic is
local, except the request the Python server makes to whichever language-model
API the user has configured with their own key.

### Build instructions

- Visual Studio, platform toolset **Windows 7.1 SDK**
- Configuration **Release**, platform **x64**
- Character set: **Multi-Byte**
- Language standard: C++ (VS2010-era subset; no range-based `for`)
- Source files: UTF-8 **without BOM**, CRLF line endings

The project links against **KenshiLib** by BFrizzleFoShizzle, the library
behind [RE_Kenshi](https://github.com/BFrizzleFoShizzle/RE_Kenshi). The exact
headers this DLL is built against ship here as **`KenshiLib.zip`** (854 files),
because they are an older snapshot and differ from current upstream master in
26 of 852 files — see `KenshiLib_ORIGIN.md` for the list and the reason, and
`KenshiLib_LICENSE.txt` for their licence. Building against current upstream
headers will fail.

Unpack `KenshiLib.zip` so that a `KenshiLib` folder appears. The include paths
at the top of `main.cpp` expect the headers at
`../RE_Kenshi_Source/KenshiLib/Include/`, so place the folder accordingly, or
adjust the include directories in your project settings. Build with **Rebuild
Solution**; the output `SentientSands.dll` goes into the mod folder next to
`SentientSands.mod`.

No Visual Studio project file is included in this repository; the sources are
compiled as a single DLL project with the settings listed above.

---

## По-русски

Здесь лежит исходный код плагина `SentientSands.dll` — той части мода,
которая написана на C++ и связывает игру Kenshi с языковой моделью.

Всё, что относится к самому моду (тексты, досье персонажей, скрипты на
Python), распространяется отдельно, в архиве мода.

### Из чего собирается DLL

Папка `src/` — 40 файлов. Рабочих, которые правятся чаще всего, восемь:

| Файл | За что отвечает |
|---|---|
| `main.cpp` | точка входа, перехваты движка, разбор действий модели |
| `GameActions.cpp` | исполнение действий в игре: выдача, деньги, задачи, сдача |
| `Context.cpp` | сбор сведений об окружении и отправка их серверу |
| `Utils.cpp` / `Utils.h` | лог, работа со строками, вспомогательное |
| `ChatWindow.cpp` / `ChatWindow.h` | окно разговора |
| `Globals.h` | общие переменные и перечень типов действий |

Остальные файлы — окна интерфейса (настройки, библиотека, события, кампании,
профиль, история) и связь с сервером.

### Как собрать

1. Visual Studio, набор инструментов **Windows 7.1 SDK**.
2. Конфигурация **Release**, платформа **x64**, набор символов
   **Multi-Byte**.
3. Рядом должны лежать заголовки **KenshiLib** по пути
   `../RE_Kenshi_Source/KenshiLib/Include/`. Они лежат в этом же репозитории,
   в архиве `KenshiLib.zip` — распакуй его и бери оттуда, а **не** свежие с
   гитхаба: наша копия старее, и на новой сборка ломается. Подробности — в
   `KenshiLib_ORIGIN.md`.
4. **Rebuild Solution**. Готовую `SentientSands.dll` положить в папку мода
   рядом с `SentientSands.mod`.

Файлы хранятся в UTF-8 без BOM с переносами CRLF — так их ждёт сборка.

### Лицензия

GNU GPL v3. Полный текст лицензии — в файле `LICENSE`.

Архив `KenshiLib.zip` — чужая работа: заголовки библиотеки KenshiLib
авторства BFrizzleFoShizzle, тоже под GPL v3. Её лицензия лежит отдельно, в
`KenshiLib_LICENSE.txt`, а происхождение копии и внесённые в неё правки
описаны в `KenshiLib_ORIGIN.md`.
