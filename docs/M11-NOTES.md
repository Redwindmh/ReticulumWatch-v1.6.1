# M11 — Storage Recovery & Identity Backup

M11 begins from hardware-accepted `ReticulumWatch-M10-build3.zip`.

Parent SHA-256:

`f19248b3fd5ed4e222d4adbbd906b719dc8a4bf26a216484f461727e179418d1`

## Why M11 exists

The watch is operational even with the dedicated Reticulum FFat volume reporting zero free bytes because M10 added RAM failover for messaging and peers. The remaining risk is durability: RAM-only messages are lost on reboot, and the persistent volume's FAT allocation accounting is inconsistent with the small traversable logical file set.

M11 adds an explicit off-device recovery workflow. It does not automatically format storage.

## M11 recovery protocol

`src/recovery/RecoveryService.*` exposes a USB-serial maintenance protocol at the existing 115200-baud console.

User-facing commands:

- `M11 HELP`
- `M11 STATUS`
- `M11 BACKUP`
- `M11 FORMAT <token>`
- `M11 RESTORE BEGIN <token>`
- `M11 RESTORE BEGIN FORCE` for an interrupted recovery with an already verified host backup

The source tree also includes `tools/rw_recovery.py`.

Recommended safe repair command:

```bash
uv run --with pyserial python tools/rw_recovery.py repair \
  --port /dev/cu.usbmodemXXXX \
  --out backups/reticulumwatch
```

The host workflow is deliberately ordered:

1. stream files from the watch;
2. verify every file's byte count and CRC32 on the host;
3. obtain the same-session format token from the completed backup;
4. format only the `reticulum` FAT partition;
5. restore the verified files;
6. verify every restored file on the watch;
7. require the restored `/rns/identity.key` to be exactly 64 bytes;
8. restart into the restored identity/history.

## Backup contents

The recovery stream includes persistent files under the Reticulum FAT volume, including:

- persistent RNS identity;
- user/radio/power/network/transport configuration;
- LMX message history;
- read/unread and delivery overlays;
- saved contact/peer metadata;
- audio sidecars;
- counters and other persistent application state;
- corrupt quarantine artifacts when present.

Rebuildable `.idx` message-ID accelerators and temporary `.tmp` files are intentionally omitted. They are not authoritative and are regenerated/fallback-scanned by normal storage recovery.

## Destructive-operation gate

`M11 FORMAT` is refused unless a complete backup finished successfully in the same boot and the supplied 8-hex token matches that backup. Formatting is never automatic.

After formatting, StorageService enters maintenance mode so ordinary application filesystem writes cannot race the restore. The loop supervisor is also placed in maintenance mode during long backup/recovery operations.

## Diagnostics

`M11 STATUS` reports:

- raw partition offset and size;
- FFat total/free/used bytes;
- traversable file count and logical bytes;
- the difference between FAT-reported used bytes and traversable logical file bytes.

A large difference is reported as an allocation anomaly and is the primary M11 evidence for deciding whether a backup/rebuild is warranted.

## Important limitation

Emergency RAM-journal records from a previous firmware boot cannot survive the reboot needed to flash M11. M11 prevents this limitation from recurring during future storage recovery by providing the off-device recovery path, but it cannot retroactively export RAM from M10 after that firmware has been replaced.

## Acceptance target

M11 is accepted when:

1. the project builds/flashes;
2. normal Reticulum/LXMF regression remains healthy;
3. `M11 STATUS` reports useful accounting;
4. `rw_recovery.py capture` produces a verified host backup containing the same persistent identity;
5. a controlled `repair` run can format only the Reticulum FFat, restore the backup, reboot, and preserve the original LXMF destination/identity;
6. filesystem free-space accounting returns to a sane value after rebuild;
7. restored message history/settings remain readable.
