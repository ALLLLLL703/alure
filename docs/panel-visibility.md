# Panel visibility

Each `[[panels]]` entry has its own `visibility` table, including independent state
for each output matched by `output="*"`. Existing configurations default to
`always`; no reservation behavior changes in that mode.

```toml
[[panels]]
id = "main"
output = "primary"
edge = "top"
layer = "top"
# Other panel keys inherit defaults.
[panels.visibility]
mode = "dodge-windows" # always, dodge-windows, auto-hide
respect_fullscreen = true
show_delay_ms = 100
hide_delay_ms = 350
edge_trigger_px = 2
unknown_geometry = "hide" # hide or show
```

See [the three-panel example](../config/panel-visibility-example.toml). Settings →
Panels → Panel visibility exposes all six fields, with the same enums/ranges as
TOML. Save applies through the normal valid configuration reload; with
`runtime.watch=false`, restart the panel process. Invalid types, enum strings,
and out-of-range numbers produce diagnostics and retain the last good runtime
configuration. Missing keys inherit the defaults above.

| Key | Type / bounds | Meaning |
|---|---|---|
| `mode` | string enum | `always`: visible, preserves `exclusive_zone` / `window_gap`; `dodge-windows`: hide for an intersecting window; `auto-hide`: hide when pointer leaves |
| `respect_fullscreen` | boolean, default `true` | Use native compositor fullscreen stacking: cap an overlay body to top and put the edge trigger on top. False preserves the requested body layer and overlay trigger. |
| `show_delay_ms` | integer 0–10000 | Continuous edge hover or unobstructed dodge state before revealing; popup/menu pinning reveals immediately |
| `hide_delay_ms` | integer 0–10000 | Delay before hiding after pointer/popup pinning ends and mode requires hiding; repeated layout events do not postpone an already pending transition |
| `edge_trigger_px` | integer 1–16 logical pixels | Invisible physical-output-edge input strip, along the panel's configured length; clamped to the output |
| `unknown_geometry` | string `hide` / `show` | Conservative obstruction / ignore unknown rectangles in dodge mode; also selects hide / show while initial snapshots are incomplete or the Niri connection is unavailable |

## Native fullscreen stacking

By default all modes respect fullscreen through layer-shell, without IPC fullscreen
fields, window-size heuristics or polling. A requested `overlay` body becomes `top`;
`top`, `bottom` and `background` remain unchanged. Dynamic edge triggers use `top`
so they do not capture the edge above fullscreen. Set `respect_fullscreen=false`
per panel to restore its requested body layer and the old overlay trigger.

[Niri's layer-shell documentation](https://niri-wm.github.io/niri/Layer%E2%80%90Shell-Components.html)
states that focused, settled fullscreen windows render above the top layer;
overlay renders above fullscreen. Focus, output/workspace selection and transition
stacking remain compositor-owned. This is not app-specific forced hiding: panel
visibility state, reservation, popup pinning and explicit popup layers are unchanged.
In particular `always` retains its reservation, and an overlay body only stays above
fullscreen when this option is off. Other compositors use their native layer policy.
Save/reload applies the option (restart with `runtime.watch=false`).

Native verification (Niri 26.04, 2026-09-15): with a synthetic fullscreen window,
a requested overlay top bar and an auto-hide bottom bar both stayed underneath;
hovering the physical bottom edge for 2200ms did not reveal the bar over fullscreen.
Protocol logs confirmed top layers for the body and trigger. Reloading
`respect_fullscreen=false` restored the overlay top bar above fullscreen and the
overlay trigger; re-enabling the option restored top layers. Leaving fullscreen
restored normal panel/menu access. This single-output test used a private runtime
and private DBus, without changing host configuration; multi-output transitions
were not exercised.

## Geometry and precision limits

Dodge uses the taskbar's existing Niri EventStream, including
`WindowLayoutsChanged`, full window snapshots, open/change/close and workspace
activation events. It does **not** poll `niri msg`, use subprocesses, or use the
workspace module's connection. `modules.taskbar.behavior.socket_path`,
`interval_ms` (reconnect retry), and `timeout_ms` govern this shared subscription.
An enabled dodge panel keeps the stream running even if taskbar UI is disabled;
task activation stays disabled in that case. Taskbar presentation filters and
ordering do not affect dodge detection. Auto-hide itself needs no Niri data.

Only windows assigned to the panel output's **active workspace** participate;
keyboard focus on another output does not suppress that output's active windows.
Known rectangles use output-local logical coordinates:
`tile_pos_in_workspace_view + window_offset_in_tile`, with `window_size`.
Intersection is with the full panel rectangle (including its rounded transparent
corners), not with the whole edge or merely the existence of a window. This is
**client visual geometry**: Niri borders, shadows and fullscreen backdrop outside
the client are excluded. Fractional coordinates are retained.

Installed Niri 26.04 does not supply tiled pixel positions. Column/row indices
(`pos_in_scrolling_layout`) are **never** converted into positions. The default
`unknown_geometry="hide"` treats any geometry-unknown window on the panel output's
active workspace as obstructing. This can include off-viewport tiles or occluded
tiles: it is **conservative hiding, not precise tiled intersection**. Select
`show` to ignore unknown rectangles instead; floating windows with known client
geometry still dodge precisely. Missing/malformed geometric pairs likewise use
the selected policy. No per-window occlusion/minimization information is inferred.
Workspace switch/overview animations are not reconstructed from IPC.

Sources consulted (concepts only, no upstream code copied):
[Niri WindowLayout](https://niri-wm.github.io/niri/niri_ipc/struct.WindowLayout.html),
[Niri IPC Event](https://niri-wm.github.io/niri/niri_ipc/enum.Event.html), and
[KDE Panel.qml](https://github.com/KDE/plasma-desktop/blob/master/desktoppackage/contents/views/Panel.qml).
The proposed Niri viewport extension is not treated as an available API.

## Input, surfaces and lifecycle

- Dynamic modes reserve **no space in either state**, ignoring `exclusive_zone`
  and `window_gap`. They send protocol zone `-1` (ignore other exclusive zones,
  reserve none); always retains the existing config `-1` → thickness conversion.
- The body stays mapped at its fixed size with a fixed layer-shell zone. Hidden
  scene content is invisible and its input mask lies entirely outside the surface.
  Hidden body clicks therefore pass through; an empty Qt mask is deliberately not
  used because that means full input. Reveal/hide never resizes the desktop.
- One transparent, non-keyboard-interactive edge surface per dynamic panel
  catches the physical edge. While hidden **only the thin strip** takes input;
  while revealed a transparent bridge spans the configured edge margin, allowing
  travel from edge to panel without losing hover. Outside the configured panel
  length the edge is untouched. Dynamic margins are capped to half the output
  extent (leaving at least one pixel) to keep bodies/bridges reachable after resize.
- Hovering the panel or bridge pins it. Alure module popups, tray menus and their
  submenus pin the originating panel from the queued open request through dismissal.
  Hover reveal never requests activation or keyboard focus. Existing explicit
  popup opening retains its on-demand keyboard policy; it is restored on close.
- `layer` controls the body subject to the fullscreen cap above. Use `top` or
  `overlay` to reveal over ordinary clients; use `overlay` with
  `respect_fullscreen=false` to appear over fullscreen clients. A configured
  `bottom`/`background` body can remain behind clients even when logically revealed.
- Same-edge panels may overlap, including their triggers; there is no stacking
  solver. Prefer disjoint edges/outputs/lengths. Compositor shortcuts at the edge
  may take precedence. Preview windows cannot prove Wayland input routing or
  reservation behavior.
- Configuration/output/geometry changes close popups, cancel pending timers, remove
  old trigger surfaces and rebuild. Teardown removes event filters before destroying
  surfaces. Visibility uses single-shot timers only; there is no pointer polling,
  frame loop or new external dependency. Existing module timers remain unchanged.
- Transitions are immediate visual toggles after the configured delays; no new
  animation, hidden handle appearance, or activation gesture is introduced.
  `runtime.trace_windows=true` logs reveal/hide and output/rebuild diagnostics.

## Validation

Workers only edited code, as requested. The parent built `alure`, config,
config-editing, panel, taskbar and UI test targets. Focused config/editing tests,
48 panel cases, 8 taskbar cases and 17 UI cases passed. The UI run includes all
four edges with margins 0/4/24 and a 16-pixel trigger, popup/reload lifecycle,
unchanged task-delegate identity after layout events, and right-side icon actions.
Offscreen Qt warns that it cannot apply native window masks; those tests check
policy and region geometry, **not** compositor input delivery.

The parent then used computer-use in an isolated Sway → Niri 26.04 (`8ed0da4`)
session (inner output 973×1176 logical pixels, scale 1), with only Calendar enabled
and the hidden taskbar geometry subscription. All hardware/clipboard/notification
services were disabled. Actual screenshots and Niri window data confirmed:

- A floating client at `[220,180,500,260]` leaves the top bar visible; moving it
  to `[220,0,500,260]` hides the bar; moving away restores it.
- With another fixture focused, clicking `[400,31]` through the hidden body
  focuses the underlying fixture (ID 3 → 2), proving native input-through.
- A real pointer at the physical edge reveals the bar. With zero margin and a
  16-pixel trigger, clicking the revealed Calendar button works; the popup pins
  the bar while the pointer is away. Escape closes it and dodge hiding resumes.
- Auto-hide reveals on edge hover and hides after leaving even with no window
  intersecting the bar. Always mode stays visible and removes its edge surface.
- Tiled unknown geometry stays visible with `show` and hides with `hide`.
  Switching that policy does not resize clients: sizes remained `[500,1152]`
  and `[220,220]`. Always mode visibly reserves the top strip instead.
- Config reloads and final session teardown completed without a Wayland error.

Evidence: `/tmp/alure-visibility-live/` contains the isolated configuration,
trace log and geometry/focus JSON snapshots; screenshots are in the parent
computer-use transcript. The isolated session was stopped afterwards. Host
Niri configuration, shell and hardware values were not changed.

No complete CTest run is claimed. Physical multi-output hotplug, fullscreen
stacking variants, and native visibility input under fractional scaling remain
unverified combinations (OSD scaling was validated separately).

## Pointer reconciliation (refresh audit)

Reveal shrinks the trigger input region to its margin bridge. Alure maps the
last trigger-local point into the panel-output rectangle: points now over the
body transfer their hover intent to it; points over the bridge retain edge
hover. A mask ownership change is not a physical leave, and first native Enter
can arrive after the configured hide delay. The trigger's mask-induced Leave
must not undo that transfer; subsequent body events or located trigger motion
clear or confirm it. Local move/press/release events outside
the body and leave events also clear hover. Popup dismissal releases its pin
without inventing a pointer leave: the stationary pointer can still be over the
bar. Ungrab alone does not prove that the pointer moved away. No global Wayland
cursor coordinates or continuous polling are used. Native grabs can consume
Leave or generate parent Leave even while the pointer is physically over it;
without fresh local evidence, these cases remain a limitation. No universal
popup-grab hover correction is claimed.

Regression tests keep the pointer stationary during an edge-mask ownership
handoff, including trigger Leave before delayed body Enter, then deliver a real
body Leave; they also omit Leave during a popup grab,
and send an out-of-body move while an implicit grab is held. All four
edges and margins 0/4/24 are covered. These protect concrete stale-state paths;
they do not establish that every native compositor delivers identical events.
The parent owns native testing. `always` mode deliberately does not hide; dodge
mode may stay visible when no observed window intersects it. Existing configured
show/hide delays remain unchanged, and hidden bodies retain empty effective input
and blur regions. Active explicit module popups intentionally keep their parent
visible until dismissed.

The parent subsequently verified the corrected handoff in isolated Niri 26.04
with an explicit private DBus session, zero margins and the original 350ms hide
delay: stationary edge hover remained revealed for over 2200ms; moving away hid
it; a second reveal stayed visible; a real task popup opened, and pointer-away
Escape dismissed it and hid the panel. One click during input ownership handoff
missed before a second succeeded. This is not proof that stationary-over-body
Escape or every first-click timing is resolved. Evidence: parent-owned
`/tmp/alure-improve-session/handoff2-live.log` and computer-use screenshots.
