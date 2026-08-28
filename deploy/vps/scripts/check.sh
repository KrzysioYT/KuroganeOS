#!/usr/bin/env bash
set -euo pipefail

hosts=(
  "${PORTAL_HOST:-kuroganeos.147-79-62-37.sslip.io}"
  "${DOCS_HOST:-docs.kuroganeos.147-79-62-37.sslip.io}"
  "${PACKAGES_HOST:-packages.kuroganeos.147-79-62-37.sslip.io}"
)

for host in "${hosts[@]}"; do
    echo "[check] https://$host/healthz"
    curl --fail --silent --show-error --max-time 15 "https://$host/healthz"
done

curl --fail --silent --show-error --max-time 15 \
  "https://${PACKAGES_HOST:-packages.kuroganeos.147-79-62-37.sslip.io}/index.kuro" \
  | head -n 3
