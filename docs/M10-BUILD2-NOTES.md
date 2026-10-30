# M10-build2 Notes — Storage Failover, Live Peers, Alerts and Clock Discipline

Parent: `ReticulumWatch-M10.zip`

M10 hardware logs showed that the radio/Reticulum endpoint remained healthy while the application layer became unusable after FFat free space reached zero. Announces were accepted, an incoming encrypted Link established, and packets were passed into Transport; the failure occurred when the persistence guard correctly refused new journal writes. This build changes the degradation semantics without weakening the preservation-first filesystem policy.

## Emergency RAM journal

When a journal write cannot preserve the 16 KiB emergency reserve, StorageService switches to a bounded 16-message RAM journal for the remainder of the boot. Incoming and outgoing LXMF continue to be indexed by sequence and remain visible to the UI. Incoming unread state and mutable delivery state are tracked in RAM. Up to eight 512-byte audio sidecars can also remain volatile.

This is deliberately **not** a substitute for filesystem repair. RAM-only records are lost on reboot. The persistent identity and existing LMX1/LMX2/LMX3 files remain untouched.

## Peer discovery

Announce discovery now updates a 48-entry RAM peer cache before any persistence attempt. `listPeers()` unions RAM and readable disk metadata. This makes the Peers screen operational when `/lxmf/metadata/peers` cannot be written. The screen also refreshes while open when a new announce arrives.

## Message feedback

Incoming message:

- three strong DRV2605 clicks, asynchronously scheduled;
- three-note audible alert generated on the dedicated audio worker.

Successful outgoing transmission:

- two strong clicks;
- shorter two-note audible alert.

The alert generator does not modify a recorded voice clip and refuses to interrupt active recording/playback/codec processing.

## Clock discipline

The PCF8563 is interpreted as local JST wall time and converted with `mktime()` when a Unix timestamp is needed. At boot, a firmware build epoch that is more than 60 seconds newer than the RTC advances both system time and the RTC. Once running, Link-carried LXMF timestamps (or verified opportunistic LXMF timestamps) may correct a skew of at most 30 minutes. The corrected time is written back to the PCF8563.

The bounded skew guard is intentional: LXMF timestamps are useful for correcting small drift but are not treated as an unrestricted network time authority.

## Frozen boundary

M10-build2 does not modify the accepted Reticulum service, LoRa interface, radio service/config, RNS build config, or partition maps.

## Acceptance tests

1. Boot with the existing nearly-full FFat volume; verify no auto-format and no loss of identity/history.
2. Receive an announce and open PEERS; the announced peer must be visible even if peer metadata persistence is blocked.
3. Receive an LXMF text with FFat at zero free bytes; it must appear in the conversation and be marked as RAM-journal/volatile in diagnostics.
4. Send Link and opportunistic text under the same condition; physical TX must proceed and UI must report success instead of persistence failure.
5. Verify distinct loud RX/TX sound plus strong haptic patterns.
6. Verify the watch time advances to the current build time after flash, then corrects toward a current incoming Link timestamp if the remaining skew is within 30 minutes.
7. Confirm normal announces, RX/TX, audio, sleep/wake, and health telemetry remain operational.
