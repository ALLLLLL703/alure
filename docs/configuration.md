# Configuration contract (version 1)

The canonical complete defaults are [`config/default.toml`](../config/default.toml),
embedded in `alure_core`; custom examples include `config/multi-panel.toml` and `config/four-edge.toml`.
The complete v0.1 UI/settings key table and formatting/interaction boundaries are
in [interface.md](interface.md#configuration-coverage-and-defaults).
QML and downstream C++ consume the **same fully defaulted** ConfigStore model.
Maps merge recursively. Arrays replace in full; missing `panels` uses one default
panel, `panels = []` intentionally creates none. Each panel entry is merged with
the default panel. Supply unique IDs for multiple entries. Unknown tables/keys
are retained in the source and forwarded into the model, but are **not validated
or implemented features**. Unknown TOML date/time nodes are uninterpreted in the
QVariant model; their exact source survives saves. Maximum document size: 1 MiB.

## Apply, errors and save semantics

- Startup uses defaults for missing files without creating files/directories.
  Invalid startup config stops panel/preview mode (exit 1); settings opens the raw
  document for repair, with default styling and a diagnostic.
- Running panels watch the file and parent directory (nearest existing ancestor
  for missing paths). Debounced valid reload publishes a model and rebuilds
  panels only when the effective model changes. Unrelated sibling-file changes and
  comment-only edits do not dismiss popups or restart services. Invalid reload reports a diagnostic and retains the **last good runtime
  model**. Raw `source` follows the last explicitly read document, even if invalid.
- Settings is intentionally **not auto-reloaded**, so unsaved edits are not lost.
  Reload discards edits explicitly and loads the disk text, including invalid
  text for repair. Save first validates and only then replaces the file.
- Save uses QSaveFile with direct-write fallback disabled, a QLockFile to
  coordinate Alure writers, and byte/existence comparisons with the last-read
  snapshot before writing and again before commit. External modification,
  creation/deletion, locks and symlinks produce diagnostics rather than overwrite.
  Copy edits elsewhere before Reload after a conflict. Comments and unknown keys
  survive exactly when using the raw editor, which saves full text. Form edits use
  source spans; panel collection operations explicitly warn before normalizing only
  panel regions. See the [source-preserving forms policy](interface.md#settings-one-draft-no-silent-rewrites).
  Deliberately deleting keys in the editor does, of course, delete those keys.
- There is an unavoidable tiny check-to-rename race with **non-cooperating external
  writers**: no cross-process filesystem compare-and-swap is available here. Alure
  writers serialize via the lock; do not simultaneously save with another editor.
  Atomicity is not a guarantee against storage/power failure or malicious writers.
- Settings changes apply on explicit Save, then the panel process's watcher
  reloads. `runtime.watch=false` disables that automatic application; restart the
  panel process to enable it again. Other supported values apply on valid reload.
  Qt platform/qt6ct environment selection is external and requires process restart.

## Frosted glass backgrounds

`theme.blur_enabled = true` (default) requests real compositor background blur;
`false` restores alpha-only backgrounds. This is a strict TOML boolean. The
Appearance form exposes **Frosted glass background**. Save & apply reloads
watching panels; with `runtime.watch=false`, restart. Existing standalone
clipboard windows read the option at launch; settings stays opaque.
`theme.opacity` still controls tint alpha, not blur strength. See
[background blur](background-blur.md) for surface coverage, an example,
unsupported-platform fallback and Niri xray behavior.

## Known keys and validation

TOML types must match defaults exactly, except float fields also accept integers.
Integers do not accept floats. Tables cannot be replaced with scalars. Diagnostics
name the offending key or give toml++ line/column syntax context. Strings/booleans
shown in defaults are validated by type; enum/range constraints follow below.

| Table | Keys, bounds and meaning |
|---|---|
| root | `version` integer, exactly 1 |
| runtime | `watch` bool=true; `reload_delay_ms` integer 50..10000, default 200; `trace_windows` bool=false emits window lifecycle stderr diagnostics |
| theme | `name` midnight/dawn/forest/onedark/catppuccin/tokyonight; `font` nonempty string; `font_size` integer 6..72; `spacing`, `padding`, `radius` integer 0..128; `border_width` integer 0..16; `opacity` finite number 0..1 (backdrop alpha); `blur_enabled` boolean, default true, requests compositor background blur; `icon_mode` builtin/theme; `icon_size` integer 8..128 |
| theme.palette | Optional `background`, `surface`, `foreground`, `muted`, `accent`, `border`: Qt color strings (`#rrggbb` recommended). Start with selected named theme, then apply overrides. `opacity` controls panel background/border alpha; content stays opaque. |
| settings | `width` integer 400..7680; `height` 300..4320; `editor_font` nonempty string; `editor_font_size` integer 6..72 |
| foundation | `label`, `notice`, `icon` strings; `bold`, `show_icon`, `show_notice` booleans. `icon` is a nonempty built-in or freedesktop theme icon name (letters/digits/underscore/dot/hyphen), not a filesystem path. Deprecated compatibility keys, validated but no longer rendered. |

Theme defaults: Sans Serif 13 px, spacing 10, padding 12, radius 14, border 1,
opacity .88, icons 18 px, built-in mode. Settings defaults: 900×680, monospace 13 px.
Built-in palette values are in `config/themes.toml`. Unknown icon names resolve to
`fallback.svg`; `alure` resolves to the original logo. SVG colors are intrinsic,
not recolored by the palette. Settings offers source-preserving typed controls plus an advanced complete TOML editor and unsaved
Save/Discard/Cancel dialogs. Standard Qt control/editor semantics apply; no arbitrary
shortcut editor, per-control stylesheet or translations are provided. See interface.md
for UI, per-module style, controls_style and icon_theme keys.

### `[[panels]]` (up to 128 definitions, any number per output/edge)

| Key | Default | Constraint/behavior |
|---|---|---|
| id | main | Unique nonempty string; also layer-shell scope suffix |
| enabled | true | Boolean; false creates no window |
| output | primary | Nonempty string: `primary`, `*` (each output), or exact QScreen output name; unmatched waits and warns, no fallback to wrong display |
| edge | top | top / bottom / left / right |
| thickness | 44 | Integer 16..512 logical pixels, clamped to output extent |
| length | 0 | Integer 0..32768; 0 fills the edge, otherwise centered along edge and clamped to output |
| exclusive_zone | -1 | Integer -1..32768; -1 sends clamped thickness as the protocol zone, 0 disables reservation, positive sends an explicit protocol zone; the compositor adds the anchored edge margin to positive zones |
| layer | top | background / bottom / top / overlay |
| visibility | see below | Per-panel table: `mode="always"` (always/dodge-windows/auto-hide), `show_delay_ms=100`, `hide_delay_ms=350` (integers 0..10000), `edge_trigger_px=2` (integer 1..16), `unknown_geometry="hide"` (hide/show). Dynamic modes never reserve space; reload applies. |
| margins | all 8 | Table top/right/bottom/left integers 0..4096 logical pixels |
| modules | all ten names in default example | Ordered string array, no duplicate or undefined names |

See [panel visibility](panel-visibility.md) for edge input, popup pinning, the shared
Niri subscription and conservative tiled-geometry precision limits. Settings exposes
all visibility keys. `exclusive_zone` and `window_gap` apply only in `always` mode.

Bars are normally non-keyboard-interactive (explicit popups temporarily use on-demand
keyboard policy). Hover reveal does not activate them. Output add/remove, primary-screen and
geometry changes trigger rebuild. QQuickWindow default alpha buffer is enabled
before the first window; scene clear color is transparent. Edge reservation and
placement are real LayerShellQt requests, not a compositor configuration change.
Panels on the same edge can overlap: no automatic stacking/offset solver is
provided. Set margins/length/zone deliberately, with total margins smaller than
your output. With default thickness 44 and anchored margin 8, automatic mode sends
zone 44 plus margin 8 separately: the total reservation is 52, not 60. Explicit
positive zones also exclude the margin. Alure's config sentinel -1 means automatic;
it is not the protocol's -1 (ignore reservations), used only for auxiliary surfaces.
Reserving surfaces are arranged relative to the remaining usable rectangle after
previous reservations. Their corner margins add insets to that rectangle, not
necessarily to the physical output edges. In `four-edge.toml`, the vertical bars'
68-pixel top/bottom margins are additional insets, not an absolute 68-pixel output
offset. There is no cross-panel placement solver or guaranteed compositor ordering.
Compositor policy ultimately determines placement and reservations.
Preview is a normal resizable window: geometry is initialized but edge anchors,
focus suppression and reservations intentionally are not emulated.

### `[modules.<name>]`

Known initial names: `workspaces`, `media`, `tray`, `volume`, `updates`, `wifi`,
`bluetooth`, `notifications`, `calendar`, `battery`. **Live services now run for
enabled modules, now rendered with actionable detail surfaces.** Their stable outer contract is:

- `enabled`: bool, true by default.
- `style.show_icon`, `style.show_label`: bool, true except tray label false.
- `behavior.interval_ms`: integer 100..86400000.
- `behavior.timeout_ms`: integer 100..600000.
- `behavior.command`: argv array of at most 128 strings without NUL; executable
  must be nonempty when array is nonempty. Empty means no external command.
  Never interpreted by a shell. Default command/interval/format for each module
  is listed explicitly in the canonical TOML example.
- `behavior.allow_actions`: bool, default true; false disables service controls.
- `behavior.format`: string. UI field substitutions and calendar Qt date patterns
  are documented in [interface.md](interface.md#configuration-coverage-and-defaults).

New named module tables are accepted, merged with the same common defaults
(enabled, both labels/icons, interval 1000, timeout 3000, empty argv and format).
Only shared service options are inherited: media-only fields such as
`preferred_player`, artwork and playback controls are not added to other modules.
Explicit unknown TOML keys remain preserved, but a legacy `preferred_player` on
a non-media module is not exposed by its settings page.
This is an extension seam, **not a plugin loader**. Downstream implementations
must add their supported keys' defaults, validation, examples, tests and docs
before exposing new behavior. Enabled volume/updates/WiFi services execute their
**read** commands on startup/interval; control commands execute only via explicit
action calls. See [service readiness and API](services.md) for live behavior and
unsupported scope. Settings/validation run no services; preview does.


### Service behavior keys and reload semantics

Canonical defaults and complete argv are in `config/default.toml`; custom overrides
are demonstrated in `config/services-example.toml`. All keys below are under
`modules.<name>.behavior`, except the module's `enabled` switch. Type/range errors
retain the prior model on runtime reload and prevent invalid startup as above.
A valid change to enabled/execution behavior stops and restarts only that service;
style/format-only changes do not reset it. Disabling stops timers and requests,
kills its direct subprocess and releases owned bus names. Notification opt-in
false also disables that service. Module presence/order in a panel is a UI concern,
not a second enable switch. Disable unused modules explicitly to avoid their work.

| Module | Additional keys / defaults / validation |
|---|---|
| workspaces | `socket_path=""`: string, empty selects NIRI_SOCKET; otherwise absolute path without NUL. Native JSON event stream; `command` is unused. `ordering="output-index"`: string enum `output-index` / `provider`. Default reconnect interval 1000, initial snapshot/action acknowledgement timeout 3000; healthy workspace updates are event-driven, not interval-delayed. |
| media / tray | DBus integrations; `command` unused. Default interval 1000, timeout 3000. |
| volume | `backend="auto"` (`auto`, `pipewire`, `pulseaudio`); PulseAudio command defaults and validation: [audio.md](audio.md). `set_volume_command=["wpctl","set-volume","@DEFAULT_AUDIO_SINK@"]` appends a decimal volume ratio; `mute_command=["wpctl","set-mute","@DEFAULT_AUDIO_SINK@","toggle"]`; `max_percent=100` integer 1..150; `debounce_ms=100` integer 10..2000 (minimum write cadence; idle input dispatches next event turn, busy completion waits only remaining cadence); `scroll_enabled=true`, `scroll_step=5` integer 1..100 percentage points/notch, `scroll_inverted=false`. Read interval 2000, timeout 3000. |
| updates | `update_command=[]`: empty disables explicit update action. Default read `command=["checkupdates"]`, interval 1800000 (30 min), timeout 120000. No install command is preconfigured or automatic. |
| wifi | `radio_command=["nmcli","radio","wifi"]` appends on/off; `radio_status_command=["nmcli","-t","-f","WIFI","general"]`; `saved_command=["nmcli","-t","-f","UUID,NAME,TYPE","connection","show"]`; `connect_command=["nmcli","connection","up","uuid"]` appends an observed UUID. Read command lists ACTIVE,SSID,SIGNAL with `--rescan no`. Interval 10000, timeout 5000 **per subprocess**. |
| bluetooth | Native BlueZ DBus; `command` unused. Interval 10000, timeout 5000 per DBus call. |
| notifications | `server_enabled=false` bool, explicit opt-in; `dnd=false` bool; `history_limit=100` integer 1..1000; `default_expire_ms=5000`, `max_expire_ms=86400000`, both integers 100..86400000 with default<=max. Interval 1000 governs retry/expiry resolution, timeout 3000. Changing execution config clears in-memory history and resets runtime DND. |
| battery | `sysfs_path="/sys/class/power_supply"`: absolute nonempty path without NUL; fixture roots supported. Interval 30000; command/timeout unused for bounded local reads. |

Workspace ordering defaults to ascending output name (case-sensitive lexical order;
null/empty output first), then numeric `idx`, then numeric stable `id` to break ties.
`ordering="provider"` preserves incoming IPC array order. This is a presentation
policy shared by panel and details, not output filtering or workspace renumbering;
activation still uses stable IDs. Changing ordering on valid reload restarts the
workspace subscription and applies to its next full snapshot (or restart with watching off).
Invalid values/types produce a `modules.workspaces.behavior.ordering` diagnostic.

All known `*_command` options use the same argv-array validation as `command`:
maximum 128 string entries without NUL, nonempty executable if supplied. Empty
action arrays disable those actions; empty required **read** arrays produce an
unavailable diagnostic. Native Niri/DBus/sysfs modules ignore command arrays;
commands do not override their protocols. All query overrides must emit their
provider's documented C-locale format; no generic command-output widget exists.
Commands are trusted user configuration, not sandboxed, and never shell-expanded.
If opting into an update terminal, supply executable and arguments separately;
Alure does not add `sh -c`, authentication, confirmations or package installation.
Do not verify custom update/power/connect actions on a live desktop inadvertently.

The service layer returns raw data only. The v0.1 interface consumes module style
and formatting; see [interface.md](interface.md) for supported boundaries.

Settings uses normal window-manager closing, without an in-content Close button
or saved-state badge. The bottom Save & apply remains; the title's `*` marks a draft.
Legacy `settings.show_close_button` is preserved as an unknown key but has no effect.
Settings shortcut (takes effect on Preview or next settings launch):
`settings.close_shortcut` is a canonical Qt portable key sequence string, default
`"Ctrl+W"`; `""` disables it. Invalid types/sequences produce diagnostics. Compositor
close handling remains available independently; all routes guard unsaved edits.


Typed settings controls edit these same TOML keys; there is no second settings
format or hidden per-widget configuration. Apply fields changes the draft only;
Preview validates and applies it to the settings process; Save & apply uses the
existing atomic source-preserving save/conflict checks. See
[typed controls and preservation boundaries](interface.md#settings-one-draft-no-silent-rewrites).
The system Qt QuickDialogs2 module is required for the themed nonnative color picker.

Tray dropdowns use `modules.tray.behavior.menu_width` (integer 160..1920, default
280) and `menu_height` (integer 100..2160, default 420; scrolls beyond this limit).
Both are logical pixels, clamp to the output and apply on config reload. The
existing popup gap/alignment/direction, theme, `popup_enabled` and `allow_actions`
settings apply. Settings exposes the dimensions on the Tray module page.

```toml
[modules.tray.behavior]
menu_width = 320
menu_height = 480
```

Protocol reference: [DBusMenu specification](https://github.com/gnustep/libs-dbuskit/blob/master/Bundles/DBusMenu/com.canonical.dbusmenu.xml)
(LGPL-2.1/3 specification consulted; no upstream implementation copied).
Nested Wayland computer-use check: a synthetic DBusMenu provider's right-click
dropdown appeared beneath its tray icon; submenu navigation and action dismissal
worked. Automated tests cover protocol updates/errors, disabled/hidden entries,
owner loss, cancellation and the actual QML right-click route.

## Clipboard history

Enable `modules.clipboard.enabled` and include `"clipboard"` in a panel's module
list for an anchored history view, or run `alure --clipboard` for a standalone
cursor-local preview without the rest of the shell. It consumes existing
cliphist history and never starts recording. The module is enabled and placed in
the default bar; existing explicit lists are preserved. Drag Clipboard into a slot
to add it to an existing layout. Standalone invocation is a single-instance toggle.

Configuration, limits, deletion/copy semantics and the Wayland cursor fallback
are documented in [clipboard.md](clipboard.md), with a complete
[clipboard-example.toml](../config/clipboard-example.toml).

## Notification DND and banners

```toml
[modules.notifications.behavior]
server_enabled = true # still opt-in; never replaces an existing notification daemon
toast_enabled = true
dnd = false
persist_dnd = true
[ui.toast]
enabled = true
output = "primary"
duration_ms = 5000
```

`persist_dnd` is boolean, defaults to `true` and applies on reload. The dropdown's
explicit Turn on/off do not disturb action now saves `dnd` to TOML, so a restart
does not silently restore the opposite configured value. Set `persist_dnd=false`
for the previous session-only switch. Save errors/conflicts are displayed and
leave the switch unchanged. A DND-only reload updates the live server without
releasing its DBus name or discarding history. Other execution-option changes
still restart the service as documented in services.md.

DND suppresses **new banners**, not history. Turning it off does not replay older
suppressed items; test with a fresh `notify-send`. Both toast enable flags must
be true and the configured output must exist. The dropdown now explains these
states. With `runtime.trace_windows=true`, each new arrival logs whether it is
DND-suppressed, disabled or banner-eligible; an absent output gives a diagnostic.

Regression coverage uses a private DBus end-to-end: Notify → service → panel host
→ Toast, DND off via an actual QML click, retained history/name ownership,
configuration persistence, session-only mode, restart and banner timer dismissal.
Native computer-use verification on nested Niri confirmed the overlay with no
keyboard interactivity after DND-off and after restart (the screenshot used a
30-second duration; the normal 5-second mapping and automatic dismissal were
also observed). No host service or live user configuration was replaced.

## Now-playing media card

The panel automatically uses the first **Playing** MPRIS player, then Paused,
then Stopped (service name breaks ties). A failing browser player no longer hides
another player's metadata. `preferred_player` defaults to empty (automatic); a
nonempty full MPRIS service name or dotted prefix such as
`org.mpris.MediaPlayer2.musicfox` takes precedence while present. Invalid names
are rejected. The dropdown also allows choosing a player for that open card.

```toml
[modules.media.behavior]
popup_width = 360
popup_height = 460
control_size = 40
control_icon_size = 26
preferred_player = ""
show_artwork = true
artwork_remote = true
artwork_height = 160
show_artist = true
show_album = true
show_progress = true
show_shuffle = true
show_repeat = true
```

These are the defaults; flags require booleans. Dimensions are integer logical
pixels: popup width 240–1920, height 240–2160, control size 24–96, control icon
size 16–64, artwork height 80–720 (a maximum, also capped to fit the card).
All apply on reload, without restarting Alure. Media has its own compact popup
size rather than inheriting the larger general dropdown. Vector transport icons
are independent of font glyph coverage; the main play button is 1.4× the control
size. Playback-source hover/selection explicitly pairs accent backgrounds with
contrasting text in both light and dark themes. Other layout/appearance follows
the theme. Verified via computer-use on Niri and QML hover/click regressions. The cover
supports `file:` and, with `artwork_remote=true`, HTTP(S); it loads only in a
visible dropdown, decodes at its display size and releases its source on hide.
Missing/failed artwork has a music-symbol fallback. Remote artwork may contact
the player-provided server; disable `artwork_remote` for local covers only.

Position/duration come from real MPRIS data at the configured `interval_ms`, not
an invented timeline. Unknown duration and unsupported controls are disabled.
Seek retains the dragged track ID even if the current song changes; shuffle and
repeat require those optional player properties. Repeat cycles None → Playlist
→ Track. `allow_actions=false` disables every transport/seek action. No actual
host playback was changed during validation: controls were tested against private
DBus fixtures; the real musicfox cover/metadata/progress were visually checked
with computer-use on nested Niri.

## Dropdown dismissal and workspace presentation

`ui.escape_closes=true` also handles Escape before focused child controls consume
it. While a native dropdown is open, its layer parent temporarily uses on-demand
keyboard interactivity (popup children inherit the parent's policy); closing
restores a non-keyboard panel. `ui.toggle_on_click` is boolean, default `true`:
pressing the source module again closes it. Both apply on reload; setting the
latter false disables Alure's toggle, not the compositor's native outside dismissal.
Dropdowns no longer contain an Open integration settings action; the panel's
settings shortcut remains available.

Workspace `style.show_icon` now shows one shared leading module icon, followed
by the ordered workspace labels, without repeated per-workspace icons.
`modules.workspaces.style.active_indicator` accepts `"underline"` (default flat
selection) or `"pill"` (previous filled selection); invalid names/types are rejected.
The existing `show_label`, format, min/max width and icon options still apply.

```toml
[ui]
escape_closes = true
toggle_on_click = true
[modules.workspaces.style]
show_icon = true
active_indicator = "underline"
```

Verified with actual clicks/Escape in a nested Niri session and computer-use,
including returning layer keyboard interactivity to none; QML tests cover child
focus, repeated opens, source destruction and five unclipped workspace numbers.

## Additional built-in themes

`theme.name` also accepts `onedark`, `catppuccin` (Mocha), and `tokyonight` (Night).
The default remains `midnight`. Settings reads its theme choices directly from
`config/themes.toml`, the same catalog used by validation. Preview affects only
settings; Save & apply publishes the theme to watching panels. Existing explicit
`theme.palette` overrides still win over the selected theme's colors.

```toml
[theme]
name = "catppuccin"
```

Palette references: [One Dark](https://github.com/joshdick/onedark.vim) (MIT),
[Catppuccin](https://github.com/catppuccin/palette) (MIT),
[Tokyo Night](https://github.com/folke/tokyonight.nvim) (Apache-2.0).
Only color values were used to map Alure's six palette roles; no theme code or
assets were copied. One Dark uses a lighter muted tone for small shell text.

## Panel module placement

Each `[[panels]]` accepts these hot-reloaded settings (also editable under Panels):

- `layout = "linear"` (default) uses the existing ordered `modules` list.
- `layout = "three-zone"` uses `modules_left`, `modules_center`, `modules_right`.
  On vertical panels these mean top, center, bottom. The center stays at the
  panel midpoint, independent of side widths. If groups cannot fit, the strip
  scrolls instead of overlapping. Each list is limited to 128 string entries.
- Default zones: left `["workspaces", "media"]`, center `["calendar"]`, right
  `["tray", "volume", "updates", "wifi", "bluetooth", "notifications", "battery"]`.
  Inactive-layout lists are preserved. Set all three explicitly to customize a
  three-zone layout; ordinary modules may occur only once across active zones.
- `@spacer` inserts `spacer_size` logical pixels (integer 0..4096, default 24).
  `@stretch` shares spare group space equally with the group's other stretches.
  These two tokens may repeat. Normal theme spacing remains between entries.
- `@settings` places the settings shortcut explicitly. Otherwise it is appended
  to the linear strip or right group. `ui.show_settings=false` hides it everywhere.

Settings can move modules between zones and reorder/add/remove spacers. These
operations remain a draft until Save & apply, without rewriting unrelated TOML.
Unknown tokens, duplicate placements and invalid types/ranges report an error.
See [`config/layouts.toml`](../config/layouts.toml) for both layouts.

```toml
[[panels]]
layout = "three-zone"
modules_left = ["workspaces", "@spacer", "media"]
modules_center = ["calendar"]
modules_right = ["tray", "volume", "@settings"]
spacer_size = 32
```

Geometry tests cover both axes, narrow strips and equal elastic spacing; settings
pointer tests cover cross-zone moves. A nested Wayland computer-use check showed
left settings, a centered clock and right tray, with the clock popup still anchored.
