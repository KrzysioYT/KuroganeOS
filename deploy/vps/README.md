# KuroganeOS VPS backend

This directory defines the public KuroganeOS backend: portal, browser-friendly docs and the official Anvil package mirror.

## Default public hosts

- `https://kuroganeos.147-79-62-37.sslip.io`
- `https://docs.kuroganeos.147-79-62-37.sslip.io`
- `https://packages.kuroganeos.147-79-62-37.sslip.io`

`sslip.io` resolves the embedded IPv4 address automatically, so these names need no DNS-zone ownership. Caddy obtains normal public TLS certificates with HTTP-01 once ports 80 and 443 reach the VPS.

## Deploy

On a Debian VPS as root:

```bash
curl -fsSL https://raw.githubusercontent.com/KrzysioYT/KuroganeOS/gpt/kuroganeos-5-gui/deploy/vps/bootstrap.sh | bash
```

The bootstrap installs Docker, clones the active KuroganeOS branch under `/opt/kuroganeos`, starts Caddy, and starts a package mirror sync loop. No VPS password or private key is stored in this repository.

## Update

```bash
cd /opt/kuroganeos/repo
git fetch origin gpt/kuroganeos-5-gui
git reset --hard origin/gpt/kuroganeos-5-gui
cd deploy/vps
docker compose up -d --pull always --remove-orphans
```

## Verify

```bash
cd /opt/kuroganeos/repo/deploy/vps
./scripts/check.sh
```

The package container synchronizes `KrzysioYT/KuroganeOS-Packages` every five minutes. Caddy serves the current `index.kuro`, manifests and payload ELF files directly from the mirror volume.

## Custom domain later

Copy `.env.example` to `.env` and replace `PORTAL_HOST`, `DOCS_HOST`, and `PACKAGES_HOST`. Caddy will request certificates for the new names after their A/AAAA records point at the VPS.
