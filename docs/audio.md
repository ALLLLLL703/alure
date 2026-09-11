# Audio output backends

The Sound module now supports both WirePlumber/PipeWire (`wpctl`) and PulseAudio
(`pactl`), including servers exposing the PulseAudio-compatible protocol.
No sound server is installed, replaced or restarted by Alure.

## Configuration

All fields below belong to `[modules.volume.behavior]`. Existing wpctl command
settings remain valid. Missing keys use these defaults:

```toml
[modules.volume.behavior]
backend = "auto" # auto | pipewire | pulseaudio
scroll_enabled = true
scroll_step = 5
scroll_inverted = false
command = ["wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@"]
set_volume_command = ["wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@"]
mute_command = ["wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "toggle"]
pulse_info_command = ["pactl", "--format=json", "info"]
pulse_sinks_command = ["pactl", "--format=json", "list", "sinks"]
pulse_set_volume_command = ["pactl", "set-sink-volume"]
pulse_mute_command = ["pactl", "set-sink-mute"]
allow_actions = true
max_percent = 100
debounce_ms = 100 # minimum spacing between writes; idle input dispatches next event turn
interval_ms = 2000
timeout_ms = 3000
```

`backend` is an enum string; other values/types are rejected. Commands are argv
arrays of at most 128 NUL-free strings. Empty action arrays disable that backend's
corresponding control. An empty read command is diagnosed (auto can try the other
backend). The settings form accepts ordinary quoted command text, not TOML arrays.
`allow_actions=false` disables writes on **both** backends. Existing validation
for `max_percent` (integer 1..150), `debounce_ms` (10..2000), interval and timeout
continues to apply. Valid saved changes reload the service and cancel pending
adjustments; `runtime.watch=false` requires a shell restart.

Auto detection tests readable output, not merely executable/process presence.
It starts with wpctl, tries pactl if that read fails, and remembers the successful
backend to avoid repeatedly probing a broken provider. If that backend later
fails, auto tries the other once per refresh. Explicit modes do not fall back.
Mutation failures are reported; an action is **never replayed on another backend**.

PulseAudio reads `info.default_sink_name`, then selects that sink from JSON
`list sinks`. The first listed sink is not assumed to be the default. Numeric
channel values use PulseAudio's 65536=100% scale; the displayed value is their
mean rounded to two decimal places, with actual mute state. Localized percentage
strings are not parsed. The active backend is shown in the popup status.

For PulseAudio actions, Alure appends the observed numeric sink index and either
`NN.NNN%` or `toggle` to the configured prefixes. Do not add these arguments to
the prefixes yourself. A single volume argument changes all channels equally.
Debounced changes are cancelled if a refresh detects a different sink/backend
before dispatch, or the module is disabled/reconfigured. For wpctl, the existing
configured target and decimal-ratio setter contract remain unchanged.

Runtime snapshots include `percent`, `muted`, `backend`, `canSetVolume`, `canMute`;
PulseAudio additionally reports `sinkIndex`, `sinkName`, `description`, `serverName`.
Controls use the detected backend's capabilities, not the wpctl command settings.
Only the default output is controlled: no microphone, per-stream mixer or output
selection UI is added here.

## Wheel and continuous slider input

Hover the panel's Sound module and scroll vertically to adjust volume without
opening the popup. `scroll_enabled` is a boolean (default true); `scroll_step`
is integer 1..100 percentage points per notch (default 5); `scroll_inverted`
is a boolean (default false). Positive/up delivered wheel deltas increase volume;
inversion reverses this. Settings → Sound exposes all three options. Saved
changes apply on reload, as described above. Invalid types/ranges are diagnosed.

Mouse-wheel notches use Qt's 120 angle units; high-resolution angle deltas scale
proportionally. Horizontal and pixel-only events are ignored. This does not
change mute state, hijack clicks or change other modules. Scrolling is disabled
when the module, actions, scrolling or the backend's setter is disabled, or the
provider is unavailable. Targets are clamped to 0..`max_percent`.

`adjustVolume({delta})` takes signed percentage points and accumulates against
the newest pending/in-flight target, so fast wheel input does not reuse stale
server values. Absolute `setVolume({percent})` replaces that target. Both accept
new input while an audio job is busy; mute and all other services retain their
existing busy guards. Only one command and one latest-value pending slot exist.
`debounce_ms` sets the minimum coalescing cadence between dispatched volume
writes (default 100 ms), not a delay on every input. The first idle input is
sent on the next event-loop turn (synchronous inputs still coalesce). Subsequent
writes wait only the remaining monotonic cadence; a busy command does not
restart that delay. An overdue queued write takes priority over an unnecessary
readback, with a fairness limit of two consecutive setters before a real read.
This bounds observed panel/OSD feedback and changed-default-sink detection during
continuous input without adding a full read sequence after every setter. After
the final write, Alure immediately reads actual volume/mute state; an
already-running read is allowed to finish before dispatch, including default-sink
validation. Slow commands can still delay observation by up to two setters plus
the read sequence, each subject to `timeout_ms`. No concurrent commands or
speculative read results are introduced.

The volume service's `adjusting` property remains true until pending writes and
readback finish; `state.percent` still contains only observed data. The separate
`pendingPercent` property is the latest requested target (null/invalid when none),
not proof of a successful write. The existing popup status line immediately shows
“Pending volume: N%”; diagnostics take precedence. Panel percentages and OSD still
use observed snapshots only, and the pending status clears on readback, failure,
disable or reconfigure. The slider
keeps its mouse grab across busy transitions and ignores snapshots while pressed
or adjusting, then reconciles with the final readback. Progress messages no longer
insert/remove rows or move the slider during dragging; provider errors remain
visible in the status area. Disabling/reconfiguring cancels queued work.

## Dependencies and verification

`pactl` must support JSON output for info/sinks (tested with PulseAudio 17); on
Arch it is provided by the system `libpulse` package. The target computer already
has it installed. The existing wpctl dependency remains optional. Commands run
as bounded asynchronous processes with C locale, a 1 MiB output cap and the
configured per-command deadline; idle polling follows `interval_ms`.

Focused checks cover default/custom/invalid config, JSON/default-sink selection,
channel averaging, mute, timeout/missing tools, automatic/sticky fallback,
explicit PipeWire mode, coalesced setters, permissions, disabled controls,
changed-sink cancellation and no write fallback. Writes were exercised only
against fixtures recording argv. A separate opt-in read-only probe on the target
computer selected `pulseaudio` and observed approximately 60%, unmuted. No actual
volume or mute setting was changed during that backend-introduction probe.

Interaction regression checks use offscreen wheel/press/move/release events and
slow fixture commands, not host audio writes. They reproduce the old slider's
snapshot overwrite and verify uninterrupted dragging, post-release readback,
unchanged slider position, wheel direction/step/gates/clicks, queued input on both
backends, clamping and cancellation. No full CTest suite or live GUI test was run
for that initial interaction stage; later validation is recorded below.

Upstream command contracts (and the installed `pactl(1)` manual):
https://github.com/pulseaudio/pulseaudio/blob/master/man/pactl.1.xml.in

Latency regressions, subsequently executed by the parent (see
[verification and limits](latency.md)), use file-gated fixture commands to verify
idle dispatch before a long cadence,
continuous latest-value accumulation during held reads/writes, queued-write
priority, bounded observed readback while input remains queued, final actual
readback, changed-sink detection during a gesture, write failure and cancellation. Pending feedback
has a separate offscreen UI regression source. No host audio writes are required.

Qt input/binding references:
- https://doc.qt.io/qt-6/qtimer.html (starting an active timer restarts its delay)
- https://doc.qt.io/qt-6/qelapsedtimer.html (monotonic elapsed cadence)
- https://doc.qt.io/qt-6/qml-qtquick-wheelhandler.html
- https://doc.qt.io/qt-6/qml-qtquick-wheelevent.html
- https://doc.qt.io/qt-6/qml-qtquick-controls-slider.html
- https://doc.qt.io/qt-6/qml-qtqml-binding.html
