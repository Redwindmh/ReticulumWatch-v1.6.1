# M11-build2 Source Audit

## Intended change boundary

Runtime/source changes are limited to:

- `src/recovery/RecoveryService.cpp` — per-chunk restore ACK;
- `tools/rw_recovery.py` — paced, retryable restore;
- `src/storage/StorageService.{h,cpp}` — explicit chat-history wipe and legacy canned-reply migration;
- `src/config/UserConfig.h` — canned slots may be unset;
- `src/ui/WatchUI.{h,cpp}` — canned-reply settings/visibility and confirmed history wipe;
- `src/config/BuildConfig.h` — version only;
- documentation/release-preflight artifacts.

## Frozen protocol boundary

No intended changes to:

- `src/reticulum/ReticulumService.cpp/.h`;
- `src/reticulum/WatchLoRaInterface.cpp/.h`;
- `src/radio/RadioService.cpp/.h`;
- `src/config/RnsBuildConfig.h`;
- `src/config/RadioConfig.h`;
- either partition CSV.

Release preflight must continue to match the accepted hashes exactly.

## Persistence compatibility

- LMX1/LMX2/LMX3 record layouts are unchanged.
- `PersistedUserProfileV1` layout is unchanged.
- the M11 recovery backup protocol remains RWB1 protocol 1;
- the restore side adds an acknowledgement line but does not change file data or CRC semantics.

## Destructive-action review

`wipeChatHistory()` contains no identity path, user-profile path, or peer-directory deletion. It is only reachable from a dedicated confirmation screen.

M11 format rules are unchanged: no automatic format and token-gated explicit repair only.
