#!/usr/bin/env python3
from pathlib import Path
import hashlib, re, subprocess, sys

ROOT = Path(__file__).resolve().parents[1]
checks=[]; errors=[]

def txt(rel):
    p=ROOT/rel
    if not p.exists():
        errors.append(f'missing {rel}')
        return ''
    return p.read_text(errors='ignore')

def sha(rel):
    return hashlib.sha256((ROOT/rel).read_bytes()).hexdigest()

def check(cond, ok, bad):
    (checks if cond else errors).append(ok if cond else bad)

BASELINE={
 'src/reticulum/ReticulumService.cpp':'b10f3a3f81606b67a06c34fbd68be3fa233a105bf86e7c85a1d007b114a6e311',
 'src/reticulum/ReticulumService.h':'66f67783c564a4280ef338cfa880140f22f899f1e97f16884f2203f810f4b910',
 'src/config/RnsBuildConfig.h':'288176052cbad5085bff9f1a4c0bf6425a6ec0544a85688aa838ce1f104acd04',
 'src/reticulum/WatchLoRaInterface.cpp':'a0ec923752b65a02e9cdc267cbdbdbaba9e8bfdcf68f5fc95d8e00f1560a2e49',
 'src/reticulum/WatchLoRaInterface.h':'f2ab916e2bdafa2cd3ad265ce537f275a71e9b9b1285008e01bc7c3548eb625c',
 'src/radio/RadioService.cpp':'6d1a966c2b7d192831b679e3666fbd49bab7843c09b075ae2c2ee409c7fee415',
 'src/radio/RadioService.h':'3e39860839dee2b75d0befd052b7fb18b72020e7076869a87267b18ea4a767fc',
 'src/config/RadioConfig.h':'1e09d23c2c07d4dd0ae472e49353fa901b652ec913f3f0e1fc12059801e0da4e',
 'partitions/reticulum_watch_16mb.csv':'c108905af498a1d2e348f66af2ebd03ac98c3c8b65ebdba9a177dbb652b21dff',
 'partitions/reticulum_watch_8mb.csv':'e8109c3a8dbe1af484cf78fbcf43ebb222f356dffe34a595a6257e8ab7e60223',
}

build=txt('src/config/BuildConfig.h'); ini=txt('platformio.ini'); main=txt('src/main.cpp')
ui=txt('src/ui/WatchUI.cpp'); uih=txt('src/ui/WatchUI.h'); store=txt('src/storage/StorageService.cpp'); storeh=txt('src/storage/StorageService.h'); user=txt('src/config/UserConfig.h'); audio=txt('src/audio/AudioService.cpp'); audioh=txt('src/audio/AudioService.h'); voice=txt('src/audio/VoiceService.cpp'); voiceh=txt('src/audio/VoiceService.h'); lxmf=txt('src/lxmf/LxmfService.cpp'); lxmfh=txt('src/lxmf/LxmfService.h'); third=txt('THIRD_PARTY.md'); health=txt('src/system/HealthMonitor.cpp'); healthh=txt('src/system/HealthMonitor.h'); hw=txt('src/hal/WatchHardware.cpp'); hwh=txt('src/hal/WatchHardware.h'); power=txt('src/power/PowerManager.cpp'); powerh=txt('src/power/PowerManager.h'); recovery=txt('src/recovery/RecoveryService.cpp'); recoveryh=txt('src/recovery/RecoveryService.h'); recoverytool=txt('tools/rw_recovery.py')

check('kVersion = "1.6.1"' in build,'version 1.6.1','wrong version')
check('-D RW_MILESTONE=161' in ini,'v1.6.1 maintenance marker 161','wrong milestone macro')
check('-D LV_USE_QRCODE=1' in ini,'LVGL QR enabled','QR build flag missing')
check('attermann/microReticulum @ 0.5.0' in ini and 'attermann/microStore @ 0.1.7' in ini,'pinned RNS dependencies retained','RNS dependency pins changed')
check('-D USTORE_USE_NOOPFS=1' in ini,'NoopFS build contract retained','NoopFS macro missing')
check('extra_scripts = pre:scripts/build_epoch.py' in ini,'accepted build-epoch hook retained','unexpected PlatformIO pre-build hook')
check('patch_microreticulum' not in ini.lower(),'no dependency source patch configured','dependency source patch configured')
check(not any(ROOT.glob('scripts/patch_microreticulum*.py')),'no dependency patch script present','dependency patch script present')

for rel, expected in BASELINE.items():
    check((ROOT/rel).exists() and sha(rel)==expected,f'frozen Reticulum/radio endpoint exact: {rel}',f'frozen endpoint file changed: {rel}')

# M9 audio isolation / resource bounds
check((ROOT/'src/audio/AudioService.cpp').exists() and (ROOT/'src/audio/AudioService.h').exists(),'AudioService module exists','AudioService module missing')
check('kSampleRate = 16000' in audioh and 'kBitsPerSample = 16' in audioh and 'kMaxRecordMs = 4000' in audioh,'bounded 16k/16-bit/4s audio profile','audio capture profile changed or unbounded')
check('MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT' in audio and 'heap_caps_malloc(kBufferBytes' in audio,'audio buffer allocated in PSRAM','audio buffer not PSRAM-bounded')
check('heap_caps_malloc(kBufferBytes, MALLOC_CAP_8BIT)' not in audio,'no internal-heap audio fallback','audio can fall back to scarce internal heap')
check('xTaskCreatePinnedToCore' in audio and 'workerEntry' in audio and 'workerLoop' in audio,'dedicated audio worker task','audio worker task missing')
check('AudioService::loop()' in audio and 'Intentionally non-blocking' in audio,'non-blocking AudioService loop','audio loop blocking policy missing')
check('instance.mic.readBytes' in audio and 'instance.player.write' in audio,'I2S capture/playback implemented','I2S capture/playback missing')
check('configureTX(' in audio and 'I2S_SLOT_MODE_STEREO' in audio,'speaker TX normalized explicitly','speaker TX normalization missing')
check('instance.powerControl(POWER_SPEAK, enabled)' in audio,'speaker rail controlled by AudioService','speaker rail control missing')
check('LxmfService' not in audio and 'ReticulumService' not in audio,'AudioService isolated from protocol services','AudioService directly depends on Reticulum/LXMF')
check(main.find('lxmfService.begin') < main.find('audioService.begin'),'Reticulum/LXMF starts before optional audio','audio starts before accepted communicator')
check('DEGRADED audio only' in main and 'audioService.ready()' not in main[main.find('if (!reticulumService.ready()'):main.find('hardware.vibrate()')],'audio failure does not gate communicator','audio readiness gates communicator startup')
check('audioService.loop();' in main,'audio service wired into loop','audio service loop missing')
check((ROOT/'src/audio/VoiceService.cpp').exists() and (ROOT/'src/audio/VoiceService.h').exists(),'VoiceService module exists','VoiceService module missing')
check('sh123/esp32_codec2 @ 1.0.7' in ini,'Codec2 dependency pinned exactly','Codec2 dependency missing/unpinned')
check('sh123/esp32_codec2 @ 1.0.7' in third and 'GPL-3.0' in third,'Codec2 dependency/license documented','Codec2 third-party notice missing')
check('kLxmfAudioField = 0x07' in voiceh and 'kCodecMode = 0x03' in voiceh and 'kLegacyCodecMode450 = 0x02' in voiceh,'standard LXMF Codec2-700C outbound + legacy 450 constants','LXMF audio field/mode compatibility constants wrong')
check('CODEC2_MODE_700C' in voice and 'CODEC2_MODE_450' in voice,'Codec2 700C outbound and legacy 450 decode present','Codec2 700C/450 compatibility mapping missing')
check('kMaxEncodedBytes = 240' in voiceh,'voice encoded field capped below Link MDU budget','voice encoded field cap is not 240 bytes')
check('kPreferredVoiceRecordMs = 3000' in voiceh and 'maxRecordMs() const' in voiceh,'voice recorder capped from codec geometry','voice record duration cap missing')
check('startRecording(uint32_t maxDurationMs = kMaxRecordMs)' in audioh and 'recordLimitBytes_' in audioh and 'recordLimitBytes_' in audio,'AudioService supports bounded per-operation recording','voice-specific recording bound missing')
check('xTaskCreatePinnedToCore' in voice and 'workerLoop' in voice,'dedicated codec worker task','voice codec worker missing')
check('kWorkerStackBytes = 32768' in voiceh,'voice worker has 32 KiB stack','voice worker stack is below accepted recovery bound')
voice_begin = voice[voice.find('bool VoiceService::begin('):voice.find('void VoiceService::loop()')]
voice_worker = voice[voice.find('void VoiceService::workerLoop()'):voice.find('void VoiceService::encodeRecording()')]
check('codec_ = codec2_create' not in voice_begin,'Codec2 construction absent from Arduino setup/loopTask path','codec2_create still runs inside VoiceService::begin')
check('ensureCodec(kCodecMode)' in voice_worker and 'codec2_create(c2Mode)' in voice,'Codec2 creation reached only through worker-owned codec switch','worker codec creation/switch path missing')
check('initialisation deferred off loopTask' in voice,'runtime diagnostic documents deferred Codec2 init','Codec2 deferred-init diagnostic missing')
check('sendVoiceMessage(' in voice and 'lxmf_->sendVoiceMessage' in voice,'voice send dispatched through LxmfService','voice send path missing')
check('sendVoiceMessage' not in voice[voice.find('void VoiceService::workerLoop()'):voice.find('void VoiceService::setState')],'codec worker never calls Reticulum/LXMF send','codec worker directly sends network traffic')
check('voiceService.loop();' in main,'voice service wired into main loop','voice service loop missing')
check(main.find('lxmfService.begin') < main.find('voiceService.begin'),'LXMF starts before optional voice','voice starts before accepted communicator')
check('DEGRADED voice only' in main,'voice failure does not gate text communicator','voice failure policy missing')
check('Screen::AudioLab' in ui and '"AUDIO"' in ui and '"RECORD"' in ui and '"PLAY"' in ui and '"STOP"' in ui and '"CLEAR"' in ui,'Audio Lab controls wired','Audio Lab UI incomplete')
check('Codec2 700C voice message' in ui and 'VOICE' in ui,'standard Codec2-700C LXMF voice scope explicit in UI','voice UI does not identify standard 700C LXMF audio')
check('info.mode != kCodecMode && info.mode != kLegacyCodecMode450' in voice,'legacy Codec2-450 sidecars remain accepted','legacy 450 playback compatibility missing')
check('ensureCodec(mode)' in voice and 'codec2_destroy' in voice,'worker codec switching is bounded to one context','codec switching/cleanup missing')
check('AM_CODEC2_700C' in voiceh and 'Sideband' in voiceh,'Sideband 700C interoperability rationale in source','700C interoperability rationale missing')
check('(audio_->busy() || (voice_ && voice_->busy()))' in ui and 'power_->notifyActivity()' in ui,'active audio/codec work keeps display awake','audio/voice activity does not refresh display activity')

check('reticulumService.begin(storageService, radioService);' in main,'accepted two-argument RNS startup retained','RNS startup contract changed')
check('transportProfile.transportEnabled' not in main,'Transport setting cannot alter live RNS boot','Transport setting affects RNS boot')
check('Mode: ENDPOINT (LOCKED)' in ui,'routing remains endpoint-locked','routing endpoint lock missing')
check('Transport toggle ignored' in ui,'routing toggle remains fail-safe','routing toggle no longer fail-safe')

# M9-build4+ non-destructive storage-pressure recovery
check('kStoragePressureThresholdBytes = 256ULL * 1024ULL' in storeh,'256 KiB storage-pressure reserve','storage-pressure threshold missing/changed')
check('purgeDerivedMessageIdIndexMarkers' in store and 'nameEndsWith(name, ".idx")' in store and 'nameEndsWith(name, ".idx.tmp")' in store,'derived message-ID index purge present','safe derived-index purge missing')
check('lxmfMessageIdIndexDeferred_' in storeh and 'lxmfMessageIdIndexDeferred_' in store,'message-ID rebuild deferral tracked','message-ID deferral state missing')
check('history scan fallback active' in store,'journal-scan fallback retained under storage pressure','storage-pressure dedupe fallback missing')
purge_block = store[store.find('uint32_t purgeDerivedMessageIdIndexMarkers()'):store.find('void logStoragePressureSummary()')]
check('kIdentityPath' not in purge_block and 'kLxmfInboxDir' not in purge_block and 'kLxmfPeersDir' not in purge_block and 'kLxmfAudioDir' not in purge_block,'pressure purge cannot erase identity/journal/contacts/audio','pressure purge touches authoritative/user data')

# Journal compatibility
def struct_block(src,name):
    i=src.find('struct '+name)
    if i < 0: return ''
    j=src.find('};',i)
    return src[i:j+2]
LMX={
 'PersistedLxmfRecordV1':'2feebf39ac9aebd0c5ab688fe1a2956e9efc5171e1f7d1fbb256d69e3780cf0b',
 'PersistedLxmfRecordV2':'b3d10f80c515d1d3f4fe7a7fb34ef4737bfa7ba290b80f0c162616065d23ce5e',
 'PersistedLxmfRecordV3':'16c5f946b5e570555a237801e92e4b3a69acef8ff2348eaa43436d02ad6d8a47',
}
for name, expected in LMX.items():
    got=hashlib.sha256(struct_block(store,name).encode()).hexdigest()
    check(got==expected,f'{name} unchanged',f'{name} layout changed')

# M9-build5 interoperable voice: Sideband-compatible 700C + legacy 450 decode + PTT burst.
check('kFieldAudio = 0x07' in lxmfh,'LXMF FIELD_AUDIO constant retained','LXMF audio field constant missing')
check('sendVoiceMessage' in lxmf and 'buildSignedVoiceMessage' in lxmf,'LXMF voice packing/send path present','LXMF voice send path missing')
check('packer.packInteger(kFieldAudio)' in lxmf and 'packer.packArraySize(2)' in lxmf,'standard FIELD_AUDIO array packed','audio field wire shape changed')
check('wire.size() > mdu' in lxmf and 'replyLink_.get_mdu()' in lxmf,'voice active-Link MDU guard present','voice MDU guard missing')
check('kMaxEncodedBytes = 240' in voiceh and 'voice exceeds active Link MDU' in lxmf,'complete voice wire has bounded payload plus runtime MDU guard','voice wire budget protections missing')
check('voice requires active Link' in lxmf,'voice remains active-Link only','voice unexpectedly uses non-Link transport')
check('RNS::Resource' not in lxmf and 'Resource(' not in voice,'unproven Resource path not introduced','Resource path introduced into M9-build3')
check('saveLxmfAudio' in storeh and 'loadLxmfAudio' in storeh,'audio sidecar storage API declared','audio sidecar API missing')
check('PersistedLxmfAudioHeaderV1' in store and 'payloadCrc32' in store and 'headerCrc32' in store,'CRC-protected audio sidecar format','audio sidecar integrity protection missing')
check('kLxmfAudioDir = "/lxmf/metadata/audio"' in store,'audio metadata isolated from immutable journal','audio sidecar directory missing')
check('pruneAudioSidecarsThrough' in store,'audio sidecars follow retention policy','audio sidecar retention missing')
check('skipMsgPackValue' in lxmf and 'unpacker.unpackMapSize()' in lxmf,'unknown LXMF fields can be skipped','LXMF field parser is not coexistence-safe')
check('unpacker.isFloat()' not in lxmf and 'unpacker.isFloat32()' in lxmf and 'unpacker.isFloat64()' in lxmf,
      'pinned MsgPack 0.4.2 width-specific float predicates used',
      'unsupported generic MsgPack isFloat() API used')

# M7 exchange tools retained
check('void WatchUI::showIdentity()' in ui,'M7 identity screen retained','identity screen missing')
check('void WatchUI::showQr(' in ui,'M7 QR screen retained','QR screen missing')
check('lv_qrcode_create(screenRoot_)' in ui,'modern LVGL QR create API used','QR create API missing/legacy')
check('lv_qrcode_set_size(qr, 148)' in ui,'QR size configured','QR size missing')
check('lv_qrcode_update(qr, qrPayload_' in ui,'QR payload update wired','QR payload update missing')
check('ADDRESS QR' in ui and 'IDENTITY QR' in ui,'public address + identity QR actions retained','QR actions missing')
check('contactShareEvent' in ui and 'CONTACT LXMF' in ui and 'fullHash(self->selectedPeer_' in ui,'contact address QR retained','contact QR missing')
check('void fullHash(const uint8_t *hash, char out[33])' in ui and ui.find('void fullHash(') < ui.find('void WatchUI::contactShareEvent'), 'contact-share fullHash helper declared before use', 'contact-share fullHash helper missing or out of scope')
check('32 hex characters' in ui and 'parseHashText' in ui,'manual 32-hex contact import retained','manual contact import missing')
private_terms=['PRIVATE KEY QR','IDENTITY KEY QR','privateKeyQr','identity.key QR']
check(not any(t in ui for t in private_terms),'private identity key never exposed by QR','private-key QR/export UI present')

# M8 user settings sidecar
check((ROOT/'src/config/UserConfig.h').exists(),'UserConfig exists','UserConfig missing')
check('AnnouncePolicy::Manual' in user and 'AnnouncePolicy::Startup' in user and 'AnnouncePolicy::SixHours' in user,'announce policy enum complete','announce policy enum incomplete')
check('AnnouncePolicy announcePolicy{AnnouncePolicy::Manual}' in user,'manual announce default','announce default is not manual')
check('notificationsEnabled{true}' in user and 'hapticsEnabled{true}' in user and 'peerDiscoveryVisible{true}' in user,'safe user defaults','user defaults changed')
check('kUserProfilePath = "/app/user.cfg"' in store,'user sidecar path isolated','user sidecar path missing')
check('struct PersistedUserProfileV1' in store and 'uint32_t crc32;' in struct_block(store,'PersistedUserProfileV1'),'CRC-protected user profile struct','user profile CRC missing')
check('offsetof(PersistedUserProfileV1, crc32)' in store,'user profile CRC validated','user profile CRC validation missing')
check('/app/user.tmp' in store and 'FFat.rename(tmp, kUserProfilePath)' in store,'atomic user profile temp write','atomic user settings write missing')
check('loadUserProfile' in storeh and 'saveUserProfile' in storeh,'user profile storage API declared','user profile storage API missing')
check('storage.loadUserProfile(userProfile_)' in ui,'user profile loaded on UI startup','user profile not loaded')
check('hardware_->setBrightness(userProfile_.brightness)' in ui,'persistent brightness applied','saved brightness not applied')

# Settings IA
for label in ['USER','MESSAGING','WATCH','RETICULUM','RADIO','INTERFACES','IDENTITY','RESTART']:
    check(f'"{label}"' in ui,f'settings section {label}',f'settings section missing: {label}')
check('EDIT NAME' in ui,'local label editor present','display-name editor missing')
check('CANNED 1:' in ui and 'CANNED 2:' in ui and 'editCanned1Event' in ui and 'editCanned2Event' in ui,'user-editable canned reply settings present','canned reply editors missing')
check('NOTIFY: %s' in ui,'notification toggle present','notification toggle missing')
check('HAPTICS %s' in ui,'haptics toggle present','haptics toggle missing')
check('STANDBY' in ui,'standby settings retained','standby settings missing')
check('kMaxRetainedMessages = 256' in storeh,'safe fixed retention remains 256 messages','retention policy changed/missing')
check('Touch no longer wakes a sleeping screen' in ui,'sleeping touch wake removed in UI contract','touch wake policy missing')
check('LIFT TO WAKE %s' in ui and 'liftToWakeToggleEvent' in ui,'M12 lift-to-wake setting wired','lift-to-wake UI/settings missing')
check('liftToWake{false}' in txt('src/config/PowerConfig.h'),'lift-to-wake defaults OFF','lift-to-wake default not OFF')
check('RW4,%lu,%u' in store and 'fields >= 2 ? (lift != 0) : false' in store,'RW4 power profile backward-compatible lift persistence','power profile lift persistence/migration missing')
check('consumeCrownPress()' in hw and 'XPOWERS_PWR_BTN_CLICK_INT' in hw and 'isPekeyShortPressIrq()' in hw and 'attachInterrupt(PMU_INT, onPmuIrq, FALLING)' in hw,'AXP2101 edge-latched crown short-click path implemented','crown PMIC edge path missing')
check('wake source=touch' not in txt('src/power/PowerManager.cpp'),'no sleeping tap-to-wake runtime path','tap-to-wake still present')
check('wake source=crown' in txt('src/power/PowerManager.cpp') and 'sleep source=crown' in txt('src/power/PowerManager.cpp'),'crown toggles display both directions','crown display toggle incomplete')
check('digitalRead(PMU_INT) == LOW' not in hw,'crown status is not gated on PMU pin still LOW','edge-unsafe PMU pin gating returned')
check('[m12:crown]' in hw and 'status-poll' in hw,'crown raw diagnostics present','crown diagnostics missing')
check('attachInterrupt(SENSOR_INT, onSensorIrq, RISING)' in hw and 'gSensorIrqPending' in hw,'BMA423 GPIO14 edge-latched interrupt path implemented','BMA423 sensor IRQ latch missing')
check('SensorBMA423::FEATURE_TILT | SensorBMA423::FEATURE_WAKEUP' in hw and 'enableTiltIRQ()' in hw and 'enableWakeupIRQ()' in hw,'BMA423 TILT/WAKEUP features re-armed for standby','BMA423 wake feature re-arm missing')
check('readIrqStatus()' in hw and 'SensorBMA423::INT_TILT' in hw and 'SensorBMA423::INT_WAKEUP' in hw,'BMA423 feature interrupt status decoded','BMA423 feature status decode missing')
check('armLiftWake(profile_.liftToWake)' in txt('src/power/PowerManager.cpp') and 'consumeLiftWake()' in txt('src/power/PowerManager.cpp'),'PowerManager consumes hardware lift wake events','hardware lift wake not wired to PowerManager')
check('poll_motion=no' in txt('src/power/PowerManager.cpp') and 'lastLiftPollMs_' not in txt('src/power/PowerManager.cpp') and 'getAccelerometer(' not in txt('src/power/PowerManager.cpp'),'lift wake no longer depends on application acceleration polling','polling-based lift detector still present')
check('mode=CONNECTED-IDLE' in hw and 'standby mode=CONNECTED-IDLE cpu=running' in txt('src/power/PowerManager.cpp'),'screen-off power state is explicit and observable','connected-idle power-state diagnostics missing')
check('standby heartbeat cpu=running' in hw and 'instance.sensor.getAccelerometer(x, y, z)' in hw,'standby motion telemetry is diagnostic-only and observable','standby diagnostic motion telemetry missing')
check('ensureAccelerometerLive()' in hw and 'instance.sensor.enableAccelerometer()' in hw,'M12-build4 explicitly enables BMA423 acquisition','BMA423 acquisition enable/liveness gate missing')
check('SensorBMA423::ODR_100HZ' in hw and 'SensorBMA423::PERF_CONTINUOUS_MODE' in hw,'BMA423 feature-safe acquisition profile explicit','BMA423 acquisition profile missing')
check('activity > 8' in hw and 'goodSamples' in hw and 'live ? "YES" : "NO"' in hw,'accelerometer liveness requires non-zero gravity vector','all-zero accelerometer vector could be accepted')
check(hw.find('ensureAccelerometerLive()') < hw.find('SensorBMA423::FEATURE_TILT | SensorBMA423::FEATURE_WAKEUP', hw.find('bool WatchHardware::armLiftWake')), 'sensor liveness gate precedes feature arming','feature IRQs can arm before acquisition liveness')
check('accelerometer acquisition is not live; lift wake will NOT arm' in hw,'failed acquisition is explicit and fail-closed','dead accelerometer can silently arm lift wake')
check('currentBrightness_' in hwh and 'instance.setBrightness(currentBrightness_)' in hw,'user brightness retained across display wake','display wake resets brightness')

# M10 stabilization / recovery
check((ROOT/'src/system/HealthMonitor.cpp').exists() and (ROOT/'src/system/HealthMonitor.h').exists(),'M10 HealthMonitor module exists','HealthMonitor module missing')
check('kLoopStallTimeoutMs = 30000UL' in healthh and 'kStartupGraceMs = 30000UL' in healthh,'30s supervisor timeout + startup grace','supervisor timing changed/missing')
check('kSupervisorStackBytes = 6144UL' in healthh,'bounded supervisor stack','supervisor stack bound missing')
check('xTaskCreatePinnedToCore' in health and 'supervisorLoop' in health,'independent supervisor task','supervisor worker missing')
check('gRecoveryMagic = kRecoveryMagic' in health and 'esp_restart()' in health,'stale-loop recovery is explicit restart','supervisor restart path missing')
check('FFat.' not in health and '#include <FFat' not in health,'supervisor/health layer does not directly mutate FFat','HealthMonitor directly accesses FFat')
check('healthMonitor.begin(storageService)' in main and 'healthMonitor.heartbeat();' in main and 'healthMonitor.loop();' in main,'M10 health monitor wired into startup/loop','HealthMonitor wiring incomplete')
check(main.find('watchUI.begin') < main.find('healthMonitor.begin'),'supervisor starts after UI construction','supervisor starts before UI')
check('DEGRADED supervisor only' in main,'supervisor failure does not gate communicator','supervisor failure policy missing')
check('FFat.begin(false, kMountPath, 10, kPartitionLabel)' in store,'persistent FFat mount is non-destructive','FFat auto-format remains enabled')
check('FFat.begin(true' not in store,'no automatic FFat format path','automatic FFat format path present')
check('kCriticalWriteReserveBytes = 16ULL * 1024ULL' in storeh,'16 KiB emergency storage reserve','storage reserve missing/changed')
check('kStoragePressureThresholdBytes = 256ULL * 1024ULL' in storeh,'256 KiB storage pressure threshold','storage pressure threshold missing/changed')
check('ensureWriteHeadroom(sizeof(PersistedLxmfRecordV3) + 2048U, "LXMF journal")' in store,'journal write reserve enforced','journal write headroom guard missing')
check('ensureWriteHeadroom(sizeof(PersistedLxmfAudioHeaderV1) + length + 1024U, "LXMF audio sidecar")' in store,'audio-sidecar write reserve enforced','audio write headroom guard missing')
check('existing data preserved' in store and 'automatic formatting is disabled' in store,'storage failure diagnostics are preservation-first','storage preservation diagnostics missing')
check('LOW STORAGE' in ui and 'underPressure()' in ui,'low-storage UI indication','low-storage UI indicator missing')
check('heap_min=' in health and 'psram_free=' in health and 'loop_gap_max=' in health and 'loop_stack_hwm=' in health,'periodic runtime health telemetry','runtime health diagnostics incomplete')
check('kEmergencyRamMessages = 16' in storeh and 'kEmergencyRamAudio = 8' in storeh,'bounded emergency RAM journal/audio capacities','RAM failover bounds missing/changed')
check('return saveLxmfMessageToRam(record);' in store and 'RAM JOURNAL' in store,'journal write failure degrades to RAM instead of disabling LXMF','RAM journal fallback missing')
check('loadRamLxmfMessage(sequence, record)' in store and 'findRamLxmfMessage(sequence)' in store,'RAM messages remain readable/mutable by UI and delivery state','RAM journal read/update path missing')
check('RAM AUDIO' in store and 'ramAudio_' in storeh,'voice sidecars have bounded RAM fallback','RAM audio fallback missing')
check('ramPeers_' in storeh and 'cachePeer(peer);' in store and 'announce cached' in store,'announce discovery has RAM-first peer cache','RAM peer cache missing')
check('screen_ == Screen::Peers' in ui and 'showPeers();' in ui and 'displayedDiscoveredCount_' in ui,'open Peers screen refreshes on announces','live Peers refresh missing')
check('State::Alerting' in audio and 'playAlertTone' in audioh and 'alertStep' in audio,'message alert audio runs on audio worker','alert-tone worker path missing')
check('messageHaptic(bool incoming)' in hwh and 'messageHapticPulsesRemaining_' in hwh and 'Strong Click' in hw,'strong asynchronous RX/TX haptic scheduler','message haptic scheduler missing')
check('setAudioFeedback' in lxmfh and 'signalIncomingFeedback' in lxmf and 'signalOutgoingFeedback' in lxmf,'LXMF RX/TX feedback wired','LXMF feedback integration missing')
check('disciplineTime(double unixEpoch' in hwh and '1800LL' in hw and 'instance.rtc.setDateTime' in hw,'bounded clock discipline writes PCF8563','RTC/time discipline missing')
check('mktime(&rtcLocal)' in hw and 'buildIsMateriallyNewer' in hw,'RTC local time converted correctly and stale RTC advanced by build epoch','RTC local/UTC correction missing')
check('RAM JOURNAL' in ui,'volatile storage mode visible in Settings','RAM-journal UI warning missing')

# M11 storage recovery / identity backup
check((ROOT/'src/recovery/RecoveryService.cpp').exists() and (ROOT/'src/recovery/RecoveryService.h').exists(),'M11 RecoveryService module exists','RecoveryService module missing')
check((ROOT/'tools/rw_recovery.py').exists(),'M11 host recovery tool exists','host recovery tool missing')
check('recoveryService.begin(storageService, healthMonitor)' in main and 'recoveryService.loop();' in main,'M11 recovery console wired into startup/loop','RecoveryService wiring incomplete')
check('M11 recovery=%s' in main and 'DEGRADED recovery console' in main,'M11 recovery readiness is explicit but non-gating','M11 recovery startup diagnostics missing')
check('M11 BACKUP' in recovery and 'RWB1 BEGIN' in recovery and 'RWB1 FILE' in recovery and 'RWB1 DATA' in recovery and 'RWB1 END' in recovery,'RWB1 off-device backup protocol implemented','backup stream protocol incomplete')
check('mbedtls_base64_encode' in recovery and 'mbedtls_base64_decode' in recovery,'backup/restore base64 transport uses bounded library codec','base64 backup/restore transport missing')
check('crc32File' in recovery and 'restoreExpectedCrc_' in recovery and 'FILE OK' in recovery,'per-file CRC32 backup/restore verification','recovery CRC verification missing')
check('hasSuffix(path, ".idx")' in recovery and 'hasSuffix(path, ".tmp")' in recovery,'rebuildable index/temp files excluded from recovery bundle','backup includes rebuildable/temp artifacts')
check('backupCompleted_' in recovery and 'token != backupToken_' in recovery and 'FORMAT REFUSED' in recovery,'format requires completed same-session backup token','format token gate missing')
check('FFat.end();' in recovery and 'FFat.format(false, label)' in recovery and 'FFat.begin(false, kMountPath, 10, kPartitionLabel)' in recovery,'explicit format/remount targets FFat only','explicit FFat rebuild path missing')
check('enterRecoveryMaintenance' in storeh and 'status_ = "recovery maintenance"' in store,'StorageService maintenance gate present','StorageService recovery maintenance missing')
check('setMaintenanceMode(bool enabled)' in healthh and 'if (maintenanceMode_) continue;' in health,'loop supervisor can be safely suspended for recovery maintenance','health maintenance gate missing')
check('/rns/identity.key' in recovery and 'identity.size() == 64' in recovery,'restore completion requires persistent 64-byte identity','identity restore gate missing')
check('RESTORE BEGIN FORCE' in recovery and 'explicit FORCE' in recovery,'interrupted-recovery FORCE path is explicit','FORCE restore recovery path missing')
check('partition_offset=' in recovery and 'logical_bytes=' in recovery and 'unaccounted=' in recovery,'M11 FAT-vs-logical allocation diagnostics','M11 storage accounting diagnostics missing')
check('capture_backup' in recoverytool and 'repair' in recoverytool and 'restore_backup' in recoverytool,'host tool supports capture/repair/restore','host recovery workflow incomplete')
check('uv run --with pyserial' in recoverytool,'host tool documents isolated pyserial invocation','host tool dependency invocation missing')
check('M11 FORMAT' in recoverytool and 'capture_backup(ser, out)' in recoverytool and 'restore_backup(ser, out, manifest' in recoverytool,'host repair orders verified backup before format and restore','host repair ordering unsafe/incomplete')
check('RWR1 DATA OK' in recovery and 'wait_prefix(ser, "RWR1 DATA OK "' in recoverytool,'M11-build2 restore is chunk-ACK paced','restore chunk pacing/ACK missing')
check('retries: int = 3' in recoverytool and 'retrying safely with FORCE' in recoverytool,'M11-build2 restore retries interrupted sessions idempotently','restore retry/resume guard missing')
check('WIPE CHAT HISTORY' in ui and 'showHistoryWipeConfirm' in ui and 'wipeChatHistory' in storeh,'explicit confirmed chat-history wipe UI/storage path present','chat-history wipe missing')
wipe_block = store[store.find('bool StorageService::wipeChatHistory()'):store.find('bool StorageService::loadIdentity')]
check('kIdentityPath' not in wipe_block and 'kUserProfilePath' not in wipe_block and 'kLxmfPeersDir' not in wipe_block,'chat-history wipe cannot delete identity/settings/peers','chat-history wipe touches preserved state')
check('migrated legacy default canned replies to unset slots' in store and 'char quickReply1[25]{""}' in user and 'char quickReply2[25]{""}' in user,'legacy fixed replies migrate to user-owned unset canned slots','legacy fixed canned reply migration missing')
check('Blank = disabled' in ui and 'if (userProfile_.quickReply1[0])' in ui and 'if (userProfile_.quickReply2[0])' in ui,'unset canned replies are supported and hidden','unset canned reply behavior missing')
check((ROOT/'docs/M11-NOTES.md').exists(),'M11 notes present','M11 notes missing')
check((ROOT/'docs/M11-SOURCE-AUDIT.md').exists(),'M11 source audit present','M11 source audit missing')
check((ROOT/'docs/M11-SOURCE-MANIFEST.sha256').exists(),'M11 source manifest present','M11 source manifest missing')
check((ROOT/'docs/M11-BUILD2-NOTES.md').exists(),'M11-build2 notes present','M11-build2 notes missing')
check((ROOT/'docs/M11-BUILD2-SOURCE-AUDIT.md').exists(),'M11-build2 source audit present','M11-build2 source audit missing')
check((ROOT/'docs/M11-build2-SOURCE-MANIFEST.sha256').exists(),'M11-build2 source manifest present','M11-build2 source manifest missing')

# Runtime wiring
check('userProfile_.quickReply1' in ui and 'userProfile_.quickReply2' in ui,'custom quick replies wired','custom quick replies not wired')
check('queueTextSend(self->selectedPeer_,self->userProfile_.quickReply1,Screen::Thread)' in ui and 'queueTextSend(self->selectedPeer_,self->userProfile_.quickReply2,Screen::Thread)' in ui,'thread quick replies use deferred send UX','thread quick replies not routed through send UX')
check('queueTextSend(self->notificationPeer_,self->userProfile_.quickReply1,Screen::WatchFace)' in ui,'notification quick reply uses deferred send UX','notification reply not routed through send UX')
check('if (!userProfile_.notificationsEnabled)' in ui,'notification suppression gate present','notification toggle not enforced')
check('userProfile_.hapticsEnabled' in ui and 'tapHaptic()' in ui,'haptics preference enforced','haptics toggle not enforced')
check('peerDiscoveryVisible' in ui,'peer-list visibility preference wired','peer visibility preference missing')
check('register_announce_handler' in txt('src/lxmf/LxmfService.cpp'),'actual announce handler remains registered','announce handler removed')

# Conservative announce scheduling, through accepted LxmfService request path only
check('21600000UL' in ui,'six-hour scheduler constant','six-hour scheduler missing')
check('requestAnnounce()' in ui,'auto scheduler uses accepted announce request path','announce request path missing')
check('six-hour automatic announce queued' in ui,'six-hour scheduler diagnostic','announce scheduler diagnostic missing')
check('EVERY 6H' in user,'six-hour policy label','six-hour policy label missing')
check('3600000UL' not in ui and 'EVERY 1H' not in ui,'no aggressive hourly auto-announce option','aggressive hourly announce option present')
check('Manual is the conservative default' in ui,'conservative announce guidance in UI','announce guidance missing')
check('kManualAnnounceCooldownMs = 60000UL' in uih,'60-second manual announce cooldown','manual announce cooldown missing')
check('manual announce suppressed; cooldown=' in ui,'manual announce cooldown diagnostic','manual announce cooldown diagnostic missing')
check('self->startupAnnounceDone_ = true;' in ui and 'self->lastAutoAnnounceMs_ = now;' in ui,'manual announce resets scheduler','manual announce does not reset scheduler')
check('ANN requests %lu' in ui and 'announceRequests()' in ui,'announce request diagnostics visible','announce diagnostics missing')
check('Rapid re-announces are deprioritised by Reticulum' in ui,'Reticulum anti-spam behavior documented in runtime diagnostic','announce anti-spam diagnostic missing')

# Name is deliberately local only: frozen LxmfService means announce app-data is untouched.
check('displayName' not in txt('src/lxmf/LxmfService.cpp'),'local display name does not mutate frozen announce payload','display name leaked into frozen LXMF core')

# Clock/date accepted M6 presentation retained.
watch=ui[ui.find('void WatchUI::showWatchFace()'):ui.find('void WatchUI::showMenu()')]
check('&lv_font_montserrat_48' in watch,'48px clock base retained','clock base font changed')
check('lv_obj_set_style_transform_scale_x(timeLabel, 304, 0)' in watch and 'lv_obj_set_style_transform_scale_y(timeLabel, 304, 0)' in watch and 'lv_obj_align(timeLabel, LV_ALIGN_CENTER, -4, -43)' in watch,'v1.1 enlarged + optically centered clock retained in v1.2','v1.1 clock scale/alignment missing')
check('Sending message...' in ui and 'Submitting to Reticulum' in ui and 'LV_OBJ_FLAG_CLICKABLE' in ui,'v1.2 modal sending-message overlay present','sending modal missing')
check('queueTextSend(' in ui and 'processPendingTextSend()' in ui and 'pendingTextSendAfterMs_' in ui,'v1.2 deferred text-send UX path present','deferred send UX path missing')
check('[v1.2:ux] sending-message submission accepted' in ui and '[v1.2:ux] sending-message submission failed' in ui,'v1.2 send UX diagnostics present','send UX diagnostics missing')
check('Serial.printf("[m4:compose] send requested chars=%u\\n", static_cast<unsigned>(strlen(text)));' in ui,'composer diagnostic string literal is escaped on one source line','composer diagnostic string literal malformed/split across lines')
check('&lv_font_montserrat_18' in watch,'date remains 18px','date size changed')
check('lv_font_montserrat_56' not in ui,'unsupported 56px font absent','unsupported LVGL font referenced')

# Fixed-arity LVGL calls, including QR API.
def arg_counts(src,func):
    out=[]; token=func+'('; pos=0
    while True:
        i=src.find(token,pos)
        if i<0: break
        if i and (src[i-1].isalnum() or src[i-1]=='_'):
            pos=i+len(token); continue
        j=i+len(token); depth=1; commas=0; non=False; quote=None; esc=False
        while j<len(src) and depth:
            c=src[j]
            if quote:
                if esc: esc=False
                elif c=='\\': esc=True
                elif c==quote: quote=None
            else:
                if c in "\"'": quote=c
                elif c=='(': depth+=1; non=True
                elif c==')':
                    depth-=1
                    if depth==0: break
                elif c==',' and depth==1: commas+=1
                elif not c.isspace(): non=True
            j+=1
        out.append(-1 if depth else (0 if not non else commas+1)); pos=j+1
    return out
fixed={
 'lv_obj_set_width':2,'lv_obj_set_size':3,'lv_obj_set_pos':3,'lv_obj_align':4,
 'lv_obj_set_style_text_font':3,'lv_obj_set_style_text_align':3,'lv_label_set_text':2,
 'lv_textarea_set_text':2,'lv_qrcode_create':1,'lv_qrcode_set_size':2,
 'lv_qrcode_set_dark_color':2,'lv_qrcode_set_light_color':2,'lv_qrcode_update':3,
}
bad=[]
for func,arity in fixed.items():
    for got in arg_counts(ui,func):
        if got != arity: bad.append(f'{func}:{got}!={arity}')
check(not bad,'LVGL fixed-arity calls validated','bad LVGL calls: '+','.join(bad))

# Header / implementation method consistency (rough but useful).
def declared_methods(h):
    return set(re.findall(r'\b(?:void|bool|size_t|lv_obj_t\s*\*)\s*([A-Za-z_]\w*)\s*\(',h))
def defined_methods(c):
    return re.findall(r'WatchUI::([A-Za-z_]\w*)\s*\(',c)
defs=defined_methods(ui); decls=declared_methods(uih)
missing=sorted({x for x in defs if x not in decls and x not in {'WatchUI'}})
check(not missing,'WatchUI definitions declared','WatchUI definitions missing declarations: '+','.join(missing))
check(len(defs)==len(set(defs)),'no duplicate WatchUI definitions','duplicate WatchUI definitions found')

# NUL/delimiter/source sanity
for rel in ['src/main.cpp','src/ui/WatchUI.cpp','src/ui/WatchUI.h','src/storage/StorageService.cpp','src/storage/StorageService.h','src/config/UserConfig.h','src/audio/AudioService.cpp','src/audio/AudioService.h','src/audio/VoiceService.cpp','src/audio/VoiceService.h','src/lxmf/LxmfService.cpp','src/lxmf/LxmfService.h','src/system/HealthMonitor.cpp','src/system/HealthMonitor.h','src/hal/WatchHardware.cpp','src/hal/WatchHardware.h','src/recovery/RecoveryService.cpp','src/recovery/RecoveryService.h']:
    b=(ROOT/rel).read_bytes(); s=b.decode(errors='ignore')
    check(b'\x00' not in b,f'no embedded NUL: {rel}',f'embedded NUL in {rel}')
    check(s.count('{')==s.count('}'),f'brace sanity: {rel}',f'unbalanced braces: {rel}')

# Documentation / process rule
check((ROOT/'docs/M7-NOTES.md').exists(),'M7 acceptance notes present','M7 notes missing')
check((ROOT/'docs/M8-NOTES.md').exists(),'M8 notes present','M8 notes missing')
check((ROOT/'docs/M9-NOTES.md').exists(),'M9 notes present','M9 notes missing')
check((ROOT/'docs/M9-SOURCE-AUDIT.md').exists(),'M9 source audit present','M9 source audit missing')
check((ROOT/'docs/M9-BUILD2-NOTES.md').exists(),'M9-build2 notes present','M9-build2 notes missing')
check((ROOT/'docs/M9-BUILD2-SOURCE-AUDIT.md').exists(),'M9-build2 source audit present','M9-build2 source audit missing')
check((ROOT/'docs/M9-BUILD3-NOTES.md').exists(),'M9-build3 notes present','M9-build3 notes missing')
check((ROOT/'docs/M9-BUILD3-SOURCE-AUDIT.md').exists(),'M9-build3 source audit present','M9-build3 source audit missing')
check((ROOT/'docs/M9-BUILD4-NOTES.md').exists(),'M9-build4 notes present','M9-build4 notes missing')
check((ROOT/'docs/M9-BUILD4-SOURCE-AUDIT.md').exists(),'M9-build4 source audit present','M9-build4 source audit missing')
check((ROOT/'docs/M9-BUILD5-NOTES.md').exists(),'M9-build5 notes present','M9-build5 notes missing')
check((ROOT/'docs/M9-BUILD5-SOURCE-AUDIT.md').exists(),'M9-build5 source audit present','M9-build5 source audit missing')
check((ROOT/'docs/M10-NOTES.md').exists(),'M10 notes present','M10 notes missing')
check((ROOT/'docs/M10-SOURCE-AUDIT.md').exists(),'M10 source audit present','M10 source audit missing')
check((ROOT/'docs/M10-BUILD2-NOTES.md').exists(),'M10-build2 notes present','M10-build2 notes missing')
check((ROOT/'docs/M10-BUILD2-SOURCE-AUDIT.md').exists(),'M10-build2 source audit present','M10-build2 source audit missing')
check((ROOT/'docs/M10-build2-SOURCE-MANIFEST.sha256').exists(),'M10-build2 source manifest present','M10-build2 source manifest missing')
notes=txt('docs/M8-NOTES.md').lower(); ledger=txt('docs/MISTAKE-LEDGER.md').lower()
check('official reticulum' in notes and 'microreticulum' in notes and 'rsdeck' in notes,'M8 reference checks documented','M8 reference checks incomplete')
check('configuration must not become a new protocol regression vector' in ledger,'settings regression rule in mistake ledger','settings regression rule missing')
check('reference rule' in ledger and 'official reticulum manual' in ledger,'reference implementation gate retained','reference gate missing')
check('local tx is not remote propagation' in ledger,'local-emission vs propagation rule recorded','announce propagation rule missing')
check('rapidly re-announcing destinations' in ledger,'announce anti-spam rule recorded','announce anti-spam ledger rule missing')
check('never construct Codec2 on Arduino loopTask'.lower() in ledger,'Codec2 loopTask stack rule recorded','Codec2 loopTask stack rule missing')
check('a standard codec mode is not automatically client-playable' in ledger,'Sideband codec interoperability mistake recorded','Sideband codec interoperability ledger rule missing')
check('persistent storage recovery must never auto-format' in ledger,'non-destructive storage recovery rule recorded','M10 storage recovery rule missing')
check('successful delivery is not proof of receiver audio playback' in ledger,'receiver playback evidence rule recorded','M10 audio evidence rule missing')
check('persistence failure must never become a communications outage' in ledger,'storage durability is decoupled from live communication','RAM journal degradation rule missing')
check('announce discovery must not depend on peer metadata writes' in ledger,'peer discovery RAM-first rule recorded','peer cache degradation rule missing')
check('distinguish local rtc wall time from unix utc' in ledger,'RTC local/UTC rule recorded','RTC discipline rule missing')
check('message feedback must be asynchronous and distinct' in ledger,'message feedback non-blocking rule recorded','message feedback rule missing')
check('never back up a failing filesystem onto itself' in ledger,'M11 off-device backup rule recorded','M11 backup rule missing')
check('formatting is never automatic' in ledger and 'same-session successfully completed backup' in ledger,'M11 destructive recovery gate recorded','M11 format gate rule missing')
check('restore chunks must be acknowledged' in ledger,'M11-build2 restore pacing lesson recorded','M11-build2 restore pacing rule missing')

check('totalSamples = tone == AlertTone::MessageReceived ? 9520U : 3200U' in audio,'M10-build3 six-beep RX cadence duration','RX cadence duration not M10-build3')
check('kCallBeepHz = 1400U' in audio and 'n < 5920U' in audio and 'n < 8640U' in audio,'M10-build3 3-pause-3 receive chime encoded','M10-build3 receive chime cadence missing')
check((ROOT/'docs/M10-BUILD3-NOTES.md').exists(),'M10-build3 notes present','M10-build3 notes missing')
check((ROOT/'docs/M10-BUILD3-SOURCE-AUDIT.md').exists(),'M10-build3 source audit present','M10-build3 source audit missing')
check((ROOT/'docs/M10-build3-SOURCE-MANIFEST.sha256').exists(),'M10-build3 source manifest present','M10-build3 source manifest missing')
check((ROOT/'docs/M12-NOTES.md').exists(),'M12 notes present','M12 notes missing')
check((ROOT/'docs/M12-SOURCE-AUDIT.md').exists(),'M12 source audit present','M12 source audit missing')
check((ROOT/'docs/M12-BUILD2-NOTES.md').exists(),'M12-build2 notes present','M12-build2 notes missing')
check((ROOT/'docs/M12-BUILD2-SOURCE-AUDIT.md').exists(),'M12-build2 source audit present','M12-build2 source audit missing')
check((ROOT/'docs/M12-build2-SOURCE-MANIFEST.sha256').exists(),'M12-build2 source manifest present','M12-build2 source manifest missing')
check((ROOT/'docs/M12-BUILD4-NOTES.md').exists(),'M12-build4 notes present','M12-build4 notes missing')
check((ROOT/'docs/M12-BUILD4-SOURCE-AUDIT.md').exists(),'M12-build4 source audit present','M12-build4 source audit missing')
check((ROOT/'docs/M12-build4-SOURCE-MANIFEST.sha256').exists(),'M12-build4 source manifest present','M12-build4 source manifest missing')
check((ROOT/'docs/M13-NOTES.md').exists(),'M13 notes present','M13 notes missing')
check((ROOT/'docs/M13-SOURCE-AUDIT.md').exists(),'M13 source audit present','M13 source audit missing')
check((ROOT/'docs/M13-SOURCE-MANIFEST.sha256').exists(),'M13 source manifest present','M13 source manifest missing')
check('## M13: release-candidate feature freeze' in txt('docs/MISTAKE-LEDGER.md'),'M13 feature-freeze rule recorded','M13 feature-freeze rule missing')
check('do not assume a watch crown is a gpio' in ledger,'M12 crown hardware rule recorded','M12 crown rule missing')
check('sleeping touch is no longer a wake source' in ledger,'M12 no-tap-wake rule recorded','M12 touch wake rule missing')
check('motion wake is opt-in until hardware tuned' in ledger,'M12 lift opt-in rule recorded','M12 lift rule missing')
check('display wake must preserve user brightness' in ledger,'M12 brightness wake rule recorded','M12 brightness rule missing')
check('feature/irq success is not sensor-liveness proof' in ledger,'M12-build4 sensor-liveness rule recorded','M12-build4 sensor-liveness ledger rule missing')
check('persistent all-zero acceleration' in ledger,'all-zero acceleration failure rule recorded','all-zero sensor health rule missing')


# v1.3 peer intelligence UI
check('PeerDetails' in uih and 'showPeerDetails()' in ui,'v1.3 Peer Details screen wired','Peer Details screen missing')
check('formatPeerAge' in ui and 'Last seen:' in ui,'peer age + timestamp visible','peer last-seen UI missing')
check('Active Link:' in ui and 'canReplyTo(selectedPeer_)' in ui,'active-Link visibility uses existing LXMF state','active-Link peer diagnostic missing')
check('h=%u seen %s' in ui,'peer list shows hops + seen age','peer-list Reticulum metadata missing')
check('peerChatEvent' in ui and 'peerFavoriteEvent' in ui and 'peerNameEvent' in ui and 'peerQrEvent' in ui,'peer detail actions wired','peer detail actions incomplete')
check('[v1.3:peer] details' in ui,'v1.3 peer diagnostics present','v1.3 peer diagnostics missing')
check('lv_event_get_current_target_obj(event)' in ui,'v1.3-build2 list callbacks use current LVGL target','v1.3-build2 current-target callback fix missing')
check('peerDetailsReturnTarget_' in uih and 'threadReturnTarget_' in uih and 'namePeerReturnTarget_' in uih,'v1.3-build2 origin-aware navigation state present','v1.3-build2 navigation state missing')
check('peerDetailsReturnTarget_ = Screen::Contacts' in ui and 'peerDetailsReturnTarget_ = Screen::Peers' in ui,'Peers/Contacts preserve Peer Details return origin','Peer Details origin routing missing')
check('threadReturnTarget_ = Screen::PeerDetails' in ui and 'backTarget_ = threadReturnTarget_' in ui,'Peer Details chat returns through origin-aware thread back target','thread origin-aware back routing missing')
check('PIN*' in ui and 'local only' in ui,'favorite UI clarified as local pin','favorite/pin semantics unclear')
# v1.3.0-build3: contrast is applied on every screen rebuild, never in loop.
check('void applyConversationTextContrast(lv_obj_t *root)' in ui and
      'applyConversationTextContrast(screenRoot_);' in ui,
      'recursive white-text style wired into conversation screens',
      'high contrast conversation helper missing')
thread=ui[ui.find('void WatchUI::showThread() {'):ui.find('void WatchUI::configureKeyboard(')]
peer_detail=ui[ui.find('void WatchUI::showPeerDetails() {'):ui.find('void WatchUI::showNamePeer()')]
compose=ui[ui.find('void WatchUI::showCompose('):ui.find('void WatchUI::showContacts()')]
check('lv_obj_set_style_bg_color(history, lv_color_hex(0x000000), LV_PART_MAIN);' in thread and
      'applyConversationTextContrast(screenRoot_);' in thread,
      'chat history black background with explicit white text',
      'chat history high-contrast styles missing')
check('lv_obj_set_style_bg_color(card, lv_color_hex(0x000000), 0);' in peer_detail and
      'lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);' in peer_detail and
      'applyConversationTextContrast(screenRoot_);' in peer_detail,
      'Peer Details black/white contrast applied including action labels',
      'Peer Details black/white contrast missing')
check('lv_obj_set_style_bg_color(composeTextArea_, lv_color_hex(0x000000), LV_PART_MAIN);' in compose and
      'applyConversationTextContrast(screenRoot_);' in compose and
      compose.find('applyConversationTextContrast(screenRoot_);') < compose.find('lv_keyboard_create(screenRoot_)'),
      'composer black/white input and keyboard theme left alone',
      'composer high contrast styles missing')
check((ROOT/'docs/V1.3.0-BUILD3-NOTES.md').exists(),'v1.3 build3 notes present','v1.3 build3 notes missing')
check((ROOT/'docs/V1.3.0-BUILD2-NOTES.md').exists(),'v1.3 build2 notes present','v1.3 build2 notes missing')
check((ROOT/'docs/V1.3.0-RELEASE-NOTES.md').exists(),'v1.3 release notes present','v1.3 release notes missing')
check((ROOT/'docs/V1.3.0-HARDWARE-TEST.md').exists(),'v1.3 hardware test guide present','v1.3 hardware test guide missing')

# v1.4 connected-standby efficiency and profiling
check('>= 15000U' in hw or 'now - lastSensorIrqLogMs_ >= 15000U' in hw,
      'non-actionable BMA423 IRQ diagnostics rate-limited',
      'motion IRQ diagnostics still churn too frequently')
check('now - lastSensorTelemetryMs_ >= 60000U' in hw and
      '[v1.4:power] standby heartbeat' in hw and
      'getAccelerometer(x, y, z)' not in hw[hw.find('// v1.4.0 connected-standby efficiency'):hw.find('// Non-blocking strong notification pattern')],
      'standby heartbeat reduced to sparse no-I2C telemetry',
      'standby telemetry still performs frequent accelerometer sampling')
check('uiPollIntervalMs = (power_ && power_->displaySleeping()) ? 1200U : 350U' in ui,
      'sleeping UI notification poll relaxed without sleeping Reticulum',
      'connected-standby UI poll cadence not reduced')
check('uint32_t PowerManager::activeMs() const' in power and
      'uint32_t PowerManager::standbyMs() const' in power and
      'crownWakeCount_' in powerh and 'liftWakeCount_' in powerh and 'standbyEntryCount_' in powerh,
      'session power counters implemented',
      'power profiling counters missing')
check('Session active %lum / standby %lum' in ui and 'Wake crown %lu / lift %lu' in ui,
      'Power screen exposes session duty/wake counters',
      'Power screen runtime profiling UI missing')
check('Reticulum RX remains active' in power and 'LoRa + Reticulum stay online.' in ui,
      'v1.4 explicitly preserves always-connected Reticulum standby',
      'v1.4 connectivity-preservation contract missing')
check((ROOT/'docs/V1.4.0-RELEASE-NOTES.md').exists(),'v1.4 release notes present','v1.4 release notes missing')
check((ROOT/'docs/V1.4.0-HARDWARE-TEST.md').exists(),'v1.4 hardware test guide present','v1.4 hardware test guide missing')
check((ROOT/'docs/V1.4.0-SOURCE-AUDIT.md').exists(),'v1.4 source audit present','v1.4 source audit missing')


# v1.5 audio / voice UX retained in v1.6.1
audio_h=(ROOT/'src/audio/AudioService.h').read_text()
audio_cpp=(ROOT/'src/audio/AudioService.cpp').read_text()
check('uint32_t playbackMs() const;' in audio_h and 'uint32_t AudioService::playbackMs() const' in audio_cpp,
     'read-only audio playback progress accessor present',
     'playback progress accessor missing')
check('void WatchUI::updateThreadVoiceStatus()' in ui and
     'VOICE needs active Link to send' in ui and
     'VOICE ready | record, preview, send' in ui,
     'conversation exposes truthful VOICE readiness semantics',
     'VOICE state guidance missing')
check('Encoding voice...' in ui and 'Sending voice...' in ui and
     'Playing %lu.%lus / %lu.%lus' in ui,
     'encode/send/play states visible in conversation',
     'voice operation progress states missing')
check('RECORD -> optional PLAY -> SEND' in ui,
     'VOICE composer explains record/preview/send workflow',
     'VOICE compose workflow guidance missing')
check((ROOT/'docs/V1.5.0-RELEASE-NOTES.md').exists(),'v1.5 release notes retained','v1.5 release notes missing')
check((ROOT/'docs/V1.5.0-HARDWARE-TEST.md').exists(),'v1.5 hardware test guide retained','v1.5 hardware test guide missing')
check((ROOT/'docs/V1.5.0-SOURCE-AUDIT.md').exists(),'v1.5 source audit retained','v1.5 source audit missing')


# v1.6.0 final UX / haptic stabilization
check('lv_obj_set_width(title, 164)' in ui and 'titleLen > 12 ? &lv_font_montserrat_10' in ui,
      'generic headers use wider adaptive title layout',
      'generic header title-fit guard missing')
check('lv_obj_set_width(title, 156)' in ui and 'makeButton(screenRoot_, "i", 36, 29, threadInfoEvent' in ui,
      'chat header prioritises wide peer title plus compact info action',
      'chat header still overcrowded')
check('makeButton(screenRoot_, "1", 34, 34, cannedReply1Event' in ui and
      'makeButton(screenRoot_, "2", 34, 34, cannedReply2Event' in ui,
      'thread canned replies use numeric compact buttons',
      'thread canned replies still use clipped text labels')
check('lv_obj_set_pos(voice, 73, 201)' in ui and
      'makeButton(screenRoot_, "VOICE", 68, 34, voiceComposeEvent' in ui and
      'lv_obj_set_pos(reply, 143, 201)' in ui and
      'makeButton(screenRoot_, "REPLY", 86, 34, replyEvent' in ui,
      'PTT-free chat action row fits 240px display bounds',
      'PTT-free chat action row layout guard missing')
check('makeButton(screenRoot_, "1", 74, 32, notificationCanned1Event' in ui and
      'makeButton(screenRoot_, "2", 74, 32, notificationCanned2Event' in ui,
      'notification canned replies use numeric labels',
      'notification canned replies still expose clipped reply text')
check('void keyHaptic();' in hwh and 'void WatchHardware::keyHaptic()' in hw and
      'instance.setHapticEffects(1)' in hw and 'self->hardware_->keyHaptic()' in ui,
      'strong dedicated keyboard haptic path present',
      'keyboard haptic strengthening missing')
check('void WatchUI::threadInfoEvent' in ui and 'peerDetailsReturnTarget_ = Screen::Thread' in ui,
      'chat info preserves return navigation',
      'chat info navigation return state missing')
check('UserEditField::CannedReply1 ? "REPLY 1" : "REPLY 2"' in ui and
      'UserEditField::DisplayName ? "NAME"' in ui,
      'photo-informed editor titles fit beside SAVE',
      'editor titles still too long for the physical display')
check('"Blank = disabled"' in ui and '"Saved immediately"' in ui,
      'editor helper copy shortened above keyboard',
      'editor helper text still risks keyboard overlap')
check((ROOT/'docs/V1.6.0-RC2-RELEASE-NOTES.md').exists(),'v1.6 RC2 release notes present','v1.6 RC2 release notes missing')
check((ROOT/'docs/V1.6.0-RC2-HARDWARE-TEST.md').exists(),'v1.6 RC2 hardware test guide present','v1.6 RC2 hardware test guide missing')
check((ROOT/'docs/V1.6.0-RC2-SOURCE-AUDIT.md').exists(),'v1.6 RC2 source audit present','v1.6 RC2 source audit missing')

try:
    r=subprocess.run([sys.executable,str(ROOT/'scripts/check_partitions.py')],cwd=ROOT,text=True,capture_output=True)
    check(r.returncode==0,'partition validator passed','partition validator failed: '+r.stdout+r.stderr)
except Exception as e:
    errors.append('partition validator exception '+repr(e))

check((ROOT/'docs/V1.6.0-RELEASE-NOTES.md').exists(),'v1.6.0 release notes present','v1.6.0 release notes missing')
check((ROOT/'docs/V1.6.0-SOURCE-AUDIT.md').exists(),'v1.6.0 source audit present','v1.6.0 source audit missing')
check((ROOT/'docs/V1.6.0-HARDWARE-ACCEPTANCE.md').exists(),'v1.6.0 hardware acceptance present','v1.6.0 hardware acceptance missing')
check((ROOT/'docs/V1.0.0-RELEASE-NOTES.md').exists(),'v1.0.0 release notes present','v1.0.0 release notes missing')
check((ROOT/'docs/V1.0.0-HARDWARE-TEST.md').exists(),'v1.0.0 hardware test guide present','v1.0.0 hardware test guide missing')
check((ROOT/'docs/V1.2.0-RELEASE-NOTES.md').exists(),'v1.2.0 release notes present','v1.2.0 release notes missing')
check((ROOT/'docs/V1.2.0-HARDWARE-TEST.md').exists(),'v1.2.0 hardware test guide present','v1.2.0 hardware test guide missing')
check((ROOT/'docs/POST-1.0-ROADMAP.md').exists(),'post-1.0 roadmap present','post-1.0 roadmap missing')
check('never confuse submission with delivery' in ledger,'post-1.0 send-state truthfulness rule recorded','send-state truthfulness rule missing')
# v1.6.1 final maintenance cleanup: remove non-LXST PTT experiment
check('pttPressedEvent' not in ui and 'pttReleasedEvent' not in ui and 'pttButton_' not in ui and 'pttRecording_' not in ui,
      'experimental custom PTT interaction removed',
      'legacy PTT UI/state remains')
check('makeButton(screenRoot_, "PTT"' not in ui and '[v1.5:ptt]' not in ui and '[m9:ptt]' not in ui,
      'no PTT control or runtime diagnostics remain',
      'PTT control/diagnostics still present')
check('makeButton(screenRoot_, "VOICE", 68, 34, voiceComposeEvent' in ui and 'requestSend' in voice,
      'LXMF VOICE compose/send feature retained',
      'VOICE feature missing after PTT cleanup')
check((ROOT/'docs/V1.6.1-RELEASE-NOTES.md').exists(),'v1.6.1 final maintenance release notes present','v1.6.1 release notes missing')
check((ROOT/'docs/V1.6.1-SOURCE-AUDIT.md').exists(),'v1.6.1 source audit present','v1.6.1 source audit missing')

print('ReticulumWatch v1.6.1 final maintenance release preflight')
for x in checks: print('  PASS',x)
if errors:
    for x in errors: print('  FAIL',x)
    print(f'RESULT: FAIL ({len(errors)} issue(s))')
    sys.exit(1)
print(f'RESULT: STATIC PASS ({len(checks)} checks); PlatformIO compile + hardware acceptance required')

