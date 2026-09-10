# Configuration contract (version 1)

The canonical complete defaults are [`config/default.toml`](../config/default.toml),
embedded in `alure_core`; a custom example is `config/multi-panel.toml`.
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
  panels. Invalid reload reports a diagnostic and retains the **last good runtime
  model**. Raw `source` follows the last explicitly read document, even if invalid.
- Settings is intentionally **not auto-reloaded**, so unsaved edits are not lost.
  Reload discards edits explicitly and loads the disk text, including invalid
  text for repair. Save first validates and only then replaces the file.
- Save uses QSaveFile with direct-write fallback disabled, a QLockFile to
  coordinate Alure writers, and byte/existence comparisons with the last-read
  snapshot before writing and again before commit. External modification,
  creation/deletion, locks and symlinks produce diagnostics rather than overwrite.
  Copy edits elsewhere before Reload after a conflict. Comments and unknown keys
  survive exactly because the editor saves the full text, not a reserialized model.
  Deliberately deleting keys in the editor does, of course, delete those keys.
- There is an unavoidable tiny check-to-rename race with **non-cooperating external
  writers**: no cross-process filesystem compare-and-swap is available here. Alure
  writers serialize via the lock; do not simultaneously save with another editor.
  Atomicity is not a guarantee against storage/power failure or malicious writers.
- Settings changes apply on explicit Save, then the panel process's watcher
  reloads. `runtime.watch=false` disables that automatic application; restart the
  panel process to enable it again. Other supported values apply on valid reload.
  Qt platform/qt6ct environment selection is external and requires process restart.

## Known keys and validation

TOML types must match defaults exactly, except float fields also accept integers.
Integers do not accept floats. Tables cannot be replaced with scalars. Diagnostics
name the offending key or give toml++ line/column syntax context. Strings/booleans
shown in defaults are validated by type; enum/range constraints follow below.

| Table | Keys, bounds and meaning |
|---|---|
| root | `version` integer, exactly 1 |
| runtime | `watch` bool=true; `reload_delay_ms` integer 50..10000, default 200 |
| theme | `name` midnight/dawn/forest; `font` nonempty string; `font_size` integer 6..72; `spacing`, `padding`, `radius` integer 0..128; `border_width` integer 0..16; `opacity` finite number 0..1 (backdrop alpha only); `icon_mode` builtin/theme; `icon_size` integer 8..128 |
| theme.palette | Optional `background`, `surface`, `foreground`, `muted`, `accent`, `border`: Qt color strings (`#rrggbb` recommended). Start with selected named theme, then apply overrides. `opacity` controls panel background/border alpha; content stays opaque. |
| settings | `width` integer 400..7680; `height` 300..4320; `editor_font` nonempty string; `editor_font_size` integer 6..72 |
| foundation | `label`, `notice`, `icon` strings; `bold`, `show_icon`, `show_notice` booleans. `icon` is a nonempty built-in or freedesktop theme icon name (letters/digits/underscore/dot/hyphen), not a filesystem path. These control the honest temporary foundation banner. |

Theme defaults: Sans Serif 13 px, spacing 10, padding 12, radius 14, border 1,
opacity .88, icons 20 px, built-in mode. Settings defaults: 900×680, monospace 13 px.
Built-in palette values are in `config/themes.toml`. Unknown icon names resolve to
`fallback.svg`; `alure` resolves to the original logo. SVG colors are intrinsic,
not recolored by the palette. Settings uses standard Qt Quick Controls semantics
and labels; this stage offers a TOML editor, not a schema-generated form, shortcut
editor, per-control style sheet, translations, or an unsaved-close dialog.

### `[[panels]]` (up to 128 definitions, any number per output/edge)

| Key | Default | Constraint/behavior |
|---|---|---|
| id | main | Unique nonempty string; also layer-shell scope suffix |
| enabled | true | Boolean; false creates no window |
| output | primary | Nonempty string: `primary`, `*` (each output), or exact QScreen output name; unmatched waits and warns, no fallback to wrong display |
| edge | top | top / bottom / left / right |
| thickness | 44 | Integer 16..512 logical pixels, clamped to output extent |
| length | 0 | Integer 0..32768; 0 fills the edge, otherwise centered along edge and clamped to output |
| exclusive_zone | -1 | Integer -1..32768; -1 computes thickness + edge margin, 0 disables reservation, positive specifies reservation |
| layer | top | background / bottom / top / overlay |
| margins | all 8 | Table top/right/bottom/left integers 0..4096 logical pixels |
| modules | all ten names in default example | Ordered string array, no duplicate or undefined names |

Bars are always non-keyboard-interactive. Output add/remove, primary-screen and
geometry changes trigger rebuild. QQuickWindow default alpha buffer is enabled
before the first window; scene clear color is transparent. Edge reservation and
placement are real LayerShellQt requests, not a compositor configuration change.
Panels on the same edge can overlap: no automatic stacking/offset solver is
provided. Set margins/length/zone deliberately, with total margins smaller than
your output. Compositor policy ultimately determines placement and reservations.
Preview is a normal resizable window: geometry is initialized but edge anchors,
focus suppression and reservations intentionally are not emulated.

### `[modules.<name>]`

Known initial names: `workspaces`, `media`, `tray`, `volume`, `updates`, `wifi`,
`bluetooth`, `notifications`, `calendar`, `battery`. **This stage does not render
or execute these modules.** Their stable outer contract is:

- `enabled`: bool, true by default.
- `style.show_icon`, `style.show_label`: bool, true except tray label false.
- `behavior.interval_ms`: integer 100..86400000.
- `behavior.timeout_ms`: integer 100..600000.
- `behavior.command`: argv array of at most 128 strings without NUL; executable
  must be nonempty when array is nonempty. Empty means no external command.
  Never interpreted by a shell. Default command/interval/format for each module
  is listed explicitly in the canonical TOML example.
- `behavior.format`: string. Placeholder examples reserve future service/UI
  formatting contracts; no substitution occurs in the foundation banner.

New named module tables are accepted, merged with the same common defaults
(enabled, both labels/icons, interval 1000, timeout 3000, empty argv and format).
This is an extension seam, **not a plugin loader**. Downstream implementations
must add their supported keys' defaults, validation, examples, tests and docs
before exposing new behavior. Built-ins currently reserve asynchronous DBus or
bounded QProcess service implementations; nothing runs checkupdates or any live
control merely because its command exists in the configuration.
