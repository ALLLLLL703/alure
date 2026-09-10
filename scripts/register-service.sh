#!/usr/bin/env bash
# Register only Alure's user service; never edit Niri configuration or stop shells.
set -euo pipefail
prefix=${HOME:?}/.local
start=false
usage() {
    printf '%s\n' 'Usage: scripts/register-service.sh [--prefix DIR] [--start]' \
        'Links the installed unit and enables Alure for future niri.service sessions.' \
        '--start additionally starts Alure now; requires an active systemd-managed Niri session.'
}
die() { printf 'Alure service: %s\n' "$*" >&2; exit 1; }
while (($#)); do
    case $1 in
        --prefix) (($# >= 2)) || die 'Missing value for --prefix'; prefix=$2; shift 2;;
        --start) start=true; shift;;
        --help|-h) usage; exit 0;;
        *) die "Unknown option: $1";;
    esac
done
[[ $EUID -ne 0 ]] || die 'Run as your normal desktop user, not with sudo.'
[[ $prefix == /* ]] || die 'Prefix must be an absolute path.'
[[ $prefix != *$'\n'* && $prefix != *$'\r'* ]] || die 'Prefix cannot contain newlines.'
prefix=$(realpath -m -- "$prefix")
unit=$prefix/lib/systemd/user/alure.service
[[ -x $prefix/bin/alure && -f $unit ]] || die "No installation at $prefix; run scripts/install.sh first."
command -v systemctl >/dev/null || die 'systemctl is required (or install with --no-service).'
[[ $(systemctl --user show niri.service --property=LoadState --value) == loaded ]] || die 'niri.service is not installed or the user manager is unavailable.'
if [[ $start == true ]]; then
    systemctl --user is-active --quiet niri.service || die 'Start Niri via niri-session or its display-manager session first.'
    systemctl --user show-environment | grep '^WAYLAND_DISPLAY=.' >/dev/null || die 'The user manager has no WAYLAND_DISPLAY; use the Niri session entry point.'
fi
# Do not replace a user's separate unit. Re-running against our own link is safe.
link=${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user/alure.service
if [[ -e $link || -L $link ]]; then
    [[ $link -ef $unit ]] || die "A different unit already exists at $link; resolve it explicitly before registering."
else
    systemctl --user link "$unit"
fi
systemctl --user daemon-reload
systemctl --user enable alure.service
printf 'Registered Alure for niri.service startup.\n'
if [[ $start == true ]]; then
    systemctl --user start alure.service
    systemctl --user --no-pager status alure.service
else
    printf 'Not started. Start explicitly: systemctl --user start alure.service\n'
fi
