# Alure v0.1 interface

The panel now renders all ten real module contracts. No demonstration values,
wallpaper, compositor replacement, new Wi-Fi credentials or automatic upgrades
are introduced. Read [services.md](services.md) for provider limitations, polling
and direct-child command timeouts. Missing providers stay visibly unavailable;
click for diagnostics, Refresh and integration settings. Notification serving
still requires explicit opt-in and never replaces another daemon.

## Panel and interaction model

- Ordered `panels[].modules` are shown when their module is enabled. Horizontal
  panels show configured icon/text; vertical panels compact to icons, except
  workspace pills retain their configured labels. A scrollable strip handles
  overflow rather than overlapping adjacent modules. Drag/scroll its scrollbar;
  text elides at each module's maximum width. All geometry is in logical pixels.
- Workspace pills show observed names (index fallback), active state and stable-ID
  activation. Right click opens details listing outputs and IDs' workspaces.
- Media displays the first observed player in the bar and **all** players with
  capability-checked transport in details. No artwork URLs are downloaded.
- Tray displays service-provided PNGs, otherwise freedesktop icon-theme names
  (attention name when provided), with original fallback. Left/middle click sends
  primary/secondary actions. Right click (also left click for ItemIsMenu) opens an
  Alure DBusMenu dropdown anchored to the icon, not a provider-owned window.
  Theme resolution for *application tray icons* applies even in built-in mode.
  Submenus navigate inside the dropdown with Back/Left; Up/Down and Enter select.
  Disabled/hidden entries, separators, checkmarks/radio states and live menu updates
  are supported. A missing menu is reported in place; no detached-window fallback.
- Sound exposes default-sink volume and mute. Movement submits the configured
  debounced setter, with controls disabled while the service is busy. No mixer.
- Updates displays the count/list and an explicit inline confirmation before
  running a nonempty configured update argv. The timeout also applies to that
  command: prefer a deliberately configured terminal; do not run a long-lived
  package manager directly unless accepting that timeout.
- Wi-Fi shows cached APs, radio control and connect-by-observed saved profile.
  Bluetooth shows adapter power and paired-device connection controls. No new
  passwords, pairing, trust, forced scans or background control actions.
  Their action buttons are icon-only: the bundled forward chevron (`next`) means
  connect, the cross (`close`) means disconnect; the Wi-Fi/Bluetooth radio glyph
  toggles power, accented while powered. Saved-profile, adapter and device names
  remain visible labels. Hover text and accessible names describe the exact action
  and target. Buttons retain action permissions and command requirements; Wi-Fi
  still only exposes saved-profile connection, not a new disconnect operation.
  Sizing uses `theme.icon_size`, `ui.module_height` and existing padding/spacing;
  hover text follows `settings.module_tooltips` and `module_tooltip_delay_ms`.
  Changes apply with configuration reload; no extra presentation toggle is needed.
- Module popups omit the routine healthy-provider status line without reserving
  a blank row. Actual errors, unavailable/refreshing state and pending volume
  feedback remain visible; successful provider data is shown directly.
- Notification history is newest first, plain text, with DND, dismiss, app actions
  and clear history. Opt-out/history-empty/backend failure are not conflated.
- Calendar uses real local dates, a 42-cell month grid, adjacent-month dates,
  selected/today highlights, previous/next month and Today. Clock/date formatting
  uses Qt local-time patterns. No events, time-zone chooser or holiday provider.
- Battery shows the first readable battery in the bar and every readable battery
  with status in details. No aggregation, estimates or power-profile controls.

A settings icon starts a **separate** normal-window process with the same config
path, direct argv and no shell. It never acquires service bus names. No application
launcher, configurable arbitrary keyboard shortcuts, translation catalogue or
per-control stylesheet is claimed. Standard Qt editor/control keys apply; Escape
closing shell details is configurable. UI English labels and derived contrast,
hover/disabled alpha ratios are design tokens, not separately editable strings.

### Popup surfaces and banners

Details are native **xdg_popup** `QQuickView` windows transient to the actual
clicked module's layer-shell panel, not another screen-anchored layer. Top bars
open below the module; bottom above, left to the right, right to the left. The
visible clicked button (including workspace/tray list entries after both nested
scroll offsets and clipping) provides the local anchor. Other bars' reservations,
parent margins and compositor placement do not need guessed global coordinates.
Bars remain non-focusable and their corrected exclusive reservation is unchanged.

`ui.popup_alignment` defaults to `center`; `start` / `end` align the outer popup
bounds with the clicked span's left/right for up/down, top/bottom for left/right.
`ui.popup_direction` defaults to `inward`, or explicitly chooses top/bottom/left/right.
The compositor can flip either axis or slide the popup to stay on screen; near a
corner it may therefore no longer be centered. Different distant modules provide
different anchors even on the same panel. Native constraint resolution, not a
client-side screen-position guess, is authoritative.

`popup_width` / `popup_height` describe card content extent. `popup_gap` is transparent
padding on **each** surface side: the near-side visible card has exactly one gap
from the panel, including after flips (not twice the gap). Center alignment also
centers the card; start/end leave one gap inset from the outer aligned bounds.
The whole surface including padding is capped to the output. Gap is additionally
capped to one quarter of the smaller output dimension to retain content space.
Small outputs/extreme sizes cannot guarantee usable content. Padding remains part
of the popup's input region, not an additional outside-click target.

One details surface is visible at a time. Close button and configurable Escape
close it. **Wayland's grabbing popup always permits compositor outside dismissal**;
`close_on_focus_loss=false` disables only Alure's additional focus-loss dismissal,
not the native outside-click/grab policy. Settings help states this boundary.
A dismissed popup releases its native surface/grab; clicking the bar again creates
a fresh one. Closing it does not quit the persistent shell. Toasts remain separately
screen-anchored, non-focusable overlay layers; settings remain a normal separate
process. Config/output changes cancel queued requests and destroy popup children
before rebuilding panel parents. Requests without a live clicked source, or with
a mismatched panel/output, are safely ignored rather than guessed.

Hover uses restrained color feedback, not Qt Controls popup tooltips: testing
found their overlay could steal the initiating panel press. Accessible names and
descriptions retain full summaries; clicking opens the full text. Nested-Niri single-output
focus/layer/alpha checks are recorded below; physical multi-output, scaling/hotplug
and the corrected exclusive-zone behavior still require live verification.

With notification serving opted in, active unsuppressed notifications create one
non-focusable top/bottom-right toast on the selected output. The latest arrival
replaces the previous banner; there is no animated queue or duplicate per-panel
banner. It hides on configured display duration, provider expiry/dismissal, or ×.
Hiding a banner does not dismiss notification history. Actions/full bodies live in
history. DND prevents new banners; changing DND does not retroactively mark an
existing notification suppressed. No sound, markup or remote icon URLs are loaded.
Raw notification image hints, local image files and themed icons are supported;
clicking invokes the default action and can focus the sender through Niri IPC.
See [current UI refinements](ui-refinement.md) for limits and configuration. `expiresAt=0` can remain active in history after the bounded banner
hides. No panel is needed for receipt, but place the notifications module on a
panel to reach history. An unmatched toast output produces no banner.

## Settings: one draft, no silent rewrites

Sidebar sections: Appearance (Theme & typography, Layout & popups, Settings window),
Panels (drag-and-drop slots and fields), Configuration (advanced complete TOML),
and a MODULES divider followed by a page for each module. Each module page combines
style, feature controls and integration. Notifications also contains banner settings. Theme controls retain their
priority order; remaining fields sort lexically by full TOML path, keeping nested
fields such as panel margins together under a single heading. This is presentation
only: browsing does not reorder or rewrite the TOML source.
Common values use real controls:

- A theme dropdown lists the built-in TOML palettes; Preview applies
  the staged theme to this window only. Existing explicit palette overrides remain.
- Sliders plus exact numeric editors cover opacity, font/icon size, spacing/radius,
  panel dimensions/margins and other numeric settings. C++ validation is authoritative;
  the form metadata mirrors its ranges. Sliders stage while moving and retain their
  delegate/focus throughout a drag; no raw/model replacement occurs on movement.
  Selected switches and slider fills use the theme accent for visibility.
- Switches edit booleans; ComboBoxes edit enum choices and installed font families.
  Font families can also be typed. Normal strings need **no TOML quotes**; escaping
  is internal. Command fields accept ordinary quoted command text and convert it
  to argv internally; unfinished quotes block Save. Labels and controls share a row.
- Colors have swatches, validated hex/Qt color text and Qt's nonnative Quick color
  picker (system QuickDialogs2). Cancel does not change the draft. Accept stages the
  selected color. Empty module colors inherit; empty global palette colors are invalid.
  Controls/popups use the configured theme palette, including disabled text roles.
- Drag module blocks into panel slots to place/enable them, beside blocks to order,
  or back into the palette to remove. Each module's own page also has its global enable switch.

Apply fields/Enter flushes staged edits into the raw draft without writing disk.
Section/category/module/panel navigation, Preview and Save also flush focused input;
invalid input blocks that transition and remains editable. Raw TOML remains the
full-fidelity fallback. Opening/browsing the UI does not materialize default keys,
remove unknown values or change comments. An unrelated theme Preview does not reset
a user/compositor-supplied window size; actual configured width/height changes do.
Navigating to a different section/category/module/panel resets only that form scroll
to the top; changing a staged value does not reset scrolling.

- Inspecting/editing forms validates against the shared model but does not write.
  Syntax-invalid raw drafts keep their exact text and show no misleading forms.
- Preview draft validates and applies styling **only inside settings**, without
  writing or launching a shell/services. Save & apply validates then atomically
  saves; the shell watcher publishes a valid reload. With `runtime.watch=false`,
  restart panels instead. Settings never silently reloads external changes.
- Close/Reload asks Save/Discard/Cancel for raw or pending form edits. Save failure
  (including external-file conflict) leaves the draft and window available.
- Scalar/array field edits use toml++ source spans, translating Unicode codepoint
  columns to QString offsets. Unchanged text/comments are retained. Simple missing
  fields can be inserted as dotted keys under the nearest unambiguous explicit
  bare ancestor header (or at root), including successive implicit palette edits.
  Existing inline-table leaves, such as default margins, are replaced by exact
  source span. If the source omits panels, editing inherited panel 0 appends only
  its edited leaf in a new `[[panels]]` table; other panel defaults remain inherited.
  Higher nonexistent indices and explicit `panels=[]` do not invent panels.
  Adding new inline members and quoted/ambiguous table syntax is
  rejected without mutation; use raw TOML. No whole-document canonicalization occurs.
- Panel operations require an explicit notice: **only panel regions may lose
  comments/formatting**. Unknown/nested panel values, including TOML date nodes,
  are kept semantically. Other sections and trailing comments are preserved.
  Header-looking lines inside multiline strings are not section boundaries.
  Quoted table syntax may be rejected. The transformed draft is validated before
  publication; disk stays unchanged until atomic Save. Removing a panel deliberately
  removes that panel's keys. Preserve panel comments by using the raw editor.

The foundation's locking, symlink rejection, 1 MiB document cap, before-write/
before-commit conflict comparisons and small non-cooperating writer race still
apply. See [configuration.md](configuration.md). Settings Basic/Fusion controls
style is chosen before loading QML and requires process restart to change.

Two pre-existing editing limitations remain: forms apply pending fields sequentially,
so coupled bounds such as `min_width`/`max_width` may need the relaxed bound applied
first even when the final pair would be valid. Quoted/unsupported structures should
be edited in Configuration **before** staging form changes: a rejected pending form
edit can also block navigation there. Reload → Discard clears the draft explicitly;
retain any wanted unsaved edits elsewhere before doing that. This update does not
introduce a transactional multi-field editor or silently discard rejected edits.

## Configuration coverage and defaults

Canonical complete example: `config/default.toml`; explicit corner spacing and
per-output definitions: `config/four-edge.toml`. Existing multi-panel/service
examples remain valid. Unknown modules have no plugin provider and show honest
unavailable details. Deprecated `foundation.*` keys are accepted for compatibility
but no longer render anything.

| Key | Default / bounds / effect |
|---|---|
| `runtime.trace_windows` | false boolean; opt-in stderr open/hide/focus/rebuild diagnostics |
| `theme.icon_theme` | empty string keeps original desktop QIcon theme; a nonempty name overrides it in panel mode; valid reload rebuilds icons |
| `settings.controls_style` | `Basic`, or `Fusion`; selected at process start only |
| `ui.panel_padding` | 6, integer 0..128; panel inset and button padding, clamped on thin panels |
| `ui.module_height` | 30, integer 16..256; buttons/vertical rows; horizontal chips fit bar cross extent |
| `ui.popup_width`, `popup_height` | 440 / 560, integers 240..1920 / 240..2160; visible card size, whole surface including gap capped to output |
| `ui.popup_gap` | 8, integer 0..256; symmetric transparent details padding, capped to quarter of smaller output dimension; existing toast opposite-edge inset unchanged |
| `ui.popup_alignment` | `center`, or `start` / `end` string; along chosen edge, outer popup bounds relative to visible clicked span |
| `ui.popup_direction` | `inward`, or `top` / `bottom` / `left` / `right` string; compositor may flip/slide |
| `ui.animation_ms` | 140, integer 0..2000; hover/press color transition; 0 disables it |
| `ui.close_on_focus_loss`, `escape_closes` | true booleans; additional focus-loss / Escape dismissal; native Wayland outside dismissal is mandatory |
| `ui.show_settings`, `settings_icon` | true / `settings`; launcher visibility and validated icon name |
| `ui.toast.enabled` | true; independent banner switch (notification serving remains opt-in) |
| `ui.toast.output`, `edge` | `primary`, `top`; exact QScreen name or primary, edge top/bottom only |
| `ui.toast.width`, `height`, `margin` | 320 / 108 / 56; integers 240..1920 / 80..1080 / 0..4096 |
| `ui.toast.duration_ms` | 5000, integer 100..600000; display lifetime, independent of protocol expiry |
| `modules.*.style.opacity` | 1.0, number 0..1; independent module content/dropdown opacity |
| `modules.*.style.icon` | matching original module icon name; name characters letters/digits/underscore/dot/hyphen, no paths |
| `modules.*.style.icon_size` | 18, integer 8..128; per-module size |
| `modules.*.style.min_width`, `max_width` | 30 / 140 (240 for media/calendar/workspaces/tray); integers 16..1024 / 16..2048, min<=max; max also bounds vertical workspace/tray strip length |
| `modules.*.style.foreground`, `background` | empty inherits theme text / transparent chip; otherwise valid Qt color string |
| `modules.*.style.show_icon`, `show_label` | existing booleans; true except tray label false; workspace list chips honor configured icons even without labels; tray keeps provider pixmap/theme resolution; vertical general labels intentionally compacted |
| `modules.workspaces.behavior.ordering` | `output-index` or `provider` string; sorted output/numeric idx/numeric ID or incoming IPC order, shared by panel/details; valid reload starts a new workspace request |
| `modules.*.behavior.popup_enabled` | true; disables opening details, not direct workspace/tray controls |
| `modules.notifications.behavior.toast_enabled` | true, module-specific banner switch |
| `modules.calendar.behavior.first_day_of_week` | 1 Monday; integer 0 Sunday / 1 Monday |
| `modules.calendar.behavior.month_format` | `MMMM yyyy`, Qt date pattern |
| `modules.calendar.behavior.allow_actions` | true; month navigation/date selection/Today switch |

Presentation-only format/popup_enabled/toast_enabled changes do not reset service
snapshots or notification history. All added known values validate types and ranges
at startup/reload/save. Other than
controls_style, they apply on valid reload, not a restart. `theme.font`, font_size,
spacing, padding, radius, border, opacity, palette and icon mode remain shared.
Module icon colors are original SVG colors, not palette-recolored; foreground
only affects text. No per-control CSS or arbitrary asset-path loader is promised.

`behavior.format` uses `{field}` replacement for module summaries (unknown fields
become `—`). Workspaces: name/idx/id/output; media: title/artist/album/playbackStatus
(first player); tray: title; volume: percent/muted; updates: count; Wi-Fi:
ssid/signal/connected/powered/count; Bluetooth: status/connectedCount;
notifications: count/activeCount/dnd; battery: name/percent/status. Empty or
unavailable snapshots never substitute fake numeric zero. Calendar's format is a
**Qt date-time pattern**, not field substitution; interval_ms drives its local
clock. Detail labels/cards use documented fixed layouts rather than arbitrary
template markup. Read commands, intervals, limits, timeouts and action permissions
remain in the service schema. Native-service command arrays stay reserved/unused.

## Research and verification

Sources inspected (design/protocol evidence only; no assets or code copied):

- `gh repo view wayle-rs/wayle --json name,description,url,licenseInfo` and upstream
  README through `gh api`/curl: https://github.com/wayle-rs/wayle (MIT). Inspiration:
  compact configurable bar, separate settings and cohesive theme tokens; no GTK
  dependency, wallpaper feature or upstream asset imported.
- `gh api repos/KDE/plasma-workspace/contents/applets/digital-clock/CalendarView.qml`
  and raw contents: https://github.com/KDE/plasma-workspace/blob/master/applets/digital-clock/CalendarView.qml
  (GPL-2.0-or-later, inspected only). Pattern: month navigation, explicit date
  context and consistent spacing; this implementation uses independent local-date
  JavaScript and Qt Controls, not Plasma/Kirigami code. Old package/contents/ui
  paths under plasma-desktop/workspace returned 404, then current path was found.
- https://doc.qt.io/qt-6/qml-qtquick-controls-popup.html fetched via curl; actual
  tests revealed panel tooltip input capture, so passive hover feedback is used.
- https://doc.qt.io/qt-6/qquickwindow.html and installed
  `/usr/include/LayerShellQt/window.h`: alpha requested before windows; explicit
  noninteractive bars. Historical overlay details used on-demand auxiliary focus
  and zone -1; native details now use transient xdg_popup (see below). Toasts retain
  independent overlay placement and zone -1.
  LayerShellQt upstream: https://github.com/KDE/layer-shell-qt.
- Stabilization review inspected LayerShellQt `src/qwaylandlayersurface.cpp`
  (`setExclusiveZone`/`setMargins` pass through unchanged) and Smithay
  `src/desktop/wayland/layer.rs` (`arrange` reserves amount + anchored margin):
  https://github.com/Smithay/smithay/blob/master/src/desktop/wayland/layer.rs.
  Automatic panel zones therefore send clamped thickness only; placement tests
  assert the wire zone separately from total reservation.

Automated checks include ConfigEditing source-preservation/conflict regressions,
QuickUi fixtures loading **every** popup in available/unavailable states, horizontal/
vertical panel and toast instantiation, 64-pixel custom row height with 32-pixel
horizontal workspace/tray delegates, icon-only workspace and tray pixmap/attention
icon resolution, fake action forwarding, settings draft/save,
leap-month math, and a real offscreen mouse click after hover through PanelHost to
calendar popup plus Escape dismissal. No fixture performs an actual desktop
control. Offscreen/software rendering is not evidence of compositor visuals.
See the stage delivery artifact for exact build/test/install commands and commit.

## Bounded live validation reported by parent

The parent used computer-use on a temporary **nested Sway Wayland** desktop,
not the user's native Niri session. It rendered the real layer panel and calendar.
Settings form `theme.name` changed to forest; Preview changed settings only, then
Save applied and the live bar updated. With `/tmp/alure-ui-isolated` configuration
and default close_on_focus_loss=true / escape_closes=true, calendar opened, next
month displayed October 2026, and Escape dismissed details. Auxiliary zone -1
corrected its top offset from y120 to y60 in that session. The nested desktop was
then stopped by its owner. This was supplemented by the nested-Niri baseline
check below, not a user-session or physical multi-output test.

An earlier config directly in `/tmp` exposed a real bug: unrelated temporary files
triggered directory watching, identical reloads emitted modelChanged and rebuilt
panels, hiding details. Effective-model/source deduplication now has an automated
sibling-file/comment-only regression, and the parent repeated the sibling-file
case live under nested Niri. QML tooltip press interception and an undefined QML
screen property were separately reproduced and fixed through end-to-end mouse
input tests.

Supplemental parent computer-use validation ran a copied immutable **8c384d1**
binary on **nested Niri 26.04**, under Sway with private DBus and **without
`--session`**. `/tmp/alure-niri-validation/layers.json` confirmed four actual Top
bars with keyboard interactivity None. Clicking Web focused stable workspace ID 3
(`workspaces.json`); calendar advanced September to October. Creating sibling
`unrelated.marker` did not hide the popup. Changing `theme.opacity` from .92 to .4
visibly blended the configured `#426478` compositor background through the panels.
Screenshot: `/tmp/alure-niri-validation/four-edge-calendar.png`; temporary config
and logs reside alongside it. Both nested desktops were stopped by their owners.

### Stabilization live recheck — `32af9d9`

The parent then independently rebuilt and reran all six CTest suites: **6/6 passed**
(8.79 seconds). Targeted independent source review found all four stabilization
findings resolved. A copied immutable `32af9d9` binary was tested in a new nested
Niri 26.04 session, again inside computer-use Sway with a private DBus and without
`--session`. Only the Niri workspace and local calendar modules were enabled;
all test configuration and binaries were under `/tmp/alure-final-niri`.

- With zero compositor layout gaps/borders and one top bar of thickness 44 plus
  top margin 8, Niri reported output size **973×1182** and a full-height tiled
  settings window of **973×1130**. The measured reservation is **52**, confirming
  that the anchored margin is no longer counted twice. Evidence: `outputs.json`,
  `windows.json`, `reservation-config.toml` and `reservation-and-workspaces.png`
  in that temporary directory.
- With `ui.module_height=64`, the 44-pixel bar still displayed horizontal chips
  without the previous vertical clipping. For this check, workspace `max_width`
  was increased to 1000 to expose the whole list rather than scroll it.
- Workspace labels appeared in numeric order: Development, Music, Web, 4.
  Reloading `show_label=false, show_icon=true` rendered four nonblank icon chips.
  Clicking the second chip eventually focused Music (stable ID 2), confirmed by
  `icon-only-workspaces.json` and the corresponding PNG. An immediate first-click
  query still showed the original workspace; after moving the pointer and clicking
  again, the focused state was confirmed. This is not a first-click latency claim.
- The normal settings process remained inside the reserved work area. Both nested
  compositors and their test applications were stopped after the check.

These are nested single-output observations, not physical multi-monitor, hotplug
or fractional-scaling coverage. Live Bluetooth/media/tray/notification
interoperability and systemd login/logout lifecycle remain unverified. Unit
installation and syntax checks are not an enabled-service test. Polling, DBusMenu,
pairing-agent and calendar-provider limitations in [services.md](services.md)
remain. Temporary screenshot/log paths above are local evidence, not committed
build products or portable test fixtures.

### Settings close lifecycle

The header **Close** button is available even on undecorated compositor windows.
It, the configured shortcut (default Ctrl+W), and compositor close events share the
same unsaved-draft guard. Save validates and checks disk conflicts; failures leave
the modal and draft open. Cancel returns to editing. Discard never writes disk.
Successful Save/Discard closes the modal first, then defers the window close to
avoid closing recursively inside a modal callback. Settings retains Qt's normal
last-window-closed process exit policy; it does not force quit or discard edits.

Regressions: `QuickUi` sends real close/key/pointer events and clicks modal buttons;
`SettingsCloseE2e` (when system Xvfb and xdotool exist) launches a private X server
and the actual settings process without a quit timer, exercising clean shortcut
close and dirty Discard. It never targets the host desktop. This is not Niri
validation, nor proof of the original user's compositor-specific failure cause.

Official references consulted (2026-07-17; no copied implementation):
- https://doc.qt.io/qt-6/qml-qtquick-controls-dialog.html (automatic accept/reject roles)
- https://doc.qt.io/qt-6/qguiapplication.html#quitOnLastWindowClosed-prop


### Typed-settings regression evidence

`QuickUi` exercises real theme/switch/ComboBox/slider/text events, including a mouse
hold longer than the parser delay, keyboard slider movement, focused numeric input
on Preview, quote/backslash serialization, color picker Cancel and actual OK button,
invalid hex Save retention, module enable/reorder with custom names and unknown panel
values, Save/reload, and the complete close modal matrix. It also resizes the window
before theme Preview and asserts that size survives. `ConfigEditing` covers sequential
palette insertions under explicit/implicit ancestors and source-exact inline leaves;
unsafe insertions return the byte-identical original. These automated tests do not
claim native Wayland/compositor interaction validation.

`QuickUi::settingsFieldOrdering` clicks Panels/Appearance repeatedly and inspects
real QML delegates for exact panel ordering, four contiguous margin fields, one
visible margins heading and retained theme priority. It covers inherited defaults
and an explicit custom panel without modifying the saved source. Both rows failed
against the prior comparator and pass with the lexical tie-break.

Official widget sources consulted (no copied implementation):
- https://doc.qt.io/qt-6/qtqml-javascript-hostenvironment.html (QML JavaScript built-ins;
  full-path comparison avoids relying on equal-priority sort stability)
- https://doc.qt.io/qt-6/qml-qtquick-controls-slider.html (`moved`, drag updates)
- https://doc.qt.io/qt-6/qml-qtquick-dialogs-colordialog.html (accepted/cancel, nonnative option)
- https://github.com/qt/qtdeclarative/blob/v6.11.2/src/quickdialogs/quickdialogsquickimpl/qml/ColorDialog.qml
  (installed Qt 6.11.2 picker object names/default button used in regression tests;
  source declares Qt commercial/LGPL/GPL licensing; consulted, not copied)

### Native popup implementation and verification

Build prerequisites are now Qt **6.9+** and LayerShellQt **6.6+**. Source investigation
confirmed Qt 6.5 and 6.8 did not have the explicit xdg-positioner overrides; Qt 6.9.0
has all four. LayerShellQt 6.3 already attaches an xdg_popup role to a layer parent,
but this project also uses `setScreen` / `setDesiredSize` from its 6.6 API.
Build/tests used installed Qt **6.11.2**, LayerShellQt **6.7.5**. Older-minimum source
inspection is not an older-version runtime claim. No dependency downloads/installs.

Official upstream source read via curl; independently implemented, no copied code:
- https://github.com/qt/qtwayland/blob/v6.9.0/src/plugins/shellintegration/xdg-shell/qwaylandxdgshell.cpp
  (`createPositioner` reads `_q_waylandPopupAnchorRect`, `Anchor`, `Gravity`,
  `ConstraintAdjustment`; private dynamic property API isolated in PopupPlacement).
- https://github.com/qt/qtbase/blob/v6.11.2/src/plugins/platforms/wayland/plugins/shellintegration/xdg-shell/qwaylandxdgshell.cpp
  (same properties, null xdg parent supported, Qt::Popup grabs; popup_done closes).
- https://github.com/qt/qtbase/blob/v6.11.2/src/plugins/platforms/wayland/qwaylandwindow.cpp
  (`addChildPopup` calls parent's shell `attachPopup`; hides reset the surface role).
- https://github.com/KDE/layer-shell-qt/blob/v6.6.0/src/qwaylandlayersurface.cpp
  (`attachPopup` obtains xdg_popup role and calls layer `get_popup`).
- https://github.com/KDE/layer-shell-qt/blob/master/src/interfaces/window.cpp
  (`Window::get` selects layer-shell per window; process-wide legacy shell selection
  is deliberately removed so module popups keep the default xdg-shell integration).
- Installed generated xdg-shell protocol header documents that anchor rectangles
  must stay inside parent geometry; transparent symmetric padding supplies gap
  without violating this constraint. Qt's override does not expose positioner offset.

All coordinates are logical pixels. Qt/compositor own output scale conversion and
native constraint placement; rounded clipped item bounds can differ by one logical
pixel. No global-coordinate positioning fallback is used on Wayland. Non-Wayland
`--preview` uses known global positions and screen clamps only for preview/tests.
These private Qt overrides require revalidation on Qt upgrades. Fractional scaling,
physical multi-output/hotplug and exotic transformed items are not runtime-verified
by offscreen tests; ordinary panel/list translations are covered.

Automated regression additions:
- PanelHostContract: all four edges × start/center/end × beginning/middle/end,
  local anchor containment, explicit directions, narrow/one-pixel outputs and
  maximum gap, flip/slide request configuration; existing reservation tests retained.
- QuickUi: actual pointer clicks on three different module positions per edge,
  native transient/type/positioner properties, Escape, actual Close widget and
  native close events, repeated reopening without last-window-closed; nested-scrolled
  workspace and tray entries on both axes. Direct pointer events without pumping
  queued callbacks cover reload, parent/source destruction and screen-removal signal
  invalidation. The offscreen removal signal is simulated, not real QScreen hotplug.
- ConfigStore: defaults, custom enums/gap/dismissal values and invalid type/value
  diagnostics. Source-preserving/atomic save implementation is untouched.

Automated tests alone do not establish Niri rendering, grabs or scaling behavior.

### Parent compositor verification of the settings/popup update

Verified in a private computer-use Sway desktop containing nested **Niri 26.04**,
without `--session`, using temporary TOML files and a private D-Bus. No host Niri
configuration, desktop component, notification ownership or device state was changed.

- Final `13f7326` binary SHA256:
  `bcb478b3f664430924a9ca3e3ae55613995acdc566848fd7a06f9a7e347953a3`.
  Parent independently configured/built Debug, reran all **7/7 CTest suites** and
  validated all five `config/*.toml` examples. Optional Vulkan headers were absent;
  configuration/build succeeded without them.
- Settings header Close exited the actual window and process. Earlier close-stage
  native checks also covered invalid Save retaining its modal/error, Cancel returning
  to editing and Discard exiting with an unchanged on-disk hash. The old baseline
  already responded to compositor close in isolated Sway; no universal original
  close-handler failure is claimed.
- Native typed-widget checks on `ad7260f` exercised theme selection, opacity dragging,
  color-picker acceptance, two successive palette changes, inherited-panel module
  reorder/disable and a subsequent margin edit. They persisted valid sparse TOML
  without removing the source comment. Preview retained the compositor's 957×1166
  window size; highlighted picker text and selected tracks were legible. Final
  `13f7326` visually confirmed ordered panel fields and a single contiguous margins
  group. Native keyboard injection through two compositors was unreliable, so
  Ctrl+W/Escape are covered by automated event/Xvfb tests, not claimed native checks.
- Final native popup checks exercised all four panel edges. For a 360×420 card and
  gap 12, the protocol surface was 384×444. Niri configured parent-local coordinates
  north `(-8,46)`, south `(-8,-444)`, west `(52,-122)`, east `(-384,-122)` on a
  973×1182 output. Visible cards opened inward with the configured normal gap;
  near-edge positions were constrained to the output. Protocol logs confirm
  `xdg_surface.get_popup` followed by the **originating** layer surface's `get_popup`.
  Native outside `popup_done` dismissed each card; reopening preserved the shell
  process. No QML/protocol errors were found in those popup logs.
- Earlier popup-stage native checks additionally compared two distinct top modules
  (different horizontal positions), clicked calendar next-month and Close, changed
  TOML while a popup was open, and exercised `start` alignment, explicit direction
  with compositor flip, and maximum size/gap. A requested 1920×2160 with gap 256
  was capped to the 973×1182 output. Transparent padding remains part of the native
  popup input region; clicking it is not an outside-surface click. Close remains
  available even when an extreme configuration covers the output.

Local evidence is under `/tmp/alure-close-recheck/`, `/tmp/alure-widgets-final/`,
`/tmp/alure-popups-final/` and `/tmp/alure-ux-final/` (PNG captures, temporary TOML,
geometry and protocol logs). These are temporary local evidence, not committed
fixtures. All test desktops were stopped afterward. Physical multi-monitor/hotplug,
fractional scaling, native keyboard shortcuts and systemd login/logout lifecycle
remain outside this compositor verification.
