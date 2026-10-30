#include "ui/WatchUI.h"

#include <Arduino.h>
#include <cstring>
#include <time.h>
#include "config/BuildConfig.h"
#include "audio/AudioService.h"
#include "audio/VoiceService.h"
#include "hal/WatchHardware.h"
#include "lxmf/LxmfService.h"
#include "power/PowerManager.h"
#include "radio/RadioService.h"
#include "reticulum/ReticulumService.h"
#include "transport/TransportService.h"

namespace rw::ui {
namespace {
void shortHash(const uint8_t *hash, char out[13]) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < 6; ++i) {
        out[i * 2] = hex[(hash[i] >> 4) & 0x0f];
        out[i * 2 + 1] = hex[hash[i] & 0x0f];
    }
    out[12] = '\0';
}

void fullHash(const uint8_t *hash, char out[33]) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < 16; ++i) {
        out[i * 2] = hex[(hash[i] >> 4) & 0x0f];
        out[i * 2 + 1] = hex[hash[i] & 0x0f];
    }
    out[32] = '\0';
}

// Explicitly style every text-bearing object in the conversation view.
// LVGL's default theme may assign a foreground to child widgets, so setting
// only the screenRoot_ color is not sufficient for legibility on black.
// Called only while building a screen (never from the render/loop path).
void applyConversationTextContrast(lv_obj_t *root) {
    if (!root) return;
    lv_obj_set_style_text_color(root, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    const uint32_t children = lv_obj_get_child_count(root);
    for (uint32_t i = 0; i < children; ++i) {
        applyConversationTextContrast(lv_obj_get_child(root, static_cast<int32_t>(i)));
    }
}

void previewText(const char *source, char *out, size_t capacity) {
    if (!out || capacity == 0) return;
    if (!source || !source[0]) {
        strncpy(out, "(empty message)", capacity - 1);
        out[capacity - 1] = '\0';
        return;
    }
    size_t j = 0;
    for (size_t i = 0; source[i] && j + 1 < capacity; ++i) {
        char c = source[i];
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        out[j++] = c;
    }
    out[j] = '\0';
}


void formatMessageTime(double timestamp, char *out, size_t capacity, bool includeDate) {
    if (!out || capacity == 0) return;
    out[0] = '\0';
    if (timestamp <= 0.0) {
        strncpy(out, "--:--", capacity - 1);
        out[capacity - 1] = '\0';
        return;
    }
    const time_t raw = static_cast<time_t>(timestamp);
    struct tm utc{};
    if (!gmtime_r(&raw, &utc)) {
        strncpy(out, "--:--", capacity - 1);
        out[capacity - 1] = '\0';
        return;
    }
    if (includeDate) strftime(out, capacity, "%m/%d %H:%M UTC", &utc);
    else strftime(out, capacity, "%H:%MZ", &utc);
}


void formatPeerAge(double timestamp, char *out, size_t capacity) {
    if (!out || capacity == 0) return;
    out[0] = '\0';
    if (timestamp <= 0.0) {
        strncpy(out, "unknown", capacity - 1);
        out[capacity - 1] = '\0';
        return;
    }
    const time_t now = ::time(nullptr);
    if (now <= 1577836800) {
        strncpy(out, "time unavailable", capacity - 1);
        out[capacity - 1] = '\0';
        return;
    }
    long age = static_cast<long>(difftime(now, static_cast<time_t>(timestamp)));
    if (age < 0) age = 0;
    if (age < 60) snprintf(out, capacity, "%lds ago", age);
    else if (age < 3600) snprintf(out, capacity, "%ldm ago", age / 60);
    else if (age < 86400) snprintf(out, capacity, "%ldh ago", age / 3600);
    else snprintf(out, capacity, "%ldd ago", age / 86400);
}

// M3 preserves the hardware-proven M1.2.7 watch-sized keyboard maps. The stock LVGL QWERTY map puts as many
// as 12 buttons across a 240 px screen. These maps cap the letter rows at
// seven buttons, trading desktop-like row geometry for substantially larger
// touch targets while preserving QWERTY letter order and LVGL's built-in
// ABC/abc/1# mode switching behavior.
static const char * const kWatchKeyboardLower[] = {
    "q", "w", "e", "r", "t", "y", "u", "\n",
    "i", "o", "p", "a", "s", "d", "f", "\n",
    "g", "h", "j", "k", "l", "z", "x", "\n",
    "c", "v", "b", "n", "m", ".", ",", "\n",
    "ABC", "1#", LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_BACKSPACE, ""
};

static const char * const kWatchKeyboardUpper[] = {
    "Q", "W", "E", "R", "T", "Y", "U", "\n",
    "I", "O", "P", "A", "S", "D", "F", "\n",
    "G", "H", "J", "K", "L", "Z", "X", "\n",
    "C", "V", "B", "N", "M", ".", ",", "\n",
    "abc", "1#", LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_BACKSPACE, ""
};

static const char * const kWatchKeyboardSpecial[] = {
    "1", "2", "3", "4", "5", "6", "7", "\n",
    "8", "9", "0", "+", "-", "=", "%", "\n",
    "!", "?", "@", "#", "$", "&", "*", "\n",
    "'", "\"", "(", ")", ":", ";", "/", "\n",
    "abc", LV_SYMBOL_LEFT, "<", " ", ">", LV_SYMBOL_RIGHT, LV_SYMBOL_BACKSPACE, ""
};

// LVGL defines lv_buttonmatrix_ctrl_t as an enum. Its own keyboard tables are
// compiled as C, where flag|width integer expressions can initialize that enum.
// WatchUI.cpp is C++, where those expressions are `int` and require an explicit
// conversion. Keep every custom control-map value strongly typed here.
constexpr lv_buttonmatrix_ctrl_t kbCtrl(uint32_t flags, uint32_t width) {
    return static_cast<lv_buttonmatrix_ctrl_t>(flags | width);
}

constexpr lv_buttonmatrix_ctrl_t kbWidth(uint32_t width) {
    return static_cast<lv_buttonmatrix_ctrl_t>(width);
}

#define RW_KB_CHAR kbCtrl(LV_BUTTONMATRIX_CTRL_POPOVER, 1U)
#define RW_KB_MODE(width) kbCtrl(LV_KEYBOARD_CTRL_BUTTON_FLAGS, (width))
#define RW_KB_NAV(width) kbCtrl(LV_BUTTONMATRIX_CTRL_CHECKED, (width))
#define RW_KB_WIDTH(width) kbWidth((width))

static const lv_buttonmatrix_ctrl_t kWatchKeyboardTextCtrl[] = {
    RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR,
    RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR,
    RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR,
    RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR,
    RW_KB_MODE(2), RW_KB_MODE(2), RW_KB_NAV(2), RW_KB_WIDTH(3), RW_KB_NAV(2), RW_KB_NAV(2)
};

static const lv_buttonmatrix_ctrl_t kWatchKeyboardSpecialCtrl[] = {
    RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR,
    RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR,
    RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR,
    RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR, RW_KB_CHAR,
    RW_KB_MODE(2), RW_KB_NAV(2), RW_KB_CHAR, RW_KB_WIDTH(3), RW_KB_CHAR, RW_KB_NAV(2), RW_KB_NAV(2)
};

static_assert(sizeof(kWatchKeyboardTextCtrl) / sizeof(kWatchKeyboardTextCtrl[0]) == 34,
              "watch text keyboard control map must match 34 buttons");
static_assert(sizeof(kWatchKeyboardSpecialCtrl) / sizeof(kWatchKeyboardSpecialCtrl[0]) == 35,
              "watch special keyboard control map must match 35 buttons");

#undef RW_KB_CHAR
#undef RW_KB_MODE
#undef RW_KB_NAV
#undef RW_KB_WIDTH
}



void WatchUI::begin(rw::hal::WatchHardware &hardware,
                    rw::power::PowerManager &power,
                    rw::storage::StorageService &storage,
                    rw::radio::RadioService &radio,
                    rw::reticulum::ReticulumService &reticulum,
                    rw::transport::TransportService &transport,
                    rw::lxmf::LxmfService &lxmf,
                    rw::audio::AudioService &audio,
                    rw::audio::VoiceService &voice) {
    hardware_ = &hardware;
    power_ = &power;
    storage_ = &storage;
    radio_ = &radio;
    reticulum_ = &reticulum;
    transport_ = &transport;
    lxmf_ = &lxmf;
    audio_ = &audio;
    voice_ = &voice;
    screenRoot_ = lv_screen_active();
    lastObservedMessageCount_ = storage.lxmfMessageCount();
    userProfile_ = rw::config::user::defaultProfile();
    storage.loadUserProfile(userProfile_);
    if (hardware_) hardware_->setBrightness(userProfile_.brightness);
    showWatchFace();
    Serial.printf("[m10:ui] M10-build3 UI ready: RAM failover + live peers + message alerts + clock discipline; user=%s audio=%s voice=%s\n",
                  userProfile_.displayName, audio.ready() ? "ready" : "unavailable",
                  voice.ready() ? "ready" : "unavailable");
}

void WatchUI::prepareScreen() {
    lv_obj_clean(screenRoot_);
    lv_obj_set_size(screenRoot_, 240, 240);
    lv_obj_clear_flag(screenRoot_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screenRoot_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(screenRoot_, 5, 0);
    lv_obj_set_style_bg_color(screenRoot_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screenRoot_, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(screenRoot_, lv_color_hex(0xE8E8E8), 0);
    for (size_t i = 0; i < kVisibleThreads; ++i) {
        threadButtons_[i] = nullptr;
        memset(threadHashes_[i], 0, 16);
    }
    for (size_t i = 0; i < kVisiblePeers; ++i) {
        peerButtons_[i] = nullptr;
        memset(peerHashes_[i], 0, 16);
    }
    composeTextArea_ = nullptr;
    composeStatusLabel_ = nullptr;
    audioStatusLabel_ = nullptr;
    audioStatsLabel_ = nullptr;
    threadVoiceStatusLabel_ = nullptr;
    sendingOverlay_ = nullptr;
}

lv_obj_t *WatchUI::makeButton(lv_obj_t *parent, const char *text, int width, int height,
                              lv_event_cb_t callback, void *userData) {
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x202020), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x555555), 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    if (userData) lv_obj_add_event_cb(button, buttonHapticEvent, LV_EVENT_PRESSED, userData);
    if (callback) lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, userData);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_center(label);
    return button;
}

lv_obj_t *WatchUI::makeIconButton(lv_obj_t *parent, const char *symbol, int width, int height,
                                  lv_event_cb_t callback, void *userData) {
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    if (userData) lv_obj_add_event_cb(button, buttonHapticEvent, LV_EVENT_PRESSED, userData);
    if (callback) lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, userData);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xE8E8E8), 0);
    lv_obj_center(label);
    return button;
}

lv_obj_t *WatchUI::makeMenuIconButton(lv_obj_t *parent, int width, int height,
                                      lv_event_cb_t callback, void *userData) {
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    if (userData) lv_obj_add_event_cb(button, buttonHapticEvent, LV_EVENT_PRESSED, userData);
    if (callback) lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, userData);
    for (int i=0;i<3;++i) {
        lv_obj_t *bar = lv_obj_create(button);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, 22, 2);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0xE8E8E8), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_align(bar, LV_ALIGN_CENTER, 0, (i-1)*6);
    }
    return button;
}

void WatchUI::makeHeader(const char *titleText, Screen backTarget) {
    backTarget_ = backTarget;
    lv_obj_t *back = makeButton(screenRoot_, "<", 36, 29, backEvent, this);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *home = makeIconButton(screenRoot_, LV_SYMBOL_HOME, 36, 29, homeEvent, this);
    lv_obj_align(home, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_t *title = lv_label_create(screenRoot_);
    // RC1: use nearly all space between Back/Home and scale long titles down
    // instead of clipping them on the 240 px display.
    lv_obj_set_width(title, 164);
    lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    const size_t titleLen = titleText ? strlen(titleText) : 0;
    lv_obj_set_style_text_font(title, titleLen > 12 ? &lv_font_montserrat_10 : &lv_font_montserrat_12, 0);
    lv_label_set_text(title, titleText);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
}

void WatchUI::displayPeerName(const uint8_t hash[16], char *out, size_t capacity) const {
    if (!out || capacity == 0) return;
    out[0] = '\0';
    rw::storage::PeerRecord peer{};
    if (storage_ && storage_->loadPeer(hash, peer)) {
        const char *name = peer.displayName[0] ? peer.displayName : peer.announcedName;
        if (name && name[0]) {
            strncpy(out, name, capacity - 1);
            out[capacity - 1] = '\0';
            return;
        }
    }
    char shortPeer[13]{};
    shortHash(hash, shortPeer);
    strncpy(out, shortPeer, capacity - 1);
    out[capacity - 1] = '\0';
}

void WatchUI::selectPeer(const uint8_t hash[16]) {
    if (!hash) return;
    memcpy(selectedPeer_, hash, 16);
    haveSelectedPeer_ = true;
}

size_t WatchUI::buildConversationSummaries(ConversationSummary *summaries, size_t capacity) const {
    if (!storage_ || !summaries || capacity == 0) return 0;
    for (size_t i = 0; i < capacity; ++i) summaries[i] = ConversationSummary{};
    size_t count = 0;
    const uint32_t high = storage_->lxmfMessageCount();
    const uint32_t floor = high > rw::storage::StorageService::kMaxRetainedMessages
        ? high - rw::storage::StorageService::kMaxRetainedMessages : 0;
    for (uint32_t seq = high; seq > floor; --seq) {
        rw::storage::LxmfMessageRecord m{};
        if (!storage_->loadLxmfMessage(seq, m)) continue;
        const uint8_t *peer = m.outgoing ? m.destinationHash : m.sourceHash;
        bool zero = true;
        for (size_t b = 0; b < 16; ++b) zero = zero && peer[b] == 0;
        if (zero) continue;
        size_t slot = count;
        for (size_t i = 0; i < count; ++i) {
            if (memcmp(summaries[i].peerHash, peer, 16) == 0) { slot = i; break; }
        }
        if (slot == count) {
            if (count >= capacity) continue;
            memcpy(summaries[count].peerHash, peer, 16);
            summaries[count].latestSequence = seq;
            summaries[count].latestTimestamp = m.timestamp;
            summaries[count].latestOutgoing = m.outgoing;
            previewText(m.content, summaries[count].preview, sizeof(summaries[count].preview));
            ++count;
        }
        if (!m.outgoing && !m.read && slot < count) ++summaries[slot].unread;
    }
    Serial.printf("[m4:nav] rebuilt conversations threads=%u high_sequence=%lu\n",
                  static_cast<unsigned>(count), static_cast<unsigned long>(high));
    return count;
}

void WatchUI::showWatchFace() {
    screen_ = Screen::WatchFace;
    haveSelectedPeer_ = false;
    prepareScreen();
    displayedMessageCount_ = storage_ ? storage_->lxmfMessageCount() : 0;
    displayedUnreadCount_ = storage_ ? storage_->unreadLxmfCount() : 0;
    displayedDiscoveredCount_ = lxmf_ ? lxmf_->discoveredPeers() : 0;
    lastClockRefreshMs_ = millis();

    char timeText[16] = "--:--";
    char dateText[32] = "DATE UNAVAILABLE";
    struct tm local{};
    if (hardware_ && hardware_->localDateTime(local)) {
        strftime(timeText, sizeof(timeText), "%H:%M", &local);
        strftime(dateText, sizeof(dateText), "%a %b %d", &local);
    }

    // M4 keeps the home face deliberately watch-like: time and date dominate;
    // networking diagnostics belong on Network/Peers rather than the clock.
    lv_obj_t *timeLabel = lv_label_create(screenRoot_);
    // Keep transform bounds inside the 240 px face while scaling the 48 px glyphs.
    lv_obj_set_width(timeLabel, 190);
    lv_obj_set_style_text_align(timeLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(timeLabel, &lv_font_montserrat_48, 0);
    // LVGL built-in Montserrat tops out at 48 px. v1.1.0 increases the
    // transform from 288 (~112.5%) to 304 (~118.75%) for a modestly larger
    // clock, then applies a 4 px optical left correction so the rendered
    // digits appear centered on the physical 240 px watch face.
    lv_obj_set_style_transform_scale_x(timeLabel, 304, 0);
    lv_obj_set_style_transform_scale_y(timeLabel, 304, 0);
    lv_label_set_text(timeLabel, timeText);
    lv_obj_align(timeLabel, LV_ALIGN_CENTER, -4, -43);

    lv_obj_t *dateLabel = lv_label_create(screenRoot_);
    lv_obj_set_width(dateLabel, 220);
    lv_obj_set_style_text_align(dateLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(dateLabel, &lv_font_montserrat_18, 0);
    lv_label_set_text(dateLabel, dateText);
    lv_obj_align(dateLabel, LV_ALIGN_CENTER, 0, 10);

    char battery[32]{};
    const int percent = hardware_ ? hardware_->batteryPercent() : -1;
    if (percent >= 0) snprintf(battery, sizeof(battery), "%d%%%s", percent, hardware_ && hardware_->charging() ? "+" : "");
    else snprintf(battery, sizeof(battery), "--%%");
    lv_obj_t *bat = lv_label_create(screenRoot_);
    lv_obj_set_style_text_font(bat, &lv_font_montserrat_10, 0);
    lv_label_set_text(bat, battery);
    lv_obj_align(bat, LV_ALIGN_TOP_RIGHT, -4, 5);

    const uint32_t unread = storage_ ? storage_->unreadLxmfCount() : 0;
    lv_obj_t *messages = makeIconButton(screenRoot_, LV_SYMBOL_ENVELOPE, 108, 38, conversationsNavEvent, this);
    lv_obj_align(messages, LV_ALIGN_BOTTOM_LEFT, 0, -6);
    if (unread) {
        lv_obj_t *badge = lv_label_create(screenRoot_);
        lv_obj_set_style_text_font(badge, &lv_font_montserrat_10, 0);
        lv_label_set_text_fmt(badge, "%lu", static_cast<unsigned long>(unread));
        lv_obj_align(badge, LV_ALIGN_BOTTOM_LEFT, 72, -29);
    }
    lv_obj_t *menu = makeMenuIconButton(screenRoot_, 108, 38, menuEvent, this);
    lv_obj_align(menu, LV_ALIGN_BOTTOM_RIGHT, 0, -6);
    Serial.printf("[m6:watch] face time=%s date=%s battery=%d unread=%lu\n",
                  timeText, dateText, percent, static_cast<unsigned long>(unread));
}

void WatchUI::showMenu() {
    screen_ = Screen::Menu;
    prepareScreen();
    lv_obj_t *title = lv_label_create(screenRoot_);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_label_set_text(title, "MENU");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 7);
    lv_obj_t *home = makeIconButton(screenRoot_, LV_SYMBOL_HOME, 36, 29, homeEvent, this);
    lv_obj_align(home, LV_ALIGN_TOP_LEFT, 0, 0);

    // Ratspeak/rsDeck-inspired primary information architecture: Messages is
    // the conversation view; Contacts are saved peers; Peers are discoveries.
    struct Entry { const char *text; lv_event_cb_t cb; int x; int y; };
    const Entry entries[] = {
        {"MESSAGES", conversationsNavEvent, 4, 40}, {"CONTACTS", contactsEvent, 122, 40},
        {"PEERS", peersEvent, 4, 82}, {"NEW MSG", composeNavEvent, 122, 82},
        {"NETWORK", networkEvent, 4, 124}, {"SETTINGS", settingsEvent, 122, 124},
    };
    for (const auto &entry : entries) {
        lv_obj_t *button = makeButton(screenRoot_, entry.text, 108, 36, entry.cb, this);
        lv_obj_set_pos(button, entry.x, entry.y);
    }
    lv_obj_t *audio = makeButton(screenRoot_, "AUDIO", 108, 36, audioEvent, this);
    lv_obj_set_pos(audio, 4, 166);
    lv_obj_t *announce = makeButton(screenRoot_, "ANNOUNCE", 108, 36, announceEvent, this);
    lv_obj_set_pos(announce, 122, 166);
    Serial.println("[m9:nav] menu opened: Messages/Contacts/Peers/Network/Settings/Audio/Announce");
}

void WatchUI::showAudioLab() {
    screen_ = Screen::AudioLab;
    prepareScreen();
    makeHeader(audioVoiceCompose_ ? "VOICE MSG" : "AUDIO LAB",
               audioVoiceCompose_ ? Screen::Thread : Screen::Menu);

    audioStatusLabel_ = lv_label_create(screenRoot_);
    lv_obj_set_pos(audioStatusLabel_, 8, 39);
    lv_obj_set_width(audioStatusLabel_, 218);
    lv_label_set_long_mode(audioStatusLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(audioStatusLabel_, &lv_font_montserrat_12, 0);

    audioStatsLabel_ = lv_label_create(screenRoot_);
    lv_obj_set_pos(audioStatsLabel_, 8, 76);
    lv_obj_set_width(audioStatsLabel_, 218);
    lv_label_set_long_mode(audioStatsLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(audioStatsLabel_, &lv_font_montserrat_10, 0);

    lv_obj_t *record = makeButton(screenRoot_, "RECORD", 108, 35, audioRecordEvent, this);
    lv_obj_set_pos(record, 4, 147);
    lv_obj_t *play = makeButton(screenRoot_, "PLAY", 108, 35, audioPlayEvent, this);
    lv_obj_set_pos(play, 122, 147);
    lv_obj_t *stop = makeButton(screenRoot_, "STOP", 108, 35, audioStopEvent, this);
    lv_obj_set_pos(stop, 4, 188);
    lv_obj_t *action = makeButton(screenRoot_, audioVoiceCompose_ ? "SEND" : "CLEAR", 108, 35,
                                  audioVoiceCompose_ ? voiceSendEvent : audioClearEvent, this);
    lv_obj_set_pos(action, 122, 188);

    updateAudioLabLabels();
    Serial.printf("[m9:audio] Audio Lab opened state=%s ready=%s mode=%s\n",
                  audio_ ? audio_->stateName() : "MISSING",
                  audio_ && audio_->ready() ? "yes" : "no",
                  audioVoiceCompose_ ? "voice-compose" : "local");
}

void WatchUI::updateAudioLabLabels() {
    if (!audioStatusLabel_ || !audioStatsLabel_) return;
    if (!audio_) {
        lv_label_set_text(audioStatusLabel_, "STATE: UNAVAILABLE");
        lv_label_set_text(audioStatsLabel_, "Audio service is not wired.\nText messaging remains available.");
        return;
    }

    const uint32_t clipMs = audio_->recordedMs();
    const uint32_t playMs = audio_->playbackMs();
    if (audioVoiceCompose_ && voice_) {
        if (audio_->state() == rw::audio::AudioService::State::Recording) {
            lv_label_set_text_fmt(audioStatusLabel_, "RECORDING  %lu.%lus / %lu.%lus",
                                  static_cast<unsigned long>(clipMs / 1000UL),
                                  static_cast<unsigned long>((clipMs % 1000UL) / 100UL),
                                  static_cast<unsigned long>(voice_->maxRecordMs() / 1000UL),
                                  static_cast<unsigned long>((voice_->maxRecordMs() % 1000UL) / 100UL));
        } else if (audio_->state() == rw::audio::AudioService::State::Playing) {
            lv_label_set_text_fmt(audioStatusLabel_, "PLAYING  %lu.%lus / %lu.%lus",
                                  static_cast<unsigned long>(playMs / 1000UL),
                                  static_cast<unsigned long>((playMs % 1000UL) / 100UL),
                                  static_cast<unsigned long>(clipMs / 1000UL),
                                  static_cast<unsigned long>((clipMs % 1000UL) / 100UL));
        } else {
            lv_label_set_text_fmt(audioStatusLabel_, "AUDIO: %s\nVOICE: %s", audio_->stateName(),
                                  voice_->stateName());
        }
        lv_label_set_text_fmt(audioStatsLabel_,
                              "Codec2 700C voice message\n"
                              "Clip %lu ms | Peak %u.%u%%\n"
                              "RECORD -> optional PLAY -> SEND\n"
                              "%s",
                              static_cast<unsigned long>(clipMs),
                              static_cast<unsigned>(audio_->peakPermille() / 10),
                              static_cast<unsigned>(audio_->peakPermille() % 10),
                              voice_->status());
        displayedVoiceRevision_ = voice_->revision();
    } else {
        if (audio_->state() == rw::audio::AudioService::State::Recording) {
            lv_label_set_text_fmt(audioStatusLabel_, "RECORDING  %lu.%lus / 4.0s",
                                  static_cast<unsigned long>(clipMs / 1000UL),
                                  static_cast<unsigned long>((clipMs % 1000UL) / 100UL));
        } else if (audio_->state() == rw::audio::AudioService::State::Playing) {
            lv_label_set_text_fmt(audioStatusLabel_, "PLAYING  %lu.%lus / %lu.%lus",
                                  static_cast<unsigned long>(playMs / 1000UL),
                                  static_cast<unsigned long>((playMs % 1000UL) / 100UL),
                                  static_cast<unsigned long>(clipMs / 1000UL),
                                  static_cast<unsigned long>((clipMs % 1000UL) / 100UL));
        } else {
            lv_label_set_text_fmt(audioStatusLabel_, "STATE: %s\n%s", audio_->stateName(),
                                  audio_->ready() ? "Mic + speaker ready" : audio_->lastError());
        }
        lv_label_set_text_fmt(audioStatsLabel_,
                              "16 kHz / 16-bit mono | 4.0 s max\n"
                              "Clip %lu ms | Peak %u.%u%%\n"
                              "PSRAM free %lu KiB\n"
                              "Local test; VOICE is available in a conversation",
                              static_cast<unsigned long>(clipMs),
                              static_cast<unsigned>(audio_->peakPermille() / 10),
                              static_cast<unsigned>(audio_->peakPermille() % 10),
                              static_cast<unsigned long>(ESP.getFreePsram() / 1024UL));
    }
    displayedAudioRevision_ = audio_->revision();
}

void WatchUI::updateThreadVoiceStatus() {
    if (screen_ != Screen::Thread || !threadVoiceStatusLabel_ || !audio_ || !voice_) return;
    const bool activeLink = lxmf_ && haveSelectedPeer_ && lxmf_->canReplyTo(selectedPeer_);
    const uint32_t clipMs = audio_->recordedMs();
    const uint32_t playMs = audio_->playbackMs();
    char line[128]{};

    if (voice_->state() == rw::audio::VoiceService::State::Encoding) {
        snprintf(line, sizeof(line), "Encoding voice...");
    } else if (voice_->state() == rw::audio::VoiceService::State::Sending) {
        snprintf(line, sizeof(line), "Sending voice...");
    } else if (voice_->state() == rw::audio::VoiceService::State::Decoding) {
        snprintf(line, sizeof(line), "Preparing voice playback...");
    } else if (audio_->state() == rw::audio::AudioService::State::Playing) {
        snprintf(line, sizeof(line), "Playing %lu.%lus / %lu.%lus",
                 static_cast<unsigned long>(playMs / 1000UL),
                 static_cast<unsigned long>((playMs % 1000UL) / 100UL),
                 static_cast<unsigned long>(clipMs / 1000UL),
                 static_cast<unsigned long>((clipMs % 1000UL) / 100UL));
    } else if (voice_->state() == rw::audio::VoiceService::State::Error) {
        snprintf(line, sizeof(line), "Voice: %s", voice_->status());
    } else if (strncmp(voice_->status(), "voice submitted", 15) == 0) {
        snprintf(line, sizeof(line), "Voice submitted over active Link");
    } else if (!activeLink) {
        snprintf(line, sizeof(line), "VOICE needs active Link to send");
    } else {
        snprintf(line, sizeof(line), "VOICE ready | record, preview, send");
    }
    lv_label_set_text(threadVoiceStatusLabel_, line);
    lv_obj_set_style_text_color(threadVoiceStatusLabel_, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    displayedAudioRevision_ = audio_->revision();
    displayedVoiceRevision_ = voice_->revision();
}

void WatchUI::showInbox() {
    screen_ = Screen::Inbox;
    prepareScreen();
    makeHeader("INBOX", Screen::Menu);
    displayedMessageCount_ = storage_ ? storage_->lxmfMessageCount() : 0;
    displayedUnreadCount_ = storage_ ? storage_->unreadLxmfCount() : 0;
    lv_obj_t *list = lv_obj_create(screenRoot_);
    lv_obj_set_pos(list, 1, 34);
    lv_obj_set_size(list, 228, 196);
    lv_obj_set_style_pad_all(list, 4, 0);
    lv_obj_set_style_pad_row(list, 3, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    size_t shown = 0;
    const uint32_t high = storage_ ? storage_->lxmfMessageCount() : 0;
    const uint32_t floor = high > rw::storage::StorageService::kMaxRetainedMessages
        ? high - rw::storage::StorageService::kMaxRetainedMessages : 0;
    for (uint32_t seq = high; seq > floor && shown < kVisibleThreads; --seq) {
        rw::storage::LxmfMessageRecord m{};
        if (!storage_->loadLxmfMessage(seq, m) || m.outgoing) continue;
        lv_obj_t *button = lv_button_create(list);
        threadButtons_[shown] = button;
        memcpy(threadHashes_[shown], m.sourceHash, 16);
        lv_obj_set_size(button, 212, 43);
        lv_obj_add_event_cb(button, buttonHapticEvent, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(button, inboxMessageEvent, LV_EVENT_CLICKED, this);
        char name[34]{}; displayPeerName(m.sourceHash, name, sizeof(name));
        char t[12]{}; formatMessageTime(m.timestamp, t, sizeof(t), false);
        char pv[52]{}; previewText(m.content, pv, sizeof(pv));
        char line[104]{};
        snprintf(line, sizeof(line), "%s%s  %s\n%s", m.read ? "" : "* ", name, t, pv);
        lv_obj_t *label = lv_label_create(button);
        lv_obj_set_width(label, 198);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
        lv_label_set_text(label, line);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
        ++shown;
    }
    if (shown == 0) {
        lv_obj_t *empty = lv_label_create(list);
        lv_obj_set_width(empty, 202);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(empty, "No incoming messages yet.");
    }
    Serial.printf("[m4:nav] inbox opened messages=%u unread=%lu\n",
                  static_cast<unsigned>(shown),
                  static_cast<unsigned long>(storage_ ? storage_->unreadLxmfCount() : 0));
}

void WatchUI::showConversations() {
    screen_ = Screen::Conversations;
    haveSelectedPeer_ = false;
    prepareScreen();
    makeHeader("MESSAGES", Screen::Menu);
    displayedMessageCount_ = storage_ ? storage_->lxmfMessageCount() : 0;
    displayedUnreadCount_ = storage_ ? storage_->unreadLxmfCount() : 0;
    ConversationSummary summaries[kVisibleThreads]{};
    const size_t count = buildConversationSummaries(summaries, kVisibleThreads);
    lv_obj_t *list = lv_obj_create(screenRoot_);
    lv_obj_set_pos(list, 1, 34);
    lv_obj_set_size(list, 228, 196);
    lv_obj_set_style_pad_all(list, 4, 0);
    lv_obj_set_style_pad_row(list, 3, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    if (count == 0) {
        lv_obj_t *empty = lv_label_create(list);
        lv_obj_set_width(empty, 202);
        lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(empty, "No conversations yet.\nUse PEERS or COMPOSE from the menu.");
    }
    for (size_t i = 0; i < count; ++i) {
        lv_obj_t *button = lv_button_create(list);
        threadButtons_[i] = button;
        memcpy(threadHashes_[i], summaries[i].peerHash, 16);
        lv_obj_set_size(button, 212, 43);
        lv_obj_add_event_cb(button, buttonHapticEvent, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(button, conversationEvent, LV_EVENT_CLICKED, this);
        char name[34]{}; displayPeerName(summaries[i].peerHash, name, sizeof(name));
        char t[12]{}; formatMessageTime(summaries[i].latestTimestamp, t, sizeof(t), false);
        char line[104]{};
        snprintf(line, sizeof(line), "%s%s  %s %s\n%s",
                 summaries[i].unread ? "* " : "", name, t,
                 summaries[i].latestOutgoing ? ">" : "<", summaries[i].preview);
        lv_obj_t *label = lv_label_create(button);
        lv_obj_set_width(label, 198);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
        lv_label_set_text(label, line);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    }
}

void WatchUI::markSelectedConversationRead() {
    if (!storage_ || !haveSelectedPeer_) return;
    const uint32_t high = storage_->lxmfMessageCount();
    const uint32_t floor = high > rw::storage::StorageService::kMaxRetainedMessages
        ? high - rw::storage::StorageService::kMaxRetainedMessages : 0;
    uint32_t marked = 0;
    for (uint32_t seq = high; seq > floor; --seq) {
        rw::storage::LxmfMessageRecord m{};
        if (!storage_->loadLxmfMessage(seq, m) || m.outgoing || m.read) continue;
        if (memcmp(m.sourceHash, selectedPeer_, 16) == 0 && storage_->markLxmfMessageRead(seq)) ++marked;
    }
    if (marked) Serial.printf("[m4:notify] conversation opened; marked %lu read\n", static_cast<unsigned long>(marked));
}

void WatchUI::showThread() {
    if (!haveSelectedPeer_) { showConversations(); return; }
    markSelectedConversationRead();
    screen_ = Screen::Thread;
    prepareScreen();
    displayedMessageCount_ = storage_ ? storage_->lxmfMessageCount() : 0;
    displayedUnreadCount_ = storage_ ? storage_->unreadLxmfCount() : 0;
    displayedDeliveryEvents_ = lxmf_ ? lxmf_->deliveredMessages() + lxmf_->retriedPackets() : 0;
    backTarget_ = threadReturnTarget_;
    lv_obj_t *back = makeButton(screenRoot_, "<", 36, 29, backEvent, this);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 0, 0);
    char name[34]{}; displayPeerName(selectedPeer_, name, sizeof(name));
    lv_obj_t *title = lv_label_create(screenRoot_);
    // RC1: conversation headers prioritise the peer name. Peer-management
    // actions live on Peer Details, reached through the compact INFO button.
    lv_obj_set_width(title, 156);
    lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(title, strlen(name) > 16 ? &lv_font_montserrat_10 : &lv_font_montserrat_12, 0);
    lv_label_set_text(title, name);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_t *info = makeButton(screenRoot_, "i", 36, 29, threadInfoEvent, this);
    lv_obj_align(info, LV_ALIGN_TOP_RIGHT, 0, 0);

    latestThreadVoiceSequence_ = 0;
    lv_obj_t *history = lv_obj_create(screenRoot_);
    lv_obj_set_pos(history, 1, 34);
    lv_obj_set_size(history, 228, 155);
    lv_obj_set_style_pad_all(history, 5, 0);
    // Every conversation uses a true-black chat surface, independent of theme.
    lv_obj_set_style_bg_color(history, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(history, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(history, lv_color_hex(0x444444), LV_PART_MAIN);
    lv_obj_set_style_text_color(history, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_scroll_dir(history, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(history, LV_SCROLLBAR_MODE_AUTO);
    uint32_t sequences[kThreadMessages]{};
    size_t n = 0;
    const uint32_t high = storage_ ? storage_->lxmfMessageCount() : 0;
    const uint32_t floor = high > rw::storage::StorageService::kMaxRetainedMessages
        ? high - rw::storage::StorageService::kMaxRetainedMessages : 0;
    for (uint32_t seq = high; seq > floor && n < kThreadMessages; --seq) {
        rw::storage::LxmfMessageRecord m{};
        if (!storage_->loadLxmfMessage(seq, m)) continue;
        const uint8_t *h = m.outgoing ? m.destinationHash : m.sourceHash;
        if (memcmp(h, selectedPeer_, 16) == 0) {
            sequences[n++] = seq;
            if (latestThreadVoiceSequence_ == 0 && storage_->hasLxmfAudio(seq))
                latestThreadVoiceSequence_ = seq;
        }
    }
    if (latestThreadVoiceSequence_ != 0) lv_obj_set_height(history, 122);
    lv_obj_t *text = lv_label_create(history);
    lv_obj_set_width(text, 206);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(text, &lv_font_montserrat_10, 0);
    String combined;
    if (n == 0) combined = "No messages yet.\nUse REPLY to start this conversation.";
    for (size_t idx = n; idx > 0; --idx) {
        rw::storage::LxmfMessageRecord m{};
        if (!storage_->loadLxmfMessage(sequences[idx - 1], m)) continue;
        char t[12]{}; formatMessageTime(m.timestamp, t, sizeof(t), false);
        char pv[56]{};
        if (storage_ && storage_->hasLxmfAudio(m.sequence))
            snprintf(pv, sizeof(pv), "[VOICE] %s", m.content[0] ? m.content : "audio message");
        else previewText(m.content, pv, sizeof(pv));
        char state = ' ';
        if (m.outgoing) {
            if (m.deliveryState == rw::storage::LxmfDeliveryState::Delivered) state = 'D';
            else if (m.deliveryState == rw::storage::LxmfDeliveryState::Failed) state = '!';
            else if (m.deliveryState == rw::storage::LxmfDeliveryState::Retrying) state = 'R';
            else state = 'S';
        }
        char line[100]{};
        snprintf(line, sizeof(line), "%s %s %c  %s\n%s\n", m.outgoing ? ">" : "<", t, state, pv,
                 m.outgoing && m.deliveryNote[0] ? m.deliveryNote : "");
        combined += line;
    }
    lv_label_set_text(text, combined.c_str());
    lv_obj_set_style_text_color(text, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    if (latestThreadVoiceSequence_ != 0) {
        lv_obj_t *voicePlay = makeButton(screenRoot_, "PLAY LATEST VOICE", 146, 27, voicePlaybackEvent, this);
        lv_obj_set_pos(voicePlay, 47, 161);
    }
    // RC1: tiny quick-reply buttons use stable numeric labels. The configured
    // reply text remains editable under Messaging settings.
    if (userProfile_.quickReply1[0]) {
        lv_obj_t *c1 = makeButton(screenRoot_, "1", 34, 34, cannedReply1Event, this);
        lv_obj_set_pos(c1, 1, 201);
    }
    if (userProfile_.quickReply2[0]) {
        lv_obj_t *c2 = makeButton(screenRoot_, "2", 34, 34, cannedReply2Event, this);
        lv_obj_set_pos(c2, 37, 201);
    }
    // v1.6.1: remove the old custom LXMF PTT-burst experiment. Retain the
    // interoperable recorded LXMF VOICE workflow; real PTT belongs to LXST.
    threadVoiceStatusLabel_ = lv_label_create(screenRoot_);
    lv_obj_set_pos(threadVoiceStatusLabel_, 2, 189);
    lv_obj_set_size(threadVoiceStatusLabel_, 226, 11);
    lv_label_set_long_mode(threadVoiceStatusLabel_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(threadVoiceStatusLabel_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(threadVoiceStatusLabel_, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(threadVoiceStatusLabel_, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_t *voice = makeButton(screenRoot_, "VOICE", 68, 34, voiceComposeEvent, this);
    lv_obj_set_pos(voice, 73, 201);
    lv_obj_t *reply = makeButton(screenRoot_, "REPLY", 86, 34, replyEvent, this);
    lv_obj_set_pos(reply, 143, 201);
    applyConversationTextContrast(screenRoot_);
    updateThreadVoiceStatus();
    Serial.printf("[m4:nav] thread opened messages_shown=%u\n", static_cast<unsigned>(n));
}

void WatchUI::configureKeyboard(lv_obj_t *keyboard, lv_obj_t *textarea) {
    lv_obj_set_size(keyboard, 230, 146);
    lv_obj_set_style_pad_all(keyboard, 2, 0);
    lv_obj_set_style_pad_row(keyboard, 2, 0);
    lv_obj_set_style_pad_column(keyboard, 2, 0);
    lv_obj_set_style_text_font(keyboard, &lv_font_montserrat_14, LV_PART_ITEMS);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER, kWatchKeyboardLower, kWatchKeyboardTextCtrl);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_UPPER, kWatchKeyboardUpper, kWatchKeyboardTextCtrl);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_SPECIAL, kWatchKeyboardSpecial, kWatchKeyboardSpecialCtrl);
    lv_keyboard_set_popovers(keyboard, true);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(keyboard, textarea);
    lv_obj_add_state(textarea, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(keyboard, keyboardHapticEvent, LV_EVENT_VALUE_CHANGED, this);
}

void WatchUI::showCompose(const char *prefill) {
    if (!haveSelectedPeer_ || !lxmf_ || !lxmf_->canSendTo(selectedPeer_)) return;
    screen_ = Screen::Compose;
    prepareScreen();
    makeHeader("COMPOSE", Screen::Thread);
    lv_obj_t *send = makeButton(screenRoot_, "SEND", 58, 28, sendEvent, this);
    lv_obj_align(send, LV_ALIGN_TOP_RIGHT, -40, 0);
    char name[34]{}; displayPeerName(selectedPeer_, name, sizeof(name));
    composeTextArea_ = lv_textarea_create(screenRoot_);
    lv_obj_set_pos(composeTextArea_, 1, 31);
    lv_obj_set_size(composeTextArea_, 228, 44);
    lv_textarea_set_placeholder_text(composeTextArea_, name);
    lv_textarea_set_max_length(composeTextArea_, 220);
    lv_obj_set_style_text_font(composeTextArea_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_bg_color(composeTextArea_, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(composeTextArea_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(composeTextArea_, lv_color_hex(0x444444), LV_PART_MAIN);
    lv_obj_set_style_text_color(composeTextArea_, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    if (prefill) lv_textarea_set_text(composeTextArea_, prefill);
    composeStatusLabel_ = lv_label_create(screenRoot_);
    lv_obj_set_pos(composeStatusLabel_, 1, 77);
    lv_obj_set_width(composeStatusLabel_, 228);
    lv_obj_set_style_text_align(composeStatusLabel_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(composeStatusLabel_, &lv_font_montserrat_10, 0);
    lv_label_set_text(composeStatusLabel_, "220 chars max");
    // Set header/buttons/status to white before adding the independently styled keyboard.
    applyConversationTextContrast(screenRoot_);
    lv_obj_t *keyboard = lv_keyboard_create(screenRoot_);
    configureKeyboard(keyboard, composeTextArea_);
    Serial.println("[m4:nav] composer opened");
}

void WatchUI::showContacts() {
    screen_ = Screen::Contacts;
    prepareScreen();
    makeHeader("CONTACTS", Screen::Menu);
    rw::storage::PeerRecord all[kVisiblePeers]{};
    const size_t total = storage_ ? storage_->listContacts(all, kVisiblePeers) : 0;
    lv_obj_t *list = lv_obj_create(screenRoot_);
    lv_obj_set_pos(list, 1, 34);
    lv_obj_set_size(list, 228, 196);
    lv_obj_set_style_pad_all(list, 4, 0);
    lv_obj_set_style_pad_row(list, 3, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    size_t shown = 0;
    for (size_t i = 0; i < total && shown < kVisiblePeers; ++i) {
        lv_obj_t *button = lv_button_create(list);
        peerButtons_[shown] = button;
        memcpy(peerHashes_[shown], all[i].destinationHash, 16);
        lv_obj_set_size(button, 212, 44);
        lv_obj_add_event_cb(button, buttonHapticEvent, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(button, contactEvent, LV_EVENT_CLICKED, this);
        char name[34]{}; displayPeerName(all[i].destinationHash, name, sizeof(name));
        char h[13]{}; shortHash(all[i].destinationHash, h);
        char line[96]{};
        snprintf(line, sizeof(line), "%s%s\n%s  %s", all[i].favorite ? "* " : "", name, h,
                 all[i].verifiedLxmf ? "VERIFIED" : "");
        lv_obj_t *label = lv_label_create(button);
        lv_obj_set_width(label, 198);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
        lv_label_set_text(label, line);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
        ++shown;
    }
    if (shown == 0) {
        lv_obj_t *empty = lv_label_create(list);
        lv_obj_set_width(empty, 202);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(empty, "No saved contacts.\nOpen a peer or conversation and use NAME / favorite.");
    }
    Serial.printf("[m4:nav] contacts opened count=%u\n", static_cast<unsigned>(shown));
}

void WatchUI::showPeers() {
    screen_ = Screen::Peers;
    displayedDiscoveredCount_ = lxmf_ ? lxmf_->discoveredPeers() : 0;
    prepareScreen();
    makeHeader("PEERS", Screen::Menu);
    lv_obj_t *nw = makeButton(screenRoot_, "NEW", 44, 28, newHashEvent, this);
    lv_obj_align(nw, LV_ALIGN_TOP_RIGHT, -40, 0);
    rw::storage::PeerRecord peers[kVisiblePeers]{};
    const size_t count = (userProfile_.peerDiscoveryVisible && storage_) ? storage_->listPeers(peers, kVisiblePeers) : 0;
    lv_obj_t *list = lv_obj_create(screenRoot_);
    lv_obj_set_pos(list, 1, 34);
    lv_obj_set_size(list, 228, 196);
    lv_obj_set_style_pad_all(list, 4, 0);
    lv_obj_set_style_pad_row(list, 3, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    if (count == 0) {
        lv_obj_t *empty = lv_label_create(list);
        lv_obj_set_width(empty, 202);
        lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(empty, userProfile_.peerDiscoveryVisible ?
            "No peers cached yet.\nUse NEW or wait for lxmf.delivery announces." :
            "Peer discovery UI is disabled in Settings -> Reticulum.\nManual destination entry is still available.");
    }
    for (size_t i = 0; i < count; ++i) {
        lv_obj_t *button = lv_button_create(list);
        peerButtons_[i] = button;
        memcpy(peerHashes_[i], peers[i].destinationHash, 16);
        lv_obj_set_size(button, 212, 44);
        lv_obj_add_event_cb(button, buttonHapticEvent, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(button, peerEvent, LV_EVENT_CLICKED, this);
        char name[34]{}; displayPeerName(peers[i].destinationHash, name, sizeof(name));
        char h[13]{}; shortHash(peers[i].destinationHash, h);
        char age[20]{}; formatPeerAge(peers[i].lastSeen, age, sizeof(age));
        char line[112]{};
        snprintf(line, sizeof(line), "%s%s  %s\n%s h=%u seen %s %s", peers[i].favorite ? "* " : "", name, h,
                 peers[i].savedContact ? "CONTACT" : (peers[i].discovered ? "DISCOVERED" : "HISTORY"),
                 peers[i].hops, age, peers[i].verifiedLxmf ? "VERIFIED" : "");
        lv_obj_t *label = lv_label_create(button);
        lv_obj_set_width(label, 198);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
        lv_label_set_text(label, line);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    }
    Serial.printf("[m4:nav] peers opened count=%u\n", static_cast<unsigned>(count));
}

void WatchUI::showPeerDetails() {
    if (!haveSelectedPeer_) { showPeers(); return; }
    screen_ = Screen::PeerDetails;
    prepareScreen();
    makeHeader("PEER DETAILS", peerDetailsReturnTarget_);

    rw::storage::PeerRecord peer{};
    const bool havePeer = storage_ && storage_->loadPeer(selectedPeer_, peer);
    char name[34]{}; displayPeerName(selectedPeer_, name, sizeof(name));
    char hash[33]{}; fullHash(selectedPeer_, hash);
    char seen[28]{}; formatPeerAge(havePeer ? peer.lastSeen : 0.0, seen, sizeof(seen));
    char seenUtc[28]{}; formatMessageTime(havePeer ? peer.lastSeen : 0.0, seenUtc, sizeof(seenUtc), true);
    const bool activeLink = lxmf_ && lxmf_->canReplyTo(selectedPeer_);
    const bool sendReady = lxmf_ && lxmf_->canSendTo(selectedPeer_);

    lv_obj_t *card = lv_obj_create(screenRoot_);
    lv_obj_set_pos(card, 2, 35);
    lv_obj_set_size(card, 226, 153);
    lv_obj_set_style_pad_all(card, 7, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);

    lv_obj_t *label = lv_label_create(card);
    lv_obj_set_width(label, 208);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
    char text[430]{};
    snprintf(text, sizeof(text),
             "%s%s\n%s\n\nHops: %u   Last seen: %s\n%s\nLXMF: %s   Active Link: %s\nSource: %s%s%s\nPinned: %s (local only)",
             havePeer && peer.favorite ? "* " : "", name, hash,
             havePeer ? peer.hops : 0, seen, seenUtc,
             havePeer && peer.verifiedLxmf ? "verified" : (sendReady ? "sendable" : "unknown"),
             activeLink ? "YES" : "no",
             havePeer && peer.savedContact ? "CONTACT" : "",
             havePeer && peer.savedContact && peer.discovered ? " + " : "",
             havePeer && peer.discovered ? "ANNOUNCE" : (!havePeer || !peer.savedContact ? "HISTORY/MANUAL" : ""),
             havePeer && peer.favorite ? "YES" : "no");
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *chat = makeButton(screenRoot_, "CHAT", 54, 34, peerChatEvent, this);
    lv_obj_set_pos(chat, 2, 199);
    lv_obj_t *nameBtn = makeButton(screenRoot_, "NAME", 54, 34, peerNameEvent, this);
    lv_obj_set_pos(nameBtn, 59, 199);
    lv_obj_t *fav = makeButton(screenRoot_, havePeer && peer.favorite ? "PIN*" : "PIN", 54, 34, peerFavoriteEvent, this);
    lv_obj_set_pos(fav, 116, 199);
    lv_obj_t *qr = makeButton(screenRoot_, "QR", 54, 34, peerQrEvent, this);
    lv_obj_set_pos(qr, 173, 199);

    applyConversationTextContrast(screenRoot_);
    Serial.printf("[v1.3:peer] details name=%s hops=%u last_seen=%.0f active_link=%s sendable=%s verified=%s\n",
                  name, havePeer ? peer.hops : 0, havePeer ? peer.lastSeen : 0.0,
                  activeLink ? "yes" : "no", sendReady ? "yes" : "no",
                  havePeer && peer.verifiedLxmf ? "yes" : "no");
}

void WatchUI::showNamePeer() {
    if (!haveSelectedPeer_) return;
    screen_ = Screen::NamePeer;
    prepareScreen();
    makeHeader("CONTACT NAME", namePeerReturnTarget_);
    lv_obj_t *save = makeButton(screenRoot_, "SAVE", 52, 28, saveNameEvent, this);
    lv_obj_align(save, LV_ALIGN_TOP_RIGHT, -40, 0);
    composeTextArea_ = lv_textarea_create(screenRoot_);
    lv_obj_set_pos(composeTextArea_, 1, 35);
    lv_obj_set_size(composeTextArea_, 228, 40);
    lv_textarea_set_max_length(composeTextArea_, 32);
    rw::storage::PeerRecord peer{};
    if (storage_ && storage_->loadPeer(selectedPeer_, peer) && peer.displayName[0])
        lv_textarea_set_text(composeTextArea_, peer.displayName);
    composeStatusLabel_ = lv_label_create(screenRoot_);
    lv_obj_set_pos(composeStatusLabel_, 1, 77);
    lv_obj_set_width(composeStatusLabel_, 228);
    lv_obj_set_style_text_align(composeStatusLabel_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(composeStatusLabel_, &lv_font_montserrat_10, 0);
    lv_label_set_text(composeStatusLabel_, "Saved names persist across reboot");
    lv_obj_t *keyboard = lv_keyboard_create(screenRoot_);
    configureKeyboard(keyboard, composeTextArea_);
}

void WatchUI::showHashEntry() {
    screen_ = Screen::HashEntry;
    prepareScreen();
    makeHeader("DESTINATION", Screen::Menu);
    lv_obj_t *go = makeButton(screenRoot_, "GO", 44, 28, hashConfirmEvent, this);
    lv_obj_align(go, LV_ALIGN_TOP_RIGHT, -40, 0);
    composeTextArea_ = lv_textarea_create(screenRoot_);
    lv_obj_set_pos(composeTextArea_, 1, 35);
    lv_obj_set_size(composeTextArea_, 228, 40);
    lv_textarea_set_max_length(composeTextArea_, 32);
    lv_textarea_set_placeholder_text(composeTextArea_, "32 hex characters");
    composeStatusLabel_ = lv_label_create(screenRoot_);
    lv_obj_set_pos(composeStatusLabel_, 1, 77);
    lv_obj_set_width(composeStatusLabel_, 228);
    lv_obj_set_style_text_align(composeStatusLabel_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(composeStatusLabel_, &lv_font_montserrat_10, 0);
    lv_label_set_text(composeStatusLabel_, "Enter lxmf.delivery destination hash");
    lv_obj_t *keyboard = lv_keyboard_create(screenRoot_);
    configureKeyboard(keyboard, composeTextArea_);
}


void WatchUI::showIdentity() {
    const Screen returnTarget = screen_ == Screen::Settings ? Screen::Settings : Screen::UserSettings;
    screen_ = Screen::Identity;
    prepareScreen();
    makeHeader("IDENTITY", returnTarget);
    const char *identity = reticulum_ ? reticulum_->identityHash() : "unavailable";
    const char *address = lxmf_ ? lxmf_->deliveryHash() : "unavailable";
    lv_obj_t *body = lv_label_create(screenRoot_);
    lv_obj_set_pos(body, 7, 39);
    lv_obj_set_width(body, 216);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(body, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(body, "%s\n\nIDENTITY HASH\n%s\n\nLXMF ADDRESS\n%s",
                          userProfile_.displayName, identity, address);
    lv_obj_t *addr = makeButton(screenRoot_, "ADDRESS QR", 108, 32, addressQrEvent, this);
    lv_obj_set_pos(addr, 4, 188);
    lv_obj_t *id = makeButton(screenRoot_, "IDENTITY QR", 108, 32, identityQrEvent, this);
    lv_obj_set_pos(id, 122, 188);
    Serial.println("[m7:identity] identity screen opened");
}

void WatchUI::showQr(const char *title, const char *payload, Screen returnTarget) {
    char titleCopy[sizeof(qrTitle_)]{};
    char payloadCopy[sizeof(qrPayload_)]{};
    strncpy(titleCopy, title ? title : "QR", sizeof(titleCopy)-1);
    strncpy(payloadCopy, payload ? payload : "", sizeof(payloadCopy)-1);
    screen_ = Screen::Qr;
    prepareScreen();
    backTarget_ = returnTarget;
    strncpy(qrTitle_, titleCopy, sizeof(qrTitle_)-1);
    qrTitle_[sizeof(qrTitle_)-1] = '\0';
    strncpy(qrPayload_, payloadCopy, sizeof(qrPayload_)-1);
    qrPayload_[sizeof(qrPayload_)-1] = '\0';
    makeHeader(qrTitle_, returnTarget);
#if LV_USE_QRCODE
    lv_obj_t *qr = lv_qrcode_create(screenRoot_);
    lv_qrcode_set_size(qr, 148);
    lv_qrcode_set_dark_color(qr, lv_color_hex(0x000000));
    lv_qrcode_set_light_color(qr, lv_color_hex(0xFFFFFF));
    lv_qrcode_update(qr, qrPayload_, static_cast<uint32_t>(strlen(qrPayload_)));
    lv_obj_align(qr, LV_ALIGN_TOP_MID, 0, 34);
#else
    lv_obj_t *missing = lv_label_create(screenRoot_);
    lv_label_set_text(missing, "QR support unavailable");
    lv_obj_align(missing, LV_ALIGN_CENTER, 0, -10);
#endif
    lv_obj_t *text = lv_label_create(screenRoot_);
    lv_obj_set_width(text, 222);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(text, &lv_font_montserrat_10, 0);
    lv_label_set_text(text, qrPayload_);
    lv_obj_align(text, LV_ALIGN_BOTTOM_MID, 0, -4);
    Serial.printf("[m7:qr] showing %s payload=%s\n", qrTitle_, qrPayload_);
}

void WatchUI::saveUserProfile() {
    if (!storage_) return;
    if (storage_->saveUserProfile(userProfile_)) {
        if (hardware_) hardware_->setBrightness(userProfile_.brightness);
    }
}

void WatchUI::showUserSettings() {
    screen_ = Screen::UserSettings;
    prepareScreen();
    makeHeader("USER", Screen::Settings);
    lv_obj_t *name = lv_label_create(screenRoot_);
    lv_obj_set_width(name, 214);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
    lv_label_set_text_fmt(name, "%s", userProfile_.displayName);
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_t *edit = makeButton(screenRoot_, "EDIT NAME", 108, 34, editDisplayNameEvent, this);
    lv_obj_set_pos(edit, 4, 96);
    lv_obj_t *identity = makeButton(screenRoot_, "IDENTITY", 108, 34, identityEvent, this);
    lv_obj_set_pos(identity, 122, 96);
    lv_obj_t *hint = lv_label_create(screenRoot_);
    lv_obj_set_width(hint, 210);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_label_set_text(hint, "Identity tools expose public hashes only. Private Reticulum key material is never shown as a QR code.");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -22);
}

void WatchUI::showMessagingSettings() {
    screen_ = Screen::MessagingSettings;
    prepareScreen();
    makeHeader("MESSAGING", Screen::Settings);
    char notify[32]{}; snprintf(notify, sizeof(notify), "NOTIFY: %s", userProfile_.notificationsEnabled ? "ON" : "OFF");
    lv_obj_t *n = makeButton(screenRoot_, notify, 226, 30, notificationToggleEvent, this); lv_obj_set_pos(n, 2, 38);

    char c1[48]{};
    char c2[48]{};
    snprintf(c1, sizeof(c1), "CANNED 1: %.18s", userProfile_.quickReply1[0] ? userProfile_.quickReply1 : "<unset>");
    snprintf(c2, sizeof(c2), "CANNED 2: %.18s", userProfile_.quickReply2[0] ? userProfile_.quickReply2 : "<unset>");
    lv_obj_t *q1 = makeButton(screenRoot_, c1, 226, 32, editCanned1Event, this); lv_obj_set_pos(q1, 2, 74);
    lv_obj_t *q2 = makeButton(screenRoot_, c2, 226, 32, editCanned2Event, this); lv_obj_set_pos(q2, 2, 112);

    lv_obj_t *wipe = makeButton(screenRoot_, "WIPE CHAT HISTORY", 226, 34, wipeHistoryPromptEvent, this);
    lv_obj_set_pos(wipe, 2, 154);

    lv_obj_t *body = lv_label_create(screenRoot_);
    lv_obj_set_pos(body, 8, 194);
    lv_obj_set_width(body, 214);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(body, &lv_font_montserrat_10, 0);
    lv_label_set_text(body, "Tap a canned reply to edit it. Blank slots are hidden from conversations and notifications.");
}

void WatchUI::showHistoryWipeConfirm() {
    screen_ = Screen::HistoryWipeConfirm;
    prepareScreen();
    makeHeader("WIPE HISTORY", Screen::MessagingSettings);
    lv_obj_t *body = lv_label_create(screenRoot_);
    lv_obj_set_width(body, 212);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(body, &lv_font_montserrat_12, 0);
    lv_label_set_text(body, "Delete all chat messages, delivery/read state and voice-message sidecars?\n\nIdentity, settings, contacts and peers are preserved.\n\nThe watch will reboot.");
    lv_obj_align(body, LV_ALIGN_CENTER, 0, -18);
    lv_obj_t *cancel = makeButton(screenRoot_, "CANCEL", 92, 38, wipeHistoryCancelEvent, this);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 4, -10);
    lv_obj_t *wipe = makeButton(screenRoot_, "WIPE", 92, 38, wipeHistoryConfirmEvent, this);
    lv_obj_align(wipe, LV_ALIGN_BOTTOM_RIGHT, -4, -10);
}

void WatchUI::showWatchSettings() {
    screen_ = Screen::WatchSettings;
    prepareScreen();
    makeHeader("WATCH", Screen::Settings);
    lv_obj_t *label = lv_label_create(screenRoot_);
    lv_obj_set_width(label, 214);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
    lv_label_set_text_fmt(label, "Brightness: %u\nHaptics: %s", userProfile_.brightness, userProfile_.hapticsEnabled ? "ON" : "OFF");
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 43);
    lv_obj_t *low = makeButton(screenRoot_, "LOW", 66, 32, brightnessLowEvent, this); lv_obj_set_pos(low, 4, 82);
    lv_obj_t *med = makeButton(screenRoot_, "MED", 66, 32, brightnessMedEvent, this); lv_obj_set_pos(med, 82, 82);
    lv_obj_t *high = makeButton(screenRoot_, "HIGH", 66, 32, brightnessHighEvent, this); lv_obj_set_pos(high, 160, 82);
    char hap[32]{}; snprintf(hap, sizeof(hap), "HAPTICS %s", userProfile_.hapticsEnabled ? "ON" : "OFF");
    lv_obj_t *h = makeButton(screenRoot_, hap, 108, 34, hapticsToggleEvent, this); lv_obj_set_pos(h, 4, 124);
    lv_obj_t *power = makeButton(screenRoot_, "STANDBY", 108, 34, powerSettingsEvent, this); lv_obj_set_pos(power, 122, 124);
    lv_obj_t *note = lv_label_create(screenRoot_);
    lv_obj_set_width(note, 210); lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_style_text_font(note, &lv_font_montserrat_10, 0);
    lv_label_set_text(note, "Crown toggles the display. Touch no longer wakes a sleeping screen; optional lift-to-wake is under Standby.");
    lv_obj_align(note, LV_ALIGN_BOTTOM_MID, 0, -16);
}

void WatchUI::showReticulumSettings() {
    screen_ = Screen::ReticulumSettings;
    prepareScreen();
    makeHeader("RETICULUM", Screen::Settings);
    char announce[48]{}; snprintf(announce, sizeof(announce), "ANNOUNCE: %s", rw::config::user::announcePolicyName(userProfile_.announcePolicy));
    lv_obj_t *a = makeButton(screenRoot_, announce, 226, 34, announcePolicyEvent, this); lv_obj_set_pos(a, 2, 42);
    char peers[48]{}; snprintf(peers, sizeof(peers), "PEER DISCOVERY UI: %s", userProfile_.peerDiscoveryVisible ? "ON" : "OFF");
    lv_obj_t *p = makeButton(screenRoot_, peers, 226, 34, peerDiscoveryToggleEvent, this); lv_obj_set_pos(p, 2, 82);
    lv_obj_t *ifs = makeButton(screenRoot_, "INTERFACES", 108, 34, interfaceSettingsEvent, this); lv_obj_set_pos(ifs, 4, 124);
    lv_obj_t *route = makeButton(screenRoot_, "ROUTING", 108, 34, routingSettingsEvent, this); lv_obj_set_pos(route, 122, 124);
    lv_obj_t *note = lv_label_create(screenRoot_);
    lv_obj_set_width(note, 214); lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP); lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_style_text_font(note, &lv_font_montserrat_10, 0);
    lv_label_set_text(note, "Manual is the conservative default. EVERY 6H follows the official guidance for balanced application announces. Endpoint mode remains locked to the accepted baseline.");
    lv_obj_align(note, LV_ALIGN_BOTTOM_MID, 0, -8);
}

void WatchUI::showUserEdit() {
    screen_ = Screen::UserEdit;
    prepareScreen();
    // RC2: editor headers are intentionally terse. On the physical 240 px
    // display the SAVE control shares this row, so desktop-length labels such
    // as "CANNED REPLY 1" visibly collide with it.
    const char *title = userEditField_ == UserEditField::DisplayName ? "NAME" :
                        userEditField_ == UserEditField::CannedReply1 ? "REPLY 1" : "REPLY 2";
    makeHeader(title, userEditField_ == UserEditField::DisplayName ? Screen::UserSettings : Screen::MessagingSettings);
    lv_obj_t *save = makeButton(screenRoot_, "SAVE", 52, 28, userEditSaveEvent, this);
    lv_obj_align(save, LV_ALIGN_TOP_RIGHT, -40, 0);
    composeTextArea_ = lv_textarea_create(screenRoot_);
    lv_obj_set_pos(composeTextArea_, 1, 35);
    lv_obj_set_size(composeTextArea_, 228, 40);
    lv_textarea_set_max_length(composeTextArea_, userEditField_ == UserEditField::DisplayName ? 32 : 24);
    const char *current = userEditField_ == UserEditField::DisplayName ? userProfile_.displayName :
                          userEditField_ == UserEditField::CannedReply1 ? userProfile_.quickReply1 : userProfile_.quickReply2;
    lv_textarea_set_text(composeTextArea_, current);
    composeStatusLabel_ = lv_label_create(screenRoot_);
    lv_obj_set_pos(composeStatusLabel_, 1, 77); lv_obj_set_width(composeStatusLabel_, 228); lv_obj_set_style_text_align(composeStatusLabel_, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_style_text_font(composeStatusLabel_, &lv_font_montserrat_10, 0);
    lv_label_set_text(composeStatusLabel_, userEditField_ == UserEditField::DisplayName ? "Saved immediately" : "Blank = disabled");
    lv_obj_t *keyboard = lv_keyboard_create(screenRoot_);
    configureKeyboard(keyboard, composeTextArea_);
}

void WatchUI::showNetwork() {
    screen_ = Screen::Network;
    prepareScreen();
    makeHeader("NETWORK", Screen::Menu);
    displayedDiscoveredCount_ = lxmf_ ? lxmf_->discoveredPeers() : 0;
    const rw::radio::RadioStats stats = radio_ ? radio_->stats() : rw::radio::RadioStats{};
    char body[560]{};
    snprintf(body, sizeof(body),
             "LoRa: %s  %.3f MHz\nBW %lu kHz SF%u CR 4/%u TX %d dBm\n\nWi-Fi/TCP: %s\n%s\nTCP RX %lu TX %lu\n\nRNS: %s  %s\nRoute store: %s\nLoRa RX %lu TX %lu\nLXMF RX %lu TX %lu\nPeers %lu\nANN requests %lu",
             radio_ && radio_->ready() ? "READY" : "OFFLINE",
             radio_ ? radio_->frequencyHz() / 1000000.0 : 0.0,
             static_cast<unsigned long>(radio_ ? radio_->bandwidthHz() / 1000UL : 0),
             radio_ ? radio_->spreadingFactor() : 0,
             radio_ ? radio_->codingRate() : 0,
             radio_ ? radio_->txPowerDbm() : 0,
             transport_ && transport_->wifiEnabled() ? (transport_->tcpConnected() ? "ONLINE" : "ENABLED") : "OFF",
             transport_ ? transport_->status() : "not configured",
             static_cast<unsigned long>(transport_ ? transport_->tcpRxFrames() : 0),
             static_cast<unsigned long>(transport_ ? transport_->tcpTxFrames() : 0),
             reticulum_ && reticulum_->ready() ? "READY" : "OFFLINE",
             "ENDPOINT",
             "RAM",
             static_cast<unsigned long>(stats.assembledPackets),
             static_cast<unsigned long>(stats.txReticulumPackets),
             static_cast<unsigned long>(lxmf_ ? lxmf_->decodedMessages() : 0),
             static_cast<unsigned long>(lxmf_ ? lxmf_->sentMessages() : 0),
             static_cast<unsigned long>(lxmf_ ? lxmf_->discoveredPeers() : 0),
             static_cast<unsigned long>(lxmf_ ? lxmf_->announceRequests() : 0));
    lv_obj_t *label = lv_label_create(screenRoot_);
    lv_obj_set_pos(label, 6, 38);
    lv_obj_set_width(label, 218);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
    lv_label_set_text(label, body);
    lv_obj_t *ann = makeButton(screenRoot_, "ANNOUNCE", 108, 30, announceEvent, this);
    lv_obj_align(ann, LV_ALIGN_BOTTOM_LEFT, 3, -3);
    lv_obj_t *ifs = makeButton(screenRoot_, "INTERFACES", 108, 30, interfaceSettingsEvent, this);
    lv_obj_align(ifs, LV_ALIGN_BOTTOM_RIGHT, -3, -3);
    Serial.println("[m6:network] network diagnostics opened");
}

void WatchUI::showSettings() {
    screen_ = Screen::Settings;
    prepareScreen();
    makeHeader("SETTINGS", Screen::Menu);
    lv_obj_t *info = lv_label_create(screenRoot_);
    lv_obj_set_width(info, 210); lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_style_text_font(info, &lv_font_montserrat_10, 0);
    const bool pressure = storage_ && storage_->underPressure();
    const bool ramJournal = storage_ && storage_->ramJournalActive();
    lv_label_set_text_fmt(info, "%s | %lu KiB free%s%s", rw::config::kVersion,
                          static_cast<unsigned long>(storage_ ? storage_->freeBytes()/1024ULL : 0),
                          pressure ? " | LOW STORAGE" : "",
                          ramJournal ? " | RAM JOURNAL" : "");
    lv_obj_align(info, LV_ALIGN_TOP_MID, 0, 39);
    lv_obj_t *user = makeButton(screenRoot_, "USER", 108, 34, userSettingsEvent, this); lv_obj_set_pos(user, 4, 66);
    lv_obj_t *msg = makeButton(screenRoot_, "MESSAGING", 108, 34, messagingSettingsEvent, this); lv_obj_set_pos(msg, 122, 66);
    lv_obj_t *watch = makeButton(screenRoot_, "WATCH", 108, 34, watchSettingsEvent, this); lv_obj_set_pos(watch, 4, 106);
    lv_obj_t *rns = makeButton(screenRoot_, "RETICULUM", 108, 34, reticulumSettingsEvent, this); lv_obj_set_pos(rns, 122, 106);
    lv_obj_t *radio = makeButton(screenRoot_, "RADIO", 108, 34, radioSettingsEvent, this); lv_obj_set_pos(radio, 4, 146);
    lv_obj_t *ifs = makeButton(screenRoot_, "INTERFACES", 108, 34, interfaceSettingsEvent, this); lv_obj_set_pos(ifs, 122, 146);
    lv_obj_t *identity = makeButton(screenRoot_, "IDENTITY", 108, 32, identityEvent, this); lv_obj_set_pos(identity, 4, 190);
    lv_obj_t *restart = makeButton(screenRoot_, "RESTART", 108, 32, restartPromptEvent, this); lv_obj_set_pos(restart, 122, 190);
    Serial.println("[m8:nav] settings opened");
}

void WatchUI::showPowerSettings() {
    screen_ = Screen::PowerSettings;
    prepareScreen();
    makeHeader("POWER", Screen::WatchSettings);
    const auto profile = power_ ? power_->profile() : rw::config::power::defaultProfile();
    const uint32_t activeMinutes = power_ ? power_->activeMs() / 60000UL : 0U;
    const uint32_t standbyMinutes = power_ ? power_->standbyMs() / 60000UL : 0U;
    const uint32_t crownWakes = power_ ? power_->crownWakeCount() : 0U;
    const uint32_t liftWakes = power_ ? power_->liftWakeCount() : 0U;
    const uint32_t standbyEntries = power_ ? power_->standbyEntryCount() : 0U;
    lv_obj_t *body = lv_label_create(screenRoot_);
    lv_obj_set_width(body, 218);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(body, &lv_font_montserrat_10, 0);
    lv_label_set_text_fmt(body, "CONNECTED STANDBY\nScreen off: %s | Lift: %s\n\nSession active %lum / standby %lum\nStandby entries %lu\nWake crown %lu / lift %lu\n\nLoRa + Reticulum stay online.",
                          rw::config::power::timeoutName(profile.displayTimeoutMs),
                          profile.liftToWake ? "ON" : "OFF",
                          static_cast<unsigned long>(activeMinutes),
                          static_cast<unsigned long>(standbyMinutes),
                          static_cast<unsigned long>(standbyEntries),
                          static_cast<unsigned long>(crownWakes),
                          static_cast<unsigned long>(liftWakes));
    lv_obj_align(body, LV_ALIGN_CENTER, 0, -35);
    lv_obj_t *timeout = makeButton(screenRoot_, "CHANGE TIMEOUT", 150, 34, powerTimeoutEvent, this);
    lv_obj_align(timeout, LV_ALIGN_BOTTOM_MID, 0, -56);
    char liftText[40]{};
    snprintf(liftText, sizeof(liftText), "LIFT TO WAKE %s", profile.liftToWake ? "ON" : "OFF");
    lv_obj_t *lift = makeButton(screenRoot_, liftText, 150, 34, liftToWakeToggleEvent, this);
    lv_obj_align(lift, LV_ALIGN_BOTTOM_MID, 0, -16);
    Serial.printf("[m12:power] settings opened timeout=%s lift_to_wake=%s crown=enabled touch_wake=disabled\n",
                  rw::config::power::timeoutName(profile.displayTimeoutMs), profile.liftToWake ? "ON" : "OFF");
}

void WatchUI::savePendingRadioProfile() {
    if (!storage_) return;
    if (storage_->saveRadioProfile(pendingRadioProfile_)) {
        const auto &active = radio_->profile();
        radioSettingsDirty_ = memcmp(&pendingRadioProfile_, &active, sizeof(active)) != 0;
        Serial.printf("[m4:radio] pending profile saved; reboot_required=%s\n", radioSettingsDirty_ ? "yes" : "no");
    }
}

void WatchUI::showRadioSettings() {
    screen_ = Screen::RadioSettings;
    prepareScreen();
    makeHeader("RADIO", Screen::Settings);
    if (storage_) storage_->loadRadioProfile(pendingRadioProfile_);
    if (!rw::config::radio::validProfile(pendingRadioProfile_)) pendingRadioProfile_ = radio_->profile();
    const auto &active = radio_->profile();
    radioSettingsDirty_ = memcmp(&pendingRadioProfile_, &active, sizeof(active)) != 0;
    struct Row { const char *name; char value[24]; lv_event_cb_t minus; lv_event_cb_t plus; } rows[6]{};
    snprintf(rows[0].value,sizeof(rows[0].value),"%.3f MHz",pendingRadioProfile_.frequencyHz/1000000.0); rows[0].name="FREQ"; rows[0].minus=radioFreqDownEvent; rows[0].plus=radioFreqUpEvent;
    snprintf(rows[1].value,sizeof(rows[1].value),"%lu kHz",static_cast<unsigned long>(pendingRadioProfile_.bandwidthHz/1000UL)); rows[1].name="BW"; rows[1].minus=radioBwEvent; rows[1].plus=radioBwEvent;
    snprintf(rows[2].value,sizeof(rows[2].value),"SF %u",pendingRadioProfile_.spreadingFactor); rows[2].name="SPREAD"; rows[2].minus=radioSfDownEvent; rows[2].plus=radioSfUpEvent;
    snprintf(rows[3].value,sizeof(rows[3].value),"4/%u",pendingRadioProfile_.codingRate); rows[3].name="CODING"; rows[3].minus=radioCrDownEvent; rows[3].plus=radioCrUpEvent;
    snprintf(rows[4].value,sizeof(rows[4].value),"%d dBm",pendingRadioProfile_.txPowerDbm); rows[4].name="TX"; rows[4].minus=radioTxDownEvent; rows[4].plus=radioTxUpEvent;
    snprintf(rows[5].value,sizeof(rows[5].value),"%u",pendingRadioProfile_.preambleSymbols); rows[5].name="PREAMBLE"; rows[5].minus=radioPreDownEvent; rows[5].plus=radioPreUpEvent;
    for (int i=0;i<6;++i) {
        const int y=35+i*31;
        lv_obj_t *minus=makeButton(screenRoot_,"-",32,27,rows[i].minus,this); lv_obj_set_pos(minus,2,y);
        lv_obj_t *plus=makeButton(screenRoot_,"+",32,27,rows[i].plus,this); lv_obj_set_pos(plus,196,y);
        lv_obj_t *label=lv_label_create(screenRoot_); lv_obj_set_width(label,156); lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0); lv_obj_set_style_text_font(label,&lv_font_montserrat_10,0);
        lv_label_set_text_fmt(label,"%s  %s",rows[i].name,rows[i].value); lv_obj_set_pos(label,40,y+7);
    }
    lv_obj_t *foot=lv_label_create(screenRoot_); lv_obj_set_width(foot,220); lv_obj_set_style_text_align(foot,LV_TEXT_ALIGN_CENTER,0); lv_obj_set_style_text_font(foot,&lv_font_montserrat_10,0);
    lv_label_set_text_fmt(foot,"sync 0x%02X  %s",pendingRadioProfile_.syncWord,radioSettingsDirty_?"REBOOT REQUIRED":"ACTIVE"); lv_obj_align(foot,LV_ALIGN_BOTTOM_MID,0,-1);
    Serial.printf("[m4:radio] settings opened active=%.3f pending=%.3f dirty=%s\n",active.frequencyHz/1000000.0,pendingRadioProfile_.frequencyHz/1000000.0,radioSettingsDirty_?"yes":"no");
}

void WatchUI::leaveRadioSettings(Screen target) {
    if (radioSettingsDirty_) { radioSettingsExitTarget_=target; showRadioRestartConfirm(); }
    else navigate(target);
}

void WatchUI::showRadioRestartConfirm() {
    screen_ = Screen::RadioRestartConfirm; prepareScreen(); makeHeader("APPLY RADIO", Screen::RadioSettings);
    lv_obj_t *body=lv_label_create(screenRoot_); lv_obj_set_width(body,210); lv_label_set_long_mode(body,LV_LABEL_LONG_WRAP); lv_obj_set_style_text_align(body,LV_TEXT_ALIGN_CENTER,0); lv_obj_set_style_text_font(body,&lv_font_montserrat_12,0);
    lv_label_set_text(body,"Radio settings are saved.\nA reboot is required to apply them."); lv_obj_align(body,LV_ALIGN_CENTER,0,-20);
    lv_obj_t *later=makeButton(screenRoot_,"LATER",88,36,radioApplyLaterEvent,this); lv_obj_align(later,LV_ALIGN_BOTTOM_LEFT,4,-12);
    lv_obj_t *reboot=makeButton(screenRoot_,"REBOOT",88,36,radioApplyRebootEvent,this); lv_obj_align(reboot,LV_ALIGN_BOTTOM_RIGHT,-4,-12);
}


void WatchUI::savePendingNetworkProfile() {
    if (!storage_) return;
    if (storage_->saveNetworkProfile(pendingNetworkProfile_)) {
        const auto active = transport_ ? transport_->profile() : rw::config::network::defaultProfile();
        networkSettingsDirty_ = memcmp(&pendingNetworkProfile_, &active, sizeof(active)) != 0;
        Serial.printf("[m5:net] pending interface profile saved; reboot_required=%s\n",
                      networkSettingsDirty_ ? "yes" : "no");
    }
}

void WatchUI::showInterfaceSettings() {
    screen_ = Screen::InterfaceSettings;
    prepareScreen();
    makeHeader("INTERFACES", Screen::Settings);
    pendingNetworkProfile_ = transport_ ? transport_->profile() : rw::config::network::defaultProfile();
    rw::config::network::Profile saved{};
    if (storage_ && storage_->loadNetworkProfile(saved)) pendingNetworkProfile_ = saved;
    const auto active = transport_ ? transport_->profile() : rw::config::network::defaultProfile();
    networkSettingsDirty_ = memcmp(&pendingNetworkProfile_, &active, sizeof(active)) != 0;

    lv_obj_t *mode = makeButton(screenRoot_, pendingNetworkProfile_.enabled ? "ENABLED" : "DISABLED", 108, 31, networkToggleEvent, this);
    lv_obj_set_pos(mode, 4, 38);
    lv_obj_t *role = makeButton(screenRoot_, rw::config::network::modeName(pendingNetworkProfile_.mode), 108, 31, networkModeEvent, this);
    lv_obj_set_pos(role, 122, 38);
    lv_obj_t *ssid = makeButton(screenRoot_, "SSID", 108, 31, networkEditSsidEvent, this);
    lv_obj_set_pos(ssid, 4, 75);
    lv_obj_t *pass = makeButton(screenRoot_, "PASSWORD", 108, 31, networkEditPasswordEvent, this);
    lv_obj_set_pos(pass, 122, 75);
    lv_obj_t *host = makeButton(screenRoot_, "HOST", 108, 31, networkEditHostEvent, this);
    lv_obj_set_pos(host, 4, 112);
    char portText[24]{}; snprintf(portText, sizeof(portText), "PORT %u", static_cast<unsigned>(pendingNetworkProfile_.port));
    lv_obj_t *port = makeButton(screenRoot_, portText, 108, 31, networkPortEvent, this);
    lv_obj_set_pos(port, 122, 112);

    char details[220]{};
    snprintf(details, sizeof(details), "SSID: %.24s\nHOST: %.30s\n%s%s",
             pendingNetworkProfile_.ssid[0] ? pendingNetworkProfile_.ssid : "(not set)",
             pendingNetworkProfile_.targetHost[0] ? pendingNetworkProfile_.targetHost : "(not set)",
             rw::config::network::readyToEnable(pendingNetworkProfile_) ? "READY" : "CONFIG INCOMPLETE",
             networkSettingsDirty_ ? "  |  REBOOT REQUIRED" : "");
    lv_obj_t *body = lv_label_create(screenRoot_);
    lv_obj_set_pos(body, 8, 153);
    lv_obj_set_width(body, 214);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(body, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(body, details);
    Serial.printf("[m5:net] interface settings mode=%s enabled=%s dirty=%s\n",
                  rw::config::network::modeName(pendingNetworkProfile_.mode),
                  pendingNetworkProfile_.enabled ? "yes" : "no", networkSettingsDirty_ ? "yes" : "no");
}

void WatchUI::showNetworkEdit() {
    screen_ = Screen::NetworkEdit;
    prepareScreen();
    const char *title = networkEditField_ == NetworkEditField::Ssid ? "WIFI SSID" :
                        networkEditField_ == NetworkEditField::Password ? "WIFI PASSWORD" : "TCP HOST";
    makeHeader(title, Screen::InterfaceSettings);
    lv_obj_t *save = makeButton(screenRoot_, "SAVE", 52, 28, networkEditSaveEvent, this);
    lv_obj_align(save, LV_ALIGN_TOP_RIGHT, -40, 0);
    composeTextArea_ = lv_textarea_create(screenRoot_);
    lv_obj_set_pos(composeTextArea_, 1, 35);
    lv_obj_set_size(composeTextArea_, 228, 40);
    lv_textarea_set_max_length(composeTextArea_, networkEditField_ == NetworkEditField::Ssid ? 32 : 64);
    if (networkEditField_ == NetworkEditField::Ssid) lv_textarea_set_text(composeTextArea_, pendingNetworkProfile_.ssid);
    else if (networkEditField_ == NetworkEditField::Password) {
        lv_textarea_set_text(composeTextArea_, pendingNetworkProfile_.password);
        lv_textarea_set_password_mode(composeTextArea_, true);
    } else lv_textarea_set_text(composeTextArea_, pendingNetworkProfile_.targetHost);
    composeStatusLabel_ = lv_label_create(screenRoot_);
    lv_obj_set_pos(composeStatusLabel_, 1, 77);
    lv_obj_set_width(composeStatusLabel_, 228);
    lv_obj_set_style_text_align(composeStatusLabel_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(composeStatusLabel_, &lv_font_montserrat_10, 0);
    lv_label_set_text(composeStatusLabel_, "Saved on flash; reboot applies interface changes");
    lv_obj_t *keyboard = lv_keyboard_create(screenRoot_);
    configureKeyboard(keyboard, composeTextArea_);
}

void WatchUI::leaveNetworkSettings(Screen target) {
    if (networkSettingsDirty_) { networkSettingsExitTarget_ = target; showNetworkRestartConfirm(); }
    else navigate(target);
}

void WatchUI::showNetworkRestartConfirm() {
    screen_ = Screen::NetworkRestartConfirm;
    prepareScreen();
    makeHeader("APPLY INTERFACE", Screen::InterfaceSettings);
    lv_obj_t *body = lv_label_create(screenRoot_);
    lv_obj_set_width(body, 210);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(body, &lv_font_montserrat_12, 0);
    lv_label_set_text(body, "Wi-Fi/TCP settings are saved.\nA reboot is required to start or stop the interface.");
    lv_obj_align(body, LV_ALIGN_CENTER, 0, -20);
    lv_obj_t *later = makeButton(screenRoot_, "LATER", 88, 36, networkApplyLaterEvent, this);
    lv_obj_align(later, LV_ALIGN_BOTTOM_LEFT, 4, -12);
    lv_obj_t *reboot = makeButton(screenRoot_, "REBOOT", 88, 36, networkApplyRebootEvent, this);
    lv_obj_align(reboot, LV_ALIGN_BOTTOM_RIGHT, -4, -12);
}


void WatchUI::savePendingTransportProfile() {
    // Deliberately disabled in M6-build3. Transport activation is held back
    // until it is validated against official Reticulum/microReticulum examples.
    routingSettingsDirty_ = false;
    Serial.println("[m6:recovery] Transport setting changes disabled; endpoint baseline locked");
}

void WatchUI::showRoutingSettings() {
    screen_ = Screen::RoutingSettings;
    prepareScreen();
    makeHeader("ROUTING", Screen::Settings);
    routingSettingsDirty_ = false;

    lv_obj_t *body = lv_label_create(screenRoot_);
    lv_obj_set_width(body, 214);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(body, &lv_font_montserrat_12, 0);
    lv_label_set_text(body,
        "Mode: ENDPOINT (LOCKED)\nRoute store: RAM\n\n"
        "M6 build3 restores the hardware-proven M5 Reticulum endpoint path. "
        "Transport mode is temporarily disabled while it is validated against "
        "official Reticulum and microReticulum transport examples.");
    lv_obj_align(body, LV_ALIGN_CENTER, 0, -4);
    Serial.println("[m6:recovery] routing page opened; endpoint baseline locked");
}

void WatchUI::leaveRoutingSettings(Screen target) {
    routingSettingsDirty_ = false;
    navigate(target);
}

void WatchUI::showRoutingRestartConfirm() {
    screen_ = Screen::RoutingRestartConfirm;
    prepareScreen();
    makeHeader("APPLY ROUTING", Screen::RoutingSettings);
    lv_obj_t *body = lv_label_create(screenRoot_);
    lv_obj_set_width(body, 210);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(body, &lv_font_montserrat_12, 0);
    lv_label_set_text(body, "Routing mode is saved.\nA reboot is required to safely rebuild Reticulum in the selected mode.");
    lv_obj_align(body, LV_ALIGN_CENTER, 0, -20);
    lv_obj_t *later = makeButton(screenRoot_, "LATER", 88, 36, routingApplyLaterEvent, this);
    lv_obj_align(later, LV_ALIGN_BOTTOM_LEFT, 4, -12);
    lv_obj_t *reboot = makeButton(screenRoot_, "REBOOT", 88, 36, routingApplyRebootEvent, this);
    lv_obj_align(reboot, LV_ALIGN_BOTTOM_RIGHT, -4, -12);
}

void WatchUI::showNotification(const rw::storage::LxmfMessageRecord &message) {
    screen_ = Screen::Notification;
    prepareScreen();
    memcpy(notificationPeer_, message.sourceHash, 16);
    haveNotificationPeer_ = true;
    notificationSequence_ = message.sequence;
    char name[34]{}; displayPeerName(message.sourceHash, name, sizeof(name));
    char preview[150]{}; previewText(message.content, preview, sizeof(preview));
    lv_obj_t *title = lv_label_create(screenRoot_);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_label_set_text(title, "NEW MESSAGE");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_t *sender = lv_label_create(screenRoot_);
    lv_obj_set_width(sender, 216);
    lv_obj_set_style_text_align(sender, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(sender, &lv_font_montserrat_12, 0);
    lv_label_set_text(sender, name);
    lv_obj_align(sender, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_t *body = lv_label_create(screenRoot_);
    lv_obj_set_width(body, 210);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(body, &lv_font_montserrat_12, 0);
    lv_label_set_text(body, preview);
    lv_obj_align(body, LV_ALIGN_CENTER, 0, -15);
    lv_obj_t *dismiss = makeButton(screenRoot_, "DISMISS", 74, 32, dismissNotificationEvent, this);
    lv_obj_align(dismiss, LV_ALIGN_BOTTOM_LEFT, 0, -44);
    lv_obj_t *open = makeButton(screenRoot_, "OPEN", 74, 32, openNotificationEvent, this);
    lv_obj_align(open, LV_ALIGN_BOTTOM_RIGHT, 0, -44);
    if (userProfile_.quickReply1[0]) {
        lv_obj_t *c1 = makeButton(screenRoot_, "1", 74, 32, notificationCanned1Event, this);
        lv_obj_align(c1, LV_ALIGN_BOTTOM_LEFT, 0, -6);
    }
    if (userProfile_.quickReply2[0]) {
        lv_obj_t *c2 = makeButton(screenRoot_, "2", 74, 32, notificationCanned2Event, this);
        lv_obj_align(c2, LV_ALIGN_BOTTOM_RIGHT, 0, -6);
    }
    Serial.printf("[m4:notify] popup sequence=%lu sender=%s preview=%.48s\n",
                  static_cast<unsigned long>(message.sequence), name, preview);
}

bool WatchUI::parseHashText(const char *text, uint8_t hash[16]) const {
    if (!text || strlen(text) != 32 || !hash) return false;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < 16; ++i) {
        const int a = nib(text[i * 2]), b = nib(text[i * 2 + 1]);
        if (a < 0 || b < 0) return false;
        hash[i] = static_cast<uint8_t>((a << 4) | b);
    }
    return true;
}

void WatchUI::showRestartConfirm() {
    screen_ = Screen::RestartConfirm;
    prepareScreen();
    makeHeader("RESTART?", Screen::Settings);
    lv_obj_t *body = lv_label_create(screenRoot_);
    lv_obj_set_width(body, 208);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(body, &lv_font_montserrat_12, 0);
    lv_label_set_text(body, "Software restart only.\n\nIdentity, history, contacts and metadata remain on flash.");
    lv_obj_align(body, LV_ALIGN_CENTER, 0, -8);
    lv_obj_t *restart = makeButton(screenRoot_, "RESTART", 110, 38, restartConfirmEvent, this);
    lv_obj_align(restart, LV_ALIGN_BOTTOM_MID, 0, -8);
}

void WatchUI::navigate(Screen target) {
    switch (target) {
        case Screen::WatchFace: showWatchFace(); break;
        case Screen::Menu: showMenu(); break;
        case Screen::Inbox: showInbox(); break;
        case Screen::Conversations: showConversations(); break;
        case Screen::Thread: showThread(); break;
        case Screen::Compose: showCompose(); break;
        case Screen::Contacts: showContacts(); break;
        case Screen::Peers: showPeers(); break;
        case Screen::PeerDetails: showPeerDetails(); break;
        case Screen::NamePeer: showNamePeer(); break;
        case Screen::HashEntry: showHashEntry(); break;
        case Screen::Identity: showIdentity(); break;
        case Screen::Qr: showQr(qrTitle_, qrPayload_, backTarget_); break;
        case Screen::Network: showNetwork(); break;
        case Screen::AudioLab: showAudioLab(); break;
        case Screen::Settings: showSettings(); break;
        case Screen::UserSettings: showUserSettings(); break;
        case Screen::MessagingSettings: showMessagingSettings(); break;
        case Screen::WatchSettings: showWatchSettings(); break;
        case Screen::ReticulumSettings: showReticulumSettings(); break;
        case Screen::UserEdit: showUserEdit(); break;
        case Screen::HistoryWipeConfirm: showHistoryWipeConfirm(); break;
        case Screen::PowerSettings: showPowerSettings(); break;
        case Screen::RadioSettings: showRadioSettings(); break;
        case Screen::RadioRestartConfirm: showRadioRestartConfirm(); break;
        case Screen::InterfaceSettings: showInterfaceSettings(); break;
        case Screen::NetworkEdit: showNetworkEdit(); break;
        case Screen::NetworkRestartConfirm: showNetworkRestartConfirm(); break;
        case Screen::RoutingSettings: showRoutingSettings(); break;
        case Screen::RoutingRestartConfirm: showRoutingRestartConfirm(); break;
        case Screen::RestartConfirm: showRestartConfirm(); break;
        case Screen::Notification: showWatchFace(); break;
    }
}

void WatchUI::checkForNotification() {
    if (!storage_) return;
    const uint32_t high = storage_->lxmfMessageCount();
    if (high <= lastObservedMessageCount_) return;
    rw::storage::LxmfMessageRecord newestIncoming{};
    bool found = false;
    for (uint32_t seq = lastObservedMessageCount_ + 1; seq <= high; ++seq) {
        rw::storage::LxmfMessageRecord m{};
        if (storage_->loadLxmfMessage(seq, m) && !m.outgoing) {
            newestIncoming = m;
            found = true;
        }
    }
    lastObservedMessageCount_ = high;
    if (!found) return;
    if (!userProfile_.notificationsEnabled) {
        Serial.printf("[m8:notify] popup suppressed by preference; sequence=%lu unread=%lu\n",
                      static_cast<unsigned long>(newestIncoming.sequence),
                      static_cast<unsigned long>(storage_->unreadLxmfCount()));
        return;
    }
    Serial.printf("[m4:notify] incoming sequence=%lu unread=%lu screen=%u\n",
                  static_cast<unsigned long>(newestIncoming.sequence),
                  static_cast<unsigned long>(storage_->unreadLxmfCount()),
                  static_cast<unsigned>(screen_));
    const bool wasSleeping = power_ && power_->displaySleeping();
    if (power_) power_->displayOn();
    if (screen_ == Screen::WatchFace || wasSleeping) {
        showNotification(newestIncoming);
    } else {
        Serial.println("[m4:notify] popup deferred because user is active; unread state retained");
    }
}

void WatchUI::refreshIfNeeded() {
    if (!storage_) return;
    if (screen_ == Screen::WatchFace) {
        const uint32_t now = millis();
        if (now - lastClockRefreshMs_ >= 1000 ||
            storage_->unreadLxmfCount() != displayedUnreadCount_ ||
            (lxmf_ && lxmf_->discoveredPeers() != displayedDiscoveredCount_)) showWatchFace();
    } else if (screen_ == Screen::Inbox || screen_ == Screen::Conversations) {
        if (storage_->lxmfMessageCount() != displayedMessageCount_ ||
            storage_->unreadLxmfCount() != displayedUnreadCount_) navigate(screen_);
    } else if (screen_ == Screen::Thread && lxmf_) {
        const uint32_t events = lxmf_->deliveredMessages() + lxmf_->retriedPackets();
        if (storage_->lxmfMessageCount() != displayedMessageCount_ || events != displayedDeliveryEvents_) {
            showThread();
        } else if (audio_ && voice_ &&
                   (audio_->busy() || voice_->busy() ||
                    audio_->revision() != displayedAudioRevision_ ||
                    voice_->revision() != displayedVoiceRevision_)) {
            if (power_) power_->notifyActivity();
            updateThreadVoiceStatus();
        }
    } else if (screen_ == Screen::Peers && lxmf_ && lxmf_->discoveredPeers() != displayedDiscoveredCount_) {
        showPeers();
    } else if (screen_ == Screen::Network && lxmf_ && lxmf_->discoveredPeers() != displayedDiscoveredCount_) {
        showNetwork();
    } else if (screen_ == Screen::AudioLab && audio_) {
        if ((audio_->busy() || (voice_ && voice_->busy())) && power_) power_->notifyActivity();
        if (audio_->busy() || audio_->revision() != displayedAudioRevision_ ||
            (voice_ && voice_->revision() != displayedVoiceRevision_)) updateAudioLabLabels();
    }
}

void WatchUI::showSendingOverlay() {
    if (!screenRoot_) return;
    if (sendingOverlay_) lv_obj_delete(sendingOverlay_);
    // Full-screen clickable scrim makes the send state modal: underlying thread/
    // composer controls cannot be activated while submission is in progress.
    sendingOverlay_ = lv_obj_create(screenRoot_);
    lv_obj_set_size(sendingOverlay_, 240, 240);
    lv_obj_align(sendingOverlay_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(sendingOverlay_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(sendingOverlay_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scrollbar_mode(sendingOverlay_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(sendingOverlay_, 0, 0);
    lv_obj_set_style_radius(sendingOverlay_, 0, 0);
    lv_obj_set_style_bg_color(sendingOverlay_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(sendingOverlay_, LV_OPA_70, 0);
    lv_obj_set_style_border_width(sendingOverlay_, 0, 0);
    lv_obj_set_style_shadow_width(sendingOverlay_, 0, 0);
    lv_obj_move_foreground(sendingOverlay_);

    lv_obj_t *panel = lv_obj_create(sendingOverlay_);
    lv_obj_set_size(panel, 214, 92);
    lv_obj_center(panel);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x707070), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_shadow_width(panel, 0, 0);

    lv_obj_t *title = lv_label_create(panel);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xF4F4F4), 0);
    lv_label_set_text(title, "Sending message...");
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -13);

    lv_obj_t *detail = lv_label_create(panel);
    lv_obj_set_width(detail, 190);
    lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(detail, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(detail, lv_color_hex(0xB8B8B8), 0);
    lv_label_set_text(detail, "Submitting to Reticulum");
    lv_obj_align(detail, LV_ALIGN_CENTER, 0, 16);

    Serial.println("[v1.2:ux] sending-message overlay shown");
}

void WatchUI::hideSendingOverlay() {
    if (!sendingOverlay_) return;
    lv_obj_delete(sendingOverlay_);
    sendingOverlay_ = nullptr;
}

void WatchUI::queueTextSend(const uint8_t destination[16], const char *text, Screen successTarget) {
    if (!destination || !text || !text[0] || pendingTextSend_) return;
    memcpy(pendingSendDestination_, destination, sizeof(pendingSendDestination_));
    strncpy(pendingSendText_, text, sizeof(pendingSendText_) - 1);
    pendingSendText_[sizeof(pendingSendText_) - 1] = '\0';
    pendingSendSuccessTarget_ = successTarget;
    pendingTextSend_ = true;
    // Leave at least one normal LVGL refresh window for the modal to reach the panel
    // before the existing synchronous Reticulum submission path is invoked.
    pendingTextSendAfterMs_ = millis() + 120U;
    showSendingOverlay();
}

void WatchUI::processPendingTextSend() {
    if (!pendingTextSend_ || !lxmf_) return;
    if (static_cast<int32_t>(millis() - pendingTextSendAfterMs_) < 0) return;

    const Screen successTarget = pendingSendSuccessTarget_;
    const bool ok = lxmf_->sendMessage(pendingSendDestination_, pendingSendText_);
    pendingTextSend_ = false;
    pendingSendText_[0] = '\0';

    if (ok) {
        Serial.printf("[v1.2:ux] sending-message submission accepted status=%s\n", lxmf_->txStatus());
        hideSendingOverlay();
        if (successTarget == Screen::Thread) showThread();
        else if (successTarget == Screen::WatchFace) showWatchFace();
        else navigate(successTarget);
        return;
    }

    const char *status = lxmf_->txStatus();
    Serial.printf("[v1.2:ux] sending-message submission failed status=%s\n", status ? status : "unknown");
    hideSendingOverlay();
    if (screen_ == Screen::Compose && composeStatusLabel_) {
        lv_label_set_text(composeStatusLabel_, status && status[0] ? status : "Send failed");
    } else {
        lv_obj_t *error = lv_label_create(screenRoot_);
        lv_obj_set_width(error, 210);
        lv_obj_set_style_text_align(error, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(error, &lv_font_montserrat_10, 0);
        lv_label_set_text(error, status && status[0] ? status : "Send failed");
        lv_obj_align(error, LV_ALIGN_BOTTOM_MID, 0, -8);
    }
}

void WatchUI::loop() {
    const uint32_t now = millis();
    // v1.4.0: when the display is asleep the Reticulum/LXMF stack remains fully
    // active, but the UI only needs to inspect the durable inbox about once per
    // second. This reduces wake-side storage/UI churn without changing radio RX.
    const uint32_t uiPollIntervalMs = (power_ && power_->displaySleeping()) ? 1200U : 350U;
    if (now - lastUiUpdateMs_ >= uiPollIntervalMs) {
        lastUiUpdateMs_ = now;
        checkForNotification();
        if (!power_ || !power_->displaySleeping()) refreshIfNeeded();
    }
    if (lxmf_ && lxmf_->ready()) {
        const uint32_t nowMs = millis();
        const auto policy = userProfile_.announcePolicy;
        if (!startupAnnounceDone_ && nowMs > 8000 && policy != rw::config::user::AnnouncePolicy::Manual) {
            if (lxmf_->requestAnnounce()) {
                startupAnnounceDone_ = true;
                lastAutoAnnounceMs_ = nowMs;
                Serial.printf("[m8:rns] automatic announce queued policy=%s\n", rw::config::user::announcePolicyName(policy));
            }
        } else if (policy == rw::config::user::AnnouncePolicy::SixHours && startupAnnounceDone_ &&
                   static_cast<uint32_t>(nowMs - lastAutoAnnounceMs_) >= 21600000UL) {
            if (lxmf_->requestAnnounce()) {
                lastAutoAnnounceMs_ = nowMs;
                Serial.println("[m8:rns] six-hour automatic announce queued");
            }
        }
    }
    if (power_ && power_->uiInputBlocked()) return;
    lv_timer_handler();
    processPendingTextSend();
}

void WatchUI::buttonHapticEvent(lv_event_t *event) { auto *self = static_cast<WatchUI*>(lv_event_get_user_data(event)); if (self) { if (self->power_) self->power_->notifyActivity(); if (self->hardware_ && self->userProfile_.hapticsEnabled) self->hardware_->tapHaptic(); } }
void WatchUI::keyboardHapticEvent(lv_event_t *event) { auto *self = static_cast<WatchUI*>(lv_event_get_user_data(event)); if (self) { if (self->power_) self->power_->notifyActivity(); if (self->hardware_ && self->userProfile_.hapticsEnabled) self->hardware_->keyHaptic(); } }
void WatchUI::announceEvent(lv_event_t *event) {
    auto *self = static_cast<WatchUI*>(lv_event_get_user_data(event));
    if (!self || !self->lxmf_ || !self->lxmf_->ready()) return;
    const uint32_t now = millis();
    if (self->lastManualAnnounceMs_ != 0 &&
        static_cast<uint32_t>(now - self->lastManualAnnounceMs_) < kManualAnnounceCooldownMs) {
        const uint32_t remaining = (kManualAnnounceCooldownMs - static_cast<uint32_t>(now - self->lastManualAnnounceMs_) + 999U) / 1000U;
        Serial.printf("[m8:announce] manual announce suppressed; cooldown=%lus. Rapid re-announces are deprioritised by Reticulum.\n",
                      static_cast<unsigned long>(remaining));
        return;
    }
    if (self->lxmf_->requestAnnounce()) {
        self->lastManualAnnounceMs_ = now;
        // A successful manual request satisfies any pending startup announce and
        // resets the periodic scheduler, avoiding an immediate duplicate.
        self->startupAnnounceDone_ = true;
        self->lastAutoAnnounceMs_ = now;
        Serial.println("[m8:announce] manual LXMF announce queued; 60s anti-spam cooldown started");
    } else {
        Serial.println("[m8:announce] announce request rejected: service busy or unavailable");
    }
}
void WatchUI::restartPromptEvent(lv_event_t *event) { auto *self = static_cast<WatchUI*>(lv_event_get_user_data(event)); if (self) self->showRestartConfirm(); }
void WatchUI::restartConfirmEvent(lv_event_t *event) { auto *self = static_cast<WatchUI*>(lv_event_get_user_data(event)); if (self && self->hardware_) { Serial.println("[m4:settings] restart confirmed"); self->hardware_->restart(); } }
void WatchUI::conversationEvent(lv_event_t *event) {
    auto *self = static_cast<WatchUI*>(lv_event_get_user_data(event));
    if (!self) return;
    lv_obj_t *t = lv_event_get_current_target_obj(event);
    for (size_t i=0; i<kVisibleThreads; ++i) {
        if (self->threadButtons_[i] == t) {
            self->selectPeer(self->threadHashes_[i]);
            self->threadReturnTarget_ = Screen::Conversations;
            Serial.println("[v1.3.0-b2:nav] conversation card -> thread; back=Messages");
            self->showThread();
            return;
        }
    }
}
void WatchUI::inboxMessageEvent(lv_event_t *event) {
    auto *self = static_cast<WatchUI*>(lv_event_get_user_data(event));
    if (!self) return;
    lv_obj_t *t = lv_event_get_current_target_obj(event);
    for (size_t i=0; i<kVisibleThreads; ++i) {
        if (self->threadButtons_[i] == t) {
            self->selectPeer(self->threadHashes_[i]);
            self->threadReturnTarget_ = Screen::Inbox;
            Serial.println("[v1.3.0-b2:nav] inbox card -> thread; back=Inbox");
            self->showThread();
            return;
        }
    }
}
void WatchUI::peerEvent(lv_event_t *event) {
    auto *self = static_cast<WatchUI*>(lv_event_get_user_data(event));
    if (!self) return;
    lv_obj_t *t = lv_event_get_current_target_obj(event);
    for (size_t i=0; i<kVisiblePeers; ++i) {
        if (self->peerButtons_[i] == t) {
            self->selectPeer(self->peerHashes_[i]);
            self->peerDetailsReturnTarget_ = Screen::Peers;
            Serial.printf("[v1.3.0-b2:nav] peer card %u -> Peer Details; back=Peers\n", static_cast<unsigned>(i));
            self->showPeerDetails();
            return;
        }
    }
    Serial.println("[v1.3.0-b2:nav] peer card click had no matching button");
}
void WatchUI::contactEvent(lv_event_t *event) {
    auto *self = static_cast<WatchUI*>(lv_event_get_user_data(event));
    if (!self) return;
    lv_obj_t *t = lv_event_get_current_target_obj(event);
    for (size_t i=0; i<kVisiblePeers; ++i) {
        if (self->peerButtons_[i] == t) {
            self->selectPeer(self->peerHashes_[i]);
            self->peerDetailsReturnTarget_ = Screen::Contacts;
            Serial.printf("[v1.3.0-b2:nav] contact card %u -> Peer Details; back=Contacts\n", static_cast<unsigned>(i));
            self->showPeerDetails();
            return;
        }
    }
    Serial.println("[v1.3.0-b2:nav] contact card click had no matching button");
}
void WatchUI::threadInfoEvent(lv_event_t *event) {
    auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event));
    if(self&&self->haveSelectedPeer_) {
        self->peerDetailsReturnTarget_ = Screen::Thread;
        Serial.println("[v1.6:nav] chat info -> Peer Details; back=Thread");
        self->showPeerDetails();
    }
}
void WatchUI::peerChatEvent(lv_event_t *event) {
    auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event));
    if(self&&self->haveSelectedPeer_) {
        self->threadReturnTarget_ = Screen::PeerDetails;
        Serial.println("[v1.3.0-b2:nav] Peer Details -> chat; back=Peer Details");
        self->showThread();
    }
}
void WatchUI::peerFavoriteEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(!self||!self->storage_||!self->haveSelectedPeer_) return; rw::storage::PeerRecord p{}; const bool current=self->storage_->loadPeer(self->selectedPeer_,p)&&p.favorite; self->storage_->setPeerFavorite(self->selectedPeer_,!current); Serial.printf("[v1.3.0-b2:peer] local favorite/pin=%s (sorting/cache only; no network priority)\n", current ? "off" : "on"); self->showPeerDetails(); }
void WatchUI::peerNameEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(self&&self->haveSelectedPeer_) { self->namePeerReturnTarget_=Screen::PeerDetails; self->showNamePeer(); } }
void WatchUI::peerQrEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(!self||!self->haveSelectedPeer_) return; char hash[33]{}; fullHash(self->selectedPeer_,hash); self->showQr("CONTACT LXMF",hash,Screen::PeerDetails); }
void WatchUI::backEvent(lv_event_t *event) { auto *self = static_cast<WatchUI*>(lv_event_get_user_data(event)); if (self) { if(self->screen_==Screen::RadioSettings) self->leaveRadioSettings(self->backTarget_); else if(self->screen_==Screen::InterfaceSettings) self->leaveNetworkSettings(self->backTarget_); else if(self->screen_==Screen::RoutingSettings) self->leaveRoutingSettings(self->backTarget_); else self->navigate(self->backTarget_); } }
void WatchUI::homeEvent(lv_event_t *event) { auto *self = static_cast<WatchUI*>(lv_event_get_user_data(event)); if (self) { if(self->screen_==Screen::RadioSettings) self->leaveRadioSettings(Screen::WatchFace); else if(self->screen_==Screen::InterfaceSettings) self->leaveNetworkSettings(Screen::WatchFace); else if(self->screen_==Screen::RoutingSettings) self->leaveRoutingSettings(Screen::WatchFace); else self->showWatchFace(); } }
void WatchUI::menuEvent(lv_event_t *event) { auto *self = static_cast<WatchUI*>(lv_event_get_user_data(event)); if (self) { if(self->screen_==Screen::RadioSettings) self->leaveRadioSettings(Screen::Menu); else if(self->screen_==Screen::InterfaceSettings) self->leaveNetworkSettings(Screen::Menu); else if(self->screen_==Screen::RoutingSettings) self->leaveRoutingSettings(Screen::Menu); else self->showMenu(); } }
void WatchUI::inboxEvent(lv_event_t *event) { auto *self = static_cast<WatchUI*>(lv_event_get_user_data(event)); if (self) self->showInbox(); }
void WatchUI::conversationsNavEvent(lv_event_t *event) { auto *self = static_cast<WatchUI*>(lv_event_get_user_data(event)); if (self) self->showConversations(); }
void WatchUI::contactsEvent(lv_event_t *event) { auto *self = static_cast<WatchUI*>(lv_event_get_user_data(event)); if (self) self->showContacts(); }
void WatchUI::peersEvent(lv_event_t *event) { auto *self = static_cast<WatchUI*>(lv_event_get_user_data(event)); if (self) self->showPeers(); }
void WatchUI::networkEvent(lv_event_t *event) { auto *self = static_cast<WatchUI*>(lv_event_get_user_data(event)); if (self) self->showNetwork(); }
void WatchUI::audioEvent(lv_event_t *event) { auto *self = static_cast<WatchUI*>(lv_event_get_user_data(event)); if (self) { self->audioVoiceCompose_=false; self->showAudioLab(); } }
void WatchUI::audioRecordEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(!self||!self->audio_)return; const uint32_t maxMs=(self->audioVoiceCompose_&&self->voice_)?self->voice_->maxRecordMs():rw::audio::AudioService::kMaxRecordMs; if(!self->audio_->startRecording(maxMs)) Serial.printf("[m9:audio] record request rejected state=%s max=%lums\n",self->audio_->stateName(),static_cast<unsigned long>(maxMs)); self->updateAudioLabLabels(); }
void WatchUI::audioPlayEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(!self||!self->audio_)return; if(!self->audio_->playRecording()) Serial.printf("[m9:audio] play request rejected state=%s clip_ms=%lu\n",self->audio_->stateName(),static_cast<unsigned long>(self->audio_->recordedMs())); self->updateAudioLabLabels(); }
void WatchUI::audioStopEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(!self||!self->audio_)return; self->audio_->stop(); self->updateAudioLabLabels(); }
void WatchUI::audioClearEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(!self||!self->audio_)return; self->audio_->clearRecording(); self->updateAudioLabLabels(); }
void WatchUI::voiceComposeEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(!self||!self->haveSelectedPeer_)return; if(self->audio_ && !self->audio_->busy()) self->audio_->clearRecording(); self->audioVoiceCompose_=true; self->showAudioLab(); }
void WatchUI::voiceSendEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(!self||!self->voice_||!self->haveSelectedPeer_)return; if(!self->voice_->requestSend(self->selectedPeer_)) Serial.printf("[v1.5:voice] send request rejected: %s\n", self->voice_->status()); else Serial.printf("[v1.5:voice] send queued clip=%lums\n", static_cast<unsigned long>(self->audio_ ? self->audio_->recordedMs() : 0)); self->updateAudioLabLabels(); }
void WatchUI::voicePlaybackEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(!self||!self->voice_||self->latestThreadVoiceSequence_==0)return; if(!self->voice_->requestPlayback(self->latestThreadVoiceSequence_)) Serial.printf("[v1.5:voice] playback request rejected #%lu: %s\n", static_cast<unsigned long>(self->latestThreadVoiceSequence_), self->voice_->status()); self->updateThreadVoiceStatus(); }

void WatchUI::settingsEvent(lv_event_t *event) { auto *self = static_cast<WatchUI*>(lv_event_get_user_data(event)); if (self) self->showSettings(); }
void WatchUI::composeNavEvent(lv_event_t *event) { auto *self = static_cast<WatchUI*>(lv_event_get_user_data(event)); if (self) self->showHashEntry(); }
void WatchUI::replyEvent(lv_event_t *event) { auto *self = static_cast<WatchUI*>(lv_event_get_user_data(event)); if (self) self->showCompose(); }
void WatchUI::sendEvent(lv_event_t *event) {
    auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event));
    if(!self||!self->lxmf_||!self->composeTextArea_||!self->haveSelectedPeer_||self->pendingTextSend_) return;
    const char *text=lv_textarea_get_text(self->composeTextArea_);
    if(!text||!text[0]) { if(self->composeStatusLabel_) lv_label_set_text(self->composeStatusLabel_,"Type a message"); return; }
    Serial.printf("[m4:compose] send requested chars=%u\n", static_cast<unsigned>(strlen(text)));
    self->queueTextSend(self->selectedPeer_, text, Screen::Thread);
}
void WatchUI::nameEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(self&&self->haveSelectedPeer_) { self->namePeerReturnTarget_=Screen::Thread; self->showNamePeer(); } }
void WatchUI::saveNameEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(!self||!self->storage_||!self->composeTextArea_||!self->haveSelectedPeer_)return; const char*name=lv_textarea_get_text(self->composeTextArea_); if(name&&name[0]&&self->storage_->savePeerName(self->selectedPeer_,name)){Serial.printf("[m4:contact] saved name=%s\n",name); if(self->namePeerReturnTarget_==Screen::PeerDetails) self->showPeerDetails(); else self->showThread();}else if(self->composeStatusLabel_)lv_label_set_text(self->composeStatusLabel_,"Name required"); }
void WatchUI::favoriteEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(!self||!self->storage_||!self->haveSelectedPeer_)return; rw::storage::PeerRecord p{}; bool current=self->storage_->loadPeer(self->selectedPeer_,p)&&p.favorite; self->storage_->setPeerFavorite(self->selectedPeer_,!current); self->showThread(); }
void WatchUI::newHashEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(self)self->showHashEntry(); }
void WatchUI::hashConfirmEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(!self||!self->composeTextArea_)return; uint8_t hash[16]{}; const char*text=lv_textarea_get_text(self->composeTextArea_); if(!self->parseHashText(text,hash)){if(self->composeStatusLabel_)lv_label_set_text(self->composeStatusLabel_,"Invalid: need exactly 32 hex chars");return;} self->selectPeer(hash); if(self->storage_)self->storage_->notePeerFromMessage(hash,false,0.0); Serial.println("[m4:compose] manual destination accepted"); self->showCompose(); }
void WatchUI::identityEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(self) self->showIdentity(); }
void WatchUI::addressQrEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(self&&self->lxmf_) self->showQr("LXMF ADDRESS", self->lxmf_->deliveryHash(), Screen::Identity); }
void WatchUI::identityQrEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(self&&self->reticulum_) self->showQr("IDENTITY HASH", self->reticulum_->identityHash(), Screen::Identity); }
void WatchUI::contactShareEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(!self||!self->haveSelectedPeer_) return; char hash[33]{}; fullHash(self->selectedPeer_,hash); self->showQr("CONTACT LXMF", hash, Screen::Thread); }
void WatchUI::userSettingsEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(self) self->showUserSettings(); }
void WatchUI::messagingSettingsEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(self) self->showMessagingSettings(); }
void WatchUI::watchSettingsEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(self) self->showWatchSettings(); }
void WatchUI::reticulumSettingsEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(self) self->showReticulumSettings(); }
void WatchUI::editDisplayNameEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(self){self->userEditField_=UserEditField::DisplayName;self->showUserEdit();} }
void WatchUI::editCanned1Event(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(self){self->userEditField_=UserEditField::CannedReply1;self->showUserEdit();} }
void WatchUI::editCanned2Event(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(self){self->userEditField_=UserEditField::CannedReply2;self->showUserEdit();} }
void WatchUI::userEditSaveEvent(lv_event_t *event) {
    auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event));
    if(!self||!self->composeTextArea_) return;
    const char *text=lv_textarea_get_text(self->composeTextArea_);
    if(!text) return;
    if(self->userEditField_==UserEditField::DisplayName && !text[0]) { if(self->composeStatusLabel_) lv_label_set_text(self->composeStatusLabel_,"Display name required"); return; }
    if(self->userEditField_==UserEditField::DisplayName) { strncpy(self->userProfile_.displayName,text,sizeof(self->userProfile_.displayName)-1); self->userProfile_.displayName[sizeof(self->userProfile_.displayName)-1]='\0'; }
    else if(self->userEditField_==UserEditField::CannedReply1) { strncpy(self->userProfile_.quickReply1,text,sizeof(self->userProfile_.quickReply1)-1); self->userProfile_.quickReply1[sizeof(self->userProfile_.quickReply1)-1]='\0'; }
    else { strncpy(self->userProfile_.quickReply2,text,sizeof(self->userProfile_.quickReply2)-1); self->userProfile_.quickReply2[sizeof(self->userProfile_.quickReply2)-1]='\0'; }
    self->saveUserProfile();
    if(self->userEditField_==UserEditField::DisplayName) self->showUserSettings(); else self->showMessagingSettings();
}
void WatchUI::notificationToggleEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(!self)return; self->userProfile_.notificationsEnabled=!self->userProfile_.notificationsEnabled; self->saveUserProfile(); self->showMessagingSettings(); }
void WatchUI::wipeHistoryPromptEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(self) self->showHistoryWipeConfirm(); }
void WatchUI::wipeHistoryCancelEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(self) self->showMessagingSettings(); }
void WatchUI::wipeHistoryConfirmEvent(lv_event_t *event) {
    auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event));
    if(!self || !self->storage_) return;
    Serial.println("[m11:history] user confirmed chat history wipe");
    if (self->storage_->wipeChatHistory()) {
        Serial.println("[m11:history] wipe complete; rebooting into empty journal");
        if (self->hardware_) self->hardware_->restart();
    } else {
        lv_obj_t *msg = lv_label_create(self->screenRoot_);
        lv_obj_set_width(msg, 210); lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_10, 0);
        lv_label_set_text(msg, "Wipe failed. Identity and settings were not touched.");
        lv_obj_align(msg, LV_ALIGN_BOTTOM_MID, 0, -52);
        Serial.println("[m11:history] ERROR wipe failed; preserving current state");
    }
}
void WatchUI::hapticsToggleEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(!self)return; self->userProfile_.hapticsEnabled=!self->userProfile_.hapticsEnabled; self->saveUserProfile(); self->showWatchSettings(); }
void WatchUI::announcePolicyEvent(lv_event_t *event) {
    auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(!self)return;
    using P=rw::config::user::AnnouncePolicy;
    if(self->userProfile_.announcePolicy==P::Manual) self->userProfile_.announcePolicy=P::Startup;
    else if(self->userProfile_.announcePolicy==P::Startup) self->userProfile_.announcePolicy=P::SixHours;
    else self->userProfile_.announcePolicy=P::Manual;
    self->startupAnnounceDone_=false; self->lastAutoAnnounceMs_=0; self->saveUserProfile(); self->showReticulumSettings();
}
void WatchUI::peerDiscoveryToggleEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(!self)return; self->userProfile_.peerDiscoveryVisible=!self->userProfile_.peerDiscoveryVisible; self->saveUserProfile(); self->showReticulumSettings(); }
void WatchUI::cannedReply1Event(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(self&&self->lxmf_&&self->haveSelectedPeer_&&self->userProfile_.quickReply1[0]&&!self->pendingTextSend_){Serial.printf("[m11:canned] reply 1: %s\n",self->userProfile_.quickReply1);self->queueTextSend(self->selectedPeer_,self->userProfile_.quickReply1,Screen::Thread);} }
void WatchUI::cannedReply2Event(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(self&&self->lxmf_&&self->haveSelectedPeer_&&self->userProfile_.quickReply2[0]&&!self->pendingTextSend_){Serial.printf("[m11:canned] reply 2: %s\n",self->userProfile_.quickReply2);self->queueTextSend(self->selectedPeer_,self->userProfile_.quickReply2,Screen::Thread);} }
void WatchUI::dismissNotificationEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(self){Serial.println("[m4:notify] dismissed");self->showWatchFace();} }
void WatchUI::openNotificationEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(self&&self->haveNotificationPeer_){self->selectPeer(self->notificationPeer_);self->threadReturnTarget_=Screen::WatchFace;self->showThread();} }
void WatchUI::notificationCanned1Event(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(self&&self->lxmf_&&self->haveNotificationPeer_&&self->userProfile_.quickReply1[0]&&!self->pendingTextSend_){self->queueTextSend(self->notificationPeer_,self->userProfile_.quickReply1,Screen::WatchFace);} }
void WatchUI::notificationCanned2Event(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(self&&self->lxmf_&&self->haveNotificationPeer_&&self->userProfile_.quickReply2[0]&&!self->pendingTextSend_){self->queueTextSend(self->notificationPeer_,self->userProfile_.quickReply2,Screen::WatchFace);} }
void WatchUI::brightnessLowEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(self){self->userProfile_.brightness=70;self->saveUserProfile();Serial.println("[m8:watch] brightness=70");self->showWatchSettings();} }
void WatchUI::brightnessMedEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(self){self->userProfile_.brightness=150;self->saveUserProfile();Serial.println("[m8:watch] brightness=150");self->showWatchSettings();} }
void WatchUI::brightnessHighEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(self){self->userProfile_.brightness=230;self->saveUserProfile();Serial.println("[m8:watch] brightness=230");self->showWatchSettings();} }
void WatchUI::powerSettingsEvent(lv_event_t *event) { auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(self) self->showPowerSettings(); }
void WatchUI::powerTimeoutEvent(lv_event_t *event) {
    auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event));
    if(!self || !self->power_ || !self->storage_) return;
    auto p=self->power_->profile();
    if(p.displayTimeoutMs==15000) p.displayTimeoutMs=30000;
    else if(p.displayTimeoutMs==30000) p.displayTimeoutMs=60000;
    else if(p.displayTimeoutMs==60000) p.displayTimeoutMs=0;
    else p.displayTimeoutMs=15000;
    if(self->storage_->savePowerProfile(p)) self->power_->configure(p);
    self->showPowerSettings();
}
void WatchUI::liftToWakeToggleEvent(lv_event_t *event) {
    auto *self=static_cast<WatchUI*>(lv_event_get_user_data(event));
    if(!self || !self->power_ || !self->storage_) return;
    auto p=self->power_->profile();
    p.liftToWake=!p.liftToWake;
    if(self->storage_->savePowerProfile(p)) self->power_->configure(p);
    Serial.printf("[m12:power] lift-to-wake user setting=%s\n", p.liftToWake ? "ON" : "OFF");
    self->showPowerSettings();
}
void WatchUI::radioSettingsEvent(lv_event_t *event) { auto*self=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(self)self->showRadioSettings(); }
void WatchUI::radioFreqDownEvent(lv_event_t *e){auto*s=static_cast<WatchUI*>(lv_event_get_user_data(e));if(!s)return;if(s->pendingRadioProfile_.frequencyHz>=150100000UL)s->pendingRadioProfile_.frequencyHz-=100000UL;s->savePendingRadioProfile();s->showRadioSettings();}
void WatchUI::radioFreqUpEvent(lv_event_t *e){auto*s=static_cast<WatchUI*>(lv_event_get_user_data(e));if(!s)return;if(s->pendingRadioProfile_.frequencyHz<=959900000UL)s->pendingRadioProfile_.frequencyHz+=100000UL;s->savePendingRadioProfile();s->showRadioSettings();}
void WatchUI::radioBwEvent(lv_event_t *e){auto*s=static_cast<WatchUI*>(lv_event_get_user_data(e));if(!s)return;const uint32_t b=s->pendingRadioProfile_.bandwidthHz;s->pendingRadioProfile_.bandwidthHz=b==62500UL?125000UL:b==125000UL?250000UL:b==250000UL?500000UL:62500UL;s->savePendingRadioProfile();s->showRadioSettings();}
void WatchUI::radioSfDownEvent(lv_event_t *e){auto*s=static_cast<WatchUI*>(lv_event_get_user_data(e));if(!s)return;if(s->pendingRadioProfile_.spreadingFactor>5)--s->pendingRadioProfile_.spreadingFactor;s->savePendingRadioProfile();s->showRadioSettings();}
void WatchUI::radioSfUpEvent(lv_event_t *e){auto*s=static_cast<WatchUI*>(lv_event_get_user_data(e));if(!s)return;if(s->pendingRadioProfile_.spreadingFactor<12)++s->pendingRadioProfile_.spreadingFactor;s->savePendingRadioProfile();s->showRadioSettings();}
void WatchUI::radioCrDownEvent(lv_event_t *e){auto*s=static_cast<WatchUI*>(lv_event_get_user_data(e));if(!s)return;if(s->pendingRadioProfile_.codingRate>5)--s->pendingRadioProfile_.codingRate;s->savePendingRadioProfile();s->showRadioSettings();}
void WatchUI::radioCrUpEvent(lv_event_t *e){auto*s=static_cast<WatchUI*>(lv_event_get_user_data(e));if(!s)return;if(s->pendingRadioProfile_.codingRate<8)++s->pendingRadioProfile_.codingRate;s->savePendingRadioProfile();s->showRadioSettings();}
void WatchUI::radioTxDownEvent(lv_event_t *e){auto*s=static_cast<WatchUI*>(lv_event_get_user_data(e));if(!s)return;if(s->pendingRadioProfile_.txPowerDbm>-9)--s->pendingRadioProfile_.txPowerDbm;s->savePendingRadioProfile();s->showRadioSettings();}
void WatchUI::radioTxUpEvent(lv_event_t *e){auto*s=static_cast<WatchUI*>(lv_event_get_user_data(e));if(!s)return;if(s->pendingRadioProfile_.txPowerDbm<22)++s->pendingRadioProfile_.txPowerDbm;s->savePendingRadioProfile();s->showRadioSettings();}
void WatchUI::radioPreDownEvent(lv_event_t *e){auto*s=static_cast<WatchUI*>(lv_event_get_user_data(e));if(!s)return;if(s->pendingRadioProfile_.preambleSymbols>6)--s->pendingRadioProfile_.preambleSymbols;s->savePendingRadioProfile();s->showRadioSettings();}
void WatchUI::radioPreUpEvent(lv_event_t *e){auto*s=static_cast<WatchUI*>(lv_event_get_user_data(e));if(!s)return;if(s->pendingRadioProfile_.preambleSymbols<64)++s->pendingRadioProfile_.preambleSymbols;s->savePendingRadioProfile();s->showRadioSettings();}
void WatchUI::radioApplyRebootEvent(lv_event_t *e){auto*s=static_cast<WatchUI*>(lv_event_get_user_data(e));if(s&&s->hardware_){Serial.println("[m4:radio] rebooting to apply saved radio profile");s->hardware_->restart();}}
void WatchUI::radioApplyLaterEvent(lv_event_t *e){auto*s=static_cast<WatchUI*>(lv_event_get_user_data(e));if(!s)return;const Screen target=s->radioSettingsExitTarget_;Serial.println("[m4:radio] leaving settings; saved profile pending reboot");s->radioSettingsDirty_=false;s->navigate(target);}

void WatchUI::interfaceSettingsEvent(lv_event_t *event) { auto*s=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(s)s->showInterfaceSettings(); }
void WatchUI::networkToggleEvent(lv_event_t *event) {
    auto*s=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(!s)return;
    s->pendingNetworkProfile_.enabled=!s->pendingNetworkProfile_.enabled;
    if(s->pendingNetworkProfile_.enabled && s->pendingNetworkProfile_.mode==rw::config::network::WifiMode::Off)
        s->pendingNetworkProfile_.mode=rw::config::network::WifiMode::StationTcpClient;
    s->savePendingNetworkProfile(); s->showInterfaceSettings();
}
void WatchUI::networkModeEvent(lv_event_t *event) {
    auto*s=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(!s)return;
    using M=rw::config::network::WifiMode;
    if(s->pendingNetworkProfile_.mode==M::Off) s->pendingNetworkProfile_.mode=M::StationTcpClient;
    else if(s->pendingNetworkProfile_.mode==M::StationTcpClient) s->pendingNetworkProfile_.mode=M::AccessPointTcpServer;
    else s->pendingNetworkProfile_.mode=M::Off;
    if(s->pendingNetworkProfile_.mode==M::Off) s->pendingNetworkProfile_.enabled=false;
    s->savePendingNetworkProfile(); s->showInterfaceSettings();
}
void WatchUI::networkEditSsidEvent(lv_event_t *event){auto*s=static_cast<WatchUI*>(lv_event_get_user_data(event));if(s){s->networkEditField_=NetworkEditField::Ssid;s->showNetworkEdit();}}
void WatchUI::networkEditPasswordEvent(lv_event_t *event){auto*s=static_cast<WatchUI*>(lv_event_get_user_data(event));if(s){s->networkEditField_=NetworkEditField::Password;s->showNetworkEdit();}}
void WatchUI::networkEditHostEvent(lv_event_t *event){auto*s=static_cast<WatchUI*>(lv_event_get_user_data(event));if(s){s->networkEditField_=NetworkEditField::Host;s->showNetworkEdit();}}
void WatchUI::networkPortEvent(lv_event_t *event){auto*s=static_cast<WatchUI*>(lv_event_get_user_data(event));if(!s)return;const uint16_t p=s->pendingNetworkProfile_.port;s->pendingNetworkProfile_.port=p==4242?4343:p==4343?7633:4242;s->savePendingNetworkProfile();s->showInterfaceSettings();}
void WatchUI::networkEditSaveEvent(lv_event_t *event){
    auto*s=static_cast<WatchUI*>(lv_event_get_user_data(event)); if(!s||!s->composeTextArea_)return;
    const char*t=lv_textarea_get_text(s->composeTextArea_); if(!t)t="";
    if(s->networkEditField_==NetworkEditField::Ssid){strncpy(s->pendingNetworkProfile_.ssid,t,sizeof(s->pendingNetworkProfile_.ssid)-1);s->pendingNetworkProfile_.ssid[sizeof(s->pendingNetworkProfile_.ssid)-1]='\0';}
    else if(s->networkEditField_==NetworkEditField::Password){strncpy(s->pendingNetworkProfile_.password,t,sizeof(s->pendingNetworkProfile_.password)-1);s->pendingNetworkProfile_.password[sizeof(s->pendingNetworkProfile_.password)-1]='\0';}
    else {strncpy(s->pendingNetworkProfile_.targetHost,t,sizeof(s->pendingNetworkProfile_.targetHost)-1);s->pendingNetworkProfile_.targetHost[sizeof(s->pendingNetworkProfile_.targetHost)-1]='\0';}
    s->savePendingNetworkProfile(); s->showInterfaceSettings();
}
void WatchUI::networkApplyRebootEvent(lv_event_t *event){auto*s=static_cast<WatchUI*>(lv_event_get_user_data(event));if(s&&s->hardware_){Serial.println("[m5:net] rebooting to apply interface profile");s->hardware_->restart();}}
void WatchUI::networkApplyLaterEvent(lv_event_t *event){auto*s=static_cast<WatchUI*>(lv_event_get_user_data(event));if(!s)return;const Screen target=s->networkSettingsExitTarget_;Serial.println("[m5:net] leaving interface settings; saved profile pending reboot");s->networkSettingsDirty_=false;s->navigate(target);}



void WatchUI::routingSettingsEvent(lv_event_t *event) {
    auto *self = static_cast<WatchUI*>(lv_event_get_user_data(event));
    if (self) self->showRoutingSettings();
}
void WatchUI::transportToggleEvent(lv_event_t *event) {
    auto *self = static_cast<WatchUI*>(lv_event_get_user_data(event));
    if (!self) return;
    Serial.println("[m6:recovery] Transport toggle ignored; endpoint baseline locked in build3");
    self->showRoutingSettings();
}
void WatchUI::routingApplyRebootEvent(lv_event_t *event) {
    auto *self = static_cast<WatchUI*>(lv_event_get_user_data(event));
    if (self && self->hardware_) self->hardware_->restart();
}
void WatchUI::routingApplyLaterEvent(lv_event_t *event) {
    auto *self = static_cast<WatchUI*>(lv_event_get_user_data(event));
    if (!self) return;
    self->routingSettingsDirty_ = false;
    self->navigate(self->routingSettingsExitTarget_);
}

} // namespace rw::ui
