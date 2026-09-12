# Frosted glass backgrounds

Alure requests **real compositor background blur**, not a blurred copy of its own
text/icons or a captured wallpaper. It uses the public KWindowEffects API from
system KWindowSystem >=6.25; that version supports `ext-background-effect-v1`.
Niri >=26.04 implements this protocol. The Wayland backend may also use KDE's
blur protocol when available. No Qt private API or vendored protocol is used by
Alure, and no extra process or background polling is started.

## Configuration and application

```toml
[theme]
blur_enabled = true # default; false removes Alure's blur request
opacity = 0.65     # 0..1 background tint alpha, independent of blur strength
radius = 14        # logical-pixel corner radius, also shapes the blur region
```

Edit an existing `[theme]` table rather than adding a duplicate. Missing
`blur_enabled` uses true; non-boolean values (including `1` and `"true"`) are
rejected with a `theme.blur_enabled` diagnostic. The settings Appearance page
exposes **Frosted glass background**. Save & apply updates watching panels on
valid configuration reload. Invalid reload retains the last valid model.
`runtime.watch=false` requires restarting panels. Standalone clipboard windows
read configuration at launch. Draft preview remains local to settings and does
not make its opaque window translucent.

Turning blur off preserves existing opacity behavior. Lower opacity reveals more
of the blurred background; it does **not** change the blur radius. Fully opaque
backgrounds obscure the effect. Existing module-content and OSD opacity options
keep their previous meanings; blur itself does not soften text or icons.

## Surface coverage and geometry

- Panels, including auto-hide/window-dodging bodies (not transparent edge triggers).
- Module details, media, clipboard and tray menus, excluding transparent popup padding.
- Notification banners and volume/brightness OSD.
- The visible standalone clipboard card only, never its full-screen cursor probe.

A helper inside each background Rectangle derives its rounded region in window-
local logical pixels. It follows resizing, movement, visibility, opacity-zero
ancestors and reparenting; regions are clipped to the window. Empty/hidden shapes
clear visible blur using a nonempty off-surface region: passing an empty enabled
region to KWindowEffects would incorrectly blur the entire window. KWindowSystem
retains ownership of Wayland objects, capability changes and surface recreation;
see the lifecycle workaround below. Shape updates are event-driven and coalesced;
there is no screenshot capture or render-loop blur workload in Alure.

## Compositor policy and fallback

Niri controls blur strength globally. Its **xray is enabled by default**, sampling
the wallpaper rather than intervening application windows. To blur those windows
too, the user must opt into `background-effect { xray false; }` in the relevant
Niri window/layer rules (and popup rules where applicable). This can be more
expensive; see the upstream documentation below. The client protocol exposes a
blur region, not strength or xray controls. Alure does not modify Niri configuration,
force a compositor rule, or claim it can override one. In particular, a user rule
forcing blur can override the effect of disabling Alure's request.

If blur is unavailable or disabled in the compositor, backgrounds retain alpha
transparency. A single informational stderr message after capability discovery
reports unavailable support. X11/offscreen previews do not request blur; their
lifecycle tests cannot establish actual compositor visuals. No fake fallback is
rendered. Support discovery can change after the diagnostic; KWindowSystem keeps
requests for subsequent capability/expose events.

## Rapid volume / OSD lifecycle fix

Rapid observed volume changes replace the OSD window. On KWindowSystem 6.30, the
Wayland `enableBlurBehind(window, false)` path removes the window's blur region,
destroys its effect and disconnects its destruction observers, but then calls
`installBlur(false)`. That call creates a **new** background-effect object with a
null region, retained by the now-untracked window address. After that surface is
destroyed, a later QWindow at the same address can reuse the stale effect. Niri
correctly rejects the request with `wl_surface was destroyed`, disconnecting the
entire application (exit 255), not just its OSD.

The parent reproduced the reported failure with simulated volume readings
alternating 20/80 every 200 ms: the old executable exited after 42 OSD requests.
Its protocol trace showed effect 69 destroyed, effect 71 created with a null
region for surface 64, surface 64 destroyed, then effect 71 reused roughly 7.4
seconds later and rejected. Evidence: `/tmp/alure-volume-crash/before-2.log`.
The fetched upstream 6.30 and master implementations matched this sequence.

Alure avoids that false path in its shared BackgroundBlur helper. A disabled,
hidden, zero-sized or detached helper uses `QRegion(-1, -1, 1, 1)`, wholly outside
valid surface pixels, while leaving KWindowSystem's ownership tracking enabled.
An initially disabled helper makes no request. Destruction listeners therefore
remain installed until the owning window/surface dies; no fake visual blur or
whole-window region is used. Public `QPlatformSurfaceEvent` tracks native
creation/destruction, and no new request is issued once destruction starts.
No protocol code is vendored and no library or compositor configuration is patched.

A deterministic same-address window test also exposed a separate helper teardown
hazard: QQuickItem's base destructor can emit `windowChanged` after the derived
connection list/timer have been destroyed. Self and ancestor connections are now
disconnected in the derived destructor before those members are destroyed.
The pre-fix test failed in `BackgroundBlur::watchAncestors` (Qt assertion; core
3788123, saved stack `/tmp/alure-volume-fix-destructor-core.txt`); the corrected
test passes. Both fixes apply to all shared blur surfaces,
not only volume; OSD timing, audio queues and all configuration semantics remain
unchanged.

### Lifecycle regression checks

Default tests remain offscreen and never construct hardware/audio services:

```sh
cmake --build build -j2
dbus-run-session -- ./build/tests/ui_tests backgroundBlurReusedWindowLifetime volumeOsdBlurChurn backgroundBlurGeometryAndLifecycle
```

For a **separately created isolated** Niri/Wayland display, set its actual runtime,
Wayland and session bus environment, then opt into native protocol checks:

```sh
ALURE_TEST_NATIVE_WAYLAND=1 ./build/tests/ui_tests backgroundBlurReusedWindowLifetime volumeOsdBlurChurn backgroundBlurGeometryAndLifecycle
```

Do not point the native probe at the user's desktop. It deliberately creates and
destroys 32 windows at the same C++ address and 120 actual PanelHost OSD windows,
covering disabled/zero/hidden shapes, helper deletion, native surface recreation,
OSD close/replacement and reparenting. A protocol error terminates the test
process, so native success (unlike offscreen geometry checks) detects the original
failure class. Native rendering and unsupported-compositor behavior require
separate verification; the parent owns that live check.

The parent ran those three native probes on isolated Niri: exit 0, 5 passed
including setup/cleanup, with no fatal protocol errors in
`/tmp/alure-volume-crash/native-tests.log`.

The parent also compared the actual application with a file-only simulated audio
provider alternating 20%/80% every 200 ms. The pre-fix binary exited 255 after 42
OSD requests. The fixed binary ran for its full 60-second deadline, handled 249
OSD requests and exited 0 without a protocol error. Back-and-forth slider drags
and wheel input were attempted during this run; the fixture recorded seven write
commands (this does not assert delivery of every wheel event). Evidence:
`/tmp/alure-volume-crash/{before-2.log,after.log,after.exit,audio.log}`.

A subsequent computer-use visual check over high-contrast terminal text confirmed
that disabling blur restored sharp background text and re-enabling it restored
rounded panel blur. That application also exited 0 at its deadline. The isolated
Niri desktop was stopped; no host audio, Niri configuration, installed binary or
running Shell was modified. Physical multi-output and hardware controls were not
tested.

Implementation checks: build passed; lifecycle/surface UI checks 13 passed;
volume controls/drag, settings, auto-hide and OSD checks 21 passed; Services 45
passed with one opt-in hardware probe skipped; blur config 3 passed. Full CTest
was not rerun for this fix; the three previously recorded unrelated baseline
suite failures remain unresolved.

## Verification and sources

New automated cases cover default/custom/invalid configuration, settings field
metadata and saving, rounded/window-clipped geometry, resize, disable/enable,
hidden and opacity-zero ancestors, native-window recreation, reparenting, all
listed QML surfaces and clipboard probe exclusion. These are request/lifecycle
checks, not proof of rendered blur. Physical multi-output/fractional-scale and
compositor visual validation must be reported separately.

Implementation validation (Qt 6.11.2 / KWindowSystem 6.30.0):
- `cmake --build build -j2` passed.
- `config_tests backgroundBlurOptions`: 3 passed (including setup/cleanup).
- Focused `ui_tests backgroundBlurGeometryAndLifecycle backgroundBlurSurfaces
  backgroundBlurSettings panelAutoHideLifecycle osdPassiveWindowAndDuration
  settingsFieldOrdering`: 27 passed, no failures.
- Default TOML CLI validation and `git diff --check` passed.
- Full CTest: 8/11 suites passed. The same baseline failures remain: ConfigStore
  expects 11 modules rather than 13; QuickUi has tooltip/clipboard fixture
  assertions and a clipboardView crash; SettingsCloseE2e fails. They are not
  repaired by this feature and no fully passing suite is claimed.

### Parent live verification

Computer-use checks ran in an isolated Sway → Niri 26.04 desktop, using a
high-contrast terminal text fixture behind a calendar-only panel. Only the
isolated Niri configuration set `background-effect { xray false; }`; it did not
force `blur true`, so Alure's request controlled the effect.

- At `theme.opacity=0.4`, enabling blur softened the terminal behind the panel
  and calendar popup while their own text/icons stayed sharp. Rounded corners
  and transparent popup padding did not blur the surrounding terminal.
- Disabling `theme.blur_enabled` and reopening the popup restored sharp
  background text; re-enabling restored blur through the file watcher.
- Initial live inspection found that discovering the protocol only after mapping
  the first panel could leave it alpha-only until reload. Discovery now starts
  at QML type registration before window creation. Two fresh process launches
  showed panel blur without a configuration reload. Wayland traces confirmed
  `set_blur_region` before the first buffer commit in the corrected startup.
- After the startup correction, the build passed; focused geometry/surface/settings
  UI checks passed 12 cases, and blur configuration checks passed 3 (including
  setup/cleanup). The earlier full-suite baseline failures remain unresolved.

Fixtures, protocol traces and focused logs are under `/tmp/alure-blur-live/`;
visual captures are in the parent computer-use transcript. The isolated desktop
was stopped. No host Niri configuration, running Shell, or hardware setting was
changed. Actual toast/OSD/clipboard visuals, physical multi-output and fractional
scaling were not covered by this bounded live check.

Sources inspected for this implementation:
- [Niri 26.04 window effects](https://niri-wm.github.io/niri/Window-Effects.html)
- [KWindowSystem v6.30 Wayland implementation](https://github.com/KDE/kwindowsystem/blob/v6.30.0/src/platforms/wayland/windoweffects.cpp)
- [KWindowSystem v6.25 protocol support](https://github.com/KDE/kwindowsystem/blob/v6.25.0/src/platforms/wayland/windoweffects.cpp)
- Installed KWindowEffects header: region coordinates and empty-region semantics.

Only public library APIs are called; no upstream implementation code is copied.
