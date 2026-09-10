# Alure v0.1 architecture and delivery plan

## Requested product

A configurable **Wayland desktop shell, not a compositor**, using Qt 6 Quick/QML
and C++20, with Niri as the first integration target. Like Plasma, any number of
panels may occupy top/bottom/left/right edges of selected outputs. The requested
finished shell includes MPRIS media, Niri workspaces, StatusNotifierItem tray,
volume, pacman updates, WiFi, Bluetooth, notifications, calendar, battery,
built-in themes, a separate Wayle-inspired settings GUI process, a systemd user
unit, genuine alpha over the wallpaper, and original SVG or Qt theme/qt6ct icons.
All user-facing behavior, module ordering, style and runtime parameters belong
in TOML. No automatic installs, shell interpolation or edits to compositor config.

## Verifiable stages

1. **Foundation (this stage):** build/install, configuration + theme shared model,
   layer-shell host, minimal panel, TOML editor, CLI and automated tests. Module
   slots are configuration contracts, **not simulated working integrations**.
2. **Services:** real asynchronous, bounded integrations, models and tests; consume
   ConfigStore's merged `model.modules` maps and extend defaults/validation/docs.
3. **UI:** full modules, popups and settings controls using shared model; actual
   Niri multi-output/scale/focus/exclusive-zone/transparency validation by parent.

## Boundaries and ownership

- `alure_core`: ConfigStore owns parsed source, last-read byte snapshot and merged
  QVariantMap. Unknown TOML survives explicit editor save; built-in defaults are
  resources, not scattered in QML. All successful loads publish one modelChanged.
- `alure_platform`: PanelHost owns QQuickViews; rebuilds on valid reload and output
  changes. It creates one window per enabled panel/output match. Bars never take
  keyboard focus. It contains per-window LayerShellQt APIs; app entry enables
  the integration only in panel mode. No Niri dependency in UI.
- `alure`: CLI selects a QCoreApplication for validation or QGuiApplication for
  UI. Settings/preview do not enable layer-shell; settings is a separate process.
- `qml/Panel.qml`: minimal branding + explicit foundation notice, not fake service
  data. `qml/Settings.qml`: explicit reload/save TOML editor with diagnostics.
- Services must use asynchronous DBus/QProcess with bounded lifetimes/output and
  argv lists; no synchronous UI polling or shell command concatenation.

## Research (consulted for this stage)

- https://doc.qt.io/qt-6/qquickwindow.html : enable default alpha buffer before
  first QQuickWindow; transparent scene background is also necessary.
- https://github.com/KDE/layer-shell-qt and upstream README retrieved with curl:
  imported Interface target, Shell::useLayerShell before windows, Window::get.
- Installed `/usr/include/LayerShellQt/{shell,window}.h` read in full: explicit
  anchors, desired size, margins, exclusive edge/zone, keyboard interactivity,
  screen selection. Installed shell header marks global activation deprecated;
  kept only in panel mode per requested compatibility contract.
- https://doc.qt.io/qt-6/qsavefile.html : retrieved official atomic replacement
  and direct-write fallback contract; fallback remains explicitly disabled.
- https://marzer.github.io/tomlplusplus/ : system toml++ parser/formatter; no
  vendored parser. Qt QSaveFile provides atomic replacement, QLockFile coordinates
  Alure writers; pre-save bytes detect external edits (see configuration limits).
- Icons in `assets/icons` are original geometric drawings, no copied assets.

Offscreen tests are lifecycle checks only: they cannot prove Wayland layer-shell,
wallpaper alpha, actual panel stacking, multi-screen placement or visual quality.
