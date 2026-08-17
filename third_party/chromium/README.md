# Chromium upstream workspace

KuroganeOS does not vendor the Chromium source tree in this repository.

The Kurogane Web port is based on the official Chromium mirror:

`https://github.com/chromium/chromium`

Pinned upstream revision:

`4137589c17766b2c0036332e00ad0d453e342a92`

Run `scripts/fetch-chromium.sh` on macOS/Linux or `scripts/fetch-chromium.ps1` on Windows to create the ignored local checkout at `third_party/chromium/src/`.

Chromium is BSD 3-Clause licensed and contains components under additional third-party licenses. Those sources retain their original licenses and are not covered by the KuroganeOS KSAL-1.0 license.
