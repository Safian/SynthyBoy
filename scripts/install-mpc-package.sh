#!/bin/bash
# SynthyBoy developer package installer. Shared by the Key 61 and XL wrappers.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail

ssh_options=(-o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null
             -o LogLevel=ERROR -o ConnectTimeout=8)
case "${1-}" in
    key61)
        device=k61
        host=root@192.168.0.30
        plugin_root='/sdcard/VST2 Plugins'
        filmstrips=/var/tmp/filmstrips
        arch=arm32
        other_arch=arm64
        ;;
    xl)
        device=xl
        host=root@192.168.0.126
        plugin_root='/nvme/VST2 Plugins'
        filmstrips=/var/volatile/tmp/filmstrips
        arch=arm64
        other_arch=arm32
        ssh_options+=(-o PreferredAuthentications=publickey,none)
        ;;
    *) echo "Use k61-install.sh or xl-install.sh." >&2; exit 2 ;;
esac
shift

usage() {
    printf 'Usage: %s-install.sh PACKAGE_DIR\n' "$device"
    printf 'Install the supplied SynthyBoy package and restart acvs without opening a project.\n'
}
if [[ "${1-}" == --help || "${1-}" == -h ]]; then
    usage
    exit 0
fi
# Accept the legacy XL argument. Both installers already leave projects alone.
if [[ "$device" == xl && "$#" == 2 && "$2" == noreopen ]]; then
    set -- "$1"
fi
if [[ "$#" != 1 || ! -d "$1" ]]; then
    usage >&2
    exit 2
fi

package=$1
if [[ ! -d "$package/Plugin Skins" || ! -f "$package/plugin.json" || ! -f "$package/version.xml" ]]; then
    echo "Expected Plugin Skins, plugin.json and version.xml in $package." >&2
    exit 2
fi
if [[ -e "$package/VST" && ! -s "$package/VST/synthyboy-$arch.so" ]]; then
    echo "Missing VST/synthyboy-$arch.so in $package." >&2
    exit 2
fi

# Complete the local archive before the remote side can activate anything.
python3 "$(dirname "$0")/validate_synthyboy_skin.py" "$package"
scratch=$(mktemp -d)
archive="$scratch/package.tar"
trap 'rm -rf "$scratch"' EXIT
trap 'exit 1' HUP INT TERM
(cd "$package" && find 'Plugin Skins' -type f ! -name '._*' ! -name .DS_Store -exec shasum -a 256 {} +) > "$scratch/skin.sha256"
# BusyBox tar on the MPC cannot restore the macOS default GNU sparse entries.
COPYFILE_DISABLE=1 tar --format=ustar --exclude '._*' --exclude '.DS_Store' -cf "$archive" -C "$package" . -C "$scratch" skin.sha256

remote=$(cat <<'REMOTE'
set -eu

target="$plugin_root/Safian - VST - SynthyBoy"
work="$plugin_root/.synthyboy-install"
candidate="$work/package"
previous="$work/previous"
restart_needed=0
committed=0

mkdir -p "$plugin_root"
if ! mkdir "$work"; then
    echo "An install is in progress or awaiting recovery in $work." >&2
    exit 1
fi

finish() {
    status=$?
    trap - EXIT
    if [ "$committed" = 0 ]; then
        # Once moved into place, the candidate no longer exists at its old path.
        if [ "$restart_needed" = 1 ] && [ ! -d "$candidate" ]; then
            mv "$target" "$candidate" || exit 1
        fi
        if [ -d "$previous" ]; then
            mv "$previous" "$target" || exit 1
        fi
    fi
    if [ "$restart_needed" = 1 ]; then
        systemctl start acvs || status=1
    fi
    rm -rf "$work"
    exit "$status"
}
trap finish EXIT
trap 'exit 1' HUP INT TERM

mkdir "$candidate"
tar -xf - -C "$candidate"
(cd "$candidate" && sha256sum -c skin.sha256 > /dev/null)
rm "$candidate/skin.sha256"

# Package directories replace their installed counterparts. Keep optional
# directories that were omitted, as on a skin-only build, and local extras.
for old in "$target"/* "$target"/.[!.]* "$target"/..?*; do
    [ -e "$old" ] || [ -L "$old" ] || continue
    new="$candidate/${old##*/}"
    [ -e "$new" ] || [ -L "$new" ] || cp -R "$old" "$new"
done
if [ ! -s "$candidate/VST/synthyboy-$arch.so" ]; then
    echo "No $arch SynthyBoy binary in the package or installed VST folder." >&2
    exit 1
fi
# Keep the installed binary and helper for the other architecture when a
# development package only builds this device's architecture.
for name in "synthyboy-$other_arch.so" "synthyboy-overlay-gpu-$other_arch"; do
    old="$target/VST/$name"
    new="$candidate/VST/$name"
    if [ -f "$old" ] && [ ! -e "$new" ]; then
        cp -p "$old" "$new"
    fi
done

restart_needed=1
systemctl stop acvs
if [ "$arch" = arm64 ]; then
    sleep 2
    rm -f /tmp/synthyboy-overlay-*
else
    rm -f /tmp/synthyboy-overlay-status.txt
fi
rm -f "$filmstrips"/temp_*.img
[ ! -d "$target" ] || mv "$target" "$previous"
mv "$candidate" "$target"
md5sum "$target/VST/synthyboy-$arch.so"
systemctl start acvs
committed=1
restart_needed=0
REMOTE
)

ssh "${ssh_options[@]}" "$host" \
    "plugin_root='$plugin_root'; filmstrips='$filmstrips'; arch='$arch'; other_arch='$other_arch'
$remote" < "$archive"
echo "installed; app started"
