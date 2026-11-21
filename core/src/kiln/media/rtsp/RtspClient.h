#pragma once

// media3 exoplayer-rtsp's RtspMessageChannel (RTSP messages and interleaved RTP over one TCP
// connection) and RtspClient (OPTIONS, DESCRIBE, SETUP, PLAY, PAUSE, TEARDOWN; authentication,
// redirects and keep-alive). The client runs on a TaskRunner, as upstream's on its looper.

#include "kiln/media/rtsp/Rtp.h"
#include "kiln/media/rtsp/RtspSocket.h"

#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace kiln::media::rtsp {

    class RtspMessageChannel {
    public:
        static constexpr int DEFAULT_RTSP_PORT = 554;
        struct Listener {
            std::function<void(std::vector<std::string>)> onMessage;   // on the receiver thread
            std::function<void(const std::string&)> onReceivingFailed;
        };
        explicit RtspMessageChannel(Listener listener) : m_Listener(std::move(listener)) {}
        ~RtspMessageChannel() { Close(); }
        // Takes the connection and starts reading it.
        void Open(TcpSocket socket);
        void Close();
        // Sends a message's lines (a failure is reported to no one, as upstream's default).
        void Send(const std::vector<std::string>& message);
        // Interleaved binary data ($, channel, length) on the connection (a server's side).
        void SendInterleaved(int channel, const std::vector<std::uint8_t>& data);
        // Interleaved data for the channel goes to the callback (on the receiver thread).
        void RegisterInterleavedBinaryDataListener(int channel, std::function<void(std::vector<std::uint8_t>)> listener);

    private:
        void Receive();
        // MessageParser.parseNext: a message's lines (its body as one last line).
        bool ParseNext(std::uint8_t firstByte, std::vector<std::string>& message);
        bool ReadLine(std::uint8_t firstByte, std::string& line);

        Listener m_Listener;
        TcpSocket m_Socket;
        std::mutex m_SendMutex, m_ListenerMutex;
        std::map<int, std::function<void(std::vector<std::uint8_t>)>> m_Interleaved;
        std::thread m_Receiver;
        std::atomic<bool> m_Closed{ false };
    };

    class RtspClient {
    public:
        enum State { RTSP_STATE_UNINITIALIZED = -1, RTSP_STATE_INIT = 0, RTSP_STATE_READY = 1, RTSP_STATE_PLAYING = 2 };
        struct SessionInfoListener {
            virtual ~SessionInfoListener() = default;
            virtual void OnSessionTimelineUpdated(const RtspSessionTiming& timing, const std::vector<RtspMediaTrack>& tracks) = 0;
            virtual void OnSessionTimelineRequestFailed(const std::string& message) = 0;
        };
        struct PlaybackEventListener {
            virtual ~PlaybackEventListener() = default;
            virtual void OnRtspSetupCompleted() = 0;
            virtual void OnPlaybackStarted(std::int64_t startPositionUs, const std::vector<RtspTrackTiming>& trackTimings) = 0;
            // udpUnsupported: a SETUP over UDP got 461 (RtspUdpUnsupportedTransportException).
            virtual void OnPlaybackError(const std::string& message, bool udpUnsupported) = 0;
        };
        struct LoadInfo {
            std::string trackUri, transport;
        };
        using SocketFactory = std::function<TcpSocket(const std::string& host, int port)>;

        // The listeners and calls run on handler.
        RtspClient(TaskRunner& handler, SessionInfoListener& sessionInfoListener, PlaybackEventListener& playbackEventListener, std::string userAgent,
                   const std::string& uri, SocketFactory socketFactory = nullptr, bool debugLoggingEnabled = false);
        ~RtspClient();
        // Connects and sends OPTIONS (throws RtspIoException).
        void Start();
        State GetState() const { return m_State; }
        void SetupSelectedTracks(const std::vector<LoadInfo>& loadInfos);
        void StartPlayback(std::int64_t offsetMs);
        void SignalPlaybackEnded() { m_State = RTSP_STATE_READY; }
        void SeekToUs(std::int64_t positionUs);
        // TEARDOWN when playing, then the connection closes.
        void Close();
        // A new connection for RTP over TCP after UDP failed.
        void RetryWithRtpTcp();
        void RegisterInterleavedDataChannel(int channel, std::function<void(std::vector<std::uint8_t>)> listener);

    private:
        friend class RtspClientTestAccess;
        void ContinueSetupRtspTrack();
        void MaybeLogMessage(const std::vector<std::string>& message) const;
        TcpSocket GetSocket(const std::string& uri);
        void DispatchRtspError(const std::string& message, bool udpUnsupported = false);
        // MessageSender.
        void SendOptionsRequest(const std::string& uri, const std::optional<std::string>& sessionId);
        void SendDescribeRequest(const std::string& uri, const std::optional<std::string>& sessionId);
        void SendSetupRequest(const std::string& trackUri, const std::string& transport, const std::optional<std::string>& sessionId);
        void SendPlayRequest(const std::string& uri, std::int64_t offsetMs, const std::string& sessionId);
        void SendTeardownRequest(const std::string& uri, const std::string& sessionId);
        void SendPauseRequest(const std::string& uri, const std::string& sessionId);
        void RetryLastRequest();
        void SendMethodNotAllowedResponse(int cSeq);
        RtspRequest RequestWithCommonHeaders(RtspMethod method, const std::optional<std::string>& sessionId,
                                             const std::vector<std::pair<std::string, std::string>>& additionalHeaders, const std::string& uri);
        void SendRequest(const RtspRequest& request);
        // MessageListener.
        void HandleRtspMessage(const std::vector<std::string>& message);
        void HandleRtspResponse(const std::vector<std::string>& message);
        void OnOptionsResponse(const std::vector<RtspMethod>& supportedMethods);
        void OnDescribeResponse(const RtspHeaders& headers, const SessionDescription& sessionDescription);
        void OnSetupResponse(const RtspSessionHeader& sessionHeader);
        void OnPlayResponse(const RtspSessionTiming& timing, const std::vector<RtspTrackTiming>& trackTimings);
        void OnPauseResponse();
        std::unique_ptr<RtspMessageChannel> NewChannel();
        void StartKeepAlive();
        void ScheduleKeepAlive();
        void StopKeepAlive();

        TaskRunner& m_Handler;
        SessionInfoListener& m_SessionInfoListener;
        PlaybackEventListener& m_PlaybackEventListener;
        std::string m_UserAgent;
        SocketFactory m_SocketFactory;
        bool m_DebugLoggingEnabled = false;
        std::deque<LoadInfo> m_PendingSetupLoadInfos;
        std::map<int, RtspRequest> m_PendingRequests;
        std::string m_Uri;
        std::unique_ptr<RtspMessageChannel> m_MessageChannel;
        std::optional<RtspAuthUserInfo> m_AuthUserInfo;
        std::optional<std::string> m_SessionId;
        std::int64_t m_SessionTimeoutMs = RtspMessageUtil::kDefaultRtspTimeoutMs;
        std::optional<RtspAuthenticationInfo> m_AuthenticationInfo;
        State m_State = RTSP_STATE_UNINITIALIZED;
        bool m_HasUpdatedTimelineAndTracks = false, m_ReceivedAuthorizationRequest = false, m_HasPendingPauseRequest = false;
        std::int64_t m_PendingSeekPositionUs = kTimeUnset;
        int m_CSeq = 0;
        std::optional<RtspRequest> m_LastRequest;
        std::optional<std::uint64_t> m_KeepAliveTask;   // KeepAliveMonitor
        std::shared_ptr<std::atomic<bool>> m_Alive = std::make_shared<std::atomic<bool>>(true);
    };

}
