#include "audio/VoiceService.h"

#include <codec2.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <cstring>

#include "audio/AudioService.h"
#include "lxmf/LxmfService.h"
#include "storage/StorageService.h"

namespace rw::audio {
namespace {
CODEC2 *asCodec(void *ptr) { return static_cast<CODEC2 *>(ptr); }
}

bool VoiceService::begin(AudioService &audio, rw::storage::StorageService &storage,
                         rw::lxmf::LxmfService &lxmf) {
    audio_ = &audio;
    storage_ = &storage;
    lxmf_ = &lxmf;

    if (!audio.ready()) {
        setState(State::Unavailable, "audio hardware unavailable");
        return false;
    }

    // Codec2 construction is deliberately NOT performed here. setup() runs on
    // Arduino's loopTask, whose stack is too small for codec2_create()'s
    // analysis-window initialisation on ESP32-S3. M9-build3 hardware logs
    // proved this with a loopTask stack-canary panic in make_analysis_window().
    // Construct Codec2 inside the dedicated 32 KiB worker instead.
    decodedPcm_ = static_cast<int16_t *>(heap_caps_malloc(
        AudioService::kBufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!decodedPcm_) {
        setState(State::Error, "voice decode PSRAM allocation failed");
        return false;
    }

    strncpy(status_, "Codec2 worker starting", sizeof(status_) - 1);
    status_[sizeof(status_) - 1] = '\0';
    BaseType_t result = xTaskCreatePinnedToCore(
        workerEntry, "rw-voice", kWorkerStackBytes, this, 1, &workerTask_, 0);
    if (result != pdPASS || !workerTask_) {
        setState(State::Error, "voice worker task creation failed");
        return false;
    }

    Serial.printf("[m9:voice] Codec2 worker scheduled stack=%u bytes core=0; "
                  "initialisation deferred off loopTask\n",
                  static_cast<unsigned>(kWorkerStackBytes));
    return true;
}

void VoiceService::loop() {
    bool send = false;
    size_t bytes = 0;
    uint32_t duration = 0;
    uint8_t destination[16]{};
    portENTER_CRITICAL(&mux_);
    if (encodedReadyForSend_ && pendingSend_ && state_ == State::Ready) {
        encodedReadyForSend_ = false;
        state_ = State::Sending;
        ++revision_;
        send = true;
        bytes = encodedBytes_;
        duration = encodedDurationMs_;
        memcpy(destination, pendingDestination_, sizeof(destination));
    }
    portEXIT_CRITICAL(&mux_);

    if (send) {
        const bool ok = lxmf_ && lxmf_->sendVoiceMessage(
            destination, kCodecMode, encoded_, bytes, duration);
        portENTER_CRITICAL(&mux_);
        pendingSend_ = false;
        state_ = ok ? State::Idle : State::Error;
        strncpy(status_, ok ? "voice submitted over active Link" :
                (lxmf_ ? lxmf_->txStatus() : "LXMF unavailable"), sizeof(status_) - 1);
        status_[sizeof(status_) - 1] = '\0';
        ++revision_;
        portEXIT_CRITICAL(&mux_);
        Serial.printf("[m9:voice] %s encoded=%u bytes duration=%lums\n",
                      ok ? "voice message submitted" : "ERROR voice send failed",
                      static_cast<unsigned>(bytes), static_cast<unsigned long>(duration));
    }

    bool play = false;
    portENTER_CRITICAL(&mux_);
    if (playbackReady_ && state_ == State::Ready) {
        playbackReady_ = false;
        state_ = State::Idle;
        ++revision_;
        play = true;
    }
    portEXIT_CRITICAL(&mux_);
    if (play && audio_) {
        if (!audio_->playRecording()) {
            setState(State::Error, "decoded voice loaded but playback failed");
        } else {
            Serial.printf("[m9:voice] playback started for LXMF #%lu\n",
                          static_cast<unsigned long>(playbackSequence_));
        }
    }
}

bool VoiceService::requestSend(const uint8_t destinationHash[16]) {
    if (!ready() || busy() || !destinationHash || !audio_ || !lxmf_) return false;
    if (!lxmf_->canReplyTo(destinationHash)) {
        setState(State::Error, "voice requires an active Link to this peer");
        return false;
    }
    if (audio_->recordedBytes() < sizeof(int16_t)) {
        setState(State::Error, "record a clip before sending");
        return false;
    }
    portENTER_CRITICAL(&mux_);
    memcpy(pendingDestination_, destinationHash, 16);
    pendingSend_ = true;
    command_ = Command::EncodeForSend;
    state_ = State::Encoding;
    strncpy(status_, "encoding Codec2 700C", sizeof(status_) - 1);
    ++revision_;
    portEXIT_CRITICAL(&mux_);
    return true;
}

bool VoiceService::requestPlayback(uint32_t sequence) {
    if (!ready() || busy() || sequence == 0 || !storage_ || !audio_) return false;
    if (audio_->busy()) { setState(State::Error, "audio hardware is busy"); return false; }
    rw::storage::LxmfAudioInfo info{};
    if (!storage_->loadLxmfAudioInfo(sequence, info)) {
        setState(State::Error, "voice sidecar missing/corrupt");
        return false;
    }
    if (info.encodedBytes > sizeof(encoded_)) {
        setState(State::Error, "voice clip exceeds Codec2 playback bound");
        return false;
    }
    size_t length = 0;
    if (!storage_->loadLxmfAudio(sequence, info, encoded_, sizeof(encoded_), length)) {
        setState(State::Error, "voice sidecar CRC/read failure");
        return false;
    }
    if (info.mode != kCodecMode && info.mode != kLegacyCodecMode450) {
        setState(State::Error, "audio codec not supported by this build");
        return false;
    }
    portENTER_CRITICAL(&mux_);
    encodedBytes_ = length;
    encodedDurationMs_ = estimatedDurationMs(info.mode, length);
    playbackSequence_ = sequence;
    playbackCodecMode_ = info.mode;
    command_ = Command::DecodeForPlayback;
    state_ = State::Decoding;
    snprintf(status_, sizeof(status_), "decoding %s", codecName(info.mode));
    ++revision_;
    portEXIT_CRITICAL(&mux_);
    return true;
}

bool VoiceService::cancel() {
    // Codec2 frame operations are intentionally short; do not tear down a
    // frame halfway through. Cancellation only clears work not yet consumed.
    portENTER_CRITICAL(&mux_);
    if (state_ != State::Encoding && state_ != State::Decoding) {
        portEXIT_CRITICAL(&mux_);
        return false;
    }
    command_ = Command::None;
    pendingSend_ = false;
    encodedReadyForSend_ = false;
    playbackReady_ = false;
    state_ = State::Idle;
    strncpy(status_, "cancelled", sizeof(status_) - 1);
    ++revision_;
    portEXIT_CRITICAL(&mux_);
    return true;
}

bool VoiceService::ready() const {
    const State s = state();
    // Error is a recoverable operation state, not loss of the codec/task. A
    // later record/send/play request may therefore retry without rebooting.
    return codec_ && decodedPcm_ && workerTask_ && s != State::Unavailable;
}

bool VoiceService::busy() const {
    const State s = state();
    return s == State::Encoding || s == State::Sending || s == State::Decoding;
}

VoiceService::State VoiceService::state() const {
    portENTER_CRITICAL(&mux_);
    const State s = state_;
    portEXIT_CRITICAL(&mux_);
    return s;
}

const char *VoiceService::stateName() const {
    switch (state()) {
        case State::Unavailable: return "UNAVAILABLE";
        case State::Idle: return "IDLE";
        case State::Encoding: return "ENCODING";
        case State::Sending: return "SENDING";
        case State::Decoding: return "DECODING";
        case State::Ready: return "READY";
        case State::Error: return "ERROR";
    }
    return "UNKNOWN";
}

uint32_t VoiceService::revision() const {
    portENTER_CRITICAL(&mux_);
    const uint32_t r = revision_;
    portEXIT_CRITICAL(&mux_);
    return r;
}

size_t VoiceService::lastEncodedBytes() const {
    portENTER_CRITICAL(&mux_);
    const size_t n = encodedBytes_;
    portEXIT_CRITICAL(&mux_);
    return n;
}

uint32_t VoiceService::maxRecordMs() const {
    if (outgoingSamplesPerFrame_ <= 0 || outgoingBytesPerFrame_ <= 0) return 0;
    const size_t frames = kMaxEncodedBytes / static_cast<size_t>(outgoingBytesPerFrame_);
    const uint32_t codecBoundMs = static_cast<uint32_t>(
        (frames * static_cast<uint64_t>(outgoingSamplesPerFrame_) * 1000ULL) / kCodecSampleRate);
    return std::min<uint32_t>(kPreferredVoiceRecordMs, codecBoundMs);
}

uint32_t VoiceService::estimatedDurationMs(uint8_t mode, size_t encodedBytes) const {
    const size_t frameBytes = standardBytesPerFrame(mode);
    const size_t frameSamples = standardSamplesPerFrame(mode);
    if (frameBytes == 0 || frameSamples == 0) return 0;
    const size_t frames = encodedBytes / frameBytes;
    return static_cast<uint32_t>((frames * static_cast<uint64_t>(frameSamples) * 1000ULL) /
                                 kCodecSampleRate);
}

void VoiceService::workerEntry(void *context) {
    static_cast<VoiceService *>(context)->workerLoop();
}

void VoiceService::workerLoop() {
    // codec2_create() has substantial temporary stack demand. Keeping it here
    // protects Arduino loopTask and follows the ESP32 Codec2 reference pattern
    // of constructing the codec inside a generously sized audio/codec task.
    if (!ensureCodec(kCodecMode)) {
        setState(State::Error, "Codec2 700C init failed");
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    outgoingSamplesPerFrame_ = samplesPerFrame_;
    outgoingBytesPerFrame_ = bytesPerFrame_;

    setState(State::Idle, "Codec2 700C ready; legacy 450 playback enabled");
    Serial.printf("[m9:voice] ready LXMF FIELD_AUDIO=0x%02x outbound=0x%02x Codec2-700C "
                  "legacy_rx=0x%02x samples/frame=%d bytes/frame=%d max_encoded=%u "
                  "max_record=%lums worker_core=0 stack=%u bytes high_water=%u\n",
                  kLxmfAudioField, kCodecMode, kLegacyCodecMode450,
                  outgoingSamplesPerFrame_, outgoingBytesPerFrame_,
                  static_cast<unsigned>(kMaxEncodedBytes),
                  static_cast<unsigned long>(maxRecordMs()),
                  static_cast<unsigned>(kWorkerStackBytes),
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));

    for (;;) {
        Command command = Command::None;
        portENTER_CRITICAL(&mux_);
        command = command_;
        command_ = Command::None;
        portEXIT_CRITICAL(&mux_);
        if (command == Command::EncodeForSend) encodeRecording();
        else if (command == Command::DecodeForPlayback) decodeRecording();
        else vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void VoiceService::encodeRecording() {
    if (!audio_) { setState(State::Error, "voice encoder unavailable"); return; }
    if (!ensureCodec(kCodecMode)) { setState(State::Error, "Codec2 700C unavailable"); return; }
    const int16_t *pcm16k = nullptr;
    size_t samples16k = 0;
    if (!audio_->beginCodecRead(pcm16k, samples16k)) {
        setState(State::Error, "audio clip unavailable for encoding");
        return;
    }

    const size_t sourcePerFrame = static_cast<size_t>(samplesPerFrame_) * 2U;
    const size_t frames = samples16k / sourcePerFrame;
    const size_t required = frames * static_cast<size_t>(bytesPerFrame_);
    if (frames == 0 || required > sizeof(encoded_)) {
        audio_->endCodecRead();
        setState(State::Error, "voice clip does not fit safe Link payload");
        return;
    }

    size_t out = 0;
    for (size_t frame = 0; frame < frames; ++frame) {
        const size_t base = frame * sourcePerFrame;
        for (int i = 0; i < samplesPerFrame_; ++i) {
            const int32_t a = pcm16k[base + static_cast<size_t>(i) * 2U];
            const int32_t b = pcm16k[base + static_cast<size_t>(i) * 2U + 1U];
            codecSamples_[i] = static_cast<int16_t>((a + b) / 2);
        }
        codec2_encode(asCodec(codec_), codecBits_, codecSamples_);
        memcpy(encoded_ + out, codecBits_, static_cast<size_t>(bytesPerFrame_));
        out += static_cast<size_t>(bytesPerFrame_);
        taskYIELD();
    }
    audio_->endCodecRead();

    portENTER_CRITICAL(&mux_);
    encodedBytes_ = out;
    encodedDurationMs_ = static_cast<uint32_t>(
        (frames * static_cast<uint64_t>(samplesPerFrame_) * 1000ULL) / kCodecSampleRate);
    encodedReadyForSend_ = true;
    state_ = State::Ready;
    snprintf(status_, sizeof(status_), "encoded %u B / %lu ms", static_cast<unsigned>(out),
             static_cast<unsigned long>(encodedDurationMs_));
    ++revision_;
    portEXIT_CRITICAL(&mux_);
    Serial.printf("[m9:voice] encoded Codec2-700C frames=%u bytes=%u duration=%lums\n",
                  static_cast<unsigned>(frames), static_cast<unsigned>(out),
                  static_cast<unsigned long>(encodedDurationMs_));
}

void VoiceService::decodeRecording() {
    if (!audio_ || !decodedPcm_) {
        setState(State::Error, "voice decoder unavailable");
        return;
    }
    size_t bytes = 0;
    uint8_t mode = 0;
    portENTER_CRITICAL(&mux_);
    bytes = encodedBytes_;
    mode = playbackCodecMode_;
    portEXIT_CRITICAL(&mux_);
    if (!ensureCodec(mode)) {
        setState(State::Error, "voice codec switch failed");
        return;
    }
    if (bytes == 0 || bytes % static_cast<size_t>(bytesPerFrame_) != 0) {
        setState(State::Error, "invalid Codec2 payload length");
        return;
    }

    const size_t frames = bytes / static_cast<size_t>(bytesPerFrame_);
    const size_t samples16k = frames * static_cast<size_t>(samplesPerFrame_) * 2U;
    if (samples16k * sizeof(int16_t) > AudioService::kBufferBytes) {
        setState(State::Error, "decoded voice exceeds audio buffer");
        return;
    }

    size_t out = 0;
    for (size_t frame = 0; frame < frames; ++frame) {
        memcpy(codecBits_, encoded_ + frame * static_cast<size_t>(bytesPerFrame_),
               static_cast<size_t>(bytesPerFrame_));
        codec2_decode(asCodec(codec_), codecSamples_, codecBits_);
        for (int i = 0; i < samplesPerFrame_; ++i) {
            decodedPcm_[out++] = codecSamples_[i];
            decodedPcm_[out++] = codecSamples_[i];
        }
        taskYIELD();
    }

    if (!audio_->loadPcm16Mono(decodedPcm_, out)) {
        setState(State::Error, "could not load decoded voice into audio service");
        return;
    }
    portENTER_CRITICAL(&mux_);
    encodedDurationMs_ = static_cast<uint32_t>((out * 1000ULL) / AudioService::kSampleRate);
    playbackReady_ = true;
    state_ = State::Ready;
    strncpy(status_, "voice decoded; playback queued", sizeof(status_) - 1);
    ++revision_;
    portEXIT_CRITICAL(&mux_);
    Serial.printf("[m9:voice] decoded LXMF #%lu mode=0x%02x %s frames=%u pcm=%u samples\n",
                  static_cast<unsigned long>(playbackSequence_), mode, codecName(mode),
                  static_cast<unsigned>(frames), static_cast<unsigned>(out));
}

int VoiceService::codec2ModeForLxmf(uint8_t lxmfMode) {
    switch (lxmfMode) {
        case kCodecMode: return CODEC2_MODE_700C;
        case kLegacyCodecMode450: return CODEC2_MODE_450;
        default: return -1;
    }
}

const char *VoiceService::codecName(uint8_t lxmfMode) {
    switch (lxmfMode) {
        case kCodecMode: return "Codec2 700C";
        case kLegacyCodecMode450: return "Codec2 450";
        default: return "unsupported Codec2";
    }
}

size_t VoiceService::standardBytesPerFrame(uint8_t lxmfMode) {
    // Codec2 stable modes used here have 40 ms frames at 8 kHz. 700C carries
    // 28 bits rounded to 4 bytes; 450 carries 18 bits rounded to 3 bytes.
    if (lxmfMode == kCodecMode) return 4;
    if (lxmfMode == kLegacyCodecMode450) return 3;
    return 0;
}

size_t VoiceService::standardSamplesPerFrame(uint8_t lxmfMode) {
    return standardBytesPerFrame(lxmfMode) ? 320 : 0;
}

bool VoiceService::ensureCodec(uint8_t lxmfMode) {
    if (codec_ && activeCodecMode_ == lxmfMode) return true;
    const int c2Mode = codec2ModeForLxmf(lxmfMode);
    if (c2Mode < 0) return false;

    // This function is called only from rw-voice. codec2_create() must never
    // migrate back to Arduino loopTask; its stack demand caused the build3
    // hardware boot loop. Destroying/recreating here also avoids holding two
    // large Codec2 contexts simultaneously on the watch.
    if (codec_) {
        codec2_destroy(asCodec(codec_));
        codec_ = nullptr;
        activeCodecMode_ = 0;
    }
    CODEC2 *next = codec2_create(c2Mode);
    if (!next) return false;
    const int samples = codec2_samples_per_frame(next);
    const int bytes = codec2_bytes_per_frame(next);
    if (samples <= 0 || samples > static_cast<int>(kMaxCodecSamplesPerFrame) ||
        bytes <= 0 || bytes > static_cast<int>(kMaxCodecBytesPerFrame)) {
        codec2_destroy(next);
        return false;
    }
    codec_ = next;
    activeCodecMode_ = lxmfMode;
    samplesPerFrame_ = samples;
    bytesPerFrame_ = bytes;
    Serial.printf("[m9:voice] codec active mode=0x%02x %s samples/frame=%d bytes/frame=%d\n",
                  lxmfMode, codecName(lxmfMode), samplesPerFrame_, bytesPerFrame_);
    return true;
}

void VoiceService::setState(State state, const char *status) {
    portENTER_CRITICAL(&mux_);
    state_ = state;
    if (status) {
        strncpy(status_, status, sizeof(status_) - 1);
        status_[sizeof(status_) - 1] = '\0';
    }
    ++revision_;
    portEXIT_CRITICAL(&mux_);
}

} // namespace rw::audio
