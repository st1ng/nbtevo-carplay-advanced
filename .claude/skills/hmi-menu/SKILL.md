---
name: hmi-menu
description: Add, change, move or remove rows of the "CarPlay Advanced Settings" iDrive menu (checkboxes, submenus, radio groups, action buttons, labels). Use whenever plugin/CarPlayMenu.xml, src/texts.py or the menu handlers in src/cpm.cpp (g_options, kSens, appOnInit, toggle, applyAtStart) change.
---

# The CarPlay Advanced Settings menu

The menu has three halves that must agree, and nothing at build time checks them for you. A mismatch shows up in the car as a dead
row, a checkbox that never ticks, or an empty label:

| Half | File | Holds |
|---|---|---|
| RHMI description | `plugin/CarPlayMenu.xml` (committed as is, not generated) | actions, models, hmiStates (pages) and their components |
| Texts | `src/texts.py` -> `plugin/CarPlayMenu_ALL_Texts.zip` (by `build.sh`) | `textId -> English`, the same text in every HMI language |
| Behaviour | `src/cpm.cpp` | handlers (`addHandler(actionId)`), models (`updateModel(modelId, bool)`), switch files |

Always finish with:

```bash
python3 .claude/skills/hmi-menu/check_menu.py     # prints the tree, the ids in use and the first free one; exit 1 on a mismatch
./build.sh && python3 -m unittest discover -s tests
```

## How the XML works

- `pluginApp applicationType="Custom" insertStateID="20" insertAfterFocusID="8195"`: the entry row goes into the Media/Radio list
  (`ST_Ent_Selection`, state 20) right after "Now playing in Apple CarPlay" (focus 8195). `entryButton` (id 1, model 10) has the
  `combinedAction` 2 = `raAction` 3 (no handler needed) + `hmiAction` 4 -> the plugin's first page, hmiState 20.
- **Every id is unique across the whole app**: actions, models, hmiStates and components (and the entry button) share one
  number space. `check_menu.py` prints the ids in use and the first free one. Never reuse or renumber an existing id: the ids of
  options are also in `cpm.cpp`.
- Actions: `raAction` (a callback to the plugin: `addHandler`), `hmiAction target="<state>"` (opens a page; no code),
  `combinedAction`.
- Models: `textIdModel textId="<n>"` (a label from `texts.py`), `raBoolModel` (a tick the plugin sets with `updateModel`).
- Components, inside `<hmiState><components>`, in display order:

| Component | `action` | `model` | `textModel` | Use |
|---|---|---|---|---|
| `checkbox` | raAction | raBoolModel | textIdModel (the label) | an on/off option |
| `radiobutton` | raAction | raBoolModel | textIdModel | one of a group; the plugin ticks one and clears the others itself |
| `button` | hmiAction (submenu) or raAction (command) | textIdModel (the label) | - | a submenu or a command like "Restart to apply" |

  Keep `text=""` on components (the label comes from the model). An `hmiState` has `text` and `textModel` for its title.
- BACK returns to the parent page by itself; there is no back wiring.

## How the plugin side works (`src/cpm.cpp`)

- `struct Option { file, action, model, label, on }` and `enum { OPT_FS, ..., OPT_N }` + `Option g_options[OPT_N]`: one row per
  checkbox. `file` is the switch file in `/var/opt/hmi/data/rw/carplaymenu` (present = on; it survives updates and restarts),
  `label` is only for the log.
- `appOnInit` (the replaced vtable slot 22) registers `addHandler(g_options[i].action, stubs[i])` for every option, `kSens[k].action`
  for the radio buttons and `kActRestart` (9), calls the base `onInit`, then `updateModel` for every option model and the radio
  models. **`stubs[]` is in `OPT_` enum order**: a new option needs its stub at the same index.
- Each stub is `bool onX(void *, const bmw::Parcel &) { return toggle(OPT_X); }`. `toggle(i)` flips the switch file and the model
  (`setOpt`), returns at once in safe mode (it only saves), and otherwise applies what can apply at once and logs `<label> -> ON|OFF`,
  adding `(applies after restart)` when it cannot.
- `applyAtStart()` (when the HMI loads the plugin) applies everything that needs a start: HMI patches, hooks, process wrappers,
  links, `carplay.ini` copies. It is skipped in safe mode.
- Radio groups: `struct Level { action, model, value, label }` + `const Level kSens[3]`; `setSens(k)` writes the value file
  (`touchpad_mm`) and calls `updateModel` on all of the group's models (the HMI does not untick the others for a plugin).
  "HUD effects > Effect colour" is the same with `struct Colour { action, model, index, label, pad }` + `kColours[3]` and
  `setColour(k)` (`hud_colour`).
- Options that need something at boot (a path-manager link before the HMI or Connectivity starts) also need `plugin/boot.sh` to
  read their switch file.
- `loadOptions()` reads the files at start; `menu_v2` shows how an older menu's switches are converted once.

## Recipes

Pick new ids from `check_menu.py`'s "free from N up"; a new text id is the next free key in `texts.py`'s `TEXTS`.

**A new checkbox option**
1. XML: `<raAction id="A"/>` in `<actions>`; `<raBoolModel id="M"/>` and `<textIdModel id="L" textId="T"><synonyms/></textIdModel>`
   in `<models>`; `<checkbox action="A" id="C" model="M" text="" textModel="L"/>` in the page's `<components>` at its place.
2. `texts.py`: `T: 'Label'` (keep the rows grouped in menu order, as the comments there do).
3. `cpm.cpp`: `OPT_NEW` before `OPT_N`; `{ "switch_file", A, M, "Label", false }` at the same index in `g_options`; a stub
   `onNew` and the same index in `appOnInit`'s `stubs[]`; what it does in `applyAtStart()` and in `toggle()` (at once or after
   restart). If it needs a link at boot, the switch in `plugin/boot.sh`.
4. Document the row (AGENTS.md: menu table and "Files on the head unit").

**A new submenu (page)**
1. XML: a new `<hmiState id="S" text="Title" textModel="L"><components>...</components></hmiState>`; in the parent page a
   `<button action="H" id="C" model="L" text=""/>` with `<hmiAction id="H" target="S"/>` in `<actions>`; the title's
   `textIdModel` L.
2. `texts.py`: the title. No code: hmiActions need no handler.

**A radio group** (as Touchpad sensitivity)
1. XML: a page with one `radiobutton` per value (each its own raAction, raBoolModel, textIdModel).
2. `cpm.cpp`: a `Level` table, one stub per value calling a `setX(k)` like `setSens`, register them in `appOnInit`, and tick the
   current one there (`sensLevel` shows a hand-written value as the nearest level).

**A command button** (as Restart to apply): XML `button` with an `raAction` and a `textIdModel`; `cpm.cpp` a constant like
`kActRestart`, a stub, `addHandler` in `appOnInit`. `check_menu.py` expects command actions as `kAct...` constants or the tables;
extend its regexes if you add another kind.

**Rename a label**: only `texts.py` (and the `label` in `g_options` for the log). **Move a row**: move its component element (and
the parent's button for a page); ids stay. **Remove an option**: remove the component, its action and models, the text, the
`OPT_` entry, row, stub and code; keep its switch file name out of reuse (an old file may still be on a car).

## Rules and pitfalls

- Never renumber existing ids or rename existing switch files: cars that update keep their files, and the ids are in two places.
- Handlers run on the HMI's thread: no blocking work in a stub (network, MOST, long file I/O go to threads, as `hud.h` does).
- Behaviour that patches HMI code belongs in `applyAtStart()` behind `g_build` (unknown build = menu only), and must check every
  word (`patchWords`).
- The HMI reads the XML and texts when it starts: menu changes need a restart. On iDrive 6 the HMI instantiates the plugin only
  with A4A / A4A_REMOTE_APPS / REMOTE_HMI / TOUCH_COMMAND coded.
- Texts: plain ASCII is safest; every language gets the English text (`LANGS` in `texts.py`).
- The emulator project (MINIHU) has its own copy of the menu (`src/carplaymenu`, `idemu/plugins/CarPlayMenu`); the copies are not
  synced automatically.

## Checking in the car

Install (`sh carplay-advanced.sh install`), restart, open Media/Radio -> CarPlay Advanced Settings, and walk every page. The plugin
log `/dev/shmem/carplaymenu.log` shows `CarPlayMenu: onInit, handlers registered` at start and `CarPlayMenu: <label> -> ON` for
every tick; a row that logs nothing has an action without a handler (`check_menu.py` would have said so).
