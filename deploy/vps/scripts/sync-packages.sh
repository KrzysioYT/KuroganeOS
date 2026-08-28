#!/bin/sh
set -eu

repo="${PACKAGE_REPOSITORY:-https://github.com/KrzysioYT/KuroganeOS-Packages.git}"
branch="${PACKAGE_BRANCH:-main}"
interval="${SYNC_INTERVAL_SECONDS:-300}"
target=/srv/packages

sync_once() {
    if [ -d "$target/.git" ]; then
        git -C "$target" fetch --depth=1 origin "$branch"
        git -C "$target" reset --hard "origin/$branch"
        git -C "$target" clean -fdx
    else
        rm -rf "$target"
        git clone --depth=1 --branch "$branch" "$repo" "$target"
    fi
    date -u +%Y-%m-%dT%H:%M:%SZ > "$target/.mirror-updated"
    echo "[packages] mirror synced: $(cat "$target/.mirror-updated")"
}

while true; do
    if ! sync_once; then
        echo "[packages] sync failed; retaining last good mirror" >&2
    fi
    sleep "$interval"
done
