# WiFi / BLE

The WiFi/BLE application is RFScout's passive Wi-Fi and BLE observation surface. Platform lifecycle, memory, and collector ownership are defined in [Platform architecture](../ARCHITECTURE.md); future semantic-entity/topology work belongs in [ROADMAP.md](../ROADMAP.md).

> **Current naming:** the application descriptor and some Device Settings labels still use the older **WiFi / BT** name, while the monitor UI uses **WIFI / BLE**. The roadmap tracks completing that rename.

## Services and lifecycle

The application requires delayed activation and declares both the Wi-Fi monitor and BLE monitor session services. Both collectors must quiesce before app-session PSRAM can be reclaimed.

The normalized live observation store grows in app-owned PSRAM instead of enforcing the former fixed 1000-item limit. Collector ingestion uses an identity index rather than linearly rescanning the entire collection for every callback.

### Passive collection behavior

After the shared delayed-activation interval, the application starts both collectors. If either collector is unexpectedly not running, the app retries startup at one-second intervals while it remains active.

The Wi-Fi collector uses ESP32 promiscuous receive mode with no station association and always covers channels **1-14**. Channel dwell is adaptive: channels without retained AP/BSSID evidence use a **120 ms** discovery dwell, while channels with at least one retained AP/BSSID identity use a **350 ms** observation dwell. Every channel remains in the scan cycle; observed infrastructure changes only how long RFScout lingers there.

AP/BSSID role is derived from 802.11 semantics rather than vendor identity. Beacon and Probe Response frames establish AP/BSSID evidence, and ordinary infrastructure data or PS-Poll traffic can reinforce that role. The canonical observation store maintains per-channel AP counts as a projection of retained Wi-Fi membership. Counts move when an observed AP changes channel and disappear when that identity's Wi-Fi membership expires or the store is reset, even if another source such as BLE keeps the canonical identity alive.

The collector interprets management, data, and selected useful control frames before emitting observations. Data direction is used to distinguish wireless client/AP addresses from distribution-system addresses. Selected control reception includes PS-Poll, RTS, Block Ack Request, Block Ack, CF-End, and CF-End+ACK. One-address ACK/CTS traffic is not currently admitted because its identity value is limited relative to callback volume. Multicast/group addresses remain discarded.

The BLE collector performs **passive** NimBLE scanning with duplicate filtering disabled so repeated advertisements can refresh liveness and contribute newly observed evidence. Its scan interval and scan window are both **40 ms**, requesting continuous passive scan coverage while ESP-IDF arbitrates the shared 2.4 GHz radio between Wi-Fi and BLE coexistence slices. It normalizes:

- public/random BLE address plus address type;
- RSSI;
- shortened/complete advertised name;
- 16-bit advertised service UUIDs;
- 128-bit advertised/service-data UUIDs used by the surveillance fact matcher;
- manufacturer company ID and bounded manufacturer payload; and
- the current Open Drone ID marker described below.

Neither collector actively associates, pairs, probes, injects, or requests application data.

On app exit, the platform first stops both collectors and waits for in-flight observation callbacks to drain. Only after that service-close gate succeeds may app-session PSRAM be reclaimed.

## Current presentation

The primary display shows:

- total retained observation count;
- Wi-Fi and BLE source counts;
- most common known vendors;
- unresolved globally administered addresses as **UNKNOWN**;
- locally administered/private/randomized addresses as **RANDOM**; and
- currently active surveillance signatures/counts.

Both the ordinary summary and surveillance presentation are projections of the same canonical active observation collection.

The lower summary area keeps the existing UNKNOWN and RANDOM counts in the left column. The right column mirrors the same horizontal divider and uses the space below it for a fixed 7-by-2 channel matrix covering Wi-Fi channels 1-14. Every channel cell is identical in size; channels with retained AP/BSSID evidence use the theme warning color, matching the vendor list, while inactive channels use the theme info color, matching UNKNOWN/RANDOM. Surveillance rows remain above that divider and use the existing overflow summary when more signatures are active than fit.

The current primary count is still based on canonical observation/address records. It is **not yet** the semantic physical-entity model planned for v0.4.0.

### Identity and merge semantics

The current canonical identity key is exactly **(address type, MAC address)**. It is intentionally not a claim that one canonical record equals one physical device.

When another observation matches an existing identity, RFScout:

- preserves the original first-seen time;
- refreshes overall and source-specific last-seen times;
- ORs source membership so one identity can currently have both Wi-Fi and BLE membership;
- replaces current RSSI/channel with the newest observation while retaining source-specific Wi-Fi channel state for AP scan weighting;
- refreshes non-empty vendor/name and current BLE metadata; and
- merges surveillance evidence into the case before recomputing the selected hypothesis.

This means the store is a live observation model, not a packet history. Repeated packets refresh state rather than creating one row per packet.

Leaving the application ends the app session. The canonical store is app-owned state, so re-entering WiFi/BLE begins with a fresh logical collection rather than resurrecting the previous session's observations.

## Retention

Wi-Fi and BLE membership age independently.

Current settings are finite timeouts from **10–300 seconds** in 10-second steps, defaulting to **60 seconds** for each source. A record can therefore retain one source while the other source has already aged out.

Surveillance classification is an attribute of that canonical active record and expires with it; there is no independent surveillance-retention list.

WiFi/BLE persists a non-default threshold as the sparse `evidence_threshold` app setting (`everything`, `probable`, or `distinctive`). The retired pre-1.0 `surveillance_sensitivity` setting is not migrated; if encountered, it is ignored and pruned through the normal configuration-save workflow.

Instant / Timed / Never retention modes are not implemented yet.

The **Reset detections** action clears all current logical observations and identity-index contents immediately while retaining the app's already-allocated session capacity for reuse. Normal timeout expiry removes a canonical record only after all active source memberships have expired.

## Vendor identity

RFScout resolves globally administered Wi-Fi/BLE MAC addresses against IEEE Registration Authority data generated by `tools/update_oui.py`:

| Registry | Prefix |
| --- | ---: |
| MA-L / OUI | 24 bits |
| MA-M | 28 bits |
| MA-S / OUI-36 | 36 bits |
| Legacy IAB | 36 bits |

Runtime lookup uses longest-prefix precedence: 36-bit, then 28-bit, then 24-bit. Current MA-S data wins an exact 36-bit collision with legacy IAB.

CID is not used for ordinary vendor attribution because CID assignments are not globally unique MAC-address allocations. IEEE attribution is skipped for multicast and locally administered addresses.

## Surveillance evidence

`data/surveillance_signatures.csv` is the authoritative RFScout surveillance-fact database. It is deliberately separate from IEEE vendor ownership data. A **fact** is one static row of researched knowledge in this database. Each fact describes a broad surveillance class, product/family label, observable signal kind and value, numeric weight, and research provenance. Product-specific names, company IDs, service UUIDs, and curated prefixes belong in this data file rather than in classifier control flow.

`tools/update_surveillance.py` validates the human-maintained CSV and generates the compact firmware table under `main/services/wireless/`. Runtime code understands generic signal kinds and generated fact IDs; it does not assign product-specific identifiers in application code. When a fact matches an observed packet, that match becomes **evidence** for the observed device. The generator sizes the case evidence bitset in 32-bit words from the number of database facts, so adding facts does not require a hand-maintained runtime limit.

### Fact matching contract

Current surveillance facts match observations through a small set of generic rules:

- `oui` compares the first 24 address bits exactly with the curated prefix in the surveillance database. This is a surveillance-evidence match, not ordinary IEEE vendor attribution: it does not use MA-M/MA-S longest-prefix lookup and does not inherit the vendor resolver's locally-administered-address suppression.
- `name_contains` and `name_prefix` compare advertised names case-insensitively.
- `company_id` compares the advertised 16-bit BLE company identifier exactly.
- `service_uuid16` requires an exact advertised 16-bit service UUID.
- `service_uuid128` canonicalizes the BLE UUID byte order and then requires an exact 128-bit UUID match.
- `remote_id` currently matches a BLE Service Data marker: service UUID `0xFFFA` with at least one following application-code byte equal to `0x0D`. The current implementation does **not** validate a complete Open Drone ID message or payload before setting this marker.

The matcher records every fact that matches an observation. Product-specific recognition logic therefore remains data-driven in `data/surveillance_signatures.csv` rather than being spread through packet-processing code.

The canonical active observation identified by address type plus MAC also owns the lifetime of its surveillance **case**. A case is the accumulated set of unique evidence about that specific device while the identity remains active. Each matching fact sets its evidence bit; all matching facts in an observation are retained, and a stronger match does not suppress other facts from the same packet. Matching the same fact again only sets an already-set bit and does not increase the score, while different facts with the same weight do. When the canonical observation expires, its accumulated evidence expires with it; a later reappearance starts a fresh case. Timeout logging records the case lifetime, final unique-evidence count, score, and best signature hypothesis even when the configured notification threshold was never reached.

The case score for a signature is the sum of the weights of the unique facts represented by its collected evidence. The **Evidence Threshold** setting maps the user-facing levels to minimum scores:

- **Everything** — score 1 or greater. This exposes every curated fact match and is intended primarily for detector testing, fact validation, and field research; its practical value as a normal alerting mode has not yet been established.
- **Possible** — score 2 or greater.
- **Probable** — score 4 or greater.
- **Distinctive** — score 6 or greater.

These are ordinal evidence scores, not calibrated probabilities. Evidence weights use a small **1 / 2 / 4** scale based on the usefulness of each fact for the specific hypothesis: 1 for weak/common indicators, 2 for meaningful supporting evidence, and 4 for facts that strongly distinguish the target product or family. Signal type alone does not determine weight; a company ID or OUI shared across product lines can therefore carry less weight than a product-specific name or service UUID. Several unique low-weight facts can therefore move a case through the levels, and a single packet can cross a threshold immediately when it contains enough distinct facts. Repeated copies of the same fact cannot add score.

### Detection interpretation policy

A fact weight describes how discriminating that fact is for one configured product/family hypothesis; it is not a universal score for a signal type. Broad or ambiguous ecosystem/vendor indicators should remain weight 1. A curated indicator that meaningfully narrows the observed device to a surveillance-focused product ecosystem can use weight 2. Weight 4 is reserved for identifiers that strongly distinguish the intended product or family. For example, a curated Ring prefix can be stronger supporting evidence than a broad general-purpose camera-vendor prefix without implying that every Ring-branded radio endpoint is itself a camera.

The selected signature is the hypothesis with the highest accumulated score. Equal scores currently retain the earlier generated signature, whose order follows the first appearance of each class/brand pair in the authoritative CSV. That deterministic tie behavior is an implementation detail, not extra evidence and not a reason to treat the selected hypothesis as stronger than the tied alternative.

Class labels such as **Camera / doorbell**, **ALPR**, or **Body camera** describe the surveillance hypothesis supported by the observed evidence. They do not prove that the individual radio endpoint contains that sensor. Accessories, hubs, chimes, and other devices in a product ecosystem can share identifiers; the evidence level is intended to communicate that distinction rather than convert ecosystem association into exact endpoint identity.

Evidence source and score remain separate concepts. The source bitmask records which kinds of evidence contributed to the selected signature (for example curated OUI/prefix, advertised name, company identifier, service UUID, or Remote ID), while the score determines whether the configured threshold has been reached.

**Possible** remains the compiled default. **Everything** is deliberately opt-in because weight-1 facts can be weak or ambiguous on their own; selecting it is useful when testing whether RFScout is seeing all candidate evidence, but does not make those individual facts stronger evidence. Notification occurs when an active case first crosses the configured threshold. Evidence can continue to accumulate after notification without creating another new-device alert.

Changing the threshold reevaluates every currently retained case immediately. Lowering the threshold can therefore cause an already-active case to qualify and emit a surveillance transition/notification at that moment. Raising the threshold can remove a current classification. If the threshold is later lowered again while the same case is still retained, the unchanged evidence can qualify again and emit another surveillance transition/notification because the current implementation tracks qualification state, not a permanent per-case "already notified" history. Threshold cycling can therefore re-alert without new RF evidence.

### Settings and controls

Pressing the center/select control from the main WiFi/BLE view opens the app settings surface. Current items are:

- **Reset detections** — clears the current canonical observation collection;
- **Notifications** — opens the shared per-app notification settings;
- **Evidence threshold** — cycles Everything / Possible / Probable / Distinctive and immediately reevaluates retained surveillance cases;
- **WiFi timeout** — independent Wi-Fi source-retention timeout; and
- **BLE timeout** — independent BLE source-retention timeout.

Wi-Fi and BLE timeouts are edited in 10-second steps from 10 through 300 seconds. Configuration is persisted through the normal device-configuration path when the menu closes.

### SD log interpretation

When SD logging is enabled, device records expose several different surveillance values that should not be conflated:

- `evidence` is the evidence-source bitmask for the selected hypothesis, such as prefix/OUI, advertised name, company ID, service UUID, or Remote ID source kinds;
- `evidence_count` is the number of unique matched database facts accumulated by the case;
- `score` is the final score of the best signature hypothesis; and
- `case_signature` is that best hypothesis at timeout, even when its score never reached the configured alert threshold.

For timeout records, `lifetime_ms` is `last_seen_ms - first_seen_ms`: the span between the first and last observations of that canonical identity. It is **not** the amount of time the record remained retained before timeout. A device seen only once can therefore expire later with `lifetime_ms=0`.

Device log events currently use:

- `event:"added"` when a canonical identity first enters the live store;
- `event:"surveillance"` when an already-retained case first becomes classified at the current threshold;
- `event:"dropped", reason:"timeout"` when its final active source membership expires; and
- `event:"dropped", reason:"capacity"` when app-session growth fails and the new observation cannot be retained.

`added` and `surveillance` records include current RSSI/channel plus the selected surveillance signature/evidence when applicable. Timeout records intentionally record final case evidence/count/score even when the configured threshold was never crossed.

Real field logs have also confirmed the intended fresh-session behavior: after leaving and re-entering WiFi/BLE, identities seen in the prior session are emitted as new `added` records again. This is expected because app exit releases the prior canonical collection.

## Raven acoustic-sensor evidence

Raven is the first signature using the accumulating case model as a reference detector. The database contains five exact advertised 16-bit BLE service UUID records: `0x3100`, `0x3200`, `0x3300`, `0x3400`, and `0x3500`. Each contributes two points because the exact proprietary services are meaningful Raven-specific evidence. RFScout does **not** treat the entire `0x3100-0x3500` numeric interval as Raven evidence.

One distinct proprietary Raven service therefore reaches Possible, two distinct services reach Probable, and three or more reach Distinctive. Repeated advertisements of the same service do not add points. Research provenance and current interpretation:

- GainSec independently examined an owned Flock Safety Raven v1.2 and documented an ESP32-WROOM-32D main controller. Community detector projects attribute the proprietary Raven service dataset to GainSec captures spanning firmware generations 1.1.7, 1.2.0, and 1.3.1.
- Multiple implementations reproduce the same five exact proprietary services for later Raven firmware. This is useful corroboration, but many implementations share the same upstream dataset and therefore are not fully independent observations.
- Community datasets also report legacy Raven 1.1.x advertisements using standard BLE services `0x1809` (Health Thermometer) and `0x1819` (Location and Navigation), usually alongside `0x180A` (Device Information). These standard services are not Raven-specific and are not currently database facts.
- Reports assigning semantic names such as GPS, power, network status, upload statistics, and diagnostics to `0x3100-0x3500` are secondary interpretations unless independently verified against Raven firmware or GATT behavior. RFScout does not depend on those labels for detection.
- Broad matching of arbitrary UUIDs numerically between `0x3100` and `0x3500` has produced field false positives in another detector. Exact UUID matching is therefore an explicit false-positive boundary.

Legacy `0x1809` / `0x1819` support remains disabled. If added, it should require corroborating Raven/Flock-specific evidence or a validated combination rather than treating either standard UUID as independently identifying.

## Notifications

Ordinary Wi-Fi/BLE discovery feedback can be enabled independently from surveillance alerts. New canonical Wi-Fi and BLE identities can drive the ordinary detection event path according to the app-level and Wi-Fi/BLE notification preferences.

Surveillance notification is case/threshold based rather than packet based: the first transition of an active case into a qualifying surveillance classification maps to a semantic notification category such as ALPR, body camera, drone, camera, smart glasses, or acoustic sensor. Repeated unchanged evidence does not repeatedly notify. A later reappearance after the canonical identity has expired is a new case and can notify again.

## Generated-data maintenance

Refresh generated IEEE and curated signature tables through their generators, never by editing generated `.inc` files directly:

```sh
python3 tools/update_oui.py
python3 tools/update_surveillance.py
python3 -m unittest discover -s tests -p 'test_*.py'
```

Relevant generated tables live under `main/services/wireless/`. Registry, generator, workflow, and generator-test changes should stay synchronized.

## Current limitations

Current production behavior preserves enough AP/BSSID and frame-role semantics for adaptive scanning, but it does not yet provide the full semantic physical-entity model, AP/client topology and association confidence, probe relationships, stable relationship drill-down, BLE-native deep views, or conservative address-rotation correlation planned for v0.4.0. Randomized addresses therefore remain evidence rather than proof of physical identity.

## Surveillance locations: live receiver bounds and packet journal

The canonical observation store owns every currently active Wi-Fi/BLE identity until its
configured source timeout. GPS-qualified surveillance packet observations expand the
**receiver-location** minimum/maximum latitude/longitude bounding box of the live identity.
These coordinates represent where RFScout received packets, not a verified transmitter position.
Expiration simply removes the active identity; it does not write a summary to SD.

Each GPS-qualified surveillance packet is separately queued for the append-only
`locations/surveillance.jsonl` journal, including full MAC, address type, classification,
receiver latitude/longitude, trusted UTC when available, and observation time. The app
periodically appends batches from the poll context rather than writing inside radio callbacks.
The journal is retained for offline analysis. It is not replayed to populate the live UI,
and legacy bounding-box journal formats are not read or supported. A failed SD append is
retried while the app session remains active; insufficient PSRAM can still prevent capture.
App exit attempts a final flush.

The surveillance list reads active identities directly from the canonical store and displays
the distance from the current GPS receiver position to each identity's observed receiver
bounding box. Without a fresh GPS fix or valid bounds, distance is unknown. Historical-only
identities are never shown. The rotary normally switches views; center press on the
surveillance page enters/exits scroll mode. Settings is opened from the main view.

### Validation

Verify per-packet journal lines, bounding-box expansion, source timeouts, GPS loss,
SD removal/reinsertion, retry behavior, PSRAM headroom, rotary focus, and app exit.
No history replay, proximity reload threshold, or timeout-triggered SD write exists.
