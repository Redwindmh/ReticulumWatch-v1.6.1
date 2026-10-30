# Ratspeak / rsDeck reference for ReticulumWatch

Research date: 2026-09-08. This is a product/UX reference, not copied source code.

## Patterns adopted now

- One primary **Messages** conversation view.
- **Contacts** = explicitly saved peers.
- **Peers** = discovered/known identities not necessarily saved.
- **Announce** remains a first-class action.
- **Settings** owns radio and power configuration.
- Delivery state remains visually available and must stay proof-backed.

## Features to pursue in later milestones

- Radio presets in addition to the existing advanced radio tuning.
- Wi-Fi/TCP Reticulum interface and interface status.
- Identity import/export and safer backup/restore.
- Path requests, propagation status, transport/interface health, and an activity log.
- Optional standalone/RNode launcher mode over USB/BLE, if it can be added without weakening standalone behavior.
- Themes/text sizing once the UI system is redesigned.
- Reactions/replies interoperability.
- voice messages/calls are plausible on T-Watch S3 hardware but remain experimental/later work.
- Channels are interesting but should wait until direct messaging and interface management are mature on the 240x240 form factor.

## What is intentionally not copied

rsDeck uses a keyboard/trackball T-Deck and its five-tab layout cannot be transplanted literally to a 240x240 touch watch. ReticulumWatch keeps watch-sized hit targets, a home face, and touch-first navigation while adopting the underlying information architecture.
