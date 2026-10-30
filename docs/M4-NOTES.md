# ReticulumWatch M4 — Power & Handheld UX

M4 is based on the hardware-accepted M3-build3 baseline. It deliberately keeps the proven SX1262/Reticulum/LXMF data path intact.

## User-visible changes

- Larger 32 px centered time and larger 18 px centered date.
- Removed LoRa/RNS/power/peer diagnostics from the watch face.
- Home envelope opens **Messages** (conversation list).
- Main menu follows a handheld-Reticulum information architecture: Messages, Contacts, Peers, New Msg, Network, Settings, Announce.
- Dark theme remains the default.
- Connected Standby turns only the display off; CPU, radio and Reticulum remain online.
- Default inactivity timeout: 30 seconds. Settings -> Power cycles 15 sec / 30 sec / 60 sec / Never.
- The first touch while asleep only wakes the display. UI input is suppressed until that touch is released, preventing accidental activation of an invisible control.
- Incoming LXMF remains polled during display standby and wakes the screen using the existing notification path.

## Ratspeak/rsDeck reference

M4 adopts the clearest parts of rsDeck's handheld structure rather than cloning its T-Deck UI: a single Messages destination instead of competing Inbox/Chats top-level views, saved Contacts separate from discovered Peers, Announce as a first-class action, and Settings as the home for radio/power configuration. See `docs/RATSPEAK-REFERENCE.md`.

## Power boundary

M4 does **not** enter ESP light sleep, duty-cycle the SX1262, or enter deep sleep automatically. Those states can interrupt Reticulum receive processing and need separate measured acceptance tests. Connected Standby is therefore the default everyday battery-saving mode.

## T-Watch S3 display/touch implementation note

LilyGoLib 0.2.0's T-Watch S3 `sleepDisplay()` and `wakeupDisplay()` methods are no-ops. M4 therefore gates only `POWER_DISPLAY_BACKLIGHT`, matching LilyGo's own light-sleep implementation while leaving the touch controller and radio powered. Wake/release detection uses `getPoint()` instead of the latched `getTouched()` event bit.
