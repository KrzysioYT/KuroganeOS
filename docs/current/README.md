# KuroganeOS current documentation

`docs/current/` describes the active source-tree architecture. Historical milestone snapshots belong in `docs/releases/`; branch/task history belongs in roadmap/history documents.

## Current formal state

- Formal qualified milestone: **3.3.3-dev — Red Flux**.
- Active formal development milestone: **3.4.0-dev — System Services**.
- Current internal workstream: Event Broker.

## Qualification rule

Implementation, build and runtime qualification are separate states. A feature is not called QUALIFIED until its required executable tests pass. Oracle VirtualBox host execution is optional external compatibility validation and is not part of formal milestone percentages or Definition of Done.

## Current architecture documents

- `ARCHITECTURE.md` — layer model and current service architecture.
- Existing detailed subsystem documents elsewhere under `docs/` remain authoritative until migrated into this directory.

The current Event Broker is implemented but **NOT QUALIFIED** at the time this file is introduced because its Ring-3 subscribe→publish→wait→unsubscribe runtime gate is still failing and under active diagnosis.
