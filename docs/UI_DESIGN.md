# NEON COAST / 霓虹海岸 — native interface reference

## Implementation status and contributions

The native Godot 4.7.2 interface is implemented and integrated with the actual 3D game. `scripts/hud.gd` is a complete `CanvasLayer`, not a contract scaffold. It renders the title screen, gameplay HUD, pause and settings screens, full map, photo instructions, controls guide, notifications, and mission completion banners.

Claude Code, using Claude Opus 5.5 at high effort, generated `scripts/ui_map.gd`. That component supplies both the minimap and full map: road/building drawing, north-up transforms, routes, markers, clipping, cursor movement, and waypoint input. Codex implemented `scripts/hud.gd`, integrated the map into the game, and implemented input/audio support and automated interaction tests. The root integration work also refined typography, background gradients, progress bars, gamepad focus bindings, and native rendering. This does not attribute the complete HUD or game to Claude. The toolchain history and its limits are documented in [TOOLCHAIN.md](TOOLCHAIN.md).

The interface belongs to the original **Neon Coast** prototype. It does not use Rockstar branding, official GTA assets, or unreleased game material.

## Implemented screens

| Mode | Implemented behavior |
| --- | --- |
| `title` | Live 3D city backdrop; large condensed NEON / COAST lettering; Chinese subtitle; chapter card; start, continue, settings, controls, and quit buttons. Continue remains visible but is disabled when `save_exists` is false. |
| `playing` | Mission title/objective/distance upper-left; cash, five wanted-star slots, health, and heat upper-right; radio card; player-centered minimap with district/time lower-left; interaction prompt; speed in km/h and vehicle condition while driving; aiming crosshair. |
| `paused` | Dark overlay with resume, map, photo mode, settings, controls, restart, title, and quit buttons. The game controller pauses the simulation. |
| `settings` | Volume and sensitivity sliders, quality selector, invert-Y toggle, fullscreen action, controls guide, back, and a confirmed local-save reset action. |
| `map` | Full map, distinct mission/custom destination markers, route lines, map legend, distance and collectible count, clear-waypoint and back buttons. Mouse, keyboard, and gamepad can select a waypoint. |
| `photo` | An unobtrusive instruction strip. No button receives focus, so camera controls remain available. Capture and camera movement are handled by `game.gd`. |

The controls guide is an internal modal overlay rather than a separate game mode. It preserves the previous menu, contains focus inside the guide, and handles Escape/Menu/B before the parent pause handler. Notifications last five seconds; mission completion banners last six seconds and are shown during gameplay.

## Visual system and scaling

The composition uses the project's 1600 × 900 logical canvas with Godot `canvas_items` stretch. Native rendering captures cover 1280 × 720, 1600 × 900, and 2560 × 1440. These checks cover the tested 16:9 sizes; they do not establish arbitrary ultrawide or portrait layouts.

The visual language combines warm cream text, coral headings/actions, turquoise focus and route accents, dark translucent cards, a sunset-line motif, and the live 3D city. Main labels and controls are generally 18–31 logical pixels; large English headings use the condensed display font. Small map annotations and secondary captions intentionally use smaller sizes.

| Role | Color |
| --- | --- |
| Cream | `#FFF2D6` |
| Coral | `#FF755F` |
| Turquoise | `#6EE6CA` |
| Dusk | `#101D28` |
| Muted text | `#AFBFBD` |

Fonts are bundled at `assets/fonts/BarlowCondensed-Bold.ttf` and `assets/fonts/NotoSansSC.ttf`, with their OFL licenses alongside them. The body font uses an explicit variable-font weight. Missing font resources fall back to a `SystemFont` using Microsoft YaHei, Noto Sans CJK SC, or sans-serif. The interface uses native shapes, controls, and text; it does not depend on an embedded website or external UI images.

## Public API and ownership

`hud.gd` extends `CanvasLayer`, uses layer 10, and runs with `Node.PROCESS_MODE_ALWAYS`. `game.gd` owns simulation state, save operations, camera behavior, audio/settings application, and transitions between game modes. The HUD only presents supplied state and emits user intent.

```gdscript
signal action_requested(action: String)
signal setting_changed(key: String, value: float)
signal waypoint_selected(world_position: Vector3)

func set_mode(mode: String) -> void
func update_state(data: Dictionary) -> void
func notify(text: String, subtitle: String = "") -> void
func set_map_blocks(blocks: Array) -> void
func show_mission_complete(title: String, reward: int) -> void
```

`set_mode` switches visible screens, releases the previous control's focus, and defers focus to the start button, resume button, volume slider, or full-map cursor as appropriate. Entering the map places its cursor at the current player position. Playing and photo modes receive no menu focus. State refreshes use `set_value_no_signal` for sliders and do not repeatedly reset focus.

### Actions emitted by the implemented HUD

| Action | Parent behavior |
| --- | --- |
| `start`, `restart` | Start a new campaign. |
| `continue` | Resume validated local progress. |
| `resume`, `map_close` | Return to gameplay. |
| `settings` | Open settings and remember the originating mode. |
| `settings_back` | Return to the mode that opened settings. |
| `title` | Return to the title screen through the game controller. |
| `map`, `photo` | Enter the corresponding mode. |
| `clear_waypoint` | Remove the custom route. |
| `toggle_fullscreen` | Toggle the native window mode. |
| `quit` | Ask the game controller to save where permitted and quit. |
| `reset_save` | Clear local progress after the HUD's confirmation dialog is accepted. |

`controls`, `help_back`, and `reset_confirm` are internal button actions consumed by the HUD; they are not emitted to the parent. `game.gd` also accepts aliases `back`, `main_menu`, `close_map`, `photo_exit`, `screenshot`, and `capture`, but the current HUD does not emit those aliases. Photo capture is driven by the game's keyboard/gamepad handling.

Volume, sensitivity, quality, and invert Y are sent through `setting_changed`, not through `action_requested`.

| Setting key | Emitted value |
| --- | --- |
| `volume` | Float 0.0–1.0, slider step 0.05. |
| `sensitivity` | Float 0.3–2.0, slider step 0.1. |
| `quality` | 0 / 1 / 2 for low / medium / high. |
| `invert_y` | 0.0 or 1.0. |

The game applies these changes immediately. In normal play the game writes settings; `--test-mode` suppresses those writes.

### State dictionary

| Key | Type and meaning |
| --- | --- |
| `player_position` | `Vector3`; X/Z are map coordinates. |
| `player_yaw` | Float in radians, used by the map arrow. |
| `vehicle` | Boolean controlling the speed/vehicle-condition card. |
| `speed` | Float already converted to **km/h** by `game.gd`. |
| `health`, `car_health` | Floats on a 0–100 scale. |
| `cash` | Integer balance. |
| `wanted` | Integer 0–5; displayed as filled and empty stars. |
| `heat` | Float, displayed on a 0–120 heat scale. |
| `mission_title`, `objective` | Current Chinese-first story text. |
| `objective_position`, `has_objective`, `distance` | Selected destination, availability, and straight-line X/Z distance in metres. The selected destination can be a custom waypoint. |
| `mission_position`, `mission_active` | Independent mission marker and route, retained when a custom waypoint exists. |
| `waypoint_position`, `waypoint_active` | Custom destination marker and route. |
| `district`, `time` | District text and formatted game clock. |
| `controller` | Most recent input device, also passed to the map's hint text. |
| `prompt`, `radio` | Interaction prompt and current station text; the game prepares contextual button hints. |
| `save_exists` | Enables/disables Continue. |
| `settings` | Dictionary with `volume`, `sensitivity`, quality string (`low` / `medium` / `high`), and Boolean `invert_y`. |
| `collectibles`, `collectibles_total` | Collection progress shown beside the full map. |
| `aiming` | Enables the gameplay aiming crosshair. |
| `fps`, `mission_index`, `progress`, `hidden` | Additional values supplied by the game; the current HUD does not display dedicated widgets for them. |

Dictionary reads use safe defaults. Explicit types/casts are preferred when consuming Variant values to avoid GDScript inference failures.

## Map component and world geometry

`ui_map.gd` extends `Control` and exposes `full_map`, `set_data(data: Dictionary, new_blocks: Array)`, and `waypoint_selected(Vector3)`. Both map instances receive the same game state and block array. Full-map waypoint signals are forwarded by the HUD to `game.gd`.

`city.gd.get_map_blocks()` supplies dictionaries shaped `{x, z, w, d, color}`. X/Z are rectangle **centres**, and W/D are world-space width/depth. Main road centres are spaced 72 units apart at -216, -144, -72, 0, 72, 144, and 216. The coast is east, at positive X.

The full map covers X -260 to 330 and Z -260 to 260, preserving uniform X/Z scale. North is negative Z and appears upward; positive X appears rightward. Player arrows use yaw. The minimap is player-centered, clips drawing to its own control, and ignores pointer input. Both maps draw route lines and edge indicators for off-map objectives.

Routes are simple grid-based polylines, not a navmesh or an obstacle-aware shortest-path solver. The map emits `Vector3(x, 0, z)` after clamping selections to X/Z ±246. The game controller then snaps the destination to a traversable road. The displayed destination distance is straight-line distance rather than route length.

## Input and focus behavior

- Enter / gamepad A activates focused controls. Arrow keys, D-pad, and left stick navigate menus and adjust focused sliders.
- The full map keeps directional input for its cursor. Tab / Shift+Tab or gamepad RB / LB move focus between the cursor, Clear Route, and Back controls.
- Clicking the full map sets its cursor and selects a waypoint; Enter / A selects the focused cursor position. M / B closes the map through the game controller.
- Decorative labels, panels, overlays, and the minimap use `MOUSE_FILTER_IGNORE`. Interactive menus and the full map accept input. Pause, settings, map, and help remain usable while the simulation is paused.
- Photo mode has no focused UI button. B remains a camera descent control there; Escape / Menu exits. F12 / X captures a photo. F6 or the pause menu enters photo mode.
- Clearing a save opens a native confirmation dialog; the destructive parent action is emitted only after confirmation.

## Verification evidence and limits

`tests/ui_smoke.gd` completed **39 checks with zero failures**, using actual native controls and synthetic input: title activation, pause navigation, help modal focus/cancel order, settings slider changes, mouse Back, resume, map clicks, RB/LB focus switching, route clearing, photo controls, and unchanged profile/settings files. `tests/map_smoke.gd` completed **25 checks with zero failures** for actual viewport clicks, keyboard/D-pad/stick movement, Enter/A selection, north-up transforms, bounds, minimap input isolation, and paused-tree input.

`tests/visual_smoke.gd` captures the actual complete game rather than a mock interface. The current `artifacts/visual_smoke.json` records 14 successful screenshots across the three tested resolutions, including title, gameplay, pause, settings, map, a custom waypoint, night, and photo mode. It also records that profile/settings files were unchanged. Artifacts are local generated evidence and are ignored by Git; the parent verification record tracks subsequent final render/export checks.

These are synthetic input and native rendering results. They do not verify a physical controller, a continuous human playthrough, arbitrary display aspect ratios, or sustained performance. A single reported frame-rate sample is not a benchmark.
