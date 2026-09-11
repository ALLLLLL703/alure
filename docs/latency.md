# Volume and workspace latency fixes

## Changes

- Volume idle input is dispatched on the next event turn rather than waiting a
  full `debounce_ms`. Later writes use the remaining monotonic cadence; command
  completion wakes overdue input. Only one command runs at a time, with one
  replaceable pending target. At most two consecutive setters are allowed before
  actual readback, so continuous input cannot indefinitely starve observations.
- Popup `pendingPercent` feedback is explicitly requested intent. Panel/OSD
  percentages remain actual observations. Errors, sink/backend changes, disable
  and reconfigure cancel queued targets; failed writes are never replayed.
- Workspace state now follows a standalone Niri EventStream, including external
  workspace switches. `interval_ms` is the reconnect retry interval, not a delay
  before displaying changes. Active-per-output and globally focused workspace
  flags remain distinct. Action ACKs do not fabricate focus changes.

No dependency or configuration key was added. Existing configuration validation,
Settings controls and reload behavior remain; help and TOML comments describe the
updated scheduling semantics. See [audio](audio.md) and [services](services.md).

## Parent verification

Workers edited code and reviewed source only. The parent built `alure`,
`services_tests`, `ui_tests`, `taskbar_tests`, `brightness_tests`, `config_tests`
and `config_editing_tests`, then ran:

- Services: **45 passed**, 1 skipped (opt-in host audio read probe).
- Focused UI: **25 passed**, including pending-vs-observed feedback, wheel gates,
  continuous slider drag, Settings layout, OSD lifecycle, panel hiding and task
  delegate identity.
- Taskbar: **8 passed**.
- Brightness/OSD: **8 passed**, 1 skipped (opt-in host hardware read probe).
- Audio backend/scroll configuration: **4 passed**.

New gated fixtures cover immediate idle dispatch against a 2000 ms cadence,
queued writes, readback during a still-active gesture, slow reads, failures and
sink-change cancellation. Workspace fixtures cover fragmented/full/incremental
streams, long reconnect intervals, multi-output focus, IDs above 2^53, separate
action ACKs, malformed input, deadlines, reconnection and reload cleanup.

The parent also used computer-use with an isolated Sway → Niri session:

- With workspace `interval_ms=60000`, an external switch from One to Two was
  already reflected by the bar in a capture completed **256.3 ms** after command
  dispatch. This is a bounded single observation, not a latency benchmark.
- Clicking Three in the bar updated its observed active indicator to Three.
- A fixture-only volume slider adjustment changed 42% to 86%; bar and popup
  reconciled to the fixture readback. The recorded read followed the setter by
  about **19.5 ms**; this excludes input/launch latency and says nothing about
  physical audio hardware. The trace also recorded an observed-volume OSD request.

Evidence is under `/tmp/alure-latency-live/` (configuration, Python file-only audio
fixture, command timestamps, workspace screenshot/JSON and trace logs); other
screenshots are in the parent computer-use transcript. The isolated session was
stopped. No host volume/backlight value, Niri configuration or installed Shell
was modified.

## Limits

No full CTest run or physical audio latency benchmark is claimed. Native scripted
wheel attempts produced no fixture writes, and the scripted drag recorded only
one final setter; those attempts do **not** establish native wheel/continuous
motion delivery. Continuous cadence and readback fairness are established by the
gated service and offscreen UI regressions instead. Slow configured commands
still impose their actual execution time; reads and writes remain serialized.
