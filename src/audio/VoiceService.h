#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

namespace rw::audio { class AudioService; }
namespace rw::storage { class StorageService; }
namespace rw::lxmf { class LxmfService; }

namespace rw::audio {

// M9-build5 standard LXMF recorded-voice bridge.
//
// Outbound wire representation is LXMF FIELD_AUDIO (0x07) containing
// [AM_CODEC2_700C (0x03), <Codec2 bytes>]. 700C is the lowest-rate Codec2
// mode in Sideband's current playback range. Legacy local M9-build4 sidecars
// using AM_CODEC2_450 (0x02) remain decodable on the watch. Codec2 work runs
// on a dedicated worker task. Reticulum/LXMF sends are dispatched only from
// loop(), never from the codec task.
class VoiceService {
public:
    enum class State : uint8_t {
        Unavailable,
        Idle,
        Encoding,
        Sending,
        Decoding,
        Ready,
        Error,
    };

    static constexpr uint8_t kLxmfAudioField = 0x07;
    static constexpr uint8_t kCodecMode = 0x03; // LXMF AM_CODEC2_700C (outbound)
    static constexpr uint8_t kLegacyCodecMode450 = 0x02; // M9-build4 local compatibility
    // Keep the Codec2 field bounded. LxmfService performs the authoritative
    // complete-wire check against the live active Link::get_mdu() immediately
    // before send. Hardware logs on the pinned stack reported MDU=431.
    static constexpr size_t kMaxEncodedBytes = 240;
    static constexpr uint32_t kPreferredVoiceRecordMs = 3000;

    bool begin(AudioService &audio, rw::storage::StorageService &storage,
               rw::lxmf::LxmfService &lxmf);
    void loop();

    bool requestSend(const uint8_t destinationHash[16]);
    bool requestPlayback(uint32_t sequence);
    bool cancel();

    bool ready() const;
    bool busy() const;
    State state() const;
    const char *stateName() const;
    const char *status() const { return status_; }
    uint32_t revision() const;
    uint32_t estimatedDurationMs(uint8_t mode, size_t encodedBytes) const;
    size_t lastEncodedBytes() const;
    uint32_t maxRecordMs() const;

private:
    enum class Command : uint8_t { None, EncodeForSend, DecodeForPlayback };

    static constexpr uint32_t kCodecSampleRate = 8000;
    static constexpr uint32_t kWorkerStackBytes = 32768;
    static constexpr size_t kMaxCodecSamplesPerFrame = 640;
    static constexpr size_t kMaxCodecBytesPerFrame = 16;

    static void workerEntry(void *context);
    void workerLoop();
    void encodeRecording();
    void decodeRecording();
    bool ensureCodec(uint8_t lxmfMode);
    static int codec2ModeForLxmf(uint8_t lxmfMode);
    static const char *codecName(uint8_t lxmfMode);
    static size_t standardBytesPerFrame(uint8_t lxmfMode);
    static size_t standardSamplesPerFrame(uint8_t lxmfMode);
    void setState(State state, const char *status = nullptr);

    AudioService *audio_{nullptr};
    rw::storage::StorageService *storage_{nullptr};
    rw::lxmf::LxmfService *lxmf_{nullptr};
    TaskHandle_t workerTask_{nullptr};

    void *codec_{nullptr};
    uint8_t activeCodecMode_{0};
    int samplesPerFrame_{0};
    int bytesPerFrame_{0};
    int outgoingSamplesPerFrame_{0};
    int outgoingBytesPerFrame_{0};
    int16_t codecSamples_[kMaxCodecSamplesPerFrame]{};
    uint8_t codecBits_[kMaxCodecBytesPerFrame]{};
    uint8_t encoded_[kMaxEncodedBytes]{};
    int16_t *decodedPcm_{nullptr};

    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    State state_{State::Unavailable};
    Command command_{Command::None};
    size_t encodedBytes_{0};
    uint32_t encodedDurationMs_{0};
    uint32_t playbackSequence_{0};
    uint8_t playbackCodecMode_{0};
    uint8_t pendingDestination_[16]{};
    bool pendingSend_{false};
    bool encodedReadyForSend_{false};
    bool playbackReady_{false};
    uint32_t revision_{0};
    char status_[96]{"not started"};
};

} // namespace rw::audio
