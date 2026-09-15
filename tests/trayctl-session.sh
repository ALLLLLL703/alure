#!/usr/bin/env bash
set -euo pipefail
# Activation fixtures exist only on this test's private bus; never install services.
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
export XDG_DATA_HOME="$dir"
export ALURE_ACTIVATION_MARKER="$dir/activated"
mkdir -p "$dir/dbus-1/services"
printf '#!/bin/sh\ntouch "%s"\nexit 1\n' "$ALURE_ACTIVATION_MARKER" > "$dir/activate.sh"
for name in org.kde.StatusNotifierWatcher org.alure.ActivatableFixture; do
    printf '[D-BUS Service]\nName=%s\nExec=/bin/sh %s/activate.sh\n' "$name" "$dir" > "$dir/dbus-1/services/$name.service"
done
dbus-run-session -- "$@"
