# Chromium port for KuroganeOS

Kurogane Web is being moved away from the old monolithic HTTP demo and toward a port architecture based on Chromium's `content_shell` model.

## Upstream

Official mirror:

`https://github.com/chromium/chromium`

Pinned revision for the KuroganeOS 3.3.x port work:

`4137589c17766b2c0036332e00ad0d453e342a92`

Chromium is distributed under the BSD 3-Clause license, with additional third-party licenses in its source tree. Chromium source is Third-Party Material and is not relicensed under KSAL-2.0.

## What is already in KuroganeOS

The browser application now follows four explicit roles that mirror the separation used by Chromium `content_shell`:

- **BrowserContext** — owns navigation state and network-visible browser state.
- **NavigationController** — parses URLs, performs a navigation, follows bounded redirects and commits a response.
- **PlatformDelegate** — maps KuroganeOS E1000/DHCP/network ABI state into the browser.
- **RenderView bootstrap** — displays the first textual page representation until Blink can replace it.

The application intentionally calls itself a **Chromium port bootstrap**. It does not claim Blink or V8 are already linked.

## Why full Chromium is not linked yet

Current KuroganeOS Ring-3 is intentionally small. A real Chromium build requires platform services that do not yet exist as stable KuroganeOS ABI:

1. asynchronous sockets and HTTPS/TLS,
2. threads, locks, waitable events and task runners,
3. high-resolution clocks and timers,
4. a larger libc/libc++ and POSIX compatibility layer,
5. writable files, directories, profiles and cache storage,
6. shared memory and IPC suitable for browser/renderer processes,
7. process sandbox primitives,
8. font discovery and text shaping,
9. accelerated compositing / GPU process integration,
10. a GN toolchain definition for `target_os = "kurogane"`.

Trying to compile Chromium before those services exist would produce a large collection of stubs rather than a working browser.

## Current navigation support

The bootstrap path currently supports:

- `http://` navigation through the native KuroganeOS E1000/IPv4/DNS/TCP stack,
- HTTP status reporting,
- bounded redirects,
- clear reporting when a redirect reaches `https://`,
- a bootstrap text renderer for the received HTML body.

`http://google.com/` normally redirects to HTTPS. Until the TLS platform layer is implemented, that navigation is expected to stop with **HTTPS / TLS PLATFORM REQUIRED** rather than being reported as an unexplained HTTP failure.

Use `http://example.com/` as the first plain-HTTP connectivity test.

## Fetching the pinned Chromium source

### macOS / Linux

```bash
bash ./scripts/fetch-chromium.sh
```

### Windows

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\fetch-chromium.ps1
```

The checkout is stored in:

```text
third_party/chromium/src/
```

That directory is ignored by Git. It is developer input and is not copied into KuroganeOS IMG/ISO files.

## Port order

The next implementation order is intentionally dependency-driven:

1. Kurogane async socket ABI,
2. TLS provider and certificate validation,
3. thread/task primitives,
4. writable profile/cache filesystem API,
5. shared-memory + browser/renderer IPC,
6. libc++/base support,
7. GN `kurogane` target,
8. compile Chromium `base` + `url` smoke target,
9. compile `content_shell` browser process,
10. Blink renderer,
11. V8/JavaScript,
12. GPU compositing.

Each milestone must run in KuroganeOS before the next one is marked supported.

## Upstream architecture reference

Chromium's `content/shell/browser/shell_browser_main_parts.cc` creates browser contexts, initializes a shell platform delegate, installs network resources and then creates the initial browser window. Kurogane Web now uses the same high-level separation so the bootstrap renderer can later be replaced without rewriting the whole application shell again.
