#!/usr/bin/env bash
set -euo pipefail

REPOSITORY="${KUROGANE_REPOSITORY:-https://github.com/KrzysioYT/KuroganeOS.git}"
BRANCH="${KUROGANE_BRANCH:-gpt/kuroganeos-5-gui}"
INSTALL_ROOT="${KUROGANE_VPS_ROOT:-/opt/kuroganeos}"
CHECKOUT="$INSTALL_ROOT/repo"

if [[ $(id -u) -ne 0 ]]; then
    echo "bootstrap must run as root" >&2
    exit 1
fi

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y ca-certificates curl git docker.io docker-compose-plugin
systemctl enable --now docker

mkdir -p "$INSTALL_ROOT"
install -d -m 0755 "$INSTALL_ROOT/downloads"
if [[ -d "$CHECKOUT/.git" ]]; then
    git -C "$CHECKOUT" fetch origin "$BRANCH"
    git -C "$CHECKOUT" checkout "$BRANCH"
    git -C "$CHECKOUT" reset --hard "origin/$BRANCH"
else
    rm -rf "$CHECKOUT"
    git clone --depth=1 --branch "$BRANCH" "$REPOSITORY" "$CHECKOUT"
fi

cd "$CHECKOUT/deploy/vps"
if [[ ! -f .env ]]; then
    cp .env.example .env
fi

docker compose pull
docker compose up -d --remove-orphans

if command -v ufw >/dev/null 2>&1; then
    ufw allow 80/tcp || true
    ufw allow 443/tcp || true
fi

printf '\nKuroganeOS backend deployed.\n'
printf 'Portal:    https://kuroganeos.dev\n'
printf 'Docs:      https://docs.kuroganeos.dev\n'
printf 'Packages:  https://repo.kuroganeos.dev/index.kuro\n'
printf 'Downloads: https://downloads.kuroganeos.dev\n'
