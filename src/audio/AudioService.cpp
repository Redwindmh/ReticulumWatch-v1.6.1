#include "audio/AudioService.h"

#include <ESP_I2S.h>
#include <LilyGoLib.h>
#include <algorithm>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_idf_version.h>

#include "hal/WatchHardware.h"

namespace rw::audio {
namespace {
constexpr int16_t clamp16(int32_t value) {
    return value > 32767 ? 32767 : (value < -32768 ? -32768 : static_cast<int16_t>(value));
}
}

bool AudioService::begin(rw::hal::WatchHardware &hardware) {
    hardware_ = &hardware;
    freePsramAtBegin_ = ESP.getFreePsram();

    // Keep the 4-second PCM buffer in PSRAM. Failure is audio-local: never
    // consume the core heap as a fallback and never gate Reticulum startup.
    buffer_ = static_cast<uint8_t *>(heap_caps_malloc(kBufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!buffer_) {
        setError("PSRAM audio buffer allocation failed");
        Serial.printf("[m9:audio] unavailable: %s requested=%u free_psram=%lu\n",
                      lastError_, static_cast<unsigned>(kBufferBytes),
                      static_cast<unsigned long>(ESP.getFreePsram()));
        return false;
    }
    memset(buffer_, 0, kBufferBytes);

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    microphoneReady_ = instance.initMicrophone();

    // LilyGoLib 0.2.0 currently initialises the MAX98357A path at 160000 Hz
    // stereo on IDF 5.x. ReticulumWatch records at 16 kHz mono, so explicitly
    // normalise TX to 16 kHz stereo and duplicate mono samples at playback.
    instance.powerControl(POWER_SPEAK, true);
    delay(5);
    speakerReady_ = instance.initAmplifier();
    if (speakerReady_) {
        speakerReady_ = instance.player.configureTX(
            kSampleRate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    }
    instance.powerControl(POWER_SPEAK, false);
    speakerPowered_ = false;
#else
    // The accepted project toolchain is ESP-IDF 5.x via pioarduino. Do not
    // guess at LilyGoLib's legacy PDM/Player API in this milestone.
    microphoneReady_ = false;
    speakerReady_ = false;
#endif

    if (!microphoneReady_ || !speakerReady_) {
        setError(!microphoneReady_ ? "microphone init failed" : "speaker init failed");
        Serial.printf("[m9:audio] unavailable: %s mic=%s speaker=%s\n",
                      lastError_, microphoneReady_ ? "ready" : "failed",
                      speakerReady_ ? "ready" : "failed");
        heap_caps_free(buffer_);
        buffer_ = nullptr;
        return false;
    }

    setState(State::Idle);
    strncpy(lastError_, "none", sizeof(lastError_) - 1);
    lastError_[sizeof(lastError_) - 1] = '\0';

    BaseType_t taskResult = xTaskCreatePinnedToCore(
        workerEntry, "rw-audio", kWorkerStackBytes, this, 1, &workerTask_, 0);
    if (taskResult != pdPASS || !workerTask_) {
        setError("audio worker task creation failed");
        heap_caps_free(buffer_);
        buffer_ = nullptr;
        return false;
    }

    Serial.printf("[m9:audio] ready mic=16kHz/16-bit/mono playback=16kHz/16-bit/stereo "
                  "buffer=%u bytes max=%lums psram_before=%lu psram_after=%lu worker_core=0\n",
                  static_cast<unsigned>(kBufferBytes), static_cast<unsigned long>(kMaxRecordMs),
                  static_cast<unsigned long>(freePsramAtBegin_),
                  static_cast<unsigned long>(ESP.getFreePsram()));
    return true;
}

void AudioService::loop() {
    // Intentionally non-blocking. All I2S reads/writes live on the dedicated
    // worker task so the Arduino loop remains available to radio/RNS/LXMF.
}

bool AudioService::startRecording(uint32_t maxDurationMs) {
    if (!ready() || busy() || !buffer_ || maxDurationMs == 0) return false;
    const uint32_t boundedMs = std::min<uint32_t>(maxDurationMs, kMaxRecordMs);
    size_t limitBytes = static_cast<size_t>(
        (static_cast<uint64_t>(kSampleRate) * kBytesPerSample * kCaptureChannels * boundedMs) / 1000ULL);
    limitBytes &= ~static_cast<size_t>(1); // 16-bit sample alignment
    if (limitBytes < sizeof(int16_t)) return false;
    limitBytes = std::min(limitBytes, kBufferBytes);
    portENTER_CRITICAL(&stateMux_);
    recordedBytes_ = 0;
    recordLimitBytes_ = limitBytes;
    playbackOffset_ = 0;
    peakPermille_ = 0;
    state_ = State::Recording;
    ++revision_;
    portEXIT_CRITICAL(&stateMux_);
    Serial.printf("[m9:audio] recording started max=%lums capacity=%u limit=%u\n",
                  static_cast<unsigned long>(boundedMs), static_cast<unsigned>(kBufferBytes),
                  static_cast<unsigned>(limitBytes));
    return true;
}

bool AudioService::stop() {
    const State current = state();
    if (current != State::Recording && current != State::Playing) return false;
    portENTER_CRITICAL(&stateMux_);
    state_ = recordedBytes_ >= sizeof(int16_t) ? State::Ready : State::Idle;
    ++revision_;
    portEXIT_CRITICAL(&stateMux_);
    Serial.printf("[m9:audio] stopped state=%s recorded=%u bytes (%lums)\n",
                  stateName(), static_cast<unsigned>(recordedBytes()),
                  static_cast<unsigned long>(recordedMs()));
    return true;
}

bool AudioService::playRecording() {
    if (!ready() || busy() || recordedBytes() < sizeof(int16_t)) return false;
    portENTER_CRITICAL(&stateMux_);
    playbackOffset_ = 0;
    state_ = State::Playing;
    ++revision_;
    portEXIT_CRITICAL(&stateMux_);
    Serial.printf("[m9:audio] playback started recorded=%u bytes (%lums)\n",
                  static_cast<unsigned>(recordedBytes()),
                  static_cast<unsigned long>(recordedMs()));
    return true;
}

bool AudioService::clearRecording() {
    if (!ready() || busy()) return false;
    portENTER_CRITICAL(&stateMux_);
    recordedBytes_ = 0;
    playbackOffset_ = 0;
    peakPermille_ = 0;
    state_ = State::Idle;
    ++revision_;
    portEXIT_CRITICAL(&stateMux_);
    if (buffer_) memset(buffer_, 0, kBufferBytes);
    Serial.println("[m9:audio] local recording cleared");
    return true;
}

bool AudioService::playAlertTone(AlertTone tone) {
    if (!ready()) return false;
    portENTER_CRITICAL(&stateMux_);
    if (state_ == State::Recording || state_ == State::Playing ||
        state_ == State::Processing || state_ == State::Alerting) {
        portEXIT_CRITICAL(&stateMux_);
        return false;
    }
    alertTone_ = tone;
    alertSampleIndex_ = 0;
    state_ = State::Alerting;
    ++revision_;
    portEXIT_CRITICAL(&stateMux_);
    Serial.printf("[m10:notify] alert tone queued type=%s\n",
                  tone == AlertTone::MessageReceived ? "RX" : "TX");
    return true;
}


bool AudioService::beginCodecRead(const int16_t *&samples, size_t &sampleCount) {
    samples = nullptr;
    sampleCount = 0;
    if (!buffer_) return false;
    portENTER_CRITICAL(&stateMux_);
    if (state_ != State::Ready || recordedBytes_ < sizeof(int16_t)) {
        portEXIT_CRITICAL(&stateMux_);
        return false;
    }
    state_ = State::Processing;
    ++revision_;
    samples = reinterpret_cast<const int16_t *>(buffer_);
    sampleCount = recordedBytes_ / sizeof(int16_t);
    portEXIT_CRITICAL(&stateMux_);
    return true;
}

void AudioService::endCodecRead() {
    portENTER_CRITICAL(&stateMux_);
    if (state_ == State::Processing) {
        state_ = recordedBytes_ >= sizeof(int16_t) ? State::Ready : State::Idle;
        ++revision_;
    }
    portEXIT_CRITICAL(&stateMux_);
}

bool AudioService::loadPcm16Mono(const int16_t *samples, size_t sampleCount) {
    if (!buffer_ || !samples || sampleCount == 0) return false;
    const size_t bytes = sampleCount * sizeof(int16_t);
    if (bytes > kBufferBytes || busy()) return false;
    memcpy(buffer_, samples, bytes);
    uint16_t localPeak = 0;
    for (size_t i = 0; i < sampleCount; ++i) {
        const int32_t value = samples[i];
        const uint32_t magnitude = static_cast<uint32_t>(value < 0 ? -value : value);
        const uint16_t permille = static_cast<uint16_t>(std::min<uint32_t>(1000, (magnitude * 1000U) / 32767U));
        if (permille > localPeak) localPeak = permille;
    }
    portENTER_CRITICAL(&stateMux_);
    recordedBytes_ = bytes;
    playbackOffset_ = 0;
    peakPermille_ = localPeak;
    state_ = State::Ready;
    ++revision_;
    portEXIT_CRITICAL(&stateMux_);
    return true;
}

bool AudioService::ready() const {
    const State current = state();
    return buffer_ && microphoneReady_ && speakerReady_ &&
           current != State::Unavailable && current != State::Error;
}

bool AudioService::busy() const {
    const State current = state();
    return current == State::Recording || current == State::Playing ||
           current == State::Processing || current == State::Alerting;
}

AudioService::State AudioService::state() const {
    portENTER_CRITICAL(&stateMux_);
    const State current = state_;
    portEXIT_CRITICAL(&stateMux_);
    return current;
}

const char *AudioService::stateName() const {
    switch (state()) {
        case State::Unavailable: return "UNAVAILABLE";
        case State::Idle: return "IDLE";
        case State::Recording: return "RECORDING";
        case State::Ready: return "CLIP READY";
        case State::Playing: return "PLAYING";
        case State::Processing: return "PROCESSING";
        case State::Alerting: return "ALERTING";
        case State::Error: return "ERROR";
    }
    return "UNKNOWN";
}

size_t AudioService::recordedBytes() const {
    portENTER_CRITICAL(&stateMux_);
    const size_t bytes = recordedBytes_;
    portEXIT_CRITICAL(&stateMux_);
    return bytes;
}

uint32_t AudioService::recordedMs() const {
    const size_t bytes = recordedBytes();
    const uint32_t bytesPerSecond = kSampleRate * kBytesPerSample * kCaptureChannels;
    return bytesPerSecond ? static_cast<uint32_t>((bytes * 1000ULL) / bytesPerSecond) : 0;
}

uint32_t AudioService::playbackMs() const {
    size_t offset = 0;
    portENTER_CRITICAL(&stateMux_);
    offset = playbackOffset_;
    portEXIT_CRITICAL(&stateMux_);
    const uint32_t bytesPerSecond = kSampleRate * kBytesPerSample * kCaptureChannels;
    return bytesPerSecond ? static_cast<uint32_t>((offset * 1000ULL) / bytesPerSecond) : 0;
}

uint16_t AudioService::peakPermille() const {
    portENTER_CRITICAL(&stateMux_);
    const uint16_t peak = peakPermille_;
    portEXIT_CRITICAL(&stateMux_);
    return peak;
}

uint32_t AudioService::revision() const {
    portENTER_CRITICAL(&stateMux_);
    const uint32_t value = revision_;
    portEXIT_CRITICAL(&stateMux_);
    return value;
}

void AudioService::workerEntry(void *context) {
    static_cast<AudioService *>(context)->workerLoop();
}

void AudioService::workerLoop() {
    for (;;) {
        const State current = state();
        if (current == State::Recording) {
            captureStep();
        } else if (current == State::Playing) {
            playbackStep();
        } else if (current == State::Alerting) {
            alertStep();
        } else {
            if (speakerPowered_) setSpeakerPower(false);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

void AudioService::captureStep() {
    if (!buffer_ || state() != State::Recording) return;

    size_t offset = 0;
    size_t limit = 0;
    portENTER_CRITICAL(&stateMux_);
    offset = recordedBytes_;
    limit = recordLimitBytes_;
    portEXIT_CRITICAL(&stateMux_);
    if (offset >= limit) {
        setState(State::Ready);
        Serial.printf("[m9:audio] recording complete bytes=%u duration=%lums peak=%u.%u%%\n",
                      static_cast<unsigned>(recordedBytes()),
                      static_cast<unsigned long>(recordedMs()),
                      static_cast<unsigned>(peakPermille() / 10),
                      static_cast<unsigned>(peakPermille() % 10));
        return;
    }

    size_t request = std::min(kIoBytes, limit - offset);
    request &= ~static_cast<size_t>(1); // 16-bit sample alignment
    const size_t got = instance.mic.readBytes(reinterpret_cast<char *>(buffer_ + offset), request);
    if (got == 0) {
        setError("microphone read returned zero bytes");
        Serial.printf("[m9:audio] ERROR: %s\n", lastError_);
        return;
    }

    const size_t aligned = got & ~static_cast<size_t>(1);
    uint16_t localPeak = peakPermille();
    const int16_t *samples = reinterpret_cast<const int16_t *>(buffer_ + offset);
    const size_t sampleCount = aligned / sizeof(int16_t);
    for (size_t i = 0; i < sampleCount; ++i) {
        const int32_t value = samples[i];
        const uint32_t magnitude = static_cast<uint32_t>(value < 0 ? -value : value);
        const uint16_t permille = static_cast<uint16_t>(std::min<uint32_t>(1000, (magnitude * 1000U) / 32767U));
        if (permille > localPeak) localPeak = permille;
    }

    portENTER_CRITICAL(&stateMux_);
    if (state_ == State::Recording) {
        recordedBytes_ = std::min(recordLimitBytes_, offset + aligned);
        peakPermille_ = localPeak;
        if (recordedBytes_ >= recordLimitBytes_) {
            state_ = State::Ready;
            ++revision_;
        }
    }
    const bool completed = state_ == State::Ready;
    portEXIT_CRITICAL(&stateMux_);

    if (completed) {
        Serial.printf("[m9:audio] recording complete bytes=%u duration=%lums peak=%u.%u%%\n",
                      static_cast<unsigned>(recordedBytes()),
                      static_cast<unsigned long>(recordedMs()),
                      static_cast<unsigned>(peakPermille() / 10),
                      static_cast<unsigned>(peakPermille() % 10));
    }
}

void AudioService::playbackStep() {
    if (!buffer_ || state() != State::Playing) return;
    if (!speakerPowered_) {
        setSpeakerPower(true);
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    size_t offset = 0;
    size_t total = 0;
    portENTER_CRITICAL(&stateMux_);
    offset = playbackOffset_;
    total = recordedBytes_;
    portEXIT_CRITICAL(&stateMux_);

    if (offset >= total) {
        setState(State::Ready);
        setSpeakerPower(false);
        Serial.println("[m9:audio] playback complete");
        return;
    }

    const size_t monoSamples = std::min(kIoSamples, (total - offset) / sizeof(int16_t));
    const int16_t *source = reinterpret_cast<const int16_t *>(buffer_ + offset);
    for (size_t i = 0; i < monoSamples; ++i) {
        // Modest 2x monitor gain. Keep the retained capture unchanged.
        const int16_t sample = clamp16(static_cast<int32_t>(source[i]) * 2);
        playbackScratch_[i * 2] = sample;
        playbackScratch_[i * 2 + 1] = sample;
    }

    const size_t wanted = monoSamples * 2 * sizeof(int16_t);
    const size_t written = instance.player.write(playbackScratch_, wanted);
    if (written == 0) {
        setSpeakerPower(false);
        setError("speaker write returned zero bytes");
        Serial.printf("[m9:audio] ERROR: %s\n", lastError_);
        return;
    }

    const size_t stereoFrames = written / (2 * sizeof(int16_t));
    portENTER_CRITICAL(&stateMux_);
    if (state_ == State::Playing) {
        playbackOffset_ = std::min(total, playbackOffset_ + stereoFrames * sizeof(int16_t));
        if (playbackOffset_ >= total) {
            state_ = State::Ready;
            ++revision_;
        }
    }
    const bool completed = state_ == State::Ready;
    portEXIT_CRITICAL(&stateMux_);

    if (completed) {
        setSpeakerPower(false);
        Serial.println("[m9:audio] playback complete");
    }
}

void AudioService::alertStep() {
    if (state() != State::Alerting) return;
    if (!speakerPowered_) {
        setSpeakerPower(true);
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    AlertTone tone;
    uint32_t start = 0;
    portENTER_CRITICAL(&stateMux_);
    tone = alertTone_;
    start = alertSampleIndex_;
    portEXIT_CRITICAL(&stateMux_);

    // Distinct strong notification cadences. These are generated on the audio
    // worker, never loopTask, and do not modify the retained PCM recording.
    // RX uses a six-beep radio-call-inspired cadence: three short beeps, a
    // short group pause, then three more short beeps. It intentionally copies
    // only the cadence requested for ReticulumWatch, not any sampled audio.
    const uint32_t totalSamples = tone == AlertTone::MessageReceived ? 9520U : 3200U;
    if (start >= totalSamples) {
        portENTER_CRITICAL(&stateMux_);
        state_ = recordedBytes_ >= sizeof(int16_t) ? State::Ready : State::Idle;
        ++revision_;
        portEXIT_CRITICAL(&stateMux_);
        setSpeakerPower(false);
        return;
    }

    const size_t frames = std::min<size_t>(kIoSamples, totalSamples - start);
    for (size_t i = 0; i < frames; ++i) {
        const uint32_t n = start + static_cast<uint32_t>(i);
        uint32_t frequency = 0;
        if (tone == AlertTone::MessageReceived) {
            // 16 kHz timeline (595 ms total):
            // 55ms beep / 30ms / 55ms / 30ms / 55ms / 145ms pause /
            // 55ms beep / 30ms / 55ms / 30ms / 55ms beep.
            constexpr uint32_t kCallBeepHz = 1400U;
            if      (n <  880U) frequency = kCallBeepHz; // beep 1
            else if (n < 1360U) frequency = 0;           // short gap
            else if (n < 2240U) frequency = kCallBeepHz; // beep 2
            else if (n < 2720U) frequency = 0;           // short gap
            else if (n < 3600U) frequency = kCallBeepHz; // beep 3
            else if (n < 5920U) frequency = 0;           // group pause
            else if (n < 6800U) frequency = kCallBeepHz; // beep 4
            else if (n < 7280U) frequency = 0;           // short gap
            else if (n < 8160U) frequency = kCallBeepHz; // beep 5
            else if (n < 8640U) frequency = 0;           // short gap
            else                  frequency = kCallBeepHz; // beep 6
        } else {
            if (n < 1280U) frequency = 1200U;           // 80 ms
            else if (n < 1920U) frequency = 0;          // 40 ms gap
            else frequency = 1600U;                     // 80 ms
        }
        int16_t sample = 0;
        if (frequency != 0) {
            const uint32_t period = std::max<uint32_t>(2U, kSampleRate / frequency);
            sample = (n % period) < (period / 2U) ? 11000 : -11000;
        }
        playbackScratch_[i * 2] = sample;
        playbackScratch_[i * 2 + 1] = sample;
    }

    const size_t wanted = frames * 2 * sizeof(int16_t);
    const size_t written = instance.player.write(playbackScratch_, wanted);
    if (written == 0) {
        setSpeakerPower(false);
        setError("alert speaker write returned zero bytes");
        return;
    }
    const uint32_t writtenFrames = static_cast<uint32_t>(written / (2 * sizeof(int16_t)));
    portENTER_CRITICAL(&stateMux_);
    if (state_ == State::Alerting) alertSampleIndex_ += writtenFrames;
    const bool complete = state_ == State::Alerting && alertSampleIndex_ >= totalSamples;
    if (complete) {
        state_ = recordedBytes_ >= sizeof(int16_t) ? State::Ready : State::Idle;
        ++revision_;
    }
    portEXIT_CRITICAL(&stateMux_);
    if (complete) setSpeakerPower(false);
}

void AudioService::setState(State state) {
    portENTER_CRITICAL(&stateMux_);
    if (state_ != state) {
        state_ = state;
        ++revision_;
    }
    portEXIT_CRITICAL(&stateMux_);
}

void AudioService::setError(const char *message) {
    if (!message) message = "unknown audio error";
    strncpy(lastError_, message, sizeof(lastError_) - 1);
    lastError_[sizeof(lastError_) - 1] = '\0';
    setState(State::Error);
}

void AudioService::setSpeakerPower(bool enabled) {
    if (speakerPowered_ == enabled) return;
    instance.powerControl(POWER_SPEAK, enabled);
    speakerPowered_ = enabled;
}

} // namespace rw::audio
