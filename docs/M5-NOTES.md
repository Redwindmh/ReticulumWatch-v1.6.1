# ReticulumWatch M5 Notes

## Milestone

M5 adds additional Reticulum interface plumbing without changing the hardware-accepted M4 LoRa path.

## Hardware acceptance checklist

1. Boot with no `/app/network.cfg`: LoRa messaging and announce behaviour should match M4.
2. Confirm the watch face uses the 48 px clock and centered date.
3. Confirm connected standby and wake-touch suppression still work.
4. Settings -> Interfaces: save a profile, navigate away, and verify the reboot/later prompt.
5. STA/TCP client: join Wi-Fi and connect to a standard Reticulum TCP server on the configured host/port. Verify announces and LXMF can traverse that interface.
6. AP/TCP server: join the watch AP from another device, connect a standard Reticulum TCP client, and verify announces/LXMF.
7. Disable Wi-Fi/TCP, reboot, and verify the watch returns to LoRa-only operation.

## Safety boundary

Wi-Fi/TCP is disabled by default. M5 does not modify the frozen radio service, LoRa Reticulum interface, Reticulum service, radio defaults, or partition layout.

USB/BLE RNode mode remains future work until host-side RNode interoperability is complete.
