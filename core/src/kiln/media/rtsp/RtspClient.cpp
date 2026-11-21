// media3 exoplayer-rtsp: RtspMessageChannel and RtspClient.
#include "kiln/media/rtsp/RtspClient.h"

#include "kiln/Log.h"

#include <algorithm>

namespace kiln::media::rtsp {

    using namespace detail;

    // --- RtspMessageChannel ---------------------------------------------------------------------
    void RtspMessageChannel::Open(TcpSocket socket) {
        m_Socket = std::move(socket);
        m_Receiver = std::thread([this] { Receive(); });
    }

    void RtspMessageChannel::Close() {
        if (m_Closed.exchange(true)) return;
        m_Socket.Shutdown();
        if (m_Receiver.joinable()) {
            if (m_Receiver.get_id() == std::this_thread::get_id()) m_Receiver.detach();
            else m_Receiver.join();
        }
        m_Socket.Close();
    }

    void RtspMessageChannel::Send(const std::vector<std::string>& message) {
        const std::string bytes = RtspMessageUtil::ConvertMessageToBytes(message);
        std::lock_guard lock(m_SendMutex);
        try {
            if (!m_Closed) m_Socket.Write(bytes.data(), bytes.size());
        } catch (const RtspIoException&) {
            // onSendingFailed: nobody listens (RtspClient keeps upstream's default).
        }
    }

    void RtspMessageChannel::SendInterleaved(int channel, const std::vector<std::uint8_t>& data) {
        std::vector<std::uint8_t> frame = { '$', std::uint8_t(channel), std::uint8_t(data.size() >> 8), std::uint8_t(data.size()) };
        frame.insert(frame.end(), data.begin(), data.end());
        std::lock_guard lock(m_SendMutex);
        try {
            if (!m_Closed) m_Socket.Write(frame.data(), frame.size());
        } catch (const RtspIoException&) {
        }
    }

    void RtspMessageChannel::RegisterInterleavedBinaryDataListener(int channel, std::function<void(std::vector<std::uint8_t>)> listener) {
        std::lock_guard lock(m_ListenerMutex);
        m_Interleaved[channel] = std::move(listener);
    }

    void RtspMessageChannel::Receive() {
        try {
            while (!m_Closed) {
                std::uint8_t first = 0;
                if (!m_Socket.ReadFully(&first, 1)) throw RtspIoException("RTSP connection closed");
                if (first == '$') {
                    std::uint8_t header[3];
                    if (!m_Socket.ReadFully(header, 3)) throw RtspIoException("RTSP connection closed");
                    const int channel = header[0];
                    const std::size_t size = std::size_t(header[1]) << 8 | header[2];
                    std::vector<std::uint8_t> data(size);
                    if (size && !m_Socket.ReadFully(data.data(), size)) throw RtspIoException("RTSP connection closed");
                    std::function<void(std::vector<std::uint8_t>)> listener;
                    {
                        std::lock_guard lock(m_ListenerMutex);
                        if (auto it = m_Interleaved.find(channel); it != m_Interleaved.end()) listener = it->second;
                    }
                    if (listener && !m_Closed) listener(std::move(data));
                } else {
                    std::vector<std::string> message;
                    if (!ParseNext(first, message)) throw RtspIoException("RTSP connection closed");
                    if (!m_Closed && m_Listener.onMessage) m_Listener.onMessage(std::move(message));
                }
            }
        } catch (const std::exception& e) {
            if (!m_Closed && m_Listener.onReceivingFailed) m_Listener.onReceivingFailed(e.what());
        }
    }

    bool RtspMessageChannel::ReadLine(std::uint8_t firstByte, std::string& line) {
        line.clear();
        std::uint8_t peeked[2] = { firstByte, 0 };
        if (!m_Socket.ReadFully(&peeked[1], 1)) return false;
        line.push_back(char(peeked[0]));
        line.push_back(char(peeked[1]));
        while (peeked[0] != '\r' || peeked[1] != '\n') {
            peeked[0] = peeked[1];
            if (!m_Socket.ReadFully(&peeked[1], 1)) return false;
            line.push_back(char(peeked[1]));
        }
        return true;
    }

    bool RtspMessageChannel::ParseNext(std::uint8_t firstByte, std::vector<std::string>& lines) {
        enum { FirstLine, Header, Body } state = FirstLine;
        std::int64_t bodyLength = 0;
        std::string raw;
        if (!ReadLine(firstByte, raw)) return false;
        for (;;) {
            // addMessageLine.
            if (raw.size() < 2 || raw[raw.size() - 2] != '\r' || raw.back() != '\n') throw std::invalid_argument("RTSP line without CRLF");
            const std::string line = raw.substr(0, raw.size() - 2);
            lines.push_back(line);
            if (state == FirstLine) {
                if (RtspMessageUtil::IsRtspStartLine(line)) state = Header;
            } else {
                if (const std::int64_t length = RtspMessageUtil::ParseContentLengthHeader(line); length != stream::kLengthUnset) bodyLength = length;
                if (line.empty()) {
                    if (bodyLength <= 0) return true;
                    state = Body;
                }
            }
            if (state == Body) {
                std::string body(std::size_t(bodyLength), '\0');
                if (!m_Socket.ReadFully(body.data(), body.size())) return false;
                // addMessageBody: the body must end with LF; CRLF or LF is dropped.
                if (body.empty() || body.back() != '\n') throw std::invalid_argument("Message body is empty or does not end with a LF.");
                body.pop_back();
                if (!body.empty() && body.back() == '\r') body.pop_back();
                lines.push_back(body);
                return true;
            }
            std::uint8_t next = 0;
            if (!m_Socket.ReadFully(&next, 1) || !ReadLine(next, raw)) return false;
        }
    }

    // --- RtspClient -----------------------------------------------------------------------------
    RtspClient::RtspClient(TaskRunner& handler, SessionInfoListener& sessionInfoListener, PlaybackEventListener& playbackEventListener, std::string userAgent,
                           const std::string& uri, SocketFactory socketFactory, bool debugLoggingEnabled)
        : m_Handler(handler), m_SessionInfoListener(sessionInfoListener), m_PlaybackEventListener(playbackEventListener), m_UserAgent(std::move(userAgent)),
          m_SocketFactory(std::move(socketFactory)), m_DebugLoggingEnabled(debugLoggingEnabled), m_Uri(RtspMessageUtil::RemoveUserInfo(uri)), m_AuthUserInfo(RtspMessageUtil::ParseUserInfo(uri)) {
        m_MessageChannel = NewChannel();
    }

    RtspClient::~RtspClient() {
        *m_Alive = false;
        StopKeepAlive();
        if (m_MessageChannel) m_MessageChannel->Close();
    }

    std::unique_ptr<RtspMessageChannel> RtspClient::NewChannel() {
        std::weak_ptr<std::atomic<bool>> alive = m_Alive;
        RtspMessageChannel::Listener listener;
        listener.onMessage = [this, alive](std::vector<std::string> message) {
            // MessageListener.onRtspMessageReceived: handled on the client's thread.
            m_Handler.Post([this, alive, message = std::move(message)] {
                if (auto a = alive.lock(); a && *a) HandleRtspMessage(message);
            });
        };
        return std::make_unique<RtspMessageChannel>(std::move(listener));
    }

    void RtspClient::Start() {
        try {
            m_MessageChannel->Open(GetSocket(m_Uri));
        } catch (const RtspIoException&) {
            m_MessageChannel->Close();
            throw;
        }
        SendOptionsRequest(m_Uri, m_SessionId);
    }

    void RtspClient::SetupSelectedTracks(const std::vector<LoadInfo>& loadInfos) {
        m_PendingSetupLoadInfos.insert(m_PendingSetupLoadInfos.end(), loadInfos.begin(), loadInfos.end());
        ContinueSetupRtspTrack();
    }

    void RtspClient::StartPlayback(std::int64_t offsetMs) { SendPlayRequest(m_Uri, offsetMs, m_SessionId.value_or("")); }

    void RtspClient::SeekToUs(std::int64_t positionUs) {
        if (m_State == RTSP_STATE_PLAYING && !m_HasPendingPauseRequest) SendPauseRequest(m_Uri, m_SessionId.value_or(""));
        m_PendingSeekPositionUs = positionUs;
    }

    void RtspClient::Close() {
        if (m_KeepAliveTask) {
            StopKeepAlive();
            SendTeardownRequest(m_Uri, m_SessionId.value_or(""));
        }
        m_MessageChannel->Close();
    }

    void RtspClient::RetryWithRtpTcp() {
        try {
            Close();
            m_MessageChannel = NewChannel();
            m_MessageChannel->Open(GetSocket(m_Uri));
            m_SessionId.reset();
            m_ReceivedAuthorizationRequest = false;
            m_AuthenticationInfo.reset();
        } catch (const RtspIoException& e) {
            m_PlaybackEventListener.OnPlaybackError(e.what(), false);
        }
    }

    void RtspClient::RegisterInterleavedDataChannel(int channel, std::function<void(std::vector<std::uint8_t>)> listener) {
        m_MessageChannel->RegisterInterleavedBinaryDataListener(channel, std::move(listener));
    }

    void RtspClient::MaybeLogMessage(const std::vector<std::string>& message) const {
        if (!m_DebugLoggingEnabled) return;
        std::string joined;
        for (const auto& line : message) joined += (joined.empty() ? "" : "\n") + line;
        log::Debug("RtspClient", "{}", joined);
    }

    void RtspClient::ContinueSetupRtspTrack() {
        if (m_PendingSetupLoadInfos.empty()) {
            m_PlaybackEventListener.OnRtspSetupCompleted();
            return;
        }
        const LoadInfo info = m_PendingSetupLoadInfos.front();
        m_PendingSetupLoadInfos.pop_front();
        SendSetupRequest(info.trackUri, info.transport, m_SessionId);
    }

    TcpSocket RtspClient::GetSocket(const std::string& uri) {
        const auto host = UriHost(uri);
        if (!host) throw std::invalid_argument("no host in " + uri);
        const int port = UriPort(uri).value_or(-1) > 0 ? *UriPort(uri) : RtspMessageChannel::DEFAULT_RTSP_PORT;
        return m_SocketFactory ? m_SocketFactory(*host, port) : TcpSocket::Connect(*host, port);
    }

    void RtspClient::DispatchRtspError(const std::string& message, bool udpUnsupported) {
        if (m_HasUpdatedTimelineAndTracks) m_PlaybackEventListener.OnPlaybackError(message, udpUnsupported);
        else m_SessionInfoListener.OnSessionTimelineRequestFailed(message);
    }

    // --- MessageSender --------------------------------------------------------------------------
    void RtspClient::SendOptionsRequest(const std::string& uri, const std::optional<std::string>& sessionId) {
        SendRequest(RequestWithCommonHeaders(RtspMethod::Options, sessionId, {}, uri));
    }

    void RtspClient::SendDescribeRequest(const std::string& uri, const std::optional<std::string>& sessionId) {
        SendRequest(RequestWithCommonHeaders(RtspMethod::Describe, sessionId, { { RtspHeaders::ACCEPT, "application/sdp" } }, uri));
    }

    void RtspClient::SendSetupRequest(const std::string& trackUri, const std::string& transport, const std::optional<std::string>& sessionId) {
        m_State = RTSP_STATE_INIT;
        SendRequest(RequestWithCommonHeaders(RtspMethod::Setup, sessionId, { { RtspHeaders::TRANSPORT, transport } }, trackUri));
    }

    void RtspClient::SendPlayRequest(const std::string& uri, std::int64_t offsetMs, const std::string& sessionId) {
        if (m_State != RTSP_STATE_READY && m_State != RTSP_STATE_PLAYING) throw std::logic_error("PLAY before SETUP");
        SendRequest(RequestWithCommonHeaders(RtspMethod::Play, sessionId, { { RtspHeaders::RANGE, RtspSessionTiming::GetOffsetStartTimeTiming(offsetMs) } }, uri));
    }

    void RtspClient::SendTeardownRequest(const std::string& uri, const std::string& sessionId) {
        if (m_State == RTSP_STATE_UNINITIALIZED || m_State == RTSP_STATE_INIT) return;
        m_State = RTSP_STATE_INIT;
        SendRequest(RequestWithCommonHeaders(RtspMethod::Teardown, sessionId, {}, uri));
    }

    void RtspClient::SendPauseRequest(const std::string& uri, const std::string& sessionId) {
        if (m_State != RTSP_STATE_PLAYING) throw std::logic_error("PAUSE when not playing");
        SendRequest(RequestWithCommonHeaders(RtspMethod::Pause, sessionId, {}, uri));
        m_HasPendingPauseRequest = true;
    }

    void RtspClient::RetryLastRequest() {
        if (!m_LastRequest) throw std::logic_error("no request to retry");
        std::vector<std::pair<std::string, std::string>> headers;
        for (const auto& [name, values] : m_LastRequest->headers.AsMultiMap()) {
            if (name == RtspHeaders::CSEQ || name == RtspHeaders::USER_AGENT || name == RtspHeaders::SESSION || name == RtspHeaders::AUTHORIZATION) continue;
            headers.emplace_back(name, values.back());
        }
        SendRequest(RequestWithCommonHeaders(m_LastRequest->method, m_SessionId, headers, m_LastRequest->uri));
    }

    void RtspClient::SendMethodNotAllowedResponse(int cSeq) {
        const auto message = RtspMessageUtil::SerializeResponse({ 405, RtspHeaders::Builder(m_UserAgent, m_SessionId, cSeq).Build(), "" });
        MaybeLogMessage(message);
        m_MessageChannel->Send(message);
        m_CSeq = std::max(m_CSeq, cSeq + 1);
    }

    RtspRequest RtspClient::RequestWithCommonHeaders(RtspMethod method, const std::optional<std::string>& sessionId,
                                                     const std::vector<std::pair<std::string, std::string>>& additionalHeaders, const std::string& uri) {
        RtspHeaders::Builder headers(m_UserAgent, sessionId, m_CSeq++);
        if (m_AuthenticationInfo) {
            try {
                headers.Add(RtspHeaders::AUTHORIZATION, m_AuthenticationInfo->GetAuthorizationHeaderValue(m_AuthUserInfo.value_or(RtspAuthUserInfo{}), uri, method));
            } catch (const std::exception& e) {
                DispatchRtspError(e.what());
            }
        }
        headers.AddAll(additionalHeaders);
        return { uri, method, headers.Build(), "" };
    }

    void RtspClient::SendRequest(const RtspRequest& request) {
        const int cSeq = std::stoi(request.headers.Get(RtspHeaders::CSEQ).value_or("-1"));
        if (m_PendingRequests.count(cSeq)) throw std::logic_error("a request with CSeq " + std::to_string(cSeq) + " is pending");
        m_PendingRequests[cSeq] = request;
        const auto message = RtspMessageUtil::SerializeRequest(request);
        MaybeLogMessage(message);
        m_MessageChannel->Send(message);
        m_LastRequest = request;
    }

    // --- MessageListener ------------------------------------------------------------------------
    void RtspClient::HandleRtspMessage(const std::vector<std::string>& message) {
        MaybeLogMessage(message);
        if (RtspMessageUtil::IsRtspResponse(message)) {
            HandleRtspResponse(message);
        } else {
            const RtspRequest request = RtspMessageUtil::ParseRequest(message);
            SendMethodNotAllowedResponse(std::stoi(request.headers.Get(RtspHeaders::CSEQ).value_or("0")));
        }
    }

    void RtspClient::HandleRtspResponse(const std::vector<std::string>& message) {
        const RtspResponse response = RtspMessageUtil::ParseResponse(message);
        const int cSeq = std::stoi(response.headers.Get(RtspHeaders::CSEQ).value_or("-1"));
        auto pending = m_PendingRequests.find(cSeq);
        if (pending == m_PendingRequests.end()) return;
        const RtspRequest request = pending->second;
        m_PendingRequests.erase(pending);
        const RtspMethod method = request.method;
        const std::string methodAndStatus = RtspMessageUtil::ToMethodString(method) + " " + std::to_string(response.status);
        try {
            switch (response.status) {
                case 200:
                    break;
                case 301:
                case 302: {
                    if (m_State != RTSP_STATE_UNINITIALIZED) m_State = RTSP_STATE_INIT;
                    const auto location = response.headers.Get(RtspHeaders::LOCATION);
                    if (!location) {
                        m_SessionInfoListener.OnSessionTimelineRequestFailed("Redirection without new location.");
                    } else {
                        m_Uri = *location;
                        if (auto user = RtspMessageUtil::ParseUserInfo(m_Uri)) m_AuthUserInfo = user;
                        SendDescribeRequest(m_Uri, m_SessionId);
                    }
                    return;
                }
                case 401: {
                    if (m_AuthUserInfo && !m_ReceivedAuthorizationRequest) {
                        const auto values = response.headers.Values(RtspHeaders::WWW_AUTHENTICATE);
                        if (values.empty()) throw ParserException("Missing WWW-Authenticate header in a 401 response.");
                        for (const auto& value : values) {
                            m_AuthenticationInfo = RtspMessageUtil::ParseWwwAuthenticateHeader(value);
                            if (m_AuthenticationInfo->authenticationMechanism == RtspAuthenticationInfo::DIGEST) break;
                        }
                        RetryLastRequest();
                        m_ReceivedAuthorizationRequest = true;
                        return;
                    }
                    DispatchRtspError(methodAndStatus);
                    return;
                }
                case 461: {
                    const std::string transport = request.headers.Get(RtspHeaders::TRANSPORT).value_or("");
                    DispatchRtspError(methodAndStatus, method == RtspMethod::Setup && transport.find("TCP") == std::string::npos);
                    return;
                }
                default:
                    DispatchRtspError(methodAndStatus);
                    return;
            }
            switch (method) {
                case RtspMethod::Options:
                    OnOptionsResponse(RtspMessageUtil::ParsePublicHeader(response.headers.Get(RtspHeaders::PUBLIC)));
                    break;
                case RtspMethod::Describe:
                    OnDescribeResponse(response.headers, SessionDescriptionParser::Parse(response.messageBody));
                    break;
                case RtspMethod::Setup: {
                    const auto session = response.headers.Get(RtspHeaders::SESSION);
                    const auto transport = response.headers.Get(RtspHeaders::TRANSPORT);
                    if (!session || !transport) throw ParserException("Missing mandatory session or transport header");
                    OnSetupResponse(RtspMessageUtil::ParseSessionHeader(*session));
                    break;
                }
                case RtspMethod::Play: {
                    const auto range = response.headers.Get(RtspHeaders::RANGE);
                    const RtspSessionTiming timing = range ? RtspSessionTiming::ParseTiming(*range) : RtspSessionTiming::Default();
                    std::vector<RtspTrackTiming> trackTimings;
                    try {
                        if (const auto rtpInfo = response.headers.Get(RtspHeaders::RTP_INFO)) trackTimings = RtspTrackTiming::ParseTrackTiming(*rtpInfo, m_Uri);
                    } catch (const ParserException&) {
                        trackTimings.clear();
                    }
                    OnPlayResponse(timing, trackTimings);
                    break;
                }
                case RtspMethod::Pause:
                    OnPauseResponse();
                    break;
                case RtspMethod::Unset:
                    throw std::logic_error("no method");
                default:
                    break;
            }
        } catch (const ParserException& e) {
            DispatchRtspError(e.what());
        } catch (const std::invalid_argument& e) {
            DispatchRtspError(e.what());
        }
    }

    void RtspClient::OnOptionsResponse(const std::vector<RtspMethod>& supported) {
        if (m_KeepAliveTask) return;
        if (supported.empty() || std::find(supported.begin(), supported.end(), RtspMethod::Describe) != supported.end()) SendDescribeRequest(m_Uri, m_SessionId);
        else m_SessionInfoListener.OnSessionTimelineRequestFailed("DESCRIBE not supported.");
    }

    void RtspClient::OnDescribeResponse(const RtspHeaders& headers, const SessionDescription& session) {
        RtspSessionTiming timing = RtspSessionTiming::Default();
        if (auto range = session.attributes.find(SessionDescription::ATTR_RANGE); range != session.attributes.end()) {
            try {
                timing = RtspSessionTiming::ParseTiming(range->second);
            } catch (const ParserException&) {
                m_SessionInfoListener.OnSessionTimelineRequestFailed("SDP format error.");
                return;
            }
        }
        std::vector<RtspMediaTrack> tracks;
        for (const MediaDescription& d : session.mediaDescriptionList)
            if (RtpPayloadFormat::IsFormatSupported(d)) tracks.emplace_back(headers, d, m_Uri);
        if (tracks.empty()) {
            m_SessionInfoListener.OnSessionTimelineRequestFailed("No playable track.");
            return;
        }
        m_SessionInfoListener.OnSessionTimelineUpdated(timing, tracks);
        m_HasUpdatedTimelineAndTracks = true;
    }

    void RtspClient::OnSetupResponse(const RtspSessionHeader& sessionHeader) {
        if (m_State == RTSP_STATE_UNINITIALIZED) throw std::logic_error("SETUP response before SETUP");
        m_State = RTSP_STATE_READY;
        m_SessionId = sessionHeader.sessionId;
        m_SessionTimeoutMs = sessionHeader.timeoutMs;
        ContinueSetupRtspTrack();
    }

    void RtspClient::OnPlayResponse(const RtspSessionTiming& timing, const std::vector<RtspTrackTiming>& trackTimings) {
        if (m_State != RTSP_STATE_READY && m_State != RTSP_STATE_PLAYING) throw std::logic_error("PLAY response before SETUP");
        m_State = RTSP_STATE_PLAYING;
        StartKeepAlive();
        m_PendingSeekPositionUs = kTimeUnset;
        m_PlaybackEventListener.OnPlaybackStarted(stream::MsToUs(timing.startTimeMs), trackTimings);
    }

    void RtspClient::OnPauseResponse() {
        if (m_State != RTSP_STATE_PLAYING) throw std::logic_error("PAUSE response when not playing");
        m_State = RTSP_STATE_READY;
        m_HasPendingPauseRequest = false;
        if (m_PendingSeekPositionUs != kTimeUnset) StartPlayback(m_PendingSeekPositionUs / 1000);
    }

    // KeepAliveMonitor: OPTIONS every half of the session's timeout.
    void RtspClient::StartKeepAlive() {
        if (m_KeepAliveTask) return;
        ScheduleKeepAlive();
    }

    void RtspClient::ScheduleKeepAlive() {
        std::weak_ptr<std::atomic<bool>> alive = m_Alive;
        m_KeepAliveTask = m_Handler.PostDelayed([this, alive] {
            auto a = alive.lock();
            if (!a || !*a || !m_KeepAliveTask) return;
            SendOptionsRequest(m_Uri, m_SessionId);
            ScheduleKeepAlive();
        }, m_SessionTimeoutMs / 2);
    }

    void RtspClient::StopKeepAlive() {
        if (m_KeepAliveTask) m_Handler.Cancel(*m_KeepAliveTask);
        m_KeepAliveTask.reset();
    }

}
