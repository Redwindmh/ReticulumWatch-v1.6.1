#include <Arduino.h>

#include "config/BuildConfig.h"
#include "audio/AudioService.h"
#include "audio/VoiceService.h"
#include "config/PowerConfig.h"
#include "hal/WatchHardware.h"
#include "lxmf/LxmfService.h"
#include "power/PowerManager.h"
#include "recovery/RecoveryService.h"
#include "radio/RadioService.h"
#include "reticulum/ReticulumService.h"
#include "storage/StorageService.h"
#include "system/HealthMonitor.h"
#include "transport/TransportService.h"
#include "ui/WatchUI.h"

namespace {
rw::hal::WatchHardware hardware;
rw::audio::AudioService audioService;
rw::audio::VoiceService voiceService;
rw::power::PowerManager powerManager;
rw::radio::RadioService radioService;
rw::reticulum::ReticulumService reticulumService;
rw::storage::StorageService storageService;
rw::lxmf::LxmfService lxmfService;
rw::transport::TransportService transportService;
rw::system::HealthMonitor healthMonitor;
rw::recovery::RecoveryService recoveryService;
rw::ui::WatchUI watchUI;
}

void setup() {
    Serial.begin(rw::config::kSerialBaud);
    delay(250);

    Serial.printf("\n%s %s booting...\n", rw::config::kProjectName, rw::config::kVersion);

    hardware.begin();
    powerManager.begin(hardware);
    storageService.begin();
    rw::config::power::Profile powerProfile = rw::config::power::defaultProfile();
    if (storageService.loadPowerProfile(powerProfile)) {
        Serial.println("[m4:power] loaded persisted power profile");
    }
    powerManager.configure(powerProfile);
    rw::config::radio::Profile radioProfile = rw::config::radio::defaultProfile();
    if (storageService.loadRadioProfile(radioProfile)) {
        Serial.println("[m3:radio] loaded persisted radio profile; applying at boot");
    } else {
        Serial.println("[m3:radio] using compiled default radio profile");
    }
    radioService.begin(hardware, radioProfile);
    // M6-build3 recovery rule: use the exact hardware-accepted M5 Reticulum
    // endpoint startup path. Routing controls remain visible as experimental
    // scaffolding, but they cannot alter the live Reticulum instance in this build.
    Serial.println("[m6:recovery] endpoint locked to hardware-accepted M5 Reticulum startup path");
    reticulumService.begin(storageService, radioService);
    lxmfService.begin(hardware, storageService, reticulumService);
    transportService.begin(storageService);

    // M9 audio is optional and deliberately layered above the accepted endpoint.
    // Audio failure must leave the text communicator fully usable.
    const bool audioReady = audioService.begin(hardware);
    lxmfService.setAudioFeedback(audioReady ? &audioService : nullptr);
    if (!audioReady) {
        Serial.printf("[m9:audio] DEGRADED audio only: %s; Reticulum/LXMF continue normally\n",
                      audioService.lastError());
    }
    const bool voiceReady = audioReady && voiceService.begin(audioService, storageService, lxmfService);
    if (!voiceReady) {
        Serial.printf("[m9:voice] DEGRADED voice only: %s; text LXMF continues normally\n",
                      voiceService.status());
    }
    watchUI.begin(hardware, powerManager, storageService, radioService, reticulumService,
                  transportService, lxmfService, audioService, voiceService);

    // M10 supervisor starts only after normal startup/UI construction. It is
    // intentionally independent of loopTask, and never mutates the filesystem
    // during recovery.
    const bool healthReady = healthMonitor.begin(storageService);
    if (!healthReady) {
        Serial.printf("[m10:health] DEGRADED supervisor only: %s; communicator continues normally\n",
                      healthMonitor.status());
    }
    const bool recoveryReady = recoveryService.begin(storageService, healthMonitor);
    if (!recoveryReady) {
        Serial.printf("[m11:recovery] DEGRADED recovery console: %s; communicator continues normally\n",
                      recoveryService.status());
    }

    // A critical subsystem failure must never be reported as a successful boot.
    // Keep the UI alive so the device remains inspectable/restartable, but make
    // the degraded state explicit in serial diagnostics.
    if (!reticulumService.ready() || !lxmfService.ready()) {
        Serial.printf("[boot] %s DEGRADED: Reticulum=%s; LXMF=%s\n",
                      rw::config::kVersion, reticulumService.status(), lxmfService.status());
        return;
    }

    hardware.vibrate();
    Serial.printf("[boot] %s initialization complete; frozen Reticulum/radio endpoint + "
                  "M9 audio ready=%s voice=%s M10 supervisor=%s M11 recovery=%s\n", rw::config::kVersion,
                  audioReady ? "yes" : "no", voiceReady ? "yes" : "no",
                  healthReady ? "yes" : "no", recoveryReady ? "yes" : "no");
}

void loop() {
    healthMonitor.heartbeat();
    recoveryService.loop();
    hardware.loop();
    powerManager.loop();
    radioService.loop();
    transportService.loop();
    reticulumService.loop();
    lxmfService.loop();
    audioService.loop();
    voiceService.loop();
    watchUI.loop();
    healthMonitor.loop();
    delay(2);
}
