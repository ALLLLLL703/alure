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
  (attention name when provided), with original fallback. Left/middle/right click
  sends primary/secondary/menu actions with global coordinates. Theme resolution
  for *application tray icons* is used even with built-in shell icons selected.
  Native DBusMenu layout rendering is not implemented; some menus need another host.
- Sound exposes default-sink volume and mute. Movement submits the configured
  debounced setter, with controls disabled while the service is busy. No mixer.
- Updates displays the count/list and an explicit inline confirmation before
  running a nonempty configured update argv. The timeout also applies to that
  command: prefer a deliberately configured terminal; do not run a long-lived
  package manager directly unless accepting that timeout.
- Wi-Fi shows cached APs, radio control and connect-by-observed saved profile.
  Bluetooth shows adapter power and paired-device connection controls. No new
  passwords, pairing, trust, forced scans or background control actions.
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

Details are independent, bounded `QQuickView` surfaces: overlay layer, exclusive
zone -1 (reserve nothing and ignore existing reservations), keyboard-on-demand and activate-on-show. Bars remain non-focusable.
Horizontal details align to the right, beyond the bar edge; vertical details align
to the relevant edge/top, beyond the bar. They are **not positioned at the clicked
module**. One details surface is visible at a time; close button, configurable
Escape and configurable focus-loss dismissal are provided. Outside clicks dismiss
when they move keyboard focus; this is not a full-screen pointer-grab overlay.
Clicking the bar again opens a fresh details view. Configuration/output changes
close auxiliary surfaces and rebuild bars. Geometry clamps to available output
size; extreme margins/small screens cannot guarantee usable content.

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
existing notification suppressed. No sound, markup, arbitrary icon URLs or images
are loaded. `expiresAt=0` can remain active in history after the bounded banner
hides. No panel is needed for receipt, but place the notifications module on a
panel to reach history. An unmatched toast output produces no banner.

## Settings: one draft, no silent rewrites

Sidebar sections: Appearance (theme and UI), Panels (add/remove/order and fields),
Modules (enabled/style), Integrations (behavior), Configuration (complete TOML).
Booleans use switches; other fields accept **TOML literals** (quoted strings,
numbers, ordered arrays). Set/Enter commits a field to the raw draft. Pending
field input is retained and included on section/module navigation, Preview or
Save; invalid field input blocks that transition, not silently discards it.
Panel `modules` is the ordered editable array; panels themselves have add/remove/
up/down operations. Inline-table fields such as unnormalized default margins
may need the raw editor. It remains the full-fidelity path for every key.

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
  fields can be inserted into explicit bare table headers, or at root. Quoted,
  inline or unsupported implicit-table forms are rejected without mutation; use
  raw TOML. No guessing or whole-document canonicalization occurs.
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
| `ui.popup_width`, `popup_height` | 440 / 560, integers 240..1920 / 240..2160; clamped to output |
| `ui.popup_gap` | 8, integer 0..256; details gap and opposite-edge inset |
| `ui.animation_ms` | 140, integer 0..2000; hover/press color transition; 0 disables it |
| `ui.close_on_focus_loss`, `escape_closes` | true booleans; details dismissal |
| `ui.show_settings`, `settings_icon` | true / `settings`; launcher visibility and validated icon name |
| `ui.toast.enabled` | true; independent banner switch (notification serving remains opt-in) |
| `ui.toast.output`, `edge` | `primary`, `top`; exact QScreen name or primary, edge top/bottom only |
| `ui.toast.width`, `height`, `margin` | 380 / 150 / 64; integers 240..1920 / 80..1080 / 0..4096 |
| `ui.toast.duration_ms` | 5000, integer 100..600000; display lifetime, independent of protocol expiry |
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
  noninteractive bars and on-demand auxiliary focus. Auxiliary zone -1 ignores
  existing reservations because the configured margins already include bar offset.
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

This is baseline evidence for nested single-output Niri only, **not a live recheck
of the stabilization fixes**. New tests cover automatic wire zones, strip geometry,
icons and shuffled multi-output workspace ordering; offscreen tests cannot prove
compositor reservations. Physical multi-monitor, hotplug, fractional scaling,
live Bluetooth/media/tray/notification interoperability and systemd startup remain
unverified. Polling, DBusMenu, pairing-agent and calendar-provider limitations in
[services.md](services.md) remain. Independent stabilization review is still due.
