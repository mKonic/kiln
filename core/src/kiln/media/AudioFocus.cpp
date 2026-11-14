#include "kiln/media/AudioFocus.h"

#include "kiln/Log.h"
#include "kiln/media/Player.h"

#include <map>
#include <stdexcept>

namespace kiln::media {

    namespace {
        struct Registry {
            AudioHost host;
            int next = 1;
            std::map<int, AudioFocusManager*> focus;
            std::map<int, kiln::Function<void()>*> noisy;
        };
        Registry& R() {
            static Registry* r = new Registry;
            return *r;
        }
    }

    void SetAudioHost(AudioHost host) { R().host = std::move(host); }

    void OnAudioFocusChange(int id, int change) {
        auto it = R().focus.find(id);
        if (it != R().focus.end()) it->second->OnFocusChange(change);
    }
    void OnAudioBecomingNoisy(int id) {
        auto it = R().noisy.find(id);
        if (it != R().noisy.end() && *it->second) (*it->second)();
    }

    int FocusGainFor(const std::optional<AudioAttributes>& a) {
        // Video-only content, or an app that handles focus itself.
        if (!a) return kAudioFocusNone;
        switch (a->usage) {
            case AudioUsage::VoiceCommunicationSignalling: return kAudioFocusNone;   // DTMF during a call
            case AudioUsage::Game:
            case AudioUsage::Media: return kAudioFocusGain;
            case AudioUsage::Unknown:
                log::Warn("kiln", "audio focus: specify a proper usage in the audio attributes; using AUDIOFOCUS_GAIN");
                return kAudioFocusGain;
            case AudioUsage::Alarm:
            case AudioUsage::VoiceCommunication: return kAudioFocusGainTransient;
            case AudioUsage::AssistanceNavigationGuidance:
            case AudioUsage::AssistanceSonification:
            case AudioUsage::Notification:
            case AudioUsage::NotificationCommunicationDelayed:
            case AudioUsage::NotificationCommunicationInstant:
            case AudioUsage::NotificationCommunicationRequest:
            case AudioUsage::NotificationEvent:
            case AudioUsage::NotificationRingtone: return kAudioFocusGainTransientMayDuck;
            case AudioUsage::Assistant: return kAudioFocusGainTransientExclusive;
            case AudioUsage::AssistanceAccessibility:
                // Voice shouldn't be interrupted by other playback.
                return a->contentType == AudioContentType::Speech ? kAudioFocusGainTransient : kAudioFocusGainTransientMayDuck;
        }
        return kAudioFocusNone;
    }

    AudioFocusManager::AudioFocusManager(Control control) : m_Control(std::move(control)), m_Id(R().next++) { R().focus[m_Id] = this; }

    AudioFocusManager::~AudioFocusManager() {
        Release();
        R().focus.erase(m_Id);
    }

    void AudioFocusManager::SetAudioAttributes(std::optional<AudioAttributes> attributes) {
        if (m_Attributes == attributes) return;
        const int gain = FocusGainFor(attributes);
        if (gain != kAudioFocusGain && gain != kAudioFocusNone)
            throw std::invalid_argument("Automatic handling of audio focus is only available for USAGE_MEDIA and USAGE_GAME.");
        m_Attributes = attributes;
        m_FocusGain = gain;
    }

    AudioFocusManager::PlayerCommand AudioFocusManager::UpdateAudioFocus(bool playWhenReady, PlaybackState state) {
        // shouldHandleAudioFocus
        if (state == PlaybackState::Idle || m_FocusGain != kAudioFocusGain) {
            AbandonAudioFocusIfHeld();
            SetState(State::NotRequested);
            return PlayerCommand::PlayWhenReady;
        }
        if (playWhenReady) return RequestAudioFocus();
        switch (m_State) {
            case State::NoFocus: return PlayerCommand::DoNotPlay;
            case State::LossTransient: return PlayerCommand::WaitForCallback;
            default: return PlayerCommand::PlayWhenReady;
        }
    }

    void AudioFocusManager::Release() {
        m_Control = {};   // the player is going: its volume is not restored
        AbandonAudioFocusIfHeld();
        SetState(State::NotRequested);
    }

    AudioFocusManager::PlayerCommand AudioFocusManager::RequestAudioFocus() {
        if (m_State == State::HaveFocus) return PlayerCommand::PlayWhenReady;
        auto& host = R().host;
        const int result = host.requestFocus ? host.requestFocus(m_Id, m_FocusGain, *m_Attributes, WillPauseWhenDucked()) : kAudioFocusRequestGranted;
        if (result == kAudioFocusRequestGranted || result == kAudioFocusRequestDelayed) {
            SetState(State::HaveFocus);
            return PlayerCommand::PlayWhenReady;
        }
        SetState(State::NoFocus);
        return PlayerCommand::DoNotPlay;
    }

    void AudioFocusManager::AbandonAudioFocusIfHeld() {
        if (m_State == State::NoFocus || m_State == State::NotRequested) return;
        if (R().host.abandonFocus) R().host.abandonFocus(m_Id);
    }

    bool AudioFocusManager::WillPauseWhenDucked() const { return m_Attributes && m_Attributes->contentType == AudioContentType::Speech; }

    void AudioFocusManager::SetState(State s) {
        if (m_State == s) return;
        m_State = s;
        const float multiplier = s == State::LossTransientDuck ? kVolumeMultiplierDuck : 1.0f;
        if (m_VolumeMultiplier == multiplier) return;
        m_VolumeMultiplier = multiplier;
        if (m_Control.setVolumeMultiplier) m_Control.setVolumeMultiplier(multiplier);
    }

    void AudioFocusManager::Execute(PlayerCommand c) {
        if (m_Control.executePlayerCommand) m_Control.executePlayerCommand(c);
    }

    void AudioFocusManager::OnFocusChange(int change) {
        switch (change) {
            case kAudioFocusGain:
                SetState(State::HaveFocus);
                Execute(PlayerCommand::PlayWhenReady);
                return;
            case kAudioFocusLoss:
                Execute(PlayerCommand::DoNotPlay);
                AbandonAudioFocusIfHeld();
                SetState(State::NoFocus);
                return;
            case kAudioFocusLossTransient:
            case kAudioFocusLossTransientCanDuck:
                if (change == kAudioFocusLossTransient || WillPauseWhenDucked()) {
                    Execute(PlayerCommand::WaitForCallback);
                    SetState(State::LossTransient);
                } else {
                    SetState(State::LossTransientDuck);
                }
                return;
            default:
                log::Warn("kiln", "audio focus: unknown focus change {}", change);
        }
    }

    AudioBecomingNoisyManager::AudioBecomingNoisyManager(kiln::Function<void()> listener) : m_Listener(std::move(listener)), m_Id(R().next++) {
        R().noisy[m_Id] = &m_Listener;
    }
    AudioBecomingNoisyManager::~AudioBecomingNoisyManager() {
        SetEnabled(false);
        R().noisy.erase(m_Id);
    }
    void AudioBecomingNoisyManager::SetEnabled(bool enabled) {
        if (enabled == m_Enabled) return;
        m_Enabled = enabled;
        if (R().host.setNoisyReceiver) R().host.setNoisyReceiver(m_Id, enabled);
    }

}
