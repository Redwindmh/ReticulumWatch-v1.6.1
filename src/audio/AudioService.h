#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

namespace rw::hal { class WatchHardware; }

namespace rw::audio {

// M9 deliberately keeps audio local to this service. No Reticulum/LXMF wire
// code is called from the worker task. That makes microphone/speaker bring-up
// independently testable while the hardware-accepted messenger keeps running.
class AudioService {
public:
    enum class State : uint8_t { Unavailable, Idle, Recording, Ready, Playing, Processing, Alerting, Error };
    enum class AlertTone : uint8_t { MessageReceived, MessageSent };

    static constexpr uint32_t kSampleRate = 16000;
    static constexpr uint8_t kBitsPerSample = 16;
    static constexpr uint8_t kCaptureChannels = 1;
    static constexpr uint32_t kMaxRecordMs = 4000;
    static constexpr size_t kBytesPerSample = kBitsPerSample / 8;
    static constexpr size_t kBufferBytes =
        (kSampleRate * kBytesPerSample * kCaptureChannels * kMaxRecordMs) / 1000;

    bool begin(rw::hal::WatchHardware &hardware);
    void loop();

    bool startRecording(uint32_t maxDurationMs = kMaxRecordMs);
    bool stop();
    bool playRecording();
    bool clearRecording();
    bool playAlertTone(AlertTone tone);

    // Codec worker handshake. Processing locks the retained PCM clip against
    // RECORD/PLAY/CLEAR while a bounded Codec2 frame pass is in progress.
    bool beginCodecRead(const int16_t *&samples, size_t &sampleCount);
    void endCodecRead();
    bool loadPcm16Mono(const int16_t *samples, size_t sampleCount);

    bool ready() const;
    bool busy() const;
    State state() const;
    const char *stateName() const;
    const char *lastError() const { return lastError_; }

    size_t recordedBytes() const;
    uint32_t recordedMs() const;
    uint32_t playbackMs() const;
    uint16_t peakPermille() const;
    uint32_t revision() const;
    uint32_t freePsramAtBegin() const { return freePsramAtBegin_; }

private:
    static constexpr size_t kIoSamples = 256;
    static constexpr size_t kIoBytes = kIoSamples * sizeof(int16_t);
    static constexpr uint32_t kWorkerStackBytes = 6144;

    static void workerEntry(void *context);
    void workerLoop();
    void captureStep();
    void playbackStep();
    void alertStep();
    void setState(State state);
    void setError(const char *message);
    void setSpeakerPower(bool enabled);

    rw::hal::WatchHardware *hardware_{nullptr};
    uint8_t *buffer_{nullptr};
    int16_t playbackScratch_[kIoSamples * 2]{};
    TaskHandle_t workerTask_{nullptr};

    mutable portMUX_TYPE stateMux_ = portMUX_INITIALIZER_UNLOCKED;
    State state_{State::Unavailable};
    size_t recordedBytes_{0};
    size_t recordLimitBytes_{kBufferBytes};
    size_t playbackOffset_{0};
    uint16_t peakPermille_{0};
    uint32_t revision_{0};
    AlertTone alertTone_{AlertTone::MessageReceived};
    uint32_t alertSampleIndex_{0};

    bool microphoneReady_{false};
    bool speakerReady_{false};
    bool speakerPowered_{false};
    uint32_t freePsramAtBegin_{0};
    char lastError_[80]{"not started"};
};

} // namespace rw::audio
