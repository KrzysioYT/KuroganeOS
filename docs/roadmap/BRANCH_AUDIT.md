# KuroganeOS branch audit

Date: 2026-08-29

This audit records branch ancestry before Road-to-15 consolidation. It is intentionally conservative: **no branch is authorized for deletion by this document**. A branch marked `SUPERSEDED`, `ALREADY INCLUDED`, or `DOCS ONLY` remains historical evidence until an explicit cleanup decision is made.

## Canonical integration rule

- Formal product version remains `3.3.3-dev` until its release blockers are qualified.
- `main` is authoritative for current release requirements, including the reopened Kurogane Fatal Diagnostic Screen MUST HAVE.
- Later implementation work is preserved from the stacked 3.3.x/3.4.x history rather than discarded to make the graph look linear.
- Active consolidation branch: `gpt/road-to-15-consolidation`, created from `dev/3.4.2-settings-service` HEAD.
- Comparisons below use `dev/3.4.2-settings-service` (`10852fdb6b04abcf25382a0eac1896c226b59515`) as the late-stack reference unless stated otherwise.

## Audit table

| Branch | HEAD | Truth / unique work | Action | Destination / reason |
|---|---|---|---|---|
| `dev/3.3.5-installer-reliability` | `7c829aad5ee89722feb04678f11b822029005018` | Installer/profile implementation was carried forward. Only unique tail relative to 3.4.2 is `docs/releases/3.3.5-dev.md`. | **DOCS ONLY / SUPERSEDED** | Do not merge code; reconcile release-note history only where still true. |
| `dev/3.3.6-network-stabilization` | `8601a504dc04989becf59e22fc1b61bfab556c2b` | TCP close/reset implementation was carried forward. Only unique tail relative to 3.4.2 is `docs/releases/3.3.6-dev.md`. | **DOCS ONLY / SUPERSEDED** | Do not merge code; preserve accurate release-note history selectively. |
| `dev/3.3.7-tls-foundation` | `b71b454587eb84b1279fce33159765dd1c117278` | Mbed TLS/HTTPS foundation is contained by 3.4.2. | **ALREADY INCLUDED** | Preserve inherited TLS chain/hostname/time validation; requalify after integration. |
| `dev/3.3.8-userspace-io` | `b9e050167c0f4d4aff6d2bde0167db4975d66b22` | Literal ancestor of 3.4.2; later branch is 82 commits ahead and 0 behind. | **ALREADY INCLUDED** | Preserve real handle ownership/cleanup through ancestry. |
| `dev/3.3.9-red-flux-closeout` | `21ba9a619e6de2ed6bf1510a7676e32313b67138` | Red Flux closeout and IPC/event/shared-memory work is contained by 3.4.2. | **ALREADY INCLUDED** | Preserve through 3.4.2 ancestry; fresh integration regression still required. |
| `dev/3.4.0-service-architecture` | `1558c4f23a5ce18615eb18ada60442a6b3e0038a` | Full ancestor of 3.4.2. Existing named-IPC service architecture is already present. | **ALREADY INCLUDED** | No parallel/fake service registry. |
| `dev/3.4.1-event-broker` | `739bb43f347108955e7a7b527083499a836cf1ff` | Full ancestor of 3.4.2; Event Broker and Ring-3 scheduling work is inherited. | **ALREADY INCLUDED** | Preserve and requalify on integration branch. |
| `dev/3.4.2-settings-service` | `10852fdb6b04abcf25382a0eac1896c226b59515` | Latest stacked services baseline; contains 3.3.7 through 3.4.1. Known blocker: first-boot persistent settings write. | **KEEP / BASE FOR CONSOLIDATION** | Base of `gpt/road-to-15-consolidation`; reproduce and fix blocker rather than weakening its test. |
| `chatgpt/3.4-event-broker-sleep` | `5cc4bd7a5d0363cb4d9657c8e7705cb36881c531` | Contained by 3.4.2. | **ALREADY INCLUDED / SUPERSEDED** | Historical branch; no merge. |
| `chatgpt/3.4-syscall-frame-scheduling` | `a76226076cc99f2769030bb84550462dd31020e2` | Contained by 3.4.2. | **ALREADY INCLUDED / SUPERSEDED** | Historical branch; no merge. |
| `chatgpt/3.4-syscall-scheduler-integration` | `a2e4eef378a64917298048e0042e7b5b870db90a` | Contained by 3.4.2. | **ALREADY INCLUDED / SUPERSEDED** | Historical branch; no merge. |
| `chatgpt/red-flux-audio-buffering` | `6e77fd707a9e44d5ba38583b360537980e8bbcfd` | Four unique commits: AC97 buffering/layout and host regression work. | **PORT LATER / KEEP EXPERIMENTAL** | Selectively port after P0/P2 reliability blockers; do not wholesale-merge the old base. |
| `agent/ui-font-foundation` | `3fcbf7bd834229de8566f0e01864998d81e3fb8b` | 20 unique old-base commits touching framebuffer/UI/presenter/public ABI/userspace GUI. | **KEEP EXPERIMENTAL / PORT SELECTIVELY** | Re-evaluate at graphics/UI dependency milestone; wholesale merge would discard later service-stack history. |
| `gpt/kuroganeos-5-gui` | `4ed8cb953f31e65f3bc6ff70182db848471c0c39` | 162 unique old-base commits spanning GUI/GPU/assets/apps and unrelated deployment/site work. | **KEEP EXPERIMENTAL / DO NOT MERGE NOW** | Future selective review only; kernel/filesystem/service reliability gates take precedence. |
| `dev/road-to-15` | `0ea73d3455c6afe18c54d9d27afe68d0aa948618` | Four unique commits relative to 3.4.2; changes are documentation-only (`BUILD_STATUS`, `CURRENT_RELEASE`). | **DOCUMENTATION ONLY / PORT SELECTIVELY** | Reconcile against current code/CI; never overwrite newer fatal-diagnostic policy with stale completion claims. |
| `docs/tls-status-cleanup` | `5b7cff30bdbf392e12e683936c9c0c821c0178e5` | Five unique documentation-only commits from an older base. | **DOCS ONLY / SUPERSEDED** | Port only statements proven true now. |
| `gpt/road-to-15-consolidation` | active | Integration line rooted at 3.4.2 and carrying current fatal-diagnostic work plus subsequent fixes. | **KEEP / ACTIVE INTEGRATION** | Primary development branch until release gates are green. |

## Main requirement ancestry

At audit time `main` points at `b755e44b5b6de356e0d457788f1212d17c8543af`. Its ancestry contains the fatal-diagnostic reopening commit `a3bd2a463d1c834929ec6ccad5d7e0b538d7ac74`. The `OPEN / NOT COMPLETE` fatal-diagnostic requirement is therefore intentional current release policy, not an obsolete side-branch claim.

## Consolidation conclusions

1. No merge is needed for 3.3.7, 3.3.8, 3.3.9, 3.4.0, 3.4.1 or the three scheduler/Event-Broker chatgpt branches: 3.4.2 already contains them.
2. 3.3.5 and 3.3.6 have only release-document tails outside the late stack; their implementation work is already present later.
3. Audio buffering, UI-font foundation and the 5.x GUI branch contain real unique work, but they are independent old-base lines and must be selectively ported only at the correct dependency milestone.
4. Documentation-only branches are evidence sources, not merge authorities. Current code plus fresh qualification wins.
5. No audited branch is deleted by this process.

## Fresh consolidation evidence

Historical branch claims are not rewritten as fresh PASS. Fresh evidence is attached to the exact integration commit/workflow run that produced it.

- Fatal diagnostic qualification workflow: `.github/workflows/qualify-fatal-diagnostics.yml`.
- Initial run on `7c01af2835948fcfcc8a39a7c5d5a0b4907c4a83`: production build and host regression PASS; nested panic QEMU/OVMF PASS; full panic reached a valid real snapshot but the checker failed on hexadecimal zero formatting. The checker defect is fixed in `748845d797348b6ca94912a327a3a5a5126cea24`; its rerun is the authoritative follow-up evidence.
- VirtualBox runtime qualification remains **PENDING / EXTERNAL** until a VirtualBox-capable host executes the included smoke harness. Test support exists; runtime execution has not been claimed.
