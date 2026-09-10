#!/usr/bin/env bash
# No real user-manager calls or writes outside this temporary directory.
set -euo pipefail
[[ $EUID -ne 0 ]] || exit 77
root=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export HOME=$work/home XDG_CONFIG_HOME=$work/home/.config
export ALURE_TEST_LOG=$work/commands ALURE_TEST_PREFIX=$work/prefix-state
mkdir -p "$work/bin" "$XDG_CONFIG_HOME/alure"
printf 'existing user configuration\n' >"$XDG_CONFIG_HOME/alure/config.toml"
cat >"$work/bin/cmake" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
printf 'cmake %s\n' "$*" >>"$ALURE_TEST_LOG"
case $1 in
    -S) for arg in "$@"; do [[ $arg != -DCMAKE_INSTALL_PREFIX=* ]] || printf '%s' "${arg#*=}" >"$ALURE_TEST_PREFIX"; done;;
    --build) [[ ${FAIL_BUILD:-false} == false ]];;
    --install)
        prefix=$(cat "$ALURE_TEST_PREFIX")
        mkdir -p "$prefix/bin" "$prefix/lib/systemd/user"
        printf '#!/bin/sh\nexit 0\n' >"$prefix/bin/alure"; chmod +x "$prefix/bin/alure"
        printf '[Service]\nExecStart="%s/bin/alure"\n[Install]\nWantedBy=niri.service\n' "$prefix" >"$prefix/lib/systemd/user/alure.service";;
    *) exit 99;;
esac
MOCK
cat >"$work/bin/systemctl" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
printf 'systemctl %s\n' "$*" >>"$ALURE_TEST_LOG"
case $2 in
    show) printf 'loaded\n';;
    is-active) [[ ${NO_SESSION:-false} == false ]];;
    show-environment) printf 'WAYLAND_DISPLAY=wayland-test\n';;
    link) mkdir -p "$XDG_CONFIG_HOME/systemd/user"; ln -s "$3" "$XDG_CONFIG_HOME/systemd/user/alure.service";;
    daemon-reload) :;;
    enable)
        mkdir -p "$XDG_CONFIG_HOME/systemd/user/niri.service.wants"
        ln -sfn ../alure.service "$XDG_CONFIG_HOME/systemd/user/niri.service.wants/alure.service";;
    start|--no-pager) :;;
    *) exit 99;;
esac
MOCK
chmod +x "$work/bin/"*
export PATH="$work/bin:$PATH"
prefix=$work/install\ prefix
"$root/scripts/install.sh" --prefix "$prefix" --build-dir "$work/build" --no-service
if grep '^systemctl ' "$ALURE_TEST_LOG"; then exit 1; fi
[[ $(cat "$XDG_CONFIG_HOME/alure/config.toml") == 'existing user configuration' ]]
"$root/scripts/register-service.sh" --prefix "$prefix"
[[ $XDG_CONFIG_HOME/systemd/user/alure.service -ef $prefix/lib/systemd/user/alure.service ]]
[[ -L $XDG_CONFIG_HOME/systemd/user/niri.service.wants/alure.service ]]
if grep 'systemctl --user start' "$ALURE_TEST_LOG"; then exit 1; fi
"$root/scripts/install.sh" --prefix "$prefix" --build-dir "$work/build" --start
[[ $(grep -c 'systemctl --user link' "$ALURE_TEST_LOG") == 1 ]]
grep 'systemctl --user start alure.service' "$ALURE_TEST_LOG"
: >"$ALURE_TEST_LOG"
if FAIL_BUILD=true "$root/scripts/install.sh" --prefix "$prefix" --build-dir "$work/build"; then exit 1; fi
if grep -E '^cmake --install|^systemctl' "$ALURE_TEST_LOG"; then exit 1; fi
if NO_SESSION=true "$root/scripts/register-service.sh" --prefix "$prefix" --start; then exit 1; fi
if grep 'systemctl --user start' "$ALURE_TEST_LOG"; then exit 1; fi
rm "$XDG_CONFIG_HOME/systemd/user/alure.service"
printf 'user unit\n' >"$XDG_CONFIG_HOME/systemd/user/alure.service"
if "$root/scripts/register-service.sh" --prefix "$prefix"; then exit 1; fi
[[ $(cat "$XDG_CONFIG_HOME/systemd/user/alure.service") == 'user unit' ]]
if "$root/scripts/install.sh" --start --no-service; then exit 1; fi
if "$root/scripts/install.sh" --jobs zero; then exit 1; fi
if "$root/scripts/install.sh" --prefix; then exit 1; fi
if "$root/scripts/register-service.sh" --prefix relative; then exit 1; fi
[[ $(cat "$XDG_CONFIG_HOME/alure/config.toml") == 'existing user configuration' ]]
printf 'Install/service script checks passed.\n'
