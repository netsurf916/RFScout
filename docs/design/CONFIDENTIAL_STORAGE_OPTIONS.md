# Confidential SD storage and controlled host access — architecture investigation

**Status:** Draft exploration only. No encryption layer, library, format, key hierarchy, host-transfer protocol, or migration strategy has been selected. Do not implement a block cipher, filesystem filter, or per-file scheme merely because this PR has been opened.

## Goal and existing system

Protect user-generated data persisted to removable SD storage on the ESP32-S3 RFScout while preserving its app-session, media-store and SD-journal semantics. Prevent casual extraction of sensitive stored content from a removed card. Support a user-controlled path to recover/export authorized data over USB without requiring permanent unrestricted access.

Current `main/services/storage/storage.c` mounts a removable SD card via **SDSPI + ESP-IDF FatFs VFS**, with board-level shared SPI arbitration. `main/services/storage/media_store.c` provides owner/collection/file addressing, session generations, SD-loss recovery, and access through opaque media handles. Users of SD include breadcrumbs, radio observation logs, Hotspots history, Recorder audio, and Wardriving capture. These are all compatibility constraints, not implementation mandates.

## Open architecture choices — compare before implementing

| Candidate boundary | Questions to resolve |
| --- | --- |
| **Block/volume protection** | Can the filesystem and raw-card lifecycle be mediated safely at the SDSPI/FatFs boundary, including wear and interrupted writes? Does USB raw mass-storage exposure require unmount/exclusive ownership and authenticated access to plaintext? |
| **Per-file / per-record protection** | Can the existing media-store/application framework provide consistent protection without requiring each app to implement its own cryptography? What happens to filenames, directory metadata, journals, append-only JSONL, captures, and partial writes? |
| **Hybrid or selective protection** | Can user-readable exports coexist with protected journals without creating bypasses? Would two classes of data/volumes cause operator confusion or unexpected leaks? |

Choose based on explicit threat models, interoperability and cost; **do not preselect any of these options**.

## Access-control and recovery requirements to design

- Discuss a **six-digit PIN for unlocking user-facing access** (including any USB transfer interface) as a policy and UX requirement, **not** as the sole source of encryption entropy. Evaluate persistent-key protection, device-bound keys, rate limiting, lockouts, reboot behavior and recovery in the ESP32-S3's actual security model.
- A USB mass-storage device (MSC) profile is a **candidate**, not a requirement if per-file protection or safe exclusive raw-volume access makes it inappropriate. Compare read-only MSC, authenticated file-transfer interfaces, and app-export mechanisms; document host compatibility and failure semantics before choosing.
- Review whether JTAG/debug UART access should share an unlock policy, noting that firmware recovery must remain possible after faulty software, forgotten PINs or partially failed updates. Specify how production firmware can differ from debug/development builds. Do not assume software PIN gating can resist physical debug attacks by itself.
- Analyze removable-card offline attacks, hostile host access, physical theft, file replacement/rollback, corrupt/torn writes, card hot-unplug and replay. Define whether filenames, timestamps, directory structure and free-space metadata must remain confidential.
- Decide key origin, credential enrollment/change, rotation, unlock lifetime, memory wiping, rollback and optional credential recovery. Require authenticated integrity of protected data, not just obscuring contents; use reviewed cryptography when implementation is eventually selected.
- Preserve media-store owner isolation, journal replay/compaction and append behavior, atomicity, SD card generation checks, SPI arbitration, app-session teardown, OTA rollback/recovery and acceptable boot/unlock UX.
- Establish throughput/latency/PSRAM/internal SRAM and flash/efuse implications through measurements. Ensure users can back up and restore data without silently changing the trust or recovery model.

## Decision and validation gates

- [ ] Threat-model document with explicitly in-scope and out-of-scope attackers and data.
- [ ] Side-by-side design decision: volume/block, framework per-file, optional hybrid, including reversibility and future compatibility.
- [ ] Chosen key/credential and authorization model, storage migration and recovery plan reviewed before crypto code begins.
- [ ] USB host exchange protocol decided independently of underlying storage-encryption boundary.
- [ ] Hardware test matrix for reboot, PIN failures, USB connect/disconnect, app transition, SD removal/reinsertion, corruption, crash recovery, OTA upgrade/rollback and firmware rescue.
- [ ] CI/host tests for access denial, authenticated corruption rejection and lifecycle errors, plus explicit physical-device acceptance.
- [ ] Updated design and user-facing documentation with data-loss and backup semantics.

**Release constraint:** No code or release behavior changes in this initial draft. Keep the PR Draft until the owner explicitly directs otherwise. Do not modify the v0.4.0 release workflow.
