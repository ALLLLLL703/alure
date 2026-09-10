#!/usr/bin/env bash
# Only a private X server; never targets the user's desktop. No quit-after timer.
set -euo pipefail
app=$1
work=$(mktemp -d)
server= app_pid=
trap 'test -z "$app_pid" || kill "$app_pid" 2>/dev/null || :; test -z "$server" || kill "$server" 2>/dev/null || :; rm -rf "$work"' EXIT
Xvfb -displayfd 3 -screen 0 1280x900x24 -nolisten tcp 3>"$work/display" >"$work/xvfb.log" 2>&1 & server=$!
for i in {1..100}; do test -s "$work/display" && break; sleep .03; done
export DISPLAY=:$(cat "$work/display") QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=software
unset WAYLAND_DISPLAY
for kind in clean dirty; do
    printf '[theme]\nname="midnight"\n' >"$work/config.toml"
    "$app" --settings --config "$work/config.toml" >"$work/app.log" 2>&1 & app_pid=$!
    wid=
    for i in {1..100}; do wid=$(xdotool search --pid "$app_pid" --name 'Alure Settings' 2>/dev/null | head -1 || :); test -n "$wid" && break; sleep .05; done
    test -n "$wid"
    xdotool windowfocus --sync "$wid"
    if test "$kind" = dirty; then
        # Configuration navigation, then raw editor; real pointer/key events.
        xdotool mousemove --window "$wid" 85 235 click 1
        sleep .15
        xdotool mousemove --window "$wid" 450 210 click 1 key ctrl+a
        xdotool type --clearmodifiers '# changed'
    fi
    xdotool key --clearmodifiers ctrl+w
    sleep .3
    if test "$kind" = dirty; then
        kill -0 "$app_pid" # Modal must still be alive, not silently discard.
        # Modal footer Discard, centered dialog at default dimensions.
        xdotool mousemove --window "$wid" 390 455 click 1
    fi
    for i in {1..100}; do ! kill -0 "$app_pid" 2>/dev/null && break; sleep .03; done
    if kill -0 "$app_pid" 2>/dev/null; then cat "$work/app.log"; echo "settings failed to exit ($kind)"; exit 1; fi
    wait "$app_pid"; app_pid=
    ! grep -E 'qrc:|Error|failed' "$work/app.log"
done
