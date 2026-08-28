#!/usr/bin/env bash
set -euo pipefail

hosts=(
  "${PORTAL_HOST:-kuroganeos.dev}"
  "${DOCS_HOST:-docs.kuroganeos.dev}"
  "${PACKAGES_HOST:-repo.kuroganeos.dev}"
  "${DOWNLOADS_HOST:-downloads.kuroganeos.dev}"
)

for host in "${hosts[@]}"; do
    echo "[check] https://$host/healthz"
    curl --fail --silent --show-error --max-time 15 "https://$host/healthz"
done

curl --fail --silent --show-error --max-time 15 \
  "https://${PACKAGES_HOST:-repo.kuroganeos.dev}/index.kuro" \
  | head -n 3

curl --fail --silent --show-error --max-time 15 \
  "https://${DOWNLOADS_HOST:-downloads.kuroganeos.dev}/latest.json" \
  | head -n 8
