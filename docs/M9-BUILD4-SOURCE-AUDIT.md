# M9-build4 Source Audit

## Parent

Parent: immutable `ReticulumWatch-M9-build3.zip`.

Hardware evidence for the correction came from the M9-build3 serial backtrace showing `Stack canary watchpoint triggered (loopTask)` at `make_analysis_window()` called by `codec2_create()` from `VoiceService::begin()`.

## Functional source changes from M9-build3

- `src/audio/VoiceService.cpp`
  - removes synchronous Codec2 construction from `VoiceService::begin()`;
  - performs Codec2 construction/frame discovery inside `workerLoop()`;
  - logs worker stack size/high-water information.
- `src/audio/VoiceService.h`
  - increases dedicated codec task stack to 32768 bytes.
- `src/storage/StorageService.cpp/.h`
  - adds non-destructive storage-pressure diagnostics;
  - purges only rebuildable message-ID index markers when critically low on free space;
  - defers index writes/rebuild during that pressure boot and keeps journal-scan fallback active.
- version/docs/preflight metadata updated to M9-build4.

## Explicitly unchanged

Frozen Reticulum/radio files and both partition maps remain byte-identical to M8-build3. LMX1/LMX2/LMX3 persisted layouts are unchanged. No Resource transfer, private fragmentation, Transport mode, or RNS persistence redesign is introduced.

## Verification status

Release preflight and partition validation are required before packaging. Container environment does not contain PlatformIO, so only the user's real PlatformIO build can confer compile verification. Hardware acceptance requires the user's watch test.
