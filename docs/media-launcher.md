# Media launcher

```sh
alure media-launcher --config /path/to/config.toml
alure media-launcher --preview --config /path/to/config.toml
```

The command opens one centered overlay-layer surface on the configured output.
The first page is a searchable registry of readable MPRIS players. Activating a
player replaces that page with its live control/detail page in the **same window**.
A repeated command activates the existing session-bus single instance. No player
is started: discovery and property reads disable D-Bus auto-start and are bounded
to 64 current `org.mpris.MediaPlayer2.*` names. The launcher never offers queue,
volume, launch, or minimize controls.

The registry shows real MPRIS identity, playback status and current title. Search
is page-local and case-insensitive by default. Stable service names preserve
selection across unchanged refreshes. Player/track/capability changes arrive at
the configured poll interval and after acknowledged actions. If the selected
service disappears, pending work is discarded and the registry reappears with a
diagnosis. A busy service retains only the newest requested action, including
seek movements; it records both the well-known service and its unique D-Bus
owner, targets that unique owner, and is cancelled rather than retargeted if
selection, page, visibility, configuration, or service ownership changes.

## Keyboard contract

The UI displays its keyboard help and exposes accessible names:

- Registry: `Up`/`Down` move within bounds, `Tab`/`Shift+Tab` cycle filtered
  results, `Enter` opens the exact selected service, and search keeps focus.
- `F5` refreshes from either page. Registry Tab shortcuts are disabled on detail.
- Detail uses a visible deterministic forward/reverse focus loop: Back, Refresh,
  Close, enabled seek, Shuffle, Previous, Play/Pause, Next and Repeat. Hidden or
  capability-disabled controls are skipped. Focus uses the shared accent border.
  `Enter`/`Space` activates focused buttons; focused seek uses `Left`/`Right` and
  `seek_step_seconds`.
- `Alt+Left` or `Escape` returns from detail to all players. `Escape` on the
  registry closes the window. Toolbar actions therefore have keyboard paths and
  no media action is mouse-only.

Every binding is a canonical Qt portable shortcut under
`modules.media_launcher.behavior`; an empty string disables it. `Tab` bindings
only intercept the registry page, leaving native detail focus traversal intact.

## Configuration and application

`config/media-launcher-example.toml` lists every option. The independent
`[modules.media_launcher]` model intentionally includes its own
`interval_ms`, `timeout_ms`, `preferred_player`, display and action policy. Thus
`modules.media.enabled=false` cannot disable the launcher, and there is no hidden
inheritance conflict. Defaults match the existing media card where applicable.

`enabled=false` by default controls only the optional static bar entry.
`explicit_launch=true` permits the command even then; `popup_enabled` controls
the bar entry. `allow_actions=false` leaves registry and metadata browsing
available but disables MPRIS controls and seeking. Geometry, exactly one output,
search/reset, focus-loss behavior, artwork/network policy, metadata/progress,
control sizing and all shortcuts are independently configurable. Invalid types,
ranges, MPRIS prefixes, output values and shortcuts are rejected with
path-qualified diagnostics.

The card uses shared theme/font/palette/density, module foreground/background and
content opacity, plus real `BackgroundBlur`. Its layer namespace is
`alure-media-launcher`; add it alongside `alure-tray-launcher` in the optional
window-aware Niri rule. Configuration is read at process start; close/reopen to
apply it. Optional panel-entry visibility/order applies when panels reload.

## Isolated validation

Automated tests use offscreen software rendering and a private session bus with
fake MPRIS players; they never invoke a real player. For a native-ready check,
start an isolated compositor and private bus, register two fake
`org.mpris.MediaPlayer2.*` services implementing root `Identity` and Player
properties/methods, then run:

```sh
alure media-launcher --config config/media-launcher-example.toml
```

Exercise search, same-window transition, detail Tab/Shift+Tab order, Enter/Space,
seek arrows, F5, service property changes/removal, Back and Escape. Inspect
`niri msg layers` for exactly one `alure-media-launcher` surface. Never point a
control fixture at the host session bus or real players. The parent independently ran the final build against two synthetic MPRIS players
on a private native Wayland/session bus. Registry Tab/Enter navigation, same-window
detail, capability-disabled seek, keyboard seek/shuffle/play-pause, F5 metadata
refresh and selected-player removal recovery all worked without touching host
players. After the review fix, Tab visibly outlined Refresh and Shift+Tab visibly
outlined Back with the shared accent treatment; a focused seek still delivered
`SetPosition` to the exact synthetic owner. Escape closed cleanly, logs contained
no QML/Wayland errors, and the isolated session was stopped. The parent also reran
ConfigStore, PanelHostContract, CliSmoke, Services, ConfigEditing,
TrayLauncherUi and MediaLauncherUi: **7/7 suites passed** in 24.28 seconds.
Physical multi-output, fractional scaling and focus-loss remain untested.

MPRIS field/method semantics follow the official specification:
https://specifications.freedesktop.org/mpris-spec/latest/
Qt shortcut/focus behavior follows Qt Quick Shortcut and KeyNavigation docs. No
upstream implementation code is copied.
