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
command = ["wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@"]
set_volume_command = ["wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@"]
mute_command = ["wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "toggle"]
pulse_info_command = ["pactl", "--format=json", "info"]
pulse_sinks_command = ["pactl", "--format=json", "list", "sinks"]
pulse_set_volume_command = ["pactl", "set-sink-volume"]
pulse_mute_command = ["pactl", "set-sink-mute"]
allow_actions = true
max_percent = 100
debounce_ms = 100
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
volume or mute setting was changed. No full CTest suite or live GUI test was run.

Upstream command contracts (and the installed `pactl(1)` manual):
https://github.com/pulseaudio/pulseaudio/blob/master/man/pactl.1.xml.in
