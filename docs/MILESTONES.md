# ReticulumWatch milestone status

`docs/MASTER-BUILD-PLAN.md` is the project roadmap/source of truth. Patch releases do not redefine the major milestone structure.

## M0 — Platform and Reticulum Foundation — COMPLETE

- M0.1 hardware bring-up: complete.
- M0.2 persistent storage and explicit SX1262 ownership: complete.
- M0.3 microReticulum core and persistent application identity: complete.
- M0.4 Reticulum LoRa receive path: core path complete; retain >254-byte split-frame RX as a regression test.
- M0.5 Reticulum LoRa transmission/announce: complete.

## M1 — LXMF Messaging Core — HARDWARE-PROVEN CORE; TWO FOLLOW-UP GATES REMAIN

### M1.1 — LXMF Endpoint and Receive Pipeline — FUNCTIONAL CORE PROVEN, ACCEPTANCE CLEANUP REMAINS

Proven:
- canonical `lxmf.delivery` destination;
- real incoming encrypted Reticulum Link;
- LXMF full-wire reconstruction and MessagePack decode;
- persistent incoming message;
- incoming haptic;
- UI notification/inbox update.

Still retain/complete before closing M1.1:
- explicit short opportunistic LXMF receive acceptance test on the current RatSpeak baseline;
- deliberate long/split-frame LXMF receive regression test.

### M1.2 — Persistent Inbox and Message History — FUNCTIONAL STORAGE PATH HARDWARE-PROVEN

Already present before M1.2.3:
- backward-compatible LMX1 reader;
- bidirectional LMX2 history records;
- message ID, source, destination, timestamp, title/content, direction, and verification fields;
- reboot restoration.

M1.2.3 adds persistence reliability without changing radio/Reticulum wire behavior:
- authoritative history-directory recovery if `message_count.bin` is missing/stale/corrupt;
- durable on-flash message-ID marker index under `/lxmf/metadata`;
- backfill of ID markers for existing LMX1/LMX2 records;
- duplicate detection that survives reboot;
- safe skip of malformed/unsupported history records;
- latest-message restore walks backward past a damaged newest record;
- `/lxmf/outbox` and `/lxmf/metadata` directories are created for roadmap-compatible storage layout.

M1.2.4 added a confirmation-gated on-screen software restart control. Hardware validation then confirmed the second-boot persistence acceptance: boot count advanced, all five records remained readable, zero ID markers needed rebuilding, identity stayed stable, and durable dedupe remained PASS.

M1.2.5 adds the remaining storage-integrity machinery:
- LMX3 CRC32-protected records while retaining LMX1/LMX2 compatibility;
- explicit persisted delivery state (`RECEIVED` / honest `SUBMITTED`, with `DELIVERED`/`FAILED` reserved);
- atomic temp+rename history commits;
- corrupt-record quarantine under `/lxmf/corrupt`;
- bounded active retention of 256 message records;
- bounded corrupt quarantine of 16 artifacts;
- atomic/validated message-ID marker writes and retention cleanup.

M1.2.5 hardware testing then confirmed the new LMX3 path in both directions: a fresh incoming message was persisted as `RECEIVED`, a watch reply was persisted as `SUBMITTED`, and the reply was transmitted successfully. The user reported the requested build tests looked good. Exact duplicate retransmission remains a useful regression test when practical, but the durable duplicate gate is implemented and boot-time lookup is proven.

M1.2.6/M1.2.7 are the requested composer usability patch plus its C++ enum-typing compile fix. M1.2.7 is now build-, flash- and runtime-verified by user report: the seven-column keyboard works on hardware, reboot restored seven readable records with zero quarantine/index rebuilds, and a new receive/reply cycle produced incoming #8 `RECEIVED` and outgoing #9 `SUBMITTED` without disturbing the Reticulum baseline.

### M1.3 — Outgoing LXMF — HARDWARE ACCEPTED

Already proven before M1.3.0:
- signed watch-originated LXMF over an active incoming encrypted Link;
- receiving client confirmed the watch reply;
- outgoing message persistence.

M1.3.0 adds the missing independent-send route for peers already present in history:
- active matching Link remains the preferred fast path;
- otherwise construct a canonical opportunistic OUT/SINGLE `lxmf.delivery` destination from a recalled identity;
- request a Reticulum path when the destination is not yet known;
- retain one outbound message in RAM while waiting for path/identity resolution;
- send opportunistically once the destination announce/path response is learned;
- preserve honest `SUBMITTED` state after local Reticulum submission.

M1.3.0 acceptance target:
- reboot/close the incoming Link;
- open an existing peer from history;
- send a human-readable message from the watch;
- remote LXMF client receives it without first messaging the watch in that boot/session.

**Acceptance result:** PASSED by user hardware test. The monitor log shows path request -> valid peer announce -> path-table insertion -> identity recall -> opportunistic LXMF submission, and the user confirmed SEND MORE worked as expected.

M1.3.1 is a narrow UI usability patch only: message-detail and compose Back buttons are enlarged from 34x28 to 44x32 px. No M1.3 networking/storage behavior changes.

M1.3.1 hardware testing confirmed the larger Back controls and keyboard are accessible. M1.3.2 added control-scoped haptic feedback and an explicitly visible composer caret without changing M1.3 networking/storage behavior. M1.3.3 strengthened the haptic attempt, but hardware testing still found the UI click too weak to be useful. That haptic issue is explicitly deferred to later UX/hardware tuning and does not block the accepted M1.3 networking milestone.

Still remaining after that acceptance:
- arbitrary new-address/contact selection (primarily M2 UX);
- persistent multi-message outbox/retry policy;
- honest delivered/failed state once receipt validation is trustworthy.

### M1.4 — LXMF Security Hardening — CORE HARDWARE REGRESSION PASS; EXTERNAL NEGATIVE TEST REMAINS

M1.4.0 begins the controlled security rebase:
- replaces the vulnerable RatSpeak 0.2.10 lineage with immutable `attermann/microReticulum @ 0.5.0`;
- adds a fail-closed boot self-test requiring valid-signature acceptance and tampered-signature/message rejection from the exact linked runtime;
- introduces explicit `UNKNOWN` / `KNOWN` / `VERIFIED` / `FAILED` trust states;
- reuses reserved LMX3 metadata bytes without changing the record size or CRC model;
- conservatively downgrades all pre-M1.4 incoming verification claims to `KNOWN`/`UNKNOWN`;
- keeps RNS filesystem persistence/provisioning disabled during this rebase;
- preserves the proven application identity, M1.3 outbound behavior and radio profile.

M1.4.4 is now **build-, boot-, receive- and reply-regression proven on hardware**. Its boot self-test also demonstrated valid signature acceptance plus tampered-signature and tampered-message rejection. The intentionally invalid *external* message/signature acceptance test remains useful and is still tracked; it no longer blocks beginning unrelated M2 user-interface work.

A separate announce regression was exposed on M1.4.4: two watch announces were locally constructed and transmitted successfully, but the peer did not surface them. This is now an explicit interoperability issue to resolve before M2.4 discovered-peer acceptance. It is not treated as a radio TX failure.

## M2 — Messenger User Experience — HARDWARE ACCEPTED IN M2.6.1

M2.1.0 was hardware-accepted with working announces, LoRa TX/RX, Link establishment, LXMF send/receive, persistence and reboot recovery. M2.6.0 consolidates the remaining M2 messenger work into one milestone build:

- conversation threads;
- persistent read/unread state;
- discovered `lxmf.delivery` peers;
- saved contact names and favorites;
- manual new-conversation destination entry;
- full composer and quick replies;
- proof-backed opportunistic delivery state;
- bounded retry/path recovery;
- separate peer/delivery metadata overlays without changing LMX1/LMX2/LMX3 journal layout.

M2.6.1 compiled and was hardware-tested: threading, contacts/peers, quick replies, send/receive and persistence worked. The remaining announce interoperability defect is corrected in M3.

## M3 — Watch-Native Experience — HARDWARE ACCEPTED

M3 established the dark watch-native UI, watch face, notifications, Contacts/Peers distinction, radio settings, Announce action and clock fallback. M3 build3 fixed the Contacts stack overflow and radio-settings/reboot flow.

## M4 — Power / Wearable Behaviour — HARDWARE ACCEPTED

M4 added connected standby with the display/backlight off while Reticulum RX stays active, plus wake-touch suppression. User hardware testing confirmed normal LXMF messaging and announces; an apparent radio regression was traced to a dead battery in the remote test radio.

## M5 — Additional Reticulum Interfaces — HARDWARE ACCEPTED (LoRa baseline); TCP OPTIONAL

M5 preserved the accepted LoRa/LXMF path and added opt-in Wi-Fi STA/TCP client and Wi-Fi AP/TCP server interfaces. User hardware logs confirmed peer announce discovery, Link establishment, verified incoming LXMF, outgoing replies and connected standby with Wi-Fi/TCP left disabled. This is the frozen acceptance baseline for M6.

## M6 — Reticulum Routing and Transport Features — INTEGRATED; HARDWARE ACCEPTANCE REQUIRED

M6 enables microReticulum internal path/known-destination/hashlist persistence, adds an explicitly opt-in Transport Node setting with reboot-to-apply and battery warning, and corrects the pinned 0.5.0 neighbor-probe `300 -> 44` overflow before transport testing. Transport is OFF by default. The M5 radio/LXMF/TCP/power code paths remain frozen. The clock is slightly larger while the date remains unchanged.

## M7 — Identity and Exchange — HARDWARE ACCEPTED

M7 added public identity/LXMF address display, interoperable raw-address QR exchange, contact QR sharing and manual 32-hex destination import without exposing private identity key material.

## M8 — Configuration and User Settings — HARDWARE ACCEPTED

M8 consolidated persistent user, messaging, watch and Reticulum settings while preserving the frozen M6-build3 endpoint. M8-build3 is the accepted parent for M9.

## M9 — Audio and Experimental Communication — HARDWARE FUNCTIONAL; CROSS-CLIENT PLAYBACK OPEN

M9 established bounded PSRAM microphone capture, local speaker playback, dedicated audio and 32 KiB Codec2 workers, standard LXMF `FIELD_AUDIO` voice transport, CRC-protected voice sidecars and hold-to-talk LXMF bursts. Hardware testing confirmed capture/playback and Reticulum/LXMF concurrency. M9-build5 builds/flashes and can send watch-originated Codec2-700C audio messages. Sideband playback on the tested phone remains silent, so cross-client audio decoding is recorded as an open interoperability issue rather than a reason to keep mutating the working communicator.

## M10 — STABILIZATION AND RELEASE — ACTIVE

The first M10 build adds a non-destructive persistent-storage mount policy, low-space emergency write reserve, independent main-loop supervisor/recovery task, and periodic heap/PSRAM/storage/latency diagnostics while preserving the frozen Reticulum/radio endpoint and M9 functionality. Identity export, migration/repair tooling, OTA, battery profiling and extended soak/stress work remain later M10 work.

M10-build2 responds to hardware evidence from the first M10 build: storage exhaustion must not disable live messaging, peer discovery must survive metadata-write failure, watch time needs bounded RTC/message discipline, and RX/TX now receive strong asynchronous sound/haptic feedback. The underlying FFat accounting anomaly is still unresolved and remains a migration/repair task.

M10-build3 refines only the receive notification identity: the prior ascending receive tone is replaced with an original synthesized six-beep radio-call cadence (three quick beeps, short pause, three quick beeps). Strong RX haptics and the TX alert remain unchanged.

M1.3.0 is the first independent-outbound implementation. It changes LXMF send routing and the corresponding UI gating/status while deliberately freezing the hardware-proven storage, radio, Reticulum-interface, and keyboard baselines.


### M1.4.1–M1.4.4 — security-rebase compatibility corrections

The first M1.4.0 build exposed a series of application/dependency-integration mistakes around microReticulum 0.5.0 and microStore 0.1.7. The final, corrected M1.4.4 understanding is:

- `microStore::Adapters::NoopFileSystem` **does exist** in microStore 0.1.7, but is hidden unless `USTORE_USE_NOOPFS` is defined;
- microReticulum 0.5.0 still requires a registered filesystem abstraction during `Reticulum::start()` even when RNS persistence/provisioning are disabled;
- the no-op adapter's `init()` return is not a valid success gate;
- application identity/history remain owned by ReticulumWatch's FFat `StorageService`;
- critical Reticulum/LXMF startup failures propagate to a visible `DEGRADED` top-level boot state.

M1.4.4 then built and ran on hardware. The security self-test passed and normal Link receive/reply messaging worked. The detailed chronology and permanent lessons from the failed M1.4.1–M1.4.3 iterations remain in `docs/MISTAKE-LEDGER.md`.

### M1.4.4 hardware result and transition to M2

The accepted M1.4.4 hardware run showed successful startup, valid/tampered signature self-tests, incoming Link establishment, incoming LXMF persistence and a signed watch reply transmitted successfully. A later M2.1.0 hardware run confirmed announce interoperability as working. Development therefore proceeds with the known-good networking baseline frozen while M2 messenger features are integrated.


## M4 — Power & Handheld UX

Implemented in the M4 integration build: large centered watch face, Ratspeak-inspired primary navigation, connected display standby, touch-wake input guard, wake-on-message, persistent display timeout. Hardware acceptance pending.


## M4 — HARDWARE ACCEPTED

User hardware testing confirmed LoRa LXMF send/receive, announce send/receive, connected standby, wake-on-touch, dark UI and revised navigation. An apparent radio regression was traced to a dead battery in the remote test radio, not M4 firmware. Treat M4 as the working baseline for M5.

## M5 — Additional Reticulum Interfaces — IMPLEMENTED; COMPILE/HARDWARE ACCEPTANCE REQUIRED

M5 implements opt-in Wi-Fi/TCP Reticulum in STA/client and AP/server roles, persistent reboot-applied interface settings, TCP diagnostics, and a 48 px watch-face clock. LoRa remains the default and the M4 networking boundary is frozen. USB/BLE RNode modes remain future interoperability work rather than placeholders.


## M10 — Stabilization & Runtime Reliability — HARDWARE ACCEPTED THROUGH M10-build3

- non-destructive FFat mount;
- 16-message/8-audio emergency RAM failover;
- RAM-first peer announce cache;
- loop supervisor and runtime health telemetry;
- bounded clock discipline;
- strong RX/TX haptics and distinct alert sounds;
- six-beep receive chime;
- communicator remains operational at zero FFat free space.

## M11 — Storage Recovery & Identity Backup — FUNCTIONAL / HARDWARE IN USE

- USB-serial off-device backup protocol;
- host-side verified backup/repair tool;
- FAT-vs-logical allocation diagnostics;
- same-session backup-token gate before explicit Reticulum-partition format;
- verified per-file restore with persistent-identity gate;
- accepted Reticulum/radio boundary remains frozen.

### M11-build2 — recovery resume + user canned replies + history wipe

- paced restore with per-chunk acknowledgement;
- bounded automatic restore retries using explicit FORCE after interruption;
- two user-owned canned replies in Settings → Messaging; blank disables a slot;
- untouched legacy `OK` / `On my way` defaults migrate to unset;
- confirmed chat-history wipe preserving identity/settings/contacts/peers;
- frozen Reticulum/radio/partition boundary retained.


## M12 — Final UX & Power — HARDWARE ACCEPTED IN M12-build4

- tap-to-wake removed from connected standby;
- crown short press toggles screen on/off while preserving PMIC long-press shutdown;
- persistent opt-in lift-to-wake using low-rate BMA423 observation;
- incoming-message wake remains;
- selected display brightness persists across screen wake;
- communicator/radio/partition boundary frozen.

### M12-build2 — crown/lift hardware recovery

- replaces pin-level crown polling with edge-latched AXP2101 status handling + fallback poll;
- logs every observed crown IRQ/status;
- uses pinned SensorLib BMA423 `getAccelerometer()` path;
- logs asleep X/Y/Z/magnitude/orientation samples and read failures;
- relaxes lift candidate rotation to ~35 degrees while retaining settle filtering;
- tap-to-wake remains disabled; incoming-message wake remains.

## M13 — Release Candidate — CURRENT / FINAL MILESTONE BEFORE STABLE

No new product features by default. M13 is reserved for:

- long-duration RX and repeated TX/RX soak;
- sleep/wake + crown/lift cycling;
- heap/PSRAM/stack and watchdog observation;
- battery/runtime profiling;
- storage/recovery sanity;
- remaining UI correction pass;
- documentation/package cleanup;
- only evidence-backed bug fixes.

If M13 passes its acceptance matrix, publish the first stable release instead of inventing M14.


### M12-build4 — sleep-state-aware lift wake

M12-build2 remained hardware-failed for lift-to-wake. M12-build4 removes raw accelerometer polling from the wake path and consumes the BMA423 feature interrupt on GPIO14 instead. Connected standby remains CPU/radio-on; BMA423 TILT/WAKEUP is re-armed on standby entry and the hardware event is latched independently of UI/display state. M12-build4 lift-to-wake is now hardware accepted; M13 is unblocked and active.


### M12-build4 — BMA423 acquisition/liveness correction

M12-build3 hardware logs proved connected standby keeps the CPU alive and proved the crown path, but the BMA423 returned `(0,0,0)` indefinitely and never asserted GPIO14. M12-build4 explicitly configures and enables accelerometer acquisition, requires a non-zero gravity vector before arming TILT/WAKEUP, and keeps GPIO14 as the actual wake authority. Physical lift-to-wake is now hardware accepted; M13 is active.

## v1.0.0 — FIRST STABLE RELEASE

v1.0.0 graduates from the hardware-accepted M13 release candidate. No new runtime feature is introduced at graduation. The stable artifact preserves the accepted M12-build4 wake behavior and frozen Reticulum/LXMF/radio boundary, and adds stable release versioning, packaging and explicit hardware-test documentation.

## v1.1.0 — POST-STABLE HOME FACE REFINEMENT

The M0-M13 milestone roadmap is complete. New work after v1.0.0 follows semantic release versions rather than inventing M14 by default.

v1.1.0 is a narrow UI refinement:

- enlarge the home clock modestly;
- apply a small optical left correction to the time only;
- preserve the accepted v1.0.0 protocol, storage, audio and wake behavior.
