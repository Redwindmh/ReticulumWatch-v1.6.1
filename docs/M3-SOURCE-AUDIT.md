# ReticulumWatch M3 — Source Audit

- Base: M2.6.1 integrated messenger.
- Frozen/hash-checked: RadioService, WatchLoRaInterface, ReticulumService, RadioConfig, 16 MiB and 8 MiB partition maps.
- Modified application areas: WatchHardware (RTC/system-time + battery helpers), WatchUI (M3 watch-native surfaces), version/log strings.
- No third-party source rewriting: the speculative M3 build-1 announce patch was removed after confirming microReticulum 0.5.0 already carries the timestamp-form freshness encoding.
- Message journal layout: unchanged LMX1/LMX2/LMX3.
- microReticulum pin: remains 0.5.0.
- microStore pin: remains 0.1.7.
- Announce compatibility: ESP wall clock is established before Reticulum; RTC preferred, compile-time fallback; runtime epoch logged before manual announce.
