# KuroganeOS VPS backend

This directory defines the public KuroganeOS backend: portal, browser-friendly docs and the official Anvil package mirror.

## Public hosts

- `https://kuroganeos.dev`
- `https://docs.kuroganeos.dev`
- `https://repo.kuroganeos.dev`
- `https://downloads.kuroganeos.dev`

The apex and all listed subdomains must have `A` records pointing to
`147.79.62.37`. Caddy obtains public TLS certificates once DNS and ports 80/443
reach the VPS.

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

The package container synchronizes `KrzysioYT/KuroganeOS-Packages` every five minutes. Caddy serves the current `index.kuro`, manifests and payload ELF files directly from the mirror volume. Release media and `latest.json` live in `/opt/kuroganeos/downloads` and are published separately from the Git checkout.

## DNS records

Create `A` records for `@`, `www`, `docs`, `repo` and `downloads`, all pointing
to `147.79.62.37`. Do not create an `AAAA` record unless the VPS has working
public IPv6.
