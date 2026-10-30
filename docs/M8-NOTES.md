# M8 — Configuration and User Settings

## Goal

Move ordinary device preferences out of source-code constants while preserving the hardware-accepted Reticulum endpoint boundary.

## Reference checks

Before implementing Reticulum-facing controls, M8 was checked against:

- the official Reticulum 1.5.3 interface/configuration manual;
- current microReticulum_Firmware provisioning documentation;
- current rsDeck user-facing configuration patterns.

The official manual advises balanced announce rates and states that application destinations normally do not need to announce more often than every few hours. M8 therefore exposes only Manual, On Start and Every 6 Hours.

## Persistence model

`/app/user.cfg` is a versioned, CRC-protected sidecar. Preference corruption must not affect the immutable LXMF journal or prevent RNS startup.

## Live-safe settings

- persistent brightness;
- haptics;
- notification popup behavior;
- custom quick replies;
- peer-list visibility;
- announce scheduling through the already accepted announce request path.

## Frozen / deferred settings

- Radio and Wi-Fi/TCP continue using their existing reboot-to-apply flow.
- Transport mode remains locked to Endpoint.
- User device label is local-only in M8; changing network announce app-data is deferred because `LxmfService` is frozen.
- Message retention remains at the proven 256-record journal limit.
- Raise-to-wake remains deferred pending a dedicated BMA423 event-path hardware test.


## M8-build3 — announce propagation discipline

Observed hardware behavior: M8 locally emitted valid 187-byte `lxmf.delivery` announces, while Link RX/TX continued to work, but the remote path did not surface the watch announce. The protocol-critical announce/radio sources were unchanged from accepted M7.

Reference check:
- Official Reticulum interface documentation: `announce_cap` queues announce traffic when its bandwidth budget is exceeded and rapidly re-announcing destinations are down-prioritised.
- Official Reticulum announce-rate controls can impose a per-destination re-propagation interval on Transport/Gateway interfaces.
- microReticulum examples keep announce generation simple; no private ReticulumWatch packet format is required.

Build3 response:
- preserve the exact accepted announce encoder and radio/Reticulum implementation;
- add a 60-second manual anti-spam guard;
- let a manual announce satisfy the startup scheduler and reset the six-hour timer;
- expose local request count for diagnosis;
- never claim local emission proves remote propagation.
