# Task Manager and compact detail actions

Alure's `taskbar` module shows **real open Niri windows**, one task per window.
It borrows the choice of icons-only/labeled tasks and workspace/screen filtering
from [Plasma Task Manager](https://github.com/KDE/plasma-desktop/tree/master/applets/taskmanager)
([upstream settings](https://github.com/KDE/plasma-desktop/blob/master/applets/taskmanager/main.xml)).
No KDE code was copied. There is no launcher, pinning, grouping, close button or
fake minimize: clicking even the already-focused task requests `FocusWindow` by
its observed ID. The underline/pill follows compositor focus, not an optimistic
click state. Full titles and output names remain accessible when labels elide.

## Configuration and Settings

Defaults are in `config/default.toml`; `config/taskbar-example.toml` demonstrates
labeled tasks, a bottom panel on every output and **disabled workspaces module**.
Task Manager appears in the Settings sidebar and draggable module palette. Add,
remove and reorder `taskbar` in linear or three-zone slots as with other modules.
New default panel lists include it; existing explicit lists are never rewritten.
All settings apply on save/reload without restarting Niri or Alure.

| TOML key under `modules.taskbar` | Default | Type / allowed values |
| --- | --- | --- |
| `enabled` | true | boolean |
| `style.show_icon`, `style.show_label` | true, false | booleans; icons-only by default |
| `style.icon` | `taskbar` | nonempty icon name; bundled fallback |
| `style.icon_size`, `style.label_size` | 20, 13 | logical pixels, 8–128 / 6–72 |
| `style.task_width` | 160 | labeled task width, 16–512 logical pixels |
| `style.min_width`, `style.max_width` | 30, 480 | empty/unavailable minimum 16–1024; total strip cap 16–2048; min ≤ max |
| `style.active_indicator` | `underline` | `underline` or `pill` |
| `style.opacity` | 1.0 | number 0–1 |
| `style.foreground`, `style.background` | empty | Qt colors; empty inherits foreground / transparent background |
| `behavior.workspace_scope` | `all` | `all`, `active` (active workspace **on each output**), `focused` (single focused workspace) |
| `behavior.output_scope` | `panel` | `all`, `panel` (actual hosting output), `focused` (keyboard-focus output) |
| `behavior.ordering` | `id` | numeric stable `id`, case-insensitive `app-id` or `title`, ties by numeric ID |
| `behavior.allow_actions`, `behavior.focus_on_click` | true, true | booleans; both required by UI and C++ for focus |
| `behavior.popup_enabled` | true | boolean; right-click task opens scoped details |
| `behavior.socket_path` | empty | string; empty uses NIRI_SOCKET, otherwise absolute socket path without NUL |
| `behavior.interval_ms` | 1000 | reconnect interval, integer 100–86400000 ms |
| `behavior.timeout_ms` | 3000 | initial snapshots/action timeout, integer 100–600000 ms |
| `behavior.format` | `{title}` | string; title, app_id, id, output, workspace_id placeholders |
| `behavior.command` | [] | argv array; unused (native socket provider) |

Workspace and output scopes intersect in both strip and detail popup. Unknown
workspace/output windows remain in `all`/`all`, but cannot match a scoped output
or workspace until Niri identifies them. Missing title falls back to app ID then
`Window ID`. Icon-only length is icon size plus configured panel padding; labeled
horizontal tasks use `task_width`. Vertical panels use panel cross-width and
module height for labeled rows. Overflow scrolls within `max_width`; labels elide.
An empty live list renders no launcher/unavailable button. Unavailable service
shows a diagnostic button; reconnect clears stale tasks. Both visibility and
permissions remain configurable; action failures are not reported as focus.

Icons use the app ID's XDG `.desktop` file (including nested desktop-file IDs),
its `Icon` theme name or absolute image path, then the app-ID theme icon, then the
configured bundled fallback. Matching follows the [desktop-file ID convention](https://specifications.freedesktop.org/desktop-entry-spec/latest/file-naming.html). `Exec` is never run. Incorrect/non-desktop app IDs
can fall back; no fuzzy application matching or icon download occurs. Icon size
is bounded; desktop files larger than 1 MiB are ignored. Desktop-file/icon changes
may require panel recreation/restart because Qt caches loaded image URLs.

## Niri protocol and lifecycle

An independent `EventStream` handles initial `WindowsChanged`/`WorkspacesChanged`,
window open/change/close/focus, and workspace activation/configuration events.
Disabling `workspaces` does not disable Task Manager. No subprocess polls and no
periodic windows query runs while connected. Geometry-only layout events update
panel visibility geometry without notifying the public task presentation list;
they do not needlessly recreate task delegates. Disconnect, malformed/oversized JSON, missing
socket and initial timeout clear the snapshot; bounded retries recover. Config
changes to provider options cancel pending work and reestablish the stream.
Actions use a separate socket because Niri stops reading requests on an event
stream. Decimal ID strings cross QML without JavaScript integer rounding; the
native request serializes the exact ID. Unknown/closed IDs and disabled actions
are rejected, and there is at most one focus request in flight.

Protocol reference: [Niri IPC source](https://github.com/niri-wm/niri/blob/main/niri-ipc/src/lib.rs)
and [Event API](https://niri-wm.github.io/niri/niri_ipc/enum.Event.html).

## Notification/workspace detail actions

Notification **Open sender** and active **Dismiss** are now SVG arrow/close
buttons to the **right of the text**, as is workspace **Switch here**. Names,
tooltip descriptions and action permissions are retained; an active workspace
accents the switch icon. Inactive history has no Dismiss button. Provider-supplied
notification actions stay below the text and wrap. The DND/history row wraps on
narrow popups. These controls inherit existing theme icon size, panel padding,
module height and action gates; there are no new unconfigurable dimensions.

## Validation boundary

Protocol tests use temporary local sockets, not the host compositor. QML tests
exercise narrow 240/480-pixel popup layout, action IDs/names/permissions, icons,
scopes, both orientations and existing audio/brightness/OSD regressions. Real
Niri validation uses parent-owned isolated fixtures only; no host hardware,
clipboard or Niri configuration is changed. Panel hiding is documented separately
in [panel visibility](panel-visibility.md).

## Real window thumbnails: deferred on Niri 26.04

The user approved fixing the other issues first and documenting this limitation.
Current icons/titles and right-click details are **not image previews**. No
thumbnail UI, backend or preview settings have been added.

Niri 26.04 supports foreign-toplevel metadata but its
[release notes](https://github.com/niri-wm/niri/discussions/3899) explicitly say
ext-image-copy-capture is not implemented yet. Enumeration/IPC window IDs cannot
supply pixels. Its [versioned screenshot API](https://github.com/niri-wm/niri/blob/v26.04/niri-ipc/src/lib.rs)
writes to the clipboard even when also saving a file; hover capture must not
silently replace (or save/restore) clipboard contents. Capturing an output and
cropping it leaks unrelated content and cannot show covered/offscreen windows.

A future standard per-window capture backend requires compositor support plus
bounded buffers and teardown on leave/close/lock/disconnect. A
[ScreenCast portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html)
flow is an explicitly consent-driven alternative, not silent arbitrary hover
capture. Neither is implemented or promised for this release. No capture was
performed during this audit.
