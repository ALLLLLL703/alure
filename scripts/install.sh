#!/usr/bin/env bash
# User-local build/install; dependencies must already be installed by the user.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
prefix=${HOME:?}/.local
build_dir=$root/build-install
jobs=2
register=true
start=false
usage() {
    printf '%s\n' 'Usage: scripts/install.sh [--prefix DIR] [--build-dir DIR] [--jobs N] [--no-service] [--start]' \
        'Builds and installs Alure, then registers it for Niri session startup.' \
        'Default: ~/.local, build-install/, 2 build jobs; does not start the service.' \
        '--no-service skips all systemd operations. --start starts in an active Niri session.'
}
die() { printf 'Alure install: %s\n' "$*" >&2; exit 1; }
while (($#)); do
    case $1 in
        --prefix|--build-dir|--jobs)
            (($# >= 2)) || die "Missing value for $1"
            case $1 in --prefix) prefix=$2;; --build-dir) build_dir=$2;; --jobs) jobs=$2;; esac
            shift 2;;
        --no-service) register=false; shift;;
        --start) start=true; shift;;
        --help|-h) usage; exit 0;;
        *) die "Unknown option: $1";;
    esac
done
[[ $EUID -ne 0 ]] || die 'Run as your normal desktop user, not with sudo.'
[[ $prefix == /* && $build_dir == /* ]] || die 'Prefix and build directory must be absolute paths.'
[[ $prefix != *$'\n'* && $prefix != *$'\r'* ]] || die 'Prefix cannot contain newlines.'
[[ $jobs =~ ^[1-9][0-9]*$ ]] || die '--jobs must be a positive integer.'
[[ $register == true || $start == false ]] || die '--start cannot be combined with --no-service.'
for tool in cmake ninja c++; do command -v "$tool" >/dev/null || die "Missing build tool: $tool (see README.md for system packages)."; done
prefix=$(realpath -m -- "$prefix")
build_dir=$(realpath -m -- "$build_dir")
cmake -S "$root" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_INSTALL_BINDIR=bin -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_INSTALL_DATADIR=share
cmake --build "$build_dir" --parallel "$jobs"
cmake --install "$build_dir"
printf 'Installed: %s/bin/alure\nExisting Alure configuration was not changed.\n' "$prefix"
if [[ $register == true ]]; then
    args=(--prefix "$prefix")
    [[ $start == false ]] || args+=(--start)
    "$root/scripts/register-service.sh" "${args[@]}"
fi
