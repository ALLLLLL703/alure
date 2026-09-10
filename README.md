# Alure

A TOML-configurable Qt Quick desktop **shell**, initially targeting Niri/Wayland.
This commit is the usable **foundation**, not the finished shell: transparent
multi-edge panel windows, three themes, SVG/theme icons, safe configuration
loading/saving and a separate TOML settings editor. Service modules are reserved
configuration contracts; no fake workspaces, battery or network data is shown.

See [architecture and requested product](docs/architecture.md),
[configuration reference](docs/configuration.md) and [handoff](docs/foundation.md).

## Build

System dependencies (no downloads by CMake): C++20 compiler, CMake >=3.24, Ninja,
Qt >=6.5 Core/Gui/Quick/QuickControls2/Svg/DBus/Test, Qt Wayland runtime,
LayerShellQt (Interface target with `setExclusiveEdge`, tested with installed KDE
6.6-era API), and toml++ >=3.4. On Arch these correspond to `base-devel cmake
ninja qt6-base qt6-declarative qt6-svg qt6-wayland layer-shell-qt tomlplusplus`.
Qt Test is only required with BUILD_TESTING=ON. No packages are installed by Alure.

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
`--quit-after-ms 500` provides bounded lifecycle smoke tests (1..600000 ms).

```sh
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/alure --preview --quit-after-ms 500 --config /tmp/alure-demo.toml
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/alure --settings --quit-after-ms 500 --config /tmp/alure-demo.toml
```

Offscreen does **not** validate actual visuals, wallpaper alpha, output selection,
Wayland anchors, focus, stacking or exclusive zones. Those need a real Niri check.

## Install (explicit, optional)

```sh
cmake --install build
```

Installs `bin/alure`, `share/alure/{default,multi-panel}.toml`, and
`${CMAKE_INSTALL_LIBDIR}/systemd/user/alure.service` under the configured prefix.
QML, original icons, defaults and themes are embedded, so moving the executable
works. The service's absolute ExecStart uses the **configure-time** prefix;
reconfigure when changing it (a later `cmake --install --prefix` alone cannot
rewrite this file). User-local systemd unit lookup varies with libdir/distribution;
copy/symlink the installed unit into your user unit search path if necessary.
No service is enabled/started automatically. Ensure the graphical-session user
manager has WAYLAND_DISPLAY and related session environment before opting in.
Alure does not modify Niri configuration or set any session environment globally.

`theme.icon_mode = "theme"` resolves via QIcon (including qt6ct when your normal
Qt environment selects that platform theme), with original SVG fallback. Alure
does not force qt6ct or install icon packs. The fallback SVGs use intrinsic colors.
