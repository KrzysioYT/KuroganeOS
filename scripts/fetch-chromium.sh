#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
revision="4137589c17766b2c0036332e00ad0d453e342a92"
destination="$root/third_party/chromium/src"

command -v git >/dev/null 2>&1 || {
    echo "git is required to fetch Chromium" >&2
    exit 1
}

mkdir -p "$root/third_party/chromium"
if [[ ! -d "$destination/.git" ]]; then
    echo "[chromium] cloning official Chromium mirror (partial clone)"
    git clone --filter=blob:none --no-checkout https://github.com/chromium/chromium.git "$destination"
fi

git -C "$destination" remote set-url origin https://github.com/chromium/chromium.git
git -C "$destination" fetch --depth=1 origin "$revision"
git -C "$destination" checkout --detach "$revision"

# Keep the local checkout small while exposing the pieces used to design the
# first KuroganeOS port. A full Chromium build will require widening this set.
git -C "$destination" sparse-checkout init --cone || true
git -C "$destination" sparse-checkout set \
    content/shell \
    content/public \
    base \
    net \
    url \
    third_party/blink/public

echo "[chromium] source ready"
echo "[chromium] revision: $revision"
echo "[chromium] path: $destination"
echo "[chromium] this checkout is developer input and is not bundled into KuroganeOS images"
