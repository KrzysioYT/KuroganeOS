#!/usr/bin/env bash
set -euo pipefail

configuration="${1:-release}"
case "$configuration" in
  debug|release|test) ;;
  *) echo "usage: $0 [debug|release|test]" >&2; exit 2 ;;
esac

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export ANVIL_REPO_HOST="packages.kuroganeos.147-79-62-37.sslip.io"
export ANVIL_REPO_BASE="/KrzysioYT/KuroganeOS-Packages/main"

echo "[official] Anvil endpoint: https://${ANVIL_REPO_HOST}/index.kuro"

if [[ -x "$root/scripts/build.sh" ]]; then
  exec "$root/scripts/build.sh" "$configuration"
fi

if [[ "$(uname -s)" == "Darwin" && -x "$root/scripts/build-macos.sh" ]]; then
  exec "$root/scripts/build-macos.sh" --configuration "$configuration" --rebuild
fi

echo "No supported Unix build entry point found." >&2
exit 1
