Import("env")
import time

# PlatformIO runs this at build time; Unix epoch is timezone-independent. This
# avoids interpreting local __DATE__/__TIME__ text as UTC when the RTC is unset.
env.Append(CPPDEFINES=[("RW_BUILD_EPOCH_UTC", int(time.time()))])
print("[m5:build] embedded UTC build epoch for RTC fallback")
