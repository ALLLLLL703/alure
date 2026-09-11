# cliphist clipboard history

Alure reads an **existing** cliphist database. It does not start a recorder,
change Niri configuration, or take over another clipboard manager. Runtime tools:
`cliphist` (tested 0.7.0) and `wl-copy` from `wl-clipboard` (tested 2.3.0), available
as system packages. Neither is downloaded or required for building Alure.

## Two entry points

- Panel (enabled and placed by default; explicit old lists are retained): enable `modules.clipboard.enabled` and add `"clipboard"` to the chosen
  panel's ordered module list. Open by clicking its icon; Esc or the same icon
  closes it. See [clipboard-example.toml](../config/clipboard-example.toml).
- Standalone: `alure --clipboard` opens at the cursor without constructing any
  panel, notification/tray server or other system service. This explicit request
  works even when the panel module is disabled (if explicitly configured). `--config PATH`
  selects the same TOML model; `--settings` cannot be combined with this mode.
  A second invocation closes the existing standalone layer. `--preview` uses ordinary windows for testing, not Wayland positioning.

Example Niri binding, to add yourself inside your existing `binds` block:

```kdl
Mod+V { spawn "alure" "--clipboard"; }
```

This follows Klipper's **show at mouse position** interaction, not a second full
Shell process or a Plasma dependency. On Plasma the corresponding API is
`qdbus6 org.kde.klipper /klipper org.kde.klipper.klipper.showKlipperPopupMenu`.
KWin offers `org_kde_plasma_surface.open_under_cursor`; Niri does not offer that
portable interface. Alure instead briefly maps transparent, input-active layer
probes on the outputs. A real surface-local pointer-enter supplies the output
and logical cursor coordinates, including a stationary visible pointer. The
selected output retains a modal layer with the clamped card; other probes hide.
No `QCursor::pos()` global-coordinate guess is used.

The probes initially take no keyboard focus. The selected clipboard layer takes
exclusive keyboard focus; outside click closes by default. Esc follows
`ui.escape_closes`, and focus-loss dismissal follows `ui.close_on_focus_loss`.
Output hotplug/geometry changes close the view. Probing can briefly intercept
pointer input. Hidden/grabbed pointers, lock/screenshot overlays or compositor
rules can prevent pointer-enter; after the configured timeout Alure logs a
position-only diagnostic and centers on `output`, or cancels. Centered fallback
is **not** claimed to be the mouse output. Physical multi-output/fractional-scale
and hidden-pointer compositor combinations have not been visually verified.

## Viewing and management

Search filters the list's cliphist previews (not the full decoded contents).
Visible rows decode lazily, showing previews inline: plain selectable text, or a PNG/JPEG/GIF/BMP/
TIFF/WebP image preview when a Qt image reader supports it. Animated images show
one frame. Actual bytes, not cliphist's spoofable binary-description label,
determine preview type. SVG/HTML are not executed; unsupported binary content
has an honest fallback. No image fetches or disk thumbnail caches are created.
Preview buffers and history snapshots are dropped when the view closes.

Up/Down select rows; Return copies. Buttons provide Copy, Delete and Clear
history. Single deletion asks for confirmation by default; clearing always asks.
Changing selection cancels pending deletion confirmation. Copy always decodes
fresh, preserving all original bytes (including NUL/newline); failed/stale or
oversized decodes do not change the clipboard. MIME is inferred because cliphist
0.7 does not retain all original offered MIME formats. wl-copy's normal forking
handoff keeps the copied selection alive after the standalone window exits.
Do not add `--foreground` to `copy_command`: that is incompatible with this
handoff and would hit the configured command timeout. Copy is not automatic paste.

Deleting history does not clear the current clipboard and is not secure erasure
of backups/filesystem remnants. An external recorder may store a copied entry
again under a new ID. cliphist history is sensitive, unencrypted data; protect
its directory. Alure does not print clipboard bytes or command output to logs.

## Configuration

All keys below are in `[modules.clipboard.behavior]`; defaults appear in the
example and `config/default.toml`. Invalid types/ranges give config diagnostics.
Panel changes apply on reload (execution changes close/reopen the view); the
standalone process reads settings on each invocation.

| Key | Default | Validation / meaning |
|---|---|---|
| `database_path` | `""` | Empty uses `CLIPHIST_DB_PATH`, then XDG cache `cliphist/db`; otherwise absolute, NUL-free path. Missing DB is diagnosed without creating it. Alure does not parse cliphist's separate non-TOML config to discover a custom DB; set this path explicitly. |
| `cliphist_command`, `copy_command` | `["cliphist"]`, `["wl-copy"]` | Nonempty argv arrays; no shell interpolation. Alure appends `-db-path PATH operation`, or `--type MIME`. |
| `popup_width`, `popup_height` | 480, 560 | Integer logical pixels, 280–1920 / 320–2160; clipped to output size. |
| `max_items` | 200 | 1–1000 newest list entries displayed. Does not change cliphist's retention policy. |
| `max_bytes` | 5242880 | 1024–67108864, per-process stdout/decoded-data cap. |
| `max_image_pixels` | 16777216 | 1024–67108864, source pixel count allowed for preview. |
| `preview_image_size` | 512 | 64–1024, maximum decoded preview dimension; original copy bytes unchanged. |
| `preview_text_chars` | 8192 | 128–65536, text-preview limit; truncation is marked. |
| `interval_ms`, `timeout_ms` | 3000, 3000 | 100–86400000 / 100–600000; list polls only while open, one bounded command at a time. |
| `allow_actions`, `allow_delete`, `confirm_delete` | true | Booleans controlling copy/management permissions and single-row confirmation. |
| `close_on_copy`, `close_on_outside_click`, `popup_enabled` | true | Boolean interactions; outside interception remains modal if dismissal is disabled. |
| `copy_shortcut`, `next_shortcut`, `previous_shortcut` | `"Return"`, `"Down"`, `"Up"` | Canonical Qt portable shortcut strings; empty disables. |
| `cursor_timeout_ms`, `cursor_gap` | 300, 8 | 50–3000 ms for locating, 0–128 logical-pixel offset. |
| `cursor_fallback`, `output` | `"center"`, `"primary"` | `center` or `cancel`; nonempty output name or `primary` for fallback. |
| `format` | `"{count}"` | Optional panel label (style `show_label` defaults false). |

Use your existing cliphist recorder. If none is configured, upstream documents
separate text/image watchers using `wl-paste --type text --watch cliphist store`
and `wl-paste --type image --watch cliphist store`. Configure capture deliberately,
with the same DB path and appropriate privacy policy; Alure never launches them.

## Evidence and references

- Automated tests: lazy reads, missing DB/executable/entry, bounded output and
  deadlines, image-pixel limits, literal text, byte-exact binary copy, permission
  and confirmation gates, deletion/wipe, release on close, QML search/image/
  confirmation, local rather than synthetic-global pointer coordinates, Esc,
  CLI isolation from other services, default/custom/invalid configuration.
- Computer-use on nested Niri: stationary pointer at (350,280) produced a card
  at (358,288); real cliphist PNG preview; Copy survived process exit and the
  resulting private Wayland selection matched the source PNG byte-for-byte.
  Panel entry, literal multiline text, source-button dismissal and confirmed
  deletion were also checked against a temporary database. No real user history
  or desktop clipboard was changed. The nested tool's Unicode typing did not
  map correctly through Niri; search interaction is covered by Qt input tests.
- [cliphist 0.7 implementation/CLI contract](https://github.com/sentriz/cliphist/blob/v0.7.0/cliphist.go)
- [wl-clipboard usage](https://github.com/bugaevc/wl-clipboard)
- [Klipper invocation](https://github.com/KDE/plasma-workspace/blob/master/klipper/klipper.cpp)
  and [popup positioning](https://github.com/KDE/plasma-workspace/blob/master/klipper/klipperpopup.cpp)
- [Plasma open-under-cursor protocol](https://github.com/KDE/plasma-wayland-protocols/blob/master/src/protocols/plasma-shell.xml)
- [Qt Wayland pointer-enter delivery](https://github.com/qt/qtbase/blob/6.11/src/plugins/platforms/wayland/qwaylandinputdevice.cpp)
  and [Niri pointer refresh](https://github.com/niri-wm/niri/blob/v26.04/src/niri.rs)

Upstream code was studied for contracts, not copied. No Plasma library dependency
or cliphist code is bundled.

Current layout/settings changes: [UI refinements](ui-refinement.md).
