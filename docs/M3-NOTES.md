# ReticulumWatch M3 — Watch-Native Experience

## Scope

M3 completes the watch-native milestone in one build while retaining the M2.6.1 messenger stack. It also fixes mixed Reticulum announce freshness ordering at the pinned microReticulum 0.5.0 boundary.

## Acceptance checklist

1. Clean-build and flash without erasing the Reticulum storage partition.
2. Confirm boot log contains `[time] system clock seeded from RTC` and `[announce:patch] applied ...` appears during the PlatformIO build.
3. Confirm existing identity/history/contacts return and normal receive/reply still works.
4. From **Network**, press **ANNOUNCE**. Confirm the external Python Reticulum/LXMF peer learns or refreshes the watch `lxmf.delivery` destination.
5. Verify Watch Face shows correct time/date, battery, LoRa/RNS status and unread count.
6. Verify Menu navigation reaches Inbox, Conversations, Contacts, Peers, Compose, Network and Settings.
7. With the watch on its watch face (or display asleep), send it an LXMF message. Confirm display wake, notification sender/preview, unread state, OPEN, DISMISS and at least one quick reply.
8. Confirm full composer, contact naming/favorite, peer list and M2.6 threaded messaging still work.
9. Reboot and verify history/contacts/read state remain intact.

## Diagnostic tags

- `[announce:patch]` build-time microReticulum 0.5.0 wire correction
- `[announce:compat]` runtime declaration of expected patched dependency
- `[time]` RTC → ESP wall-clock seeding
- `[m3:watch]` watch-face refresh
- `[m3:nav]` navigation and list/thread rebuilds
- `[m3:notify]` incoming notification decisions and actions
- `[m3:network]` network screen / announce requests
- `[m3:compose]` compose and quick replies
- `[m3:contact]` contact naming
- `[m3:settings]` settings changes
- existing `[m2:peer]`, `[m2:tx]`, `[m2:receipt]`, `[m2:retry]`, `[radio:*]`, `[rns:*]`, `[lxmf:*]` remain available.

## Compile verification status

The packaged release is statically audited here. A real PlatformIO ESP32-S3 compile is still required on the development machine because PlatformIO is not installed in the artifact-generation environment.
