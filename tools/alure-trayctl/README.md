# alure-trayctl

Display-independent (`QCoreApplication`) client of the **existing**
`org.kde.StatusNotifierWatcher` and `com.canonical.dbusmenu`. Never starts a
watcher, provider, app, shell, or provider-owned context menu.

```
alure-trayctl [--config PATH] [--timeout MS] list
alure-trayctl [options] menu TRAY_ID [SUBMENU_ID ...]
alure-trayctl [options] click TRAY_ID MENU_ID [MENU_ID ...]
```

`list` and `menu` write a JSON array to stdout. Copy the exact `id` from `list`
(service plus object path); menu IDs are positive integers on the current page.
Paths are relative to root: `menu ID 5` opens submenu 5; `click ID 5 7` opens 5
then sends `Event(7, "clicked", ...)`. Intermediate nodes must be enabled visible
submenus. Final clicks must be enabled visible non-separator leaves. No search,
label matching, or implicit activation occurs during traversal (maximum 32 IDs).

Exit 0 means reads completed, or, **for click only**, the provider acknowledged
Event. It does not prove the app performed its business operation. Exit 1 means
unavailable/no watcher/stale ID/no menu/refused action/D-Bus error/timeout; exit 2
means syntax/configuration error. Errors go to stderr. Timeout is an overall
100..600000 ms deadline, default 10000; a timeout after sending Event means its
outcome is unknown, not that it was undone. `--help` needs no bus or display.

Uses Alure's TOML config (default `$XDG_CONFIG_HOME/alure/config.toml`, missing
file uses defaults). Explicit invocation is independent of panel enablement;
`allow_actions=false` prohibits clicks but permits inspection. Uses the shared
TrayService/TrayMenu implementation and its 128-app/512-row/depth-32 bounds.

Build: `cmake -S . -B build && cmake --build build --target alure-trayctl`.
Install is provided through the normal CMake install target; no auto-downloads.
Protocol references (API consultation only, no copied implementation):
https://specifications.freedesktop.org/status-notifier-item/latest-single and
https://github.com/gnustep/libs-dbuskit/blob/master/Bundles/DBusMenu/com.canonical.dbusmenu.xml .
The project intentionally retains the deployed `org.kde` watcher/item namespace.
