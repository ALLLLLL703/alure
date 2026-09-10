# Alure

A TOML-configurable Qt Quick desktop **shell**, initially targeting Niri/Wayland.
Alure provides transparent multi-edge panels, three themes, original SVG/theme
icons, real system modules and a separate settings application with forms and a
full TOML editor. Workspace/media/tray/sound/updates/network/Bluetooth/notification/
calendar/battery details expose supported controls and honest provider diagnostics.
No wallpaper, compositor replacement or simulated system state is supplied.

See [architecture](docs/architecture.md), [configuration](docs/configuration.md),
[interface/configuration coverage](docs/interface.md), and
[service API/readiness/limits](docs/services.md). Historical foundation handoff:
[foundation.md](docs/foundation.md).

## Build

System dependencies (no downloads by CMake): C++20 compiler, CMake >=3.24, Ninja,
Qt >=6.9 Core/Gui/Quick/QuickControls2/QuickDialogs2/Svg/DBus/Network/Test, Qt Wayland runtime,
LayerShellQt >=6.6 (Interface target with `setExclusiveEdge`, `setScreen`,
`setDesiredSize`; tested with Qt 6.11.2 / LayerShellQt 6.7.5), and toml++ >=3.4. On Arch these correspond to `base-devel cmake
ninja qt6-base qt6-declarative qt6-svg qt6-wayland layer-shell-qt tomlplusplus`.
Qt Test and `dbus-run-session` are only required with BUILD_TESTING=ON. No packages are installed by Alure. Runtime integrations use optional `wpctl`,
`checkupdates`, `nmcli`, Niri IPC, session DBus, BlueZ and sysfs; absent providers
produce diagnostics, not simulated data. Module popups rely on Qt Wayland 6.9+
explicit xdg-positioner overrides and LayerShellQt transient popup attachment;
see [native popup compatibility](docs/interface.md#native-popup-implementation-and-verification).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build -j2
ctest --test-dir build --output-on-failure
./build/alure --validate-config --config config/default.toml
./build/alure --preview --config config/multi-panel.toml
./build/alure --settings --config /tmp/alure-demo.toml
```

Without `--config`, the path is `$XDG_CONFIG_HOME/alure/config.toml` (normally
`~/.config/alure/config.toml`). A missing file uses embedded defaults, without
writing anything. Settings writes only on **Save & apply**. Use a temporary path
for experiments. `--validate-config` needs no graphical session; exits 0 for valid
or missing/default config, 1 for invalid config, 2 for invalid CLI use.

Run `./build/alure` in a Wayland session only when ready to add real bars; it does
not replace or stop any existing component. `--preview` uses ordinary windows,
with neither exclusive zones nor layer-shell. `--settings` must be a **separate
process**, never a popup in the panel process. They are mutually exclusive.
Preview **does run enabled services** (including read-only update checks and
cooperative tray hosting); settings/validation run none. Notifications are opt-in
and never replace another daemon. Disable unused services with their module
`enabled=false`; `config/services-example.toml` demonstrates supported overrides.
`--quit-after-ms 500` provides bounded lifecycle smoke tests (1..600000 ms).

```sh
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/alure --preview --quit-after-ms 500 --config /tmp/alure-demo.toml
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/alure --settings --quit-after-ms 500 --config /tmp/alure-demo.toml
```

Offscreen does **not** validate actual visuals, wallpaper alpha, output selection,
Wayland anchors, focus, stacking or exclusive zones. See the bounded nested-Niri
evidence below, including a live recheck of corrected automatic zones.

## Install (explicit, optional)

```sh
cmake --install build
```

Installs `bin/alure`, `share/alure/{default,multi-panel,services-example,four-edge}.toml`, and
`${CMAKE_INSTALL_LIBDIR}/systemd/user/alure.service` under the configured prefix.
QML, original icons, defaults and themes are embedded, so moving the executable
works. The service's absolute ExecStart uses the **configure-time** prefix;
reconfigure when changing it (a later `cmake --install --prefix` alone cannot
rewrite this file). User-local systemd unit lookup varies with libdir/distribution;
copy/symlink the installed unit into your user unit search path if necessary.
No service is enabled/started automatically. Ensure the graphical-session user
manager has WAYLAND_DISPLAY and related session environment before opting in.
Alure does not modify Niri configuration or set any session environment globally.

After explicitly installing to `$HOME/.local` with `CMAKE_INSTALL_LIBDIR=lib`,
you can opt in from your active Niri session (do not also run a manual shell instance):

```sh
# Only if the user service manager does not already have the session environment:
systemctl --user import-environment WAYLAND_DISPLAY NIRI_SOCKET
# Link the installed unit if it is not already in systemd's user search path:
systemctl --user link "$HOME/.local/lib/systemd/user/alure.service"
systemctl --user daemon-reload
systemctl --user enable --now alure.service
# Inspect or undo the opt-in:
systemctl --user status alure.service
journalctl --user -u alure.service
systemctl --user disable --now alure.service
```

Use your actual configured prefix/libdir if different. These commands are
instructions, not actions performed by the build or this delivery. Session
integration must supply fresh environment on subsequent logins; full login/logout
service lifecycle has not been tested.

`theme.icon_mode = "theme"` resolves via QIcon (including qt6ct when your normal
Qt environment selects that platform theme), with original SVG fallback. Alure
does not force qt6ct or install icon packs. The fallback SVGs use intrinsic colors.

`config/four-edge.toml` demonstrates explicit corner margins and per-output bars.
Settings Preview changes only settings styling; Save & apply publishes to watching
panels. Close/Reload protect unsaved drafts. Forms preserve unrelated source text;
panel add/remove/reorder explicitly warns before normalizing panel sections.
`runtime.trace_windows=true` enables bounded-content stderr focus/rebuild diagnostics.
See [interface validation](docs/interface.md#bounded-live-validation-reported-by-parent)
for earlier nested Sway evidence and nested **Niri 26.04** single-output checks on
immutable `8c384d1`: four Top/non-keyboard bars, stable-ID workspace activation, calendar
navigation, sibling-file watcher regression and visible panel alpha blending.
A subsequent nested-Niri recheck of `32af9d9` confirmed the corrected 52-pixel
reservation via compositor output/window sizes, ordered workspace chips, and
icon-only activation with custom 64-pixel module rows in a 44-pixel bar. Independent
build/CTest passed 6/6 and targeted code review found the four stabilization
issues resolved. Physical multi-monitor, hotplug, fractional scaling and real
device/media controls remain unverified. Automatic zones now send thickness only
(44 + separate margin 8 reserves 52); corner margins are
relative to the remaining usable rectangle. Workspace ordering defaults to
`modules.workspaces.behavior.ordering="output-index"`; `"provider"` opts out.
