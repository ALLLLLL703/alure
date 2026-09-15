# Tray launcher and CLI

```
alure tray-launcher --config /path/to/config.toml
alure-trayctl --config /path/to/config.toml list
alure-trayctl --config /path/to/config.toml menu 'SERVICE/ITEM_PATH' 5
alure-trayctl --config /path/to/config.toml click 'SERVICE/ITEM_PATH' 5 7
```

The launcher observes the **current** `org.kde.StatusNotifierWatcher` registry.
No watcher means an explicit unavailable message, not an empty fabricated list.
Neither new consumer acquires/replaces a watcher, launches tray applications,
nor requests a provider-owned ContextMenu window. Ordinary panel tray ownership
is unchanged. Observer D-Bus requests explicitly disable service auto-start,
including watcher/property reads and every menu method; stale activatable names
therefore fail rather than launching a service. The optional `tray_launcher` panel module is a static button: it
starts only the launcher, not a permanently running registry service.

The launcher creates exactly one centered overlay-layer surface on the selected
output, with exclusive keyboard focus and no reserved panel space. Selecting an
app, entering submenus, and going Back reuse that surface. A second command
activates the existing launcher (session-bus single instance). `--preview` is a
normal-window testing mode. The selected output must exist; removal or geometry
changes close the surface safely. No host compositor settings are modified.

Search is always available and filters only the **current page**, case-insensitive
by default. No recursive traversal is triggered by search. Keyboard Down/Up
skip disabled entries and separators; Return selects the highlighted entry.
Alt+Left is Back; Escape goes Back on menu pages and closes on the registry.
Mouse buttons provide Back, Apps, Refresh and Close. Configurable shortcuts can
be cleared to disable them. Refresh in a menu returns to that app's root.
Unchanged/live updates preserve search focus and the selected stable ID when it
still exists. Background registry refreshes keep the previous available snapshot
selectable and do not insert a loading row or shift the layout. Initial loading
and confirmed unavailability remain explicit. Page transitions clear search by default; this is configurable.
Hidden items are omitted; separators, disabled entries, check/radio/mixed state
and submenu indicators reflect provider data. Lazy AboutToShow/GetLayout,
LayoutUpdated/ItemsPropertiesUpdated, owner loss, request errors, and bounded
requests use the existing shared TrayMenu engine. Back can cancel a pending
submenu read. Watcher owner loss immediately discards in-flight registry reads;
registry failure or confirmed app removal invalidates the shared active menu and
cancels unsent traversal. Removal is checked as soon as registry IDs arrive,
before remaining provider metadata reads complete. An already-sent Event retains
its acknowledgement/error/timeout, without permitting further menu navigation.
Closing/backing away from an already-sent Event cannot undo it.

## Configuration and Settings

All options are in `config/default.toml`, and available through the existing
Settings module palette and typed fields. See `config/tray-launcher-example.toml`.

- `modules.tray_launcher.enabled=false` by default: controls **panel entry
  visibility only**, not explicit GUI or CLI invocation. Add `tray_launcher` to
  a panel's ordered modules and enable it to show the static button.
- `behavior.explicit_launch=true`: permits standalone GUI and all CLI commands.
  False rejects both; the panel button also cannot launch it.
- `behavior.popup_enabled=true`: permits panel-button launch only.
- `behavior.allow_actions=true`: permits leaf Event(clicked). False still allows
  app/menu/submenu inspection in GUI and CLI. It does not inherit panel tray
  policy: these are independently configured consumers.
- `popup_width` 240..1920, `popup_height` 240..2160 logical pixels, clamped to the
  screen. `output` is `primary` or exactly one screen name (not `*`).
- `interval_ms` 100..86400000 refreshes the registry; `timeout_ms` 100..600000 is
  each D-Bus request deadline. Defaults are 1000 and 3000.
- Booleans: `search_case_sensitive=false`, `reset_search_on_page=true`,
  `close_on_activate=true` (only on acknowledged Event),
  `close_on_focus_loss=false`.
- `next_shortcut="Down"`, `previous_shortcut="Up"`,
  `activate_shortcut="Return"`, `back_shortcut="Alt+Left"`,
  `close_shortcut="Esc"`: canonical Qt portable strings or empty.
- `format` is the static panel label; shared module style controls icon,
  label visibility/sizing, foreground, background and content opacity.
  The card inherits the unified theme's font, palette, spacing, padding, radius,
  border, tint (`theme.opacity`) and blur (`theme.blur_enabled`). Nonempty module
  background/foreground override the inherited colors. BackgroundBlur owns no
  window and detaches before surface destruction; unsupported compositors retain
  transparency without pretending blur is available. Strength remains compositor
  controlled. Shared `command` is unused: no app command execution is offered.

Missing keys use defaults; invalid types/ranges/shortcuts/output selections have
path-qualified validation errors (`alure --validate-config --config PATH`).
Panel entry changes apply on panel reload. **Reopen** the launcher/CLI to apply
configuration, including appearance; the standalone process does not watch disk.
Reinvocation activates an existing window without replacing its config.

CLI output/exit codes and the acknowledgement distinction are documented in
[tools/alure-trayctl/README.md](../tools/alure-trayctl/README.md). Both clients
share the 128-app, 512-row and depth-32 limits. Registry polling is bounded,
sequential; a failed provider read currently makes the registry unavailable until
its next successful refresh (same shared panel-tray behavior).

## Reproducible isolated validation

```
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build -R '^(TrayCtl|TrayLauncherUi|Services|ConfigStore|ConfigEditing|CliSmoke|PanelHostContract)$' --output-on-failure
```

TrayCtl uses QCoreApplication subprocesses with DISPLAY/WAYLAND_DISPLAY removed
and a nonexistent QPA setting, on a private bus. TrayLauncherUi uses offscreen
software Qt Quick plus a fake watcher/item/menu on a separate private-bus
connection. Tests cover search/keyboard/mouse, same-window transitions, identity,
focus, check updates, errors, disabled actions, delayed lazy reads/Back,
acknowledged closure, settings fields and blur-helper lifetime. Review regressions
also cover unknown/missing CLI options (exit 2), watcher loss and removal during
held lazy replies, retained acknowledgements for already-sent Events, and stable
registry rows during a held background refresh. `tests/trayctl-session.sh` creates
temporary activatable watcher/provider fixtures only on the private test bus;
negative observer checks and positive activation controls verify the no-start
boundary without installing anything. These are not
native compositor blur/focus or multi-output validation.

The existing `build/tests/tray_menu_demo` is explicitly synthetic and registers
against an existing watcher; it does not create one. Native validation should
use an isolated Wayland compositor/private session bus with a fake watcher and
that provider (or an independent complete fixture), never host tray apps.

References consulted (no implementation copied):
[StatusNotifierItem specification](https://specifications.freedesktop.org/status-notifier-item/latest-single),
[DBusMenu XML](https://github.com/gnustep/libs-dbuskit/blob/master/Bundles/DBusMenu/com.canonical.dbusmenu.xml).
The deployed `org.kde` namespace is intentionally retained rather than silently
switching to the specification's alternative namespace.

## Parent-run native review verification

The parent independently reports 14 passing display-independent CLI cases
(`cli-final-results.json`), including unknown-option exit 2. In an isolated
native compositor with private session/system buses, its final immutable build
showed one overlay surface above a synthetic fullscreen terminal, actual blur
at 0.45 opacity, readable selected/disabled rows, QQ search/open, search retained
across a live label update, immediate removal invalidation, and Escape returning
to the remaining registry then closing with exit 0. `native-final.log` records
exactly one layer surface and no fatal/ReferenceError/TypeError diagnostics.
An earlier parent run of `3da8e98` exercised More → Advanced → Back → Inspect,
with Event 12 acknowledged, exit 0, and one surface. The isolated native session
was stopped by the parent. This is parent-supplied evidence, not worker-run
native validation; native multi-output, scaling and focus-loss remain untested.
No unrelated full-suite success is claimed.
