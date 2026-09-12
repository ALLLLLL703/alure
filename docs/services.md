# System services — stage 2 handoff and readiness

This document describes the service-layer contracts. The subsequent v0.1
[interface stage](interface.md) now renders these providers in Panel.qml and details. Never present unavailable data as zero,
an empty successful result, a fake workspace, or a battery on a desktop machine.

## QML / ownership API

`Services` is a context object in panel and preview mode, absent in settings and
headless validation. It owns its twelve QObject services by value; the engine and
panels are destroyed before the services. Do not reparent or delete these objects.
The context properties are `workspaces`, `media`, `tray`, `volume`, `updates`,
`wifi`, `bluetooth`, `notifications`, `battery`, `clipboard`, `brightness`, `taskbar`. Each exposes:

- `enabled`, `available`, `busy`, `diagnostic`, `state` (QVariantMap), `items`
  (QVariantList of row maps). Each property has its own equality-guarded NOTIFY
  signal (`itemsChanged`, `stateChanged`, `busyChanged`, etc.). Background busy
  changes and identical snapshots do not invalidate QML list bindings. The
  aggregate `changed` signal remains for C++ consumers and pending-action state.
  Genuine snapshot changes still update their bindings; this is not a keyed
  incremental model for reordered or changed provider rows.
- `refresh()` requests a read if enabled and not busy. Ready popup status stays
  stable during ordinary polls. Popup controls retain focus/pressed state rather
  than disabling on every poll; one latest requested action can wait for busy to
  clear, dispatched on the next event turn after the provider callback returns.
  Availability loss, popup dismissal, configuration reload and (for media) a
  selected target-player change cancel that waiting request. Volume setters keep their
  existing service-side coalescing. The backend still validates target IDs and
  permissions, and the action result remains asynchronous.
- `action(name, arguments)` returns whether a request was accepted, **not whether
  the asynchronous operation succeeded**. Invalid targets, unsupported actions,
  disabled actions or a busy service return false. Follow busy/diagnostic after
  acceptance. Errors clear snapshots, set available=false, and retry at interval.
- Pending calls are tied to service lifetime/config generation; a reload cannot
  publish an old async reply. Execution config changes stop the old process/socket,
  clear snapshots and restart. Style/format-only changes do not restart services;
  notification DND-only changes apply live without clearing history or the bus name.
  Disabled services have no timers/processes or owned bus names. Services run when
  their module is enabled, even if no panel currently lists it; set enabled=false
  to opt out. Notification server additionally requires server_enabled=true.

Use `available`, not list length, as the health signal. Successful empty WiFi scan,
zero updates, empty tray or empty notification history are legitimate observations.
For a row-list Repeater, `modelData` is the row. Keep selected stable IDs across
snapshot replacement, not row indices. Place all display formatting, selection,
icons, ordering, popup geometry and interactions under the existing TOML UI model.

Workspace streams accept fragmented/batched newline JSON with a 1 MiB per-line
limit. Reload/disable destroys connection state, deadlines, partial lines and
pending actions; retries never replay actions. Reconnection requires a fresh
acknowledgement and full workspace snapshot. Settings exposes the existing retry,
timeout, socket, action-permission and ordering options; defaults/types/ranges
are unchanged. Saved execution changes reload the subscription (restart when
configuration watching is disabled).

Event semantics follow the official [Niri IPC documentation](https://github.com/niri-wm/niri/wiki/IPC)
and [Event contract](https://docs.rs/niri-ipc/latest/niri_ipc/enum.Event.html), checked
against cached installed Niri 26.04 protocol source. Regression sources cover
external/internal activation without polling, exact IDs above 2^53, multi-output
active/focused distinctions, full replacement, fragmented/batched events,
reconnect, deadlines/errors and cleanup. These latency-change tests were **not run**
in the code-only implementation phase.

## Feature matrix and action contracts

| Service | Live snapshot and actions | Supported boundary / readiness |
|---|---|---|
| Niri | items: `id` **string**, `idx`, nullable `name`, `output`, `is_active`, `is_focused` and upstream workspace fields. `activate({id})`; `state.actionError` reports rejected/timed-out actions without discarding healthy workspace data. | Standalone native newline JSON `EventStream`, independent of taskbar/panel geometry. `WorkspacesChanged` replaces the cache; `WorkspaceActivated` changes that output's active workspace and only changes global focus when `focused=true`. Urgency/active-window events also update rows. `interval_ms` retries disconnected streams, never polls healthy ones. Initial ACK/snapshot and action ACK use `timeout_ms`; healthy idle streams have no deadline. Stable **ID** activation uses a separate serialized socket; ACKs do not fabricate focus. `ordering="output-index"` sorts output/numeric idx/numeric ID; `"provider"` preserves full-snapshot order through incremental updates. Missing socket, disconnect, malformed/oversized events and initial timeout clear data. |
| MPRIS | items: `service`, derived `identity`, `title`, `artist` string list, `album`, `artUrl`, `playbackStatus`, `trackId`, `positionUs/lengthUs`, `shuffle/loopStatus`, `hasShuffle/hasLoopStatus`, `CanControl/CanPlay/CanPause/CanGoNext/CanGoPrevious/CanSeek`. `playPause/next/previous({service})`, `setPosition({service,trackId,positionUs})`, `setShuffle({service,shuffle:bool})`, `setLoopStatus({service,loopStatus})` | Session-bus discovery/GetAll polling, maximum 64 players. Playing-first selection (then Paused/Stopped); optional preferred service/prefix overrides it. Unreadable players are skipped rather than hiding healthy ones. Capability checks, signed microseconds and current track-path validation before seek. No queue/player launching. QML only loads local file or opted-in HTTP(S) covers while the dropdown is visible, with bounded decode dimensions and no image cache. |
| SNI tray | items: `id` (bus name + path), `Title`, `Status`, `IconName`, `AttentionIconName`, `ItemIsMenu`, `Menu` path string, `IconThemePath`, `iconUrl` PNG data URL. `activate/secondaryActivate({id,x,y})`; `openMenu(id)` exposes `menu.items/loading/error/canGoBack`, `menu.select(id)/back()/close()` | Hosts org.kde.StatusNotifierWatcher when free; otherwise cooperates with existing watcher and registers a host. No name stealing/queueing. Registration/removal and polling property changes; max 128 items. ARGB network-order pixmaps converted to PNG, largest valid image up to 512×512. ItemIsMenu routes primary clicks to the anchored host-side DBusMenu. GetLayout/AboutToShow/Event, LayoutUpdated/ItemsPropertiesUpdated are supported; 512 entries per level, 32 submenu levels. No provider ContextMenu calls, overlays, tooltip rendering, Scroll or legacy XEmbed. IconThemePath exposed but not searched by current icon provider. |
| Volume | state: `percent`, `muted`, `backend`, `canSetVolume`, `canMute`; PulseAudio also reports sink identity and server name. `setVolume({percent})`, `adjustVolume({delta})`, `toggleMute({})`; `adjusting` property tracks pending writes/readback; separate `pendingPercent` is a requested target, not observed state. | Async wpctl or pactl JSON; auto detects readable output and remembers the working backend. Coalesced setters/wheel input can queue while busy, configurable maximum, no mutation fallback. Default sink only; no microphone or stream mixer. See [audio backends](audio.md). |
| Updates | state: `count`; items: `name/current/next`. `update({})` | Async checkupdates; exit 2 is successful zero. Other nonzero exits fail. Only explicit action executes update_command, empty by default. No automatic install, AUR provider, privilege prompt or upgrade-output UI. Terminal argv can be configured by user; not run during tests. |
| WiFi | state: active row `ssid/signal/active` if connected, `connected`, `powered`, `count`, `savedConnections` rows `uuid/name`. items: cached AP `active/ssid/signal` rows. `setPowered({powered:bool})`, `connectSaved({uuid})` | NetworkManager via nmcli: cached APs (`--rescan no`), radio state, saved WiFi UUIDs. Escaped colons/backslashes handled. Only observed saved WiFi UUIDs accepted. No forced scans, new connections, password collection/storage, secret agent or Ethernet/VPN UI. Existing NetworkManager secret/polkit agents may be needed; absence becomes a timeout/error. Empty AP list does not prove hardware presence; radio state is NetworkManager's global WiFi flag. |
| Bluetooth | state: `adapters` rows `path/Powered/Discovering/Alias/Name/Address`, `connectedCount`; items: `path/Adapter/Connected/Paired/Trusted/Alias/Name/Address/Icon`. `setPowered({path,powered:bool})`, `connect/disconnect({path})` | BlueZ ObjectManager on system bus. At most 32 adapters/256 known devices. Connect only paired observed devices. No pairing/trust modification, agent, discovery session or Bluetooth battery support; polkit errors exposed. Missing adapter is unavailable. |
| Clipboard | items: `id/label`; `preview` and the per-ID `previews` map expose kind, id, text or memory-image URL, dimensions/byte count. `openView/closeView/previewItem(id)`, `copy/delete({id})`, `wipe({confirmed:true})`; delete also requires confirmed=true by default | Lazy existing cliphist database, no recorder. Binary-safe decode/copy via wl-copy, bounded one-at-a-time commands and image previews. Closed/unloaded is unavailable, not a fabricated empty history. Clear buffers on close, visible-entry decoding with a bounded inline thumbnail cache. See [clipboard contracts/limits](clipboard.md). |
| Notifications | state: `dnd/count/activeCount`; chronological items: `id/sender/appName/icon/iconUrl/desktopEntry/pid/summary/body/actions/urgency/active/createdAt/expiresAt/suppressed`, `closeReason` after closure; actions are `key/label` rows. `setDnd({dnd:bool})`, `dismiss({id})`, `invoke({id,key})`, `activate({id})`, `clearHistory({})` | Opt-in freedesktop server, does not replace an existing daemon. GetCapabilities/GetServerInformation/Notify/CloseNotification and close/action signals implemented. Replacement requires same sender and active ID. Plain text body, no markup/sound. Bounded raw image hints/local images/theme icons; default-action invocation plus Niri sender focus by broker-resolved PID or exact app_id, without guessing ambiguous windows. See [UI refinements](ui-refinement.md). DND retains history with suppressed=true; UI suppresses banners without replay on DND-off. History is not persisted across process/reconfiguration, except DND-only changes now apply live. The dropdown persists its DND choice to TOML by default (`persist_dnd=false` opts out); direct `setDnd` remains session-only and resets to the configured value on an execution-config restart. |
| Battery | state: first readable battery `name/percent/status`; items: all readable batteries | Linux sysfs capacity/status, configurable root for fixtures. No UPower dependency; no fabricated average, power profile action or time-to-empty. Missing/invalid battery is unavailable; UI should choose a row or honestly show multiple. |
| Calendar/clock | Local-date QML calendar/clock | Configured interval/date patterns, 42-cell navigating month grid; no event provider. |

Notification expiry 0 remains active until closed or bounded-history eviction;
negative client expiry uses default_expire_ms; positive values clamp to max_expire_ms.
Expiry checked at interval_ms granularity (default 1 second). History evicts oldest,
closing active evictions with reason 3. Expiry=1, dismissal=2, explicit close=3;
action invocation emits ActionInvoked and closes with reason 2. Text bounds:
app name 256, icon 1024, summary 1024, body 16384 characters; at most 32 action
pairs with 256-character keys/labels. Body is **plain untrusted text**; UI must set
Text.PlainText and must not execute links/actions as arbitrary commands.

## Resource/failure boundaries

All external data reads and controls are asynchronous QProcess, local sockets or
DBus calls; polls do not overlap. DBus name registration/unregistration itself
uses Qt's synchronous **broker-only** API at acquisition/release; recurring owner
checks and property reads are asynchronous. DBus deadline is timeout_ms per call,
not an entire multi-player enumeration deadline. Each module can have at most
128 pending DBus requests; config changes discard pending watchers. Qt/DBus has
its own incoming-message size limits; unlike process/socket responses these are
not capped before Qt demarshals a reply.

Processes run direct argv, never shell interpolation, with LC_ALL=C, a 1 MiB
combined stdout/stderr cap and deadline; process errors/timeouts are surfaced.
Cancellation kills only Alure's direct child, not arbitrary descendants. Custom
commands must not daemonize or spawn uncontrolled children; a launched terminal's
own lifetime is outside Alure. **The updates timeout also applies to an explicit
update_command: it can terminate the direct process. Do not configure a package
manager directly unless you accept this; the empty default is intentionally safe.**
QProcess destruction can briefly wait for the
killed direct child during shutdown; there are no waitForFinished UI polls.
Niri responses have a 1 MiB cap and deadline. Sysfs reads are bounded 4096-byte
local files, not network filesystem support.

MPRIS/tray/BlueZ and WiFi are periodic snapshots, not instant signal-driven models.
An item disappearing during a tray/BlueZ batch can fail that snapshot; next successful
poll recovers. MPRIS skips individual unreadable players; an all-failed batch is unavailable. A permanently broken tray item can make that batch unavailable.
A session/system bus disconnect is reported, not faked. Qt default connection
recovery after a **bus daemon restart** is not guaranteed: restart Alure then.
Normal player/BlueZ service/Niri disappearance is retried without shell restart.
Removing the last panel does not disable configured services. Preview runs enabled
read services too and can host the tray; settings/validation never do.

## Reproducible safe verification

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
# Isolated session AND system bus fixtures, no actual device controls:
dbus-run-session -- ./build/tests/services_tests
./build/alure --validate-config --config config/default.toml
./build/alure --validate-config --config config/services-example.toml
```

Tests cover strict parsers, command timeout/output cap/missing executable/cancel,
config validation/customization, disable/reload race, debounce, update opt-in,
cached WiFi/radio/saved fixtures, battery files, Niri multi-output ID activation and
reconnect, shuffled multi-output ordering (numeric indices/IDs including IDs above
2^53, null output and provider-order reload), MPRIS discovery/capabilities/actions, BlueZ managed objects, default
notification opt-out/name conflicts, real private-DBus Notify/expiry/action signals,
replacement/DND/history bounds, SNI own/existing watcher cooperation, removal and
pixmap byte order, and QML context access. No actual desktop controls or upgrade
commands are used. Offscreen CLI tests are not Niri visual validation. Independent
stabilization review and broader desktop interoperability/visual verification remain
required. The parent also verified stable workspace ID activation on nested Niri
26.04 at baseline `8c384d1`; see [bounded live evidence](interface.md#bounded-live-validation-reported-by-parent).
That single-output check does not validate physical providers or these new fixes.

## Research sources (retrieved for this stage)

- Niri authoritative serde requests/replies and WorkspaceReferenceArg::Id:
  https://github.com/YaLTeR/niri/blob/main/niri-ipc/src/lib.rs
- MPRIS Player interface / capability requirements:
  https://specifications.freedesktop.org/mpris-spec/latest/Player_Interface.html
  (legacy URL now redirects/404s; current page inspected for seek/optional properties:
  https://specifications.freedesktop.org/mpris/latest/Player_Interface.html)
- StatusNotifierWatcher / item specification:
  https://www.freedesktop.org/wiki/Specifications/StatusNotifierItem/StatusNotifierWatcher/
  and https://www.freedesktop.org/wiki/Specifications/StatusNotifierItem/StatusNotifierItem/
- Notification methods/expiry/closed reasons:
  https://specifications.freedesktop.org/notification/latest/protocol.html
- BlueZ Device API: https://github.com/bluez/bluez/blob/master/doc/org.bluez.Device.rst
- NetworkManager radio API: https://networkmanager.dev/docs/api/latest/gdbus-org.freedesktop.NetworkManager.html
- WirePlumber: installed `wpctl --help` verified the get-volume/set-volume/set-mute
  interface. Attempted https://pipewire.pages.freedesktop.org/wireplumber/tools/wpctl.html
  returned HTTP 404; it is not claimed as verified upstream documentation.
- checkupdates exit status: https://man.archlinux.org/man/checkupdates.8.en
- Installed `niri msg action focus-workspace --help` confirms CLI references are
  index/name, hence native JSON ID actions are used instead. Installed wpctl/nmcli
  help and upstream documents were inspected; no upstream implementation code or
  third-party assets copied. Authoritative references are protocol evidence, not
  claims of live desktop interoperability.
