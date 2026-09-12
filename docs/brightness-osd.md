# Display / keyboard brightness and OSD

Alure reads Linux backlight state directly from sysfs and writes only after an
explicit popup/button/wheel action, through the installed `brightnessctl`.
There is no DDC/CI/external-monitor backend, ambient-light policy, hotkey grab,
or automatic brightness adjustment. Keyboard backlight is independent of display
brightness. No new daemon or dependency download is needed.

Add `brightness` in Settings → Panels (module palette), or to an existing panel's
`modules` / `modules_right` list. New defaults include it beside Sound; existing
explicit panel lists are **not** rewritten. Settings → Brightness exposes the
module style, both device configurations and action/wheel controls. The popup
always shows both device statuses and controls; missing display hardware does not
prevent use of a supported keyboard, and vice versa.

All options below are in the unified TOML model, with corresponding Settings
fields. See [`config/default.toml`](../config/default.toml) for complete defaults
and [`config/brightness-osd-example.toml`](../config/brightness-osd-example.toml)
for a custom example. Save/reload applies them without restarting Alure (subject
to the existing `runtime.watch` setting). Invalid values produce a path-specific
diagnostic and do not replace the last valid running model.

## Brightness configuration

Shared `modules.brightness` / `.style` options follow other modules: `enabled`,
icon/size, label/format, foreground/background inheritance, opacity and size
limits. All new integer options require TOML integers, not floating-point values.

Under `modules.brightness.behavior`:

| Key | Default | Type / validation / meaning |
| --- | --- | --- |
| `popup_enabled` | true | bool; click opens details |
| `allow_actions` | true | bool; false monitors only |
| `interval_ms` | 500 | integer 100–86400000; sysfs polling period |
| `timeout_ms` | 3000 | integer 100–600000; writer deadline |
| `debounce_ms` | 100 | integer 10–2000; rate-limits continuous input |
| `set_command` | `["brightnessctl"]` | argv array, ≤128 strings without NUL; empty disables writes |
| `scroll_enabled` | true | bool; wheel over panel module |
| `scroll_inverted` | false | bool; reverse delivered wheel direction |
| `scroll_target` | `"screen"` | `screen` or `keyboard`; no silent fallback |
| `format` | `"{percent}%"` | string; observed screen percent, otherwise keyboard percent |
| `command` | `[]` | shared compatibility key; unused, reads are native |

Each nested `.screen` and `.keyboard` table has:

- `enabled = true`, `allow_actions = true`: booleans; per-kind read/action gates.
  Global module/action gates still apply.
- `sysfs_path`: absolute directory without NUL; defaults `/sys/class/backlight`
  and `/sys/class/leds`, respectively. Alternative paths support test fixtures.
- `device = ""`: autodetect first **readable** valid device, sorted by basename.
  A nonempty value selects exactly that basename; it does not fall back on error.
  Basenames must match `[A-Za-z0-9_][A-Za-z0-9_.:-]*` (no path or wildcard).
  Keyboard names must end **`:kbd_backlight`**, including during autodetection;
  caps-lock, mute and other unrelated LEDs are never selected.

Screen-specific defaults: `min_percent = 1` (integer 0–100),
`max_percent = 100` (integer 1–100, at least minimum), `step = 5` (integer 1–100
percentage points per wheel notch / popup button). The allowed native range is
`ceil(max_brightness * minimum / 100)` to `floor(max_brightness * maximum / 100)`.
This keeps rounding inside user limits; zero display brightness requires explicit
`min_percent = 0` and can make the screen unreadable.

Keyboard-specific defaults: `min_level = 0` (integer 0–2147483647),
`max_level = -1` (-1 uses hardware maximum, otherwise integer 0–2147483647, at
least minimum), `step = 1` (integer 1–2147483647 **native levels**). Maximum is
capped to hardware. A 0..2 device has exactly off / 1 / 2, not a misleading
hundred-step slider. Partial keyboard wheel notches accumulate until a full
notch; horizontal-only wheel events are ignored. Fractional keyboard action
arguments are rejected. Observed levels outside configured limits are still
reported truthfully; limits constrain writes, not the reading.

The kernel's `brightness` and `max_brightness` attributes are read (not the
optional display-specific `actual_brightness` attribute). This reports the
kernel's current backlight setting, not calibrated physical luminance. Invalid,
missing or unreadable values produce per-kind diagnostics; no value is invented.
The native provider can be available with neither device present: inspect each
kind's `available` flag, not just service availability. A configured range with
no representable native levels disables that kind's controls with a diagnostic.

The writer receives `--class backlight|leds --device OBSERVED_NAME set RAW_LEVEL`.
Use the normal system brightnessctl permissions/logind integration; Alure does
not elevate privileges, invoke sudo, or alter permissions. Missing executable,
timeout or permission failure is shown in the popup while preserving actual
readable hardware snapshots. No write backend fallback is attempted.

One command is in flight across **both kinds**, plus one latest pending target.
Relative input accumulates against that kind's latest desired target. Changing
kinds can replace the still-pending target of the other kind. Dragging cannot
starve dispatch, and busy jobs do not disable a grabbed slider. Readback follows
completion; slider reconciliation waits for release and the final pending job.
Device name/maximum changes cancel stale pending targets; config execution
changes/disable cancel pending jobs. A write already accepted by the kernel
cannot be undone by cancel/reload. No optimistic target is published as a
hardware observation.

## Passive lower-center OSD

Settings → Appearance → interface options exposes `ui.osd`. It covers observed
volume percentage **and mute**, screen brightness, and keyboard native levels.
Alure does not claim compositor hotkeys or install key bindings: an external
brightness/volume key is noticed through subsequent provider observations.

| Key | Default | Type / range |
| --- | --- | --- |
| `enabled` | true | bool; global OSD gate |
| `volume_enabled`, `screen_enabled`, `keyboard_enabled` | true | bool; per-kind observed-change triggers |
| `output` | `"primary"` | nonempty string without NUL: primary, `*` (all), or exact output name |
| `width`, `height` | 300, 100 | integers 160–1920, 60–1080 logical pixels |
| `margin_bottom`, `margin_horizontal` | 64, 16 | integers 0–4096 logical pixels |
| `duration_ms` | 1800 | integer 100–600000 |
| `padding`, `spacing` | 16, 8 | integers 0–64, 0–32 |
| `font_size`, `icon_size` | 14, 24 | integers 6–72, 8–128 |
| `show_icon`, `show_label` | true | bool |
| `volume_icon`, `screen_icon`, `keyboard_icon` | volume, brightness, keyboard | icon names, not paths; theme icon mode uses SVG fallback |
| `bar_height`, `radius` | 6, 14 | integers 1–32, 0–128 |
| `opacity` | 0.92 | number 0–1; entire OSD |
| `background`, `foreground`, `accent` | `""` | empty inherits theme palette, otherwise valid Qt color |

OSD is horizontally centered, inset from the **physical output bottom**, ignoring
other layer reservations. Horizontal margin is minimum edge clearance; dimensions
and margins clamp to small outputs. Values are logical pixels, so compositor
scaling applies. Output names do not silently fall back if unavailable. `*`
creates one copy per connected output. Latest change updates the current OSD in place
and restarts its timeout; there is no history/stack. The bar clamps to 100% while
the volume label can truthfully show configured amplification above 100%.

While visible, each output keeps the same QQuickView, QML root and native
surface; even a change of kind updates the `snapshot` property and restarts the
expiry timer. Expiry, configuration/output rebuild and explicit close destroy
that group. A disabled kind retains the previous dismissal behavior (closes the
current group), rather than leaving an obsolete OSD visible.

Only changed **observed** values trigger: not requests, busy signals, repeated
snapshots, or failed writes. First readings, device/output identity changes,
recovery after missing data and configuration reload baselines are silent.
Appearance/config changes hide a visible OSD; comment-only reloads do not change
the model or restart its timer. Enable toggles never replay older changes.

External latency is up to one successful poll plus provider execution time:
brightness defaults to 500 ms; volume defaults to 2000 ms and its read-command
latency. Successful local writes are reread immediately. Intermediate external
changes between polls can be missed, especially change-and-revert; disabled
services cannot observe changes. These triggers intentionally show **all**
observed changes of an enabled kind rather than guessing their origin.

On Wayland the OSD is an overlay layer with keyboard interactivity `None`, an
empty input region (`WindowTransparentForInput`), no activation, and protocol
exclusive zone `-1` (ignores reservations and reserves none). It cannot steal
pointer input, keyboard focus or panel space. It is not an interactive popup.

## Validation and sources

Focused tests: `brightness_tests` (temporary sysfs plus explicit writer fixture;
no real hardware writes), `panel_tests osdLowerCenterPlacement`, and
`ui_tests brightnessPopupContinuousDrag brightnessWheelAndSettings
osdPassiveWindowAndDuration`. Existing volume wheel/drag/queue tests remain
applicable. Tests are not evidence that privileged writes work on a given host.

Reference concepts, not copied code:

- KDE Plasma [`shell/osd.cpp`](https://github.com/KDE/plasma-workspace/blob/master/shell/osd.cpp),
  current [`shell/qml/Osd.qml`](https://github.com/KDE/plasma-workspace/blob/master/shell/qml/Osd.qml)
  and [`shell/osdwindow.cpp`](https://github.com/KDE/plasma-workspace/blob/master/shell/osdwindow.cpp):
  reusable progress OSD, no focus/input capture (upstream GPL-2.0-or-later).
- KDE PowerDevil [`keyboardbrightnesscontrol.cpp`](https://github.com/KDE/powerdevil/blob/master/applets/brightness/plugin/keyboardbrightnesscontrol.cpp):
  native keyboard brightness and maximum, not a synthetic percentage scale.
- [`brightnessctl.c`](https://github.com/Hummer12007/brightnessctl/blob/master/brightnessctl.c)
  and installed `brightnessctl --help`: explicit class/device argv, raw levels,
  default zero minimum unless `--min-value` is requested.
- Installed LayerShellQt `Window` API and live Wayland protocol traces verified
  the passive surface policy. Plasma-specific shell protocols were not reused.
