# M3 build 3 notes

This build addresses hardware feedback from M3 build 2 without changing the LMX1/LMX2/LMX3 message journal format.

- Fixes Contacts stack-canary crash by replacing the 48-record loop-stack allocation with a bounded contact scan.
- Watch face falls back to the seeded ESP system clock when the PCF8563 RTC is invalid, so time is rendered instead of `--:--`.
- Dark UI is the default.
- Removes the `RETICULUM` word from the watch face.
- Replaces the home `H` with the LVGL home icon.
- Replaces the bottom Messages control with an envelope icon and the Menu control with three drawn lines, while retaining their original hit boxes.
- Adds an ANNOUNCE action directly to the app menu; the Network page keeps its ANNOUNCE control as well.
- Adds persistent radio settings for frequency, bandwidth, spreading factor, coding rate, TX power, and preamble length. Sync word remains displayed as the compatibility value.
- Radio changes are stored under `/app/radio.cfg` and apply only after reboot. Navigating away from changed Radio settings prompts for REBOOT or LATER.
- Compiled default RF profile remains 920.600 MHz, 125 kHz, SF8, CR 4/5, sync 0x12, preamble 18, TX 10 dBm.
- Corrects stale announce diagnostic wording; microReticulum 0.5.0 is not source-patched.
