# Mozilla CA bundle

KuroganeOS ships a source-controlled Web PKI trust bundle at
`rootfs/etc/ssl/certs.pem`. Builds must use this file and must not import roots
from the host operating system.

Current provenance:

- upstream: `bagder/ca-bundle` (backup of curl's Mozilla CA extract);
- upstream commit: `38fe30abb189fb714e8b1f1b354c6c4caffbe6dc`;
- Mozilla source date recorded in the bundle: `2026-08-11`;
- bundle SHA-256: `6ee98962bc5d0f26780053eb297fb9e023848e02e9f5d6a671580fe780cfdcf6`;
- trust anchors: `119`;
- license: Mozilla Public License 2.0, as stated by the upstream bundle.

Update procedure:

1. obtain a new Mozilla extract from `https://curl.se/docs/caextract.html` or
   the pinned `bagder/ca-bundle` backup;
2. record its upstream revision and SHA-256 here;
3. replace `rootfs/etc/ssl/certs.pem` without using a host trust store;
4. run `python3 scripts/verify-trust-store.py rootfs/etc/ssl/certs.pem`;
5. run the real HTTPS qualification for `docs.kuroganeos.dev` and
   `repo.kuroganeos.dev` with certificate, hostname and RTC verification on.
