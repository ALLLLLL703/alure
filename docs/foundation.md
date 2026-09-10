# Foundation handoff

## Public API and extension seams

- `src/core/ConfigStore.h`: construct with an explicit path, call `reload()` before
  use. `model` (QVariantMap), `source` (raw TOML), `diagnostic`, `path` are QML
  properties. `reload()` and `saveText(QString)` return bool. `parse(bytes, out,
  diagnostic)` is pure validation/defaulting: failure leaves `out` unchanged.
  `defaultSource()` returns embedded canonical TOML. `startWatching()` is for the
  panel process only. Signals distinguish model, editor source and diagnostics.
- `model.theme.palette` is a resolved named palette + user overrides. QML accesses
  `Config.model`; the engine context property is set in `src/app/main.cpp`.
  Runtime services should subscribe to `modelChanged`, read `model.modules`,
  own timers/processes/DBus resources in C++, and publish actual data separately.
- `src/platform/PanelHost.h`: owns views, observes config/output changes and
  rebuilds through the event loop. `panelPlacement(map, screenSize)` is a pure
  geometry/anchor/exclusive-zone contract covered for all edges. `Panel.qml`
  receives required `panel` (fully merged map) and `vertical` (bool) properties.
- `src/core/IconProvider.h`: engine takes ownership; QML URL
  `image://icons/<builtin|theme>/<name>`. `alure` and generic fallback are original
  resources. Downstream can add source-named SVG assets and explicit mappings.
- `qml/Settings.qml`: separate ordinary-window engine; full-source editor with
  explicit reload/save. No live service controls or settings popup in bar process.
- CMake static targets `alure_core`, `alure_platform`; app `alure`; existing tests
  `ConfigStore`, `PanelHostContract`, `CliSmoke` in `tests/`.

## Verification and limits

Debug/Ninja with installed Qt 6.11.2 and toml++ 3.4.0. Automated config tests cover
missing/default/overrides/invalid inputs, retained model, raw invalid repair,
unknown keys/comments, external-write conflicts, symlink safety, document size,
file watching including replacement. Geometry covers all edges, fixed/fill sizes,
clamping and reservation. CLI tests cover display-independent validation/help,
invalid config, ordinary preview/settings creation, bounded exit and rejection of
non-Wayland panel mode. Offscreen tests execute no live controls.

Actual Niri visuals, wallpaper alpha, multi-output scaling/hotplug, focus,
exclusive-zone stacking, user-systemd startup and qt6ct desktop theme resolution
remain for graphical/session validation. Missing optional Vulkan headers do not
block the OpenGL/software build; the requested Shell::useLayerShell call emits
an upstream deprecation warning on the installed API (kept only in panel mode).
Config save has the documented narrow race with non-cooperating external writers.
No MPRIS, Niri IPC, SNI, PipeWire, pacman, NetworkManager, BlueZ, notification
server, battery provider or calendar widget is implemented in this foundation.

Next stages must consume these contracts rather than treating reserved module
formats/commands as evidence of existing integrations. Add service target(s),
then replace the explicit foundation banner with real configurable module UI.
