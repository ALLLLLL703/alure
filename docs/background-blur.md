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
explicitly disable blur: passing an empty enabled region to KWindowEffects would
incorrectly blur the entire window. KWindowSystem owns Wayland objects, capability
changes and surface recreation. Shape updates are event-driven and coalesced;
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

Sources inspected for this implementation:
- [Niri 26.04 window effects](https://niri-wm.github.io/niri/Window-Effects.html)
- [KWindowSystem v6.30 Wayland implementation](https://github.com/KDE/kwindowsystem/blob/v6.30.0/src/platforms/wayland/windoweffects.cpp)
- [KWindowSystem v6.25 protocol support](https://github.com/KDE/kwindowsystem/blob/v6.25.0/src/platforms/wayland/windoweffects.cpp)
- Installed KWindowEffects header: region coordinates and empty-region semantics.

Only public library APIs are called; no upstream implementation code is copied.
