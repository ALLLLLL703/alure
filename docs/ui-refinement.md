# UI refinements

## Settings startup fix

The left-label/right-control conversion introduced width feedback: relative
control width entered the containing layout's implicit size, and a hidden panel
selector Flow contained an item bound to the Flow's own width. Even while hidden,
that Flow kept scheduling positioning and changing its implicit size. The GUI
thread remained in `QQuickWindowPrivate::polishItems` / positioner and text layout,
preventing the window and even the exit timer from progressing.

Fields and selectors now use explicit-width Item boundaries: the outer layout
assigns width; only measured content height feeds back. The panel selector uses
a ColumnLayout with a separate button Flow, retaining the left/right form design.

Verified with isolated offscreen runs: the previous 500ms-exit startup hit an 8s
external deadline; the fixed build exits normally without layout warnings, with
both defaults and the user's configuration (read-only). One-second idle samples
reported 0 CPU ticks in both runs. `UiTest::settingsLayoutSettles` passes at
900×680, 400×500 and 1280×800, visiting appearance categories, panels and every
module, resizing, and checking event-loop heartbeat and right-hand control bounds.
No full CTest suite was run for this targeted fix.

## Bar and history

- Clipboard images now appear **inside history entries**, not in a separate
  preview pane. Visible delegates request bounded, memory-only thumbnails;
  copying still decodes the original bytes afresh. Closing releases the cache.
- Clipboard is enabled and placed on the bar by default. Existing explicit
  module lists and `enabled=false` choices are retained; move Clipboard into a
  panel slot in settings to place and enable it.
- Album covers use aspect-fit, showing the entire cover without cropping.
- Workspace numbers and their underline share the same horizontal center.
  Three-zone layouts center on the actual bar viewport, not an oversized scroll
  canvas; side groups scroll independently when too long.
- Module dropdowns have no header X. Escape, source-button toggle and native
  outside dismissal still apply. Action/navigation glyphs use SVG assets, not
  font symbols. Application-supplied tray/notification images remain supported.

New TOML fields (all apply on reload):

```toml
[modules.clipboard.behavior]
inline_image_height = 160 # integer 48..512 logical pixels
inline_text_lines = 8     # integer 2..64, expanded selected text
preview_cache_items = 24  # integer 1..128, maximum memory thumbnails

[modules.media.style]
opacity = 1.0 # number 0..1; available independently on EVERY module
```

`style.opacity` affects that module's content and dropdown as a whole; theme
background opacity remains a separate setting. Each `[[panels]]` also accepts
`window_gap = 0` (integer -256..256). It adjusts the layer-shell reservation,
not the dropdown margin. Negative values can compensate Niri's own layout gaps
without modifying Niri configuration; extreme negative values can overlap tiled
windows. `exclusive_zone=0` still explicitly means no reserved screen area.

Implementation references: Qt Quick Image PreserveAspectFit, Drag/DropArea,
https://doc.qt.io/qt-6/qml-qtquick-drag.html .

Delivery validation: compilation only, as requested; no CTest or live UI run.

## Settings and invocation

The sidebar now has a MODULES divider and a separate page for every module.
Each page combines appearance, feature controls and its integration settings;
notification banner fields live on the Notifications page. Theme selection is
one dropdown. Form labels sit on the left, controls in the right-hand 56%.

Panels have a block palette and one slot per active zone (one for linear mode).
Drag into a slot to place/enable, beside a block to order, or back to the palette
to remove. Both the palette and placed modules use compact square SVG-icon blocks
rather than shortened names; they follow the module's configured `style.icon` and
the theme icon size. Full names remain available as accessible names. Fixed and
flexible spacers have distinct SVG icons and remain repeatable. All operations edit the TOML draft; the
bottom **Save & apply** remains the publication point.

Command fields accept ordinary command text such as
`kitty sh -c 'sudo pacman -Syu'`, with single/double quotes and backslash escapes.
The editor converts this to TOML argv internally. Unfinished quoting blocks
Save; no shell expansion, pipes or substitutions run implicitly. The raw TOML
editor remains available separately.

`alure --settings` has one instance per session bus. Further invocations raise
that editor without replacing its draft/config path. `alure --clipboard` is a
single-instance toggle: a second invocation closes the existing layer rather
than creating another. These modes require the session DBus for arbitration.

## Notifications

Banner defaults are now 320×108, with 8px padding, 4px spacing and 12px text.
The Notifications sidebar page contains all banner fields; the existing
`[ui.toast]` TOML location is retained so older configurations still work.
New fields there: `padding=8` (integer 0..64), `spacing=4` (0..32),
`font_size=12` (6..72), `show_icon=true` (boolean), `icon_size=32` (8..128),
`body_lines=2` (1..8). Existing explicit width/height values remain respected.

`[modules.notifications.behavior]` adds:

```toml
focus_on_click = true
niri_socket = "" # empty uses NIRI_SOCKET; otherwise absolute path
icon_max_pixels = 1048576 # integer 1024..16777216
icon_cache_kib = 8192 # integer 64..65536
```

All apply on reload without resetting notification history. Clicking a banner
(or Open sender in history) sends its advertised `default` action, then, when
`focus_on_click=true`, requests Niri Windows/FocusWindow over the socket. Matching
prefers the sender PID obtained from the session bus, falling back to exact,
case-folded desktop-entry/app_id (appName only if desktop-entry is absent).
Multiple matches require an already-focused matching window; otherwise Alure
logs a diagnostic rather than selecting an unrelated window. The default action
lets applications open the relevant conversation themselves. No shell command,
window-title substring matching or application launching is synthesized.
`allow_actions=false` disables these interactions.

Notification icons now accept the `(iiibiiay)` image-data structure (including
legacy image_data/icon_data), local file/image-path hints, and theme names.
RGB/RGBA, alpha, padded row stride and a short final row are handled explicitly.
Dimensions/data size are bounded; decoded thumbnails fit 128×128 in a capped
memory cache. Remote image URLs are not fetched. This supports application-sent
chat/group avatars without substituting a font glyph. App-supplied raster avatars
and tray images are content, not replacements for Alure's SVG UI glyphs.

Protocol references:
- https://specifications.freedesktop.org/notification-spec/latest/icons-and-images.html
- https://specifications.freedesktop.org/notification-spec/latest/hints.html
- https://github.com/niri-wm/niri/blob/main/niri-ipc/src/lib.rs

No live QQ/Niri notification assertion is made for this delivery: the requested
verification boundary remains compilation and static QML parsing only.
