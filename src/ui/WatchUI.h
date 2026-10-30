#pragma once

#include <lvgl.h>
#include <stddef.h>
#include <stdint.h>
#include "storage/StorageService.h"
#include "config/RadioConfig.h"
#include "config/NetworkConfig.h"
#include "config/TransportConfig.h"
#include "config/UserConfig.h"

namespace rw::hal { class WatchHardware; }
namespace rw::audio { class AudioService; class VoiceService; }
namespace rw::lxmf { class LxmfService; }
namespace rw::power { class PowerManager; }
namespace rw::radio { class RadioService; }
namespace rw::reticulum { class ReticulumService; }
namespace rw::transport { class TransportService; }

namespace rw::ui {

class WatchUI {
public:
    void begin(rw::hal::WatchHardware &hardware,
               rw::power::PowerManager &power,
               rw::storage::StorageService &storage,
               rw::radio::RadioService &radio,
               rw::reticulum::ReticulumService &reticulum,
               rw::transport::TransportService &transport,
               rw::lxmf::LxmfService &lxmf,
               rw::audio::AudioService &audio,
               rw::audio::VoiceService &voice);
    void loop();

private:
    enum class Screen : uint8_t {
        WatchFace, Menu, Inbox, Conversations, Thread, Compose, Contacts, Peers, PeerDetails,
        NamePeer, HashEntry, Identity, Qr, Network, AudioLab, Settings, UserSettings,
        MessagingSettings, WatchSettings, ReticulumSettings, UserEdit, HistoryWipeConfirm,
        PowerSettings, RadioSettings, RadioRestartConfirm, InterfaceSettings, NetworkEdit,
        NetworkRestartConfirm, RoutingSettings, RoutingRestartConfirm, Notification, RestartConfirm
    };
    static constexpr size_t kVisibleThreads = 8;
    static constexpr size_t kVisiblePeers = 8;
    static constexpr size_t kThreadMessages = 7;
    enum class NetworkEditField : uint8_t { Ssid, Password, Host };
    enum class UserEditField : uint8_t { DisplayName, CannedReply1, CannedReply2 };

    struct ConversationSummary {
        uint8_t peerHash[16]{};
        uint32_t latestSequence{0};
        double latestTimestamp{0.0};
        uint32_t unread{0};
        bool latestOutgoing{false};
        char preview[42]{};
    };

    void prepareScreen();
    void showWatchFace();
    void showMenu();
    void showInbox();
    void showConversations();
    void showThread();
    void showCompose(const char *prefill = nullptr);
    void showContacts();
    void showPeers();
    void showPeerDetails();
    void showNamePeer();
    void showHashEntry();
    void showIdentity();
    void showQr(const char *title, const char *payload, Screen returnTarget);
    void showNetwork();
    void showAudioLab();
    void updateAudioLabLabels();
    void updateThreadVoiceStatus();
    void showSettings();
    void showUserSettings();
    void showMessagingSettings();
    void showHistoryWipeConfirm();
    void showWatchSettings();
    void showReticulumSettings();
    void showUserEdit();
    void saveUserProfile();
    void showPowerSettings();
    void showRadioSettings();
    void showRadioRestartConfirm();
    void showInterfaceSettings();
    void showNetworkEdit();
    void showNetworkRestartConfirm();
    void showRoutingSettings();
    void showRoutingRestartConfirm();
    void leaveRoutingSettings(Screen target);
    void savePendingTransportProfile();
    void leaveNetworkSettings(Screen target);
    void savePendingNetworkProfile();
    void leaveRadioSettings(Screen target);
    void savePendingRadioProfile();
    void showNotification(const rw::storage::LxmfMessageRecord &message);
    void showRestartConfirm();
    void refreshIfNeeded();
    void checkForNotification();
    size_t buildConversationSummaries(ConversationSummary *summaries, size_t capacity) const;
    void selectPeer(const uint8_t hash[16]);
    void markSelectedConversationRead();
    void displayPeerName(const uint8_t hash[16], char *out, size_t capacity) const;
    bool parseHashText(const char *text, uint8_t hash[16]) const;
    void configureKeyboard(lv_obj_t *keyboard, lv_obj_t *textarea);
    void makeHeader(const char *title, Screen backTarget = Screen::Menu);
    void navigate(Screen target);
    void queueTextSend(const uint8_t destination[16], const char *text, Screen successTarget);
    void processPendingTextSend();
    void showSendingOverlay();
    void hideSendingOverlay();

    static lv_obj_t *makeButton(lv_obj_t *parent, const char *text, int width, int height,
                                lv_event_cb_t callback, void *userData);
    static lv_obj_t *makeIconButton(lv_obj_t *parent, const char *symbol, int width, int height,
                                    lv_event_cb_t callback, void *userData);
    static lv_obj_t *makeMenuIconButton(lv_obj_t *parent, int width, int height,
                                        lv_event_cb_t callback, void *userData);
    static void buttonHapticEvent(lv_event_t *event);
    static void keyboardHapticEvent(lv_event_t *event);
    static void announceEvent(lv_event_t *event);
    static void restartPromptEvent(lv_event_t *event);
    static void restartConfirmEvent(lv_event_t *event);
    static void conversationEvent(lv_event_t *event);
    static void inboxMessageEvent(lv_event_t *event);
    static void peersEvent(lv_event_t *event);
    static void peerEvent(lv_event_t *event);
    static void contactEvent(lv_event_t *event);
    static void threadInfoEvent(lv_event_t *event);
    static void peerChatEvent(lv_event_t *event);
    static void peerFavoriteEvent(lv_event_t *event);
    static void peerNameEvent(lv_event_t *event);
    static void peerQrEvent(lv_event_t *event);
    static void backEvent(lv_event_t *event);
    static void homeEvent(lv_event_t *event);
    static void menuEvent(lv_event_t *event);
    static void inboxEvent(lv_event_t *event);
    static void conversationsNavEvent(lv_event_t *event);
    static void contactsEvent(lv_event_t *event);
    static void networkEvent(lv_event_t *event);
    static void audioEvent(lv_event_t *event);
    static void audioRecordEvent(lv_event_t *event);
    static void audioPlayEvent(lv_event_t *event);
    static void audioStopEvent(lv_event_t *event);
    static void audioClearEvent(lv_event_t *event);
    static void voiceComposeEvent(lv_event_t *event);
    static void voiceSendEvent(lv_event_t *event);
    static void voicePlaybackEvent(lv_event_t *event);
    static void settingsEvent(lv_event_t *event);
    static void composeNavEvent(lv_event_t *event);
    static void replyEvent(lv_event_t *event);
    static void sendEvent(lv_event_t *event);
    static void nameEvent(lv_event_t *event);
    static void saveNameEvent(lv_event_t *event);
    static void favoriteEvent(lv_event_t *event);
    static void newHashEvent(lv_event_t *event);
    static void hashConfirmEvent(lv_event_t *event);
    static void identityEvent(lv_event_t *event);
    static void addressQrEvent(lv_event_t *event);
    static void identityQrEvent(lv_event_t *event);
    static void contactShareEvent(lv_event_t *event);
    static void userSettingsEvent(lv_event_t *event);
    static void messagingSettingsEvent(lv_event_t *event);
    static void watchSettingsEvent(lv_event_t *event);
    static void reticulumSettingsEvent(lv_event_t *event);
    static void editDisplayNameEvent(lv_event_t *event);
    static void editCanned1Event(lv_event_t *event);
    static void editCanned2Event(lv_event_t *event);
    static void userEditSaveEvent(lv_event_t *event);
    static void notificationToggleEvent(lv_event_t *event);
    static void wipeHistoryPromptEvent(lv_event_t *event);
    static void wipeHistoryConfirmEvent(lv_event_t *event);
    static void wipeHistoryCancelEvent(lv_event_t *event);
    static void hapticsToggleEvent(lv_event_t *event);
    static void announcePolicyEvent(lv_event_t *event);
    static void peerDiscoveryToggleEvent(lv_event_t *event);
    static void cannedReply1Event(lv_event_t *event);
    static void cannedReply2Event(lv_event_t *event);
    static void dismissNotificationEvent(lv_event_t *event);
    static void openNotificationEvent(lv_event_t *event);
    static void notificationCanned1Event(lv_event_t *event);
    static void notificationCanned2Event(lv_event_t *event);
    static void brightnessLowEvent(lv_event_t *event);
    static void brightnessMedEvent(lv_event_t *event);
    static void brightnessHighEvent(lv_event_t *event);
    static void powerSettingsEvent(lv_event_t *event);
    static void powerTimeoutEvent(lv_event_t *event);
    static void liftToWakeToggleEvent(lv_event_t *event);
    static void radioSettingsEvent(lv_event_t *event);
    static void radioFreqDownEvent(lv_event_t *event);
    static void radioFreqUpEvent(lv_event_t *event);
    static void radioBwEvent(lv_event_t *event);
    static void radioSfDownEvent(lv_event_t *event);
    static void radioSfUpEvent(lv_event_t *event);
    static void radioCrDownEvent(lv_event_t *event);
    static void radioCrUpEvent(lv_event_t *event);
    static void radioTxDownEvent(lv_event_t *event);
    static void radioTxUpEvent(lv_event_t *event);
    static void radioPreDownEvent(lv_event_t *event);
    static void radioPreUpEvent(lv_event_t *event);
    static void radioApplyRebootEvent(lv_event_t *event);
    static void radioApplyLaterEvent(lv_event_t *event);
    static void interfaceSettingsEvent(lv_event_t *event);
    static void networkToggleEvent(lv_event_t *event);
    static void networkModeEvent(lv_event_t *event);
    static void networkEditSsidEvent(lv_event_t *event);
    static void networkEditPasswordEvent(lv_event_t *event);
    static void networkEditHostEvent(lv_event_t *event);
    static void networkPortEvent(lv_event_t *event);
    static void networkEditSaveEvent(lv_event_t *event);
    static void networkApplyRebootEvent(lv_event_t *event);
    static void networkApplyLaterEvent(lv_event_t *event);
    static void routingSettingsEvent(lv_event_t *event);
    static void transportToggleEvent(lv_event_t *event);
    static void routingApplyRebootEvent(lv_event_t *event);
    static void routingApplyLaterEvent(lv_event_t *event);

    rw::hal::WatchHardware *hardware_{nullptr};
    rw::power::PowerManager *power_{nullptr};
    rw::storage::StorageService *storage_{nullptr};
    rw::radio::RadioService *radio_{nullptr};
    rw::reticulum::ReticulumService *reticulum_{nullptr};
    rw::transport::TransportService *transport_{nullptr};
    rw::lxmf::LxmfService *lxmf_{nullptr};
    rw::audio::AudioService *audio_{nullptr};
    rw::audio::VoiceService *voice_{nullptr};

    lv_obj_t *screenRoot_{nullptr};
    lv_obj_t *threadButtons_[kVisibleThreads]{};
    uint8_t threadHashes_[kVisibleThreads][16]{};
    lv_obj_t *peerButtons_[kVisiblePeers]{};
    uint8_t peerHashes_[kVisiblePeers][16]{};
    lv_obj_t *composeTextArea_{nullptr};
    lv_obj_t *composeStatusLabel_{nullptr};
    lv_obj_t *audioStatusLabel_{nullptr};
    lv_obj_t *audioStatsLabel_{nullptr};
    lv_obj_t *threadVoiceStatusLabel_{nullptr};
    lv_obj_t *sendingOverlay_{nullptr};

    static constexpr size_t kPendingSendTextBytes = 241;
    uint8_t pendingSendDestination_[16]{};
    char pendingSendText_[kPendingSendTextBytes]{};
    bool pendingTextSend_{false};
    uint32_t pendingTextSendAfterMs_{0};
    Screen pendingSendSuccessTarget_{Screen::Thread};

    uint8_t selectedPeer_[16]{};
    bool haveSelectedPeer_{false};
    uint8_t notificationPeer_[16]{};
    bool haveNotificationPeer_{false};
    uint32_t notificationSequence_{0};
    Screen screen_{Screen::WatchFace};
    Screen backTarget_{Screen::Menu};
    Screen peerDetailsReturnTarget_{Screen::Peers};
    Screen threadReturnTarget_{Screen::Conversations};
    Screen namePeerReturnTarget_{Screen::Thread};
    uint32_t displayedMessageCount_{0};
    uint32_t displayedUnreadCount_{0};
    uint32_t displayedDiscoveredCount_{0};
    uint32_t displayedDeliveryEvents_{0};
    uint32_t lastObservedMessageCount_{0};
    uint32_t lastUiUpdateMs_{0};
    uint32_t lastClockRefreshMs_{0};
    uint32_t displayedAudioRevision_{0};
    uint32_t displayedVoiceRevision_{0};
    uint32_t latestThreadVoiceSequence_{0};
    bool audioVoiceCompose_{false};
    rw::config::radio::Profile pendingRadioProfile_{rw::config::radio::defaultProfile()};
    bool radioSettingsDirty_{false};
    Screen radioSettingsExitTarget_{Screen::Settings};
    rw::config::network::Profile pendingNetworkProfile_{rw::config::network::defaultProfile()};
    bool networkSettingsDirty_{false};
    Screen networkSettingsExitTarget_{Screen::Settings};
    NetworkEditField networkEditField_{NetworkEditField::Ssid};
    rw::config::user::Profile userProfile_{rw::config::user::defaultProfile()};
    UserEditField userEditField_{UserEditField::DisplayName};
    char qrPayload_[65]{};
    char qrTitle_[25]{};
    static constexpr uint32_t kManualAnnounceCooldownMs = 60000UL;
    uint32_t lastManualAnnounceMs_{0};
    uint32_t lastAutoAnnounceMs_{0};
    bool startupAnnounceDone_{false};
    rw::config::transport::Profile pendingTransportProfile_{rw::config::transport::defaultProfile()};
    bool routingSettingsDirty_{false};
    Screen routingSettingsExitTarget_{Screen::Settings};
};

} // namespace rw::ui
