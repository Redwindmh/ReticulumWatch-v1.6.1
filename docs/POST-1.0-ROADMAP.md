# ReticulumWatch Post-1.0 Roadmap

The original milestone roadmap M0-M13 is complete. Post-1.0 work uses semantic versions and is intentionally staged so UI, protocol, power and audio changes do not destabilize one another.

## v1.2 — Messaging and UI interaction polish

Initial scope:

- visible send-state modal;
- consistent send UX across composer and quick replies;
- clear submitted/failed wording without false delivery claims.

Likely follow-on 1.2.x refinements:

- more readable outgoing delivery-state presentation than single-letter `S/R/D/!`;
- message bubble/row hierarchy better suited to a 240 px watch;
- clearer conversation/peer identity headers;
- scroll positioning to newest messages;
- compact timestamp treatment;
- less debug-like menu presentation.

## v1.3 — Deeper Reticulum functionality

Candidate scope, reference-gated against official Reticulum and the exact pinned embedded implementation:

- peer last-seen/announce age;
- hop count and useful reachability information;
- path-request state visible in the UI;
- active Link state visible per conversation;
- useful announce diagnostics/control without announce spam;
- route/path information where the pinned stack exposes it safely;
- revisit Transport/propagation functionality only as a separate, reference-backed experiment.

The frozen endpoint path remains protected until interoperability evidence justifies a change.

## v1.4 — Power and endurance

- establish repeatable battery/runtime measurements;
- reduce diagnostic/heartbeat work in normal release mode;
- profile display-off connected standby;
- tune CPU/task cadence where Reticulum receive latency remains acceptable;
- evaluate selective peripheral power gating;
- improve lift-to-wake sensitivity only through documented BMA423 controls or measured application-side gating;
- do not trade away always-listening Reticulum behavior without an explicit user-selectable mode.

## v1.5 — Audio and PTT refinement

- improve voice-record/send UI;
- record-state/progress feedback;
- playback progress/cancel behavior;
- clearer distinction between LXMF voice messages and PTT burst behavior;
- tune Codec2 quality/duration/bandwidth tradeoffs;
- investigate LXST/streaming separately, with interoperability testing before calling anything live voice/PTT.

## Later candidates

- watch-face customization;
- richer contact management;
- identity backup UX on-device;
- OTA/update workflow if it can be done without weakening recovery;
- configurable notification profiles;
- optional low-power modes with clearly stated network tradeoffs.


## v1.3.0 implemented

Peer Details is the first deeper-Reticulum UI release: full peer identity, hop/last-seen metadata, verified state, active-Link visibility and contact actions, without changing the frozen packet/radio boundary.

## v1.4.0 implemented

The first Power & Endurance release removes 1 Hz diagnostic accelerometer polling from connected standby, reduces serial/UI standby churn, and adds session active/standby/wake counters. Always-listening LoRa + Reticulum behavior remains the product contract.


## v1.5.0 implemented

- live conversation PTT state;
- active-Link prerequisite shown and enforced in UI;
- record duration and playback progress;
- clearer VOICE record/preview/send workflow;
- no streaming/LXST protocol change.

Next: one stabilization/release-candidate pass, not another broad feature milestone.
