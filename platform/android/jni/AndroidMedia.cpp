// media::Player's engine on Android: an extractor and an AMediaCodec decoder per track, sound
// through AAudio, on a playback thread that works every 10 ms while it plays (ExoPlayer's
// doSomeWork loop). Video frames are released against the audio clock, or a standalone clock
// without sound (StandaloneMediaClock), as DecideFrameRelease says.
#include "AndroidMedia.h"

#include "kiln/media/Player.h"
#include "kiln/ui/ContentResolver.h"

#include <aaudio/AAudio.h>
#include "kiln/media/Sonic.h"
#include "kiln/media/TrackSelection.h"
#include "kiln/ui/Configuration.h"
#include <android/log.h>
#include <android/native_window.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaDataSource.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace kiln::android {

    namespace {
        std::int64_t NowNs() {
            timespec ts{};
            clock_gettime(CLOCK_MONOTONIC, &ts);
            return std::int64_t(ts.tv_sec) * 1'000'000'000 + ts.tv_nsec;
        }
        std::int64_t NowUs() { return NowNs() / 1000; }

        // Where the bytes come from: a file descriptor range, a URL, or memory.
        struct Source {
            int fd = -1;
            off64_t offset = 0, length = 0;
            std::string url;
            std::shared_ptr<std::vector<std::uint8_t>> bytes;
            ~Source() { if (fd >= 0) close(fd); }
        };

        ssize_t ReadMemory(void* user, off64_t offset, void* buffer, size_t size) {
            auto* bytes = static_cast<std::vector<std::uint8_t>*>(user);
            if (offset < 0 || std::size_t(offset) >= bytes->size()) return -1;
            size_t n = std::min(size, bytes->size() - std::size_t(offset));
            std::memcpy(buffer, bytes->data() + offset, n);
            return ssize_t(n);
        }
        ssize_t MemorySize(void* user) { return ssize_t(static_cast<std::vector<std::uint8_t>*>(user)->size()); }
        void MemoryClose(void*) {}

        std::unique_ptr<Source> OpenSource(const std::string& uri, std::string& error) {
            auto s = std::make_unique<Source>();
            auto prefixed = [&](const char* p) { return uri.rfind(p, 0) == 0; };
            if (prefixed("http://") || prefixed("https://")) {
                s->url = uri;
                return s;
            }
            if (prefixed("asset:///") || prefixed("file:///android_asset/")) {
                std::string path = uri.substr(prefixed("asset:///") ? 9 : 22);
                AAssetManager* mgr = AssetManager();
                AAsset* a = mgr ? AAssetManager_open(mgr, path.c_str(), AASSET_MODE_RANDOM) : nullptr;
                if (!a) {
                    error = "no asset " + path;
                    return nullptr;
                }
                off64_t start = 0, length = 0;
                s->fd = AAsset_openFileDescriptor64(a, &start, &length);
                if (s->fd >= 0) {
                    s->offset = start;
                    s->length = length;
                } else {
                    // Compressed in the APK: read it into memory.
                    const auto* data = static_cast<const std::uint8_t*>(AAsset_getBuffer(a));
                    if (data) s->bytes = std::make_shared<std::vector<std::uint8_t>>(data, data + AAsset_getLength64(a));
                }
                AAsset_close(a);
                if (s->fd < 0 && !s->bytes) {
                    error = "cannot read asset " + path;
                    return nullptr;
                }
                return s;
            }
            if (prefixed("content://")) {
                s->fd = ui::GetContentResolver().OpenFileDescriptor(uri, "r");
            } else {
                std::string path = prefixed("file://") ? uri.substr(7) : uri;
                s->fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
            }
            if (s->fd < 0) {
                error = "cannot open " + uri;
                return nullptr;
            }
            struct stat st{};
            fstat(s->fd, &st);
            s->length = st.st_size;
            return s;
        }

        // One track: its own extractor (seeked on its own) and decoder.
        struct Track {
            int index = -1;
            std::string mime;
            AMediaExtractor* extractor = nullptr;
            AMediaDataSource* dataSource = nullptr;
            AMediaFormat* format = nullptr;
            AMediaCodec* codec = nullptr;
            bool inputDone = false, outputDone = false;
            std::int64_t seekTargetUs = 0;   // frames before it are decoded, not shown or played

            void ReleaseCodec() {
                if (codec) {
                    AMediaCodec_stop(codec);
                    AMediaCodec_delete(codec);
                    codec = nullptr;
                }
            }
            ~Track() {
                ReleaseCodec();
                if (format) AMediaFormat_delete(format);
                if (extractor) AMediaExtractor_delete(extractor);
                if (dataSource) AMediaDataSource_delete(dataSource);
            }
        };

        AMediaExtractor* NewExtractor(const Source& s, AMediaDataSource** dataSource) {
            AMediaExtractor* ex = AMediaExtractor_new();
            media_status_t r;
            if (!s.url.empty()) {
                r = AMediaExtractor_setDataSource(ex, s.url.c_str());
            } else if (s.bytes) {
                *dataSource = AMediaDataSource_new();
                AMediaDataSource_setUserdata(*dataSource, s.bytes.get());
                AMediaDataSource_setReadAt(*dataSource, &ReadMemory);
                AMediaDataSource_setGetSize(*dataSource, &MemorySize);
                AMediaDataSource_setClose(*dataSource, &MemoryClose);
                r = AMediaExtractor_setDataSourceCustom(ex, *dataSource);
            } else {
                r = AMediaExtractor_setDataSourceFd(ex, s.fd, s.offset, s.length);
            }
            if (r != AMEDIA_OK) {
                AMediaExtractor_delete(ex);
                return nullptr;
            }
            return ex;
        }

        class Engine final : public media::PlayerEngine {
        public:
            Engine() : m_Thread([this] { Run(); }) {}
            ~Engine() override {
                {
                    std::lock_guard lock(m_Mutex);
                    m_Quit = true;
                }
                m_Cv.notify_all();
                m_Thread.join();
                if (m_Window) ANativeWindow_release(m_Window);
            }

            void Load(media::EngineLoad load) override { Command([&] { m_Load = std::move(load); m_UriChanged = true; }); }
            void SetPlayWhenReady(bool play) override { Command([&] { m_PlayWhenReady = play; }); }
            void SeekTo(std::int64_t ms) override { Command([&] { m_SeekUs = ms * 1000; }); }
            void SetRepeat(bool repeat) override { Command([&] { m_RepeatWanted = repeat; }); }
            void SetVolume(float v) override { Command([&] { m_VolumeWanted = v; }); }
            void SetPlaybackParameters(media::PlaybackParameters p) override { Command([&] { m_ParamsWanted = p; }); }
            void SetTrackSelectionParameters(media::TrackSelectionParameters p) override {
                Command([&] {
                    m_TrackSelectionWanted = std::move(p);
                    m_TrackSelectionChanged = true;
                });
            }
            // AAudio's usage and content type have AudioAttributes' values; used when the stream
            // next opens.
            void SetAudioAttributes(media::AudioAttributes a) override {
                m_Usage = int(a.usage);
                m_ContentType = int(a.contentType);
                m_CapturePolicy = a.allowedCapturePolicy;
                m_Spatialization = a.spatializationBehavior;
            }
            void SetSurface(ui::PlatformSurface surface) override {
                auto* w = static_cast<ANativeWindow*>(surface.nativeWindow);
                if (w) ANativeWindow_acquire(w);
                std::unique_lock lock(m_Mutex);
                if (m_Window) ANativeWindow_release(m_Window);
                m_Window = w;
                ++m_SurfaceGeneration;
                m_Cv.notify_all();
                // The window may go once this returns: wait until the decoder let go of it.
                if (!w) m_Cv.wait(lock, [this] { return m_AppliedSurface == m_SurfaceGeneration || m_Quit; });
            }

        private:
            template<typename F>
            void Command(F&& f) {
                {
                    std::lock_guard lock(m_Mutex);
                    f();
                }
                m_Cv.notify_all();
            }

            // --- the playback thread ---------------------------------------------------------
            void Run() {
                for (;;) {
                    std::unique_lock lock(m_Mutex);
                    bool busy = m_Prepared && (m_Playing || !m_FirstFrameRendered || NeedsDecoding());
                    if (busy) m_Cv.wait_for(lock, std::chrono::milliseconds(10));
                    else m_Cv.wait(lock, [this] { return m_Quit || m_UriChanged || m_SeekUs || m_PlayWhenReady != m_PlayingWanted || m_AppliedSurface != m_SurfaceGeneration || m_RepeatWanted != m_Repeat || m_VolumeWanted != m_Volume || m_ParamsWanted != m_Params || m_TrackSelectionChanged; });
                    if (m_Quit) break;
                    // Take the commands.
                    std::optional<media::EngineLoad> load;
                    if (m_UriChanged) { load = m_Load; m_UriChanged = false; }
                    std::optional<std::int64_t> seek = std::exchange(m_SeekUs, std::nullopt);
                    bool play = m_PlayWhenReady;
                    m_Repeat = m_RepeatWanted;
                    m_Volume = m_VolumeWanted;
                    const bool parametersChanged = m_ParamsWanted != m_Params;
                    const media::PlaybackParameters parameters = m_ParamsWanted;
                    std::optional<media::TrackSelectionParameters> trackSelection;
                    if (m_TrackSelectionChanged) trackSelection = m_TrackSelectionWanted;
                    m_TrackSelectionChanged = false;
                    ANativeWindow* window = nullptr;
                    bool surfaceChanged = m_AppliedSurface != m_SurfaceGeneration;
                    std::uint64_t generation = m_SurfaceGeneration;
                    if (surfaceChanged && m_Window) {
                        window = m_Window;
                        ANativeWindow_acquire(window);
                    }
                    lock.unlock();

                    if (load) {
                        m_LoadId = load->loadId;
                        m_StartUs = std::max<std::int64_t>(load->startPositionMs, 0) * 1000;
                        m_EndUs = load->endPositionMs == media::kTimeEndOfSource ? -1 : load->endPositionMs * 1000;
                        m_StartClipUs = std::max<std::int64_t>(load->clipStartMs, 0) * 1000;
                        // From the item's start (a clipped start, or where playback resumes).
                        m_Uri = load->uri;
                        m_TrackSelection = load->trackSelection;
                        Open(load->uri, m_StartUs);
                    }
                    if (surfaceChanged) {
                        ApplySurface(window);
                        if (window) ANativeWindow_release(window);
                        {
                            std::lock_guard l(m_Mutex);
                            m_AppliedSurface = generation;
                        }
                        m_Cv.notify_all();
                    }
                    if (seek && m_Prepared) Seek(std::min(*seek, m_DurationUs > 0 ? m_DurationUs : *seek));
                    if (parametersChanged) ApplyParameters(parameters);
                    if (trackSelection) ApplyTrackSelection(std::move(*trackSelection));
                    m_PlayingWanted = play;
                    if (m_Prepared) {
                        SetPlaying(play && m_State.state != media::PlaybackState::Ended);
                        DoSomeWork();
                    }
                    Report();
                }
                Close();
                if (m_LastWindow) ANativeWindow_release(m_LastWindow);
                m_LastWindow = nullptr;
            }

            bool NeedsDecoding() const {
                return (m_Video && !m_Video->outputDone && m_Video->codec && !m_FirstFrameRendered)
                    || (m_Audio && !m_AudioReady);
            }

            void Fail(const std::string& message, int code = media::PlaybackException::Unspecified) {
                __android_log_print(ANDROID_LOG_ERROR, "kiln", "media: %s", message.c_str());
                Close();
                m_State = {};
                m_State.error = message;
                m_State.playerError = media::PlaybackException{ .errorCode = code, .message = message,
                                                                .timestampMs = NowUs() / 1000 };
                m_State.playWhenReady = m_PlayingWanted;
            }

            void Close() {
                CloseAudio();
                m_Video.reset();
                m_Audio.reset();
                m_Source.reset();
                m_Prepared = false;
                m_Playing = false;
                m_PendingVideo = -1;
            }

            // Reading starts at startUs: the extractors seek before the decoders see any input (a
            // flush straight after start loses a decoder's output on some codecs).
            void Open(const std::string& uri, std::int64_t startUs) {
                Close();
                m_State = {};
                m_State.playWhenReady = m_PlayingWanted;
                if (uri.empty()) return;
                std::string error;
                m_Source = OpenSource(uri, error);
                if (!m_Source)
                    return Fail(error, uri.starts_with("http") ? media::PlaybackException::IoNetworkConnectionFailed
                                                              : media::PlaybackException::IoFileNotFound);
                AMediaDataSource* probeSource = nullptr;
                AMediaExtractor* probe = NewExtractor(*m_Source, &probeSource);
                if (!probe) {
                    if (probeSource) AMediaDataSource_delete(probeSource);
                    return Fail("cannot read " + uri, media::PlaybackException::ParsingContainerUnsupported);
                }
                m_DurationUs = -1;
                m_Formats.clear();
                m_Types.clear();
                bool anyAudioOrVideo = false;
                for (size_t i = 0, n = AMediaExtractor_getTrackCount(probe); i < n; ++i) {
                    AMediaFormat* f = AMediaExtractor_getTrackFormat(probe, i);
                    std::int64_t d = 0;
                    if (AMediaFormat_getInt64(f, AMEDIAFORMAT_KEY_DURATION, &d)) m_DurationUs = std::max(m_DurationUs, d);
                    const media::Format format = FormatOf(f, int(i));
                    const std::string& mime = format.sampleMimeType;
                    m_Types.push_back(mime.starts_with("video/") ? media::TrackType::Video
                                      : mime.starts_with("audio/") ? media::TrackType::Audio
                                      : mime.starts_with("text/") || mime.starts_with("application/") ? media::TrackType::Text
                                                                                                   : media::TrackType::Unknown);
                    anyAudioOrVideo = anyAudioOrVideo || m_Types.back() == media::TrackType::Video || m_Types.back() == media::TrackType::Audio;
                    m_Formats.push_back(format);
                    AMediaFormat_delete(f);
                }
                AMediaExtractor_delete(probe);
                if (probeSource) AMediaDataSource_delete(probeSource);
                if (!anyAudioOrVideo) return Fail("no audio or video in " + uri, media::PlaybackException::ParsingContainerUnsupported);
                const auto [videoIndex, audioIndex] = SelectTracks();
                ReportTracks(videoIndex, audioIndex);
                if (videoIndex >= 0 && !(m_Video = OpenTrack(videoIndex)))
                    return Fail("cannot read the video of " + uri, media::PlaybackException::ParsingContainerMalformed);
                if (audioIndex >= 0 && !(m_Audio = OpenTrack(audioIndex)))
                    return Fail("cannot read the audio of " + uri, media::PlaybackException::ParsingContainerMalformed);
                for (Track* t : { m_Video.get(), m_Audio.get() }) {
                    if (!t || startUs <= 0) continue;
                    AMediaExtractor_seekTo(t->extractor, startUs, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
                    t->seekTargetUs = startUs;
                }
                m_DecodedVideoSize = {};
                if (m_Video) {
                    std::int32_t w = 0, h = 0, rotation = 0, sarW = 0, sarH = 0;
                    AMediaFormat_getInt32(m_Video->format, AMEDIAFORMAT_KEY_WIDTH, &w);
                    AMediaFormat_getInt32(m_Video->format, AMEDIAFORMAT_KEY_HEIGHT, &h);
                    AMediaFormat_getInt32(m_Video->format, AMEDIAFORMAT_KEY_ROTATION, &rotation);
                    m_State.videoWidth = w;
                    m_State.videoHeight = h;
                    m_State.rotationDegrees = rotation;
                    // Format.pixelWidthHeightRatio: the container's sample aspect ratio.
                    m_PixelRatio = AMediaFormat_getInt32(m_Video->format, "sar-width", &sarW) && AMediaFormat_getInt32(m_Video->format, "sar-height", &sarH)
                                       && sarW > 0 && sarH > 0 ? float(sarW) / float(sarH) : 1.0f;
                } else {
                    m_ReportedVideoSize = {};   // the video renderer is disabled (clearReportedVideoSize)
                }
                if (m_Audio) {
                    m_Audio->codec = AMediaCodec_createDecoderByType(m_Audio->mime.c_str());
                    if (!m_Audio->codec || AMediaCodec_configure(m_Audio->codec, m_Audio->format, nullptr, nullptr, 0) != AMEDIA_OK
                        || AMediaCodec_start(m_Audio->codec) != AMEDIA_OK)
                        return Fail("no decoder for " + m_Audio->mime, media::PlaybackException::DecoderInitFailed);
                    AMediaFormat_getInt32(m_Audio->format, AMEDIAFORMAT_KEY_SAMPLE_RATE, &m_SampleRate);
                    AMediaFormat_getInt32(m_Audio->format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &m_Channels);
                    ConfigureSonic();
                }
                m_Prepared = true;
                m_State.hasVideo = bool(m_Video);
                m_State.hasAudio = bool(m_Audio);
                m_State.durationMs = m_DurationUs >= 0 ? m_DurationUs / 1000 : -1;
                m_Network = uri.starts_with("http://") || uri.starts_with("https://");
                if (m_Video && m_LastWindow) CreateVideoCodec(m_LastWindow);
                StartFrom(std::max<std::int64_t>(startUs, 0));
            }

            std::unique_ptr<Track> OpenTrack(int index) {
                auto t = std::make_unique<Track>();
                t->index = index;
                t->extractor = NewExtractor(*m_Source, &t->dataSource);
                if (!t->extractor) return nullptr;
                AMediaExtractor_selectTrack(t->extractor, size_t(index));
                t->format = AMediaExtractor_getTrackFormat(t->extractor, size_t(index));
                const char* mime = nullptr;
                AMediaFormat_getString(t->format, AMEDIAFORMAT_KEY_MIME, &mime);
                t->mime = mime ? mime : "";
                return t;
            }

            bool CreateVideoCodec(ANativeWindow* window) {
                m_Video->codec = AMediaCodec_createDecoderByType(m_Video->mime.c_str());
                if (!m_Video->codec || AMediaCodec_configure(m_Video->codec, m_Video->format, window, nullptr, 0) != AMEDIA_OK
                    || AMediaCodec_start(m_Video->codec) != AMEDIA_OK) {
                    m_Video->ReleaseCodec();
                    Fail("no decoder for " + m_Video->mime, media::PlaybackException::DecoderInitFailed);
                    return false;
                }
                return true;
            }

            // The decoder moves to the window (or lets go of the old one) before that is released.
            void ApplySurface(ANativeWindow* window) {
                ANativeWindow* old = std::exchange(m_LastWindow, window);
                if (window) ANativeWindow_acquire(window);
                struct ReleaseOld {
                    ANativeWindow* w;
                    ~ReleaseOld() { if (w) ANativeWindow_release(w); }
                } releaseOld{ old };
                if (!m_Video) return;
                if (window && m_Video->codec && AMediaCodec_setOutputSurface(m_Video->codec, window) == AMEDIA_OK) {
                    m_FirstFrameRendered = false;   // show the current frame on the new surface
                    return;
                }
                m_PendingVideo = -1;
                m_Video->ReleaseCodec();
                if (window && CreateVideoCodec(window)) {
                    // Decode from the keyframe before the clock, showing from the clock on.
                    std::int64_t now = ClockUs();
                    AMediaExtractor_seekTo(m_Video->extractor, now, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
                    m_Video->inputDone = m_Video->outputDone = false;
                    m_Video->seekTargetUs = now;
                    m_FirstFrameRendered = false;
                }
            }

            // --- tracks ---------------------------------------------------------------------------
            // A track's Format from the extractor (its id the container's track id, else n + 1).
            static media::Format FormatOf(AMediaFormat* f, int index) {
                media::Format out;
                const char* s = nullptr;
                if (AMediaFormat_getString(f, AMEDIAFORMAT_KEY_MIME, &s) && s) out.sampleMimeType = s;
                if (AMediaFormat_getString(f, AMEDIAFORMAT_KEY_LANGUAGE, &s) && s && std::strcmp(s, "und") != 0) out.language = media::NormalizeLanguageCode(s);
                std::int32_t v = 0;
                out.id = std::to_string(AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_TRACK_ID, &v) ? v : index + 1);
                if (AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_BIT_RATE, &v) && v > 0) out.bitrate = v;
                if (AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &v) && v > 0) out.channelCount = v;
                if (AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_SAMPLE_RATE, &v) && v > 0) out.sampleRate = v;
                if (AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_WIDTH, &v) && v > 0) out.width = v;
                if (AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_HEIGHT, &v) && v > 0) out.height = v;
                float rate = 0;
                if (AMediaFormat_getFloat(f, AMEDIAFORMAT_KEY_FRAME_RATE, &rate) && rate > 0) out.frameRate = rate;
                else if (AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_FRAME_RATE, &v) && v > 0) out.frameRate = float(v);
                if (AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_IS_DEFAULT, &v) && v) out.selectionFlags |= media::kSelectionFlagDefault;
                if (AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_IS_FORCED_SUBTITLE, &v) && v) out.selectionFlags |= media::kSelectionFlagForced;
                if (AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_IS_AUTOSELECT, &v) && v) out.selectionFlags |= media::kSelectionFlagAutoselect;
                return out;
            }
            // Which of the media's tracks play, by the track selection parameters: an override, else
            // the first video track and the audio track AudioTrackInfo ranks first. Embedded text
            // tracks are listed but not played.
            std::pair<int, int> SelectTracks() const {
                auto pick = [&](media::TrackType type, auto&& otherwise) -> int {
                    if (m_TrackSelection.disabledTrackTypes.contains(type)) return -1;
                    if (auto o = media::OverrideForType(m_TrackSelection, type)) {
                        for (std::size_t i = 0; i < m_Formats.size(); ++i)
                            if (m_Types[i] == type && o->groupId == "m" + std::to_string(i)) return o->track == 0 ? int(i) : -1;
                    }
                    return otherwise();
                };
                const int video = pick(media::TrackType::Video, [&] {
                    for (std::size_t i = 0; i < m_Formats.size(); ++i) if (m_Types[i] == media::TrackType::Video) return int(i);
                    return -1;
                });
                const int audio = pick(media::TrackType::Audio, [&] {
                    std::vector<media::Format> formats;
                    std::vector<int> indices;
                    for (std::size_t i = 0; i < m_Formats.size(); ++i)
                        if (m_Types[i] == media::TrackType::Audio) {
                            formats.push_back(m_Formats[i]);
                            indices.push_back(int(i));
                        }
                    const int best = media::SelectAudioTrack(formats, std::vector<bool>(formats.size(), true), m_TrackSelection, ui::PlatformLocaleList());
                    return best < 0 ? -1 : indices[std::size_t(best)];
                });
                return { video, audio };
            }
            void ReportTracks(int video, int audio) {
                m_SelectedVideo = video;
                m_SelectedAudio = audio;
                m_State.tracks = {};
                for (std::size_t i = 0; i < m_Formats.size(); ++i) {
                    media::Tracks::Group g;
                    g.id = "m" + std::to_string(i);
                    g.type = m_Types[i];
                    g.formats = { m_Formats[i] };
                    g.trackSupported = { m_Types[i] == media::TrackType::Video || m_Types[i] == media::TrackType::Audio };
                    g.trackSelected = { int(i) == video || int(i) == audio };
                    m_State.tracks.groups.push_back(std::move(g));
                }
            }
            // New preferences: when the chosen tracks change, the media opens again where it is.
            void ApplyTrackSelection(media::TrackSelectionParameters p) {
                m_TrackSelection = std::move(p);
                if (!m_Prepared) return;
                const auto [video, audio] = SelectTracks();
                if (video == m_SelectedVideo && audio == m_SelectedAudio) return;
                const std::int64_t now = std::max<std::int64_t>(ClockUs(), 0);
                Open(m_Uri, now);
            }

            // --- position and seeking -----------------------------------------------------------
            // SonicAudioProcessor for speed and pitch (it passes audio through at the defaults).
            void ConfigureSonic() {
                m_Sonic.Configure(std::max(m_SampleRate, 1), std::max(m_Channels, 1), m_Params.speed, m_Params.pitch);
                m_SonicEnded = false;
                m_SonicFirst = true;
            }
            // Not when there is no audio output (the sound's time passes on the standalone clock).
            bool Stretching() const { return m_Sonic.IsActive() && (m_Stream || !m_AudioOpened); }
            // New playback parameters: from where playback is, with nothing queued at the old speed.
            void ApplyParameters(const media::PlaybackParameters& p) {
                const std::int64_t now = m_Prepared ? ClockUs() : 0;
                m_Params = p;
                if (m_Audio) ConfigureSonic();
                if (m_Prepared) {
                    Seek(std::max<std::int64_t>(now, 0));
                    // Ready again starts the stream anew (SetPlaying(m_PlayingWanted)).
                    m_Playing = false;
                }
            }

            void StartFrom(std::int64_t us) {
                m_AllowDiscontinuity = true;
                m_Rebase = false;
                m_Sonic.Flush();
                m_SonicEnded = false;
                m_SonicFirst = true;
                m_ClockBaseUs = us;
                m_ClockRealUs = NowUs();
                m_AudioBaseFrame = -1;
                m_FirstFrameRendered = false;
                m_AudioReady = !m_Audio;
                m_PendingPcm.clear();
                m_PendingPcmOffset = 0;
                m_State.state = media::PlaybackState::Buffering;
                m_LastReleaseUs = NowUs();
            }

            void Seek(std::int64_t us) {
                m_PendingVideo = -1;
                for (Track* t : { m_Video.get(), m_Audio.get() }) {
                    if (!t) continue;
                    AMediaExtractor_seekTo(t->extractor, us, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
                    if (t->codec) AMediaCodec_flush(t->codec);
                    t->inputDone = t->outputDone = false;
                    t->seekTargetUs = us;
                }
                if (m_Stream) {
                    AAudioStream_requestPause(m_Stream);
                    AAudioStream_requestFlush(m_Stream);
                    m_StreamStarted = false;
                }
                bool wasEnded = m_State.state == media::PlaybackState::Ended;
                StartFrom(us);
                if (wasEnded) m_Playing = false;
            }

            // The media time: the audio's while it plays, else the standalone clock.
            std::int64_t ClockUs() {
                if (m_Playing && m_Stream && m_StreamStarted && m_AudioBaseFrame >= 0) {
                    std::int64_t frame = 0, time = 0, playoutUs = -1;
                    // Only a timestamp taken since the stream (re)started: after a pause or flush
                    // AAudio still reports the last one, from before.
                    if (AAudioStream_getTimestamp(m_Stream, CLOCK_MONOTONIC, &frame, &time) == AAUDIO_OK && frame > m_AudioBaseFrame
                        && time >= m_StreamStartNs) {
                        playoutUs = (frame - m_AudioBaseFrame) * 1'000'000 / m_SampleRate + (NowNs() - time) / 1000;
                    } else {
                        // AudioTrackPositionTracker without a timestamp yet: the playback head (the
                        // frames read), and until it moves, the position where the audio starts,
                        // not real time (which runs ahead of the output's latency, then jumps back).
                        const std::int64_t read = AAudioStream_getFramesRead(m_Stream);
                        playoutUs = read > m_AudioBaseFrame ? (read - m_AudioBaseFrame) * 1'000'000 / m_SampleRate : 0;
                    }
                    // Time-stretched audio: the media time it played (getMediaDuration).
                    std::int64_t us = m_AudioBasePtsUs + (Stretching() ? m_Sonic.MediaDuration(playoutUs) : playoutUs);
                    // MediaCodecAudioRenderer.updateCurrentPosition: never backwards, except
                    // after a discontinuity (a seek or a new start).
                    if (!m_AllowDiscontinuity) us = std::max(us, m_AudioPositionUs);
                    m_AllowDiscontinuity = false;
                    m_AudioPositionUs = us;
                    // The standalone clock follows, to take over without a jump.
                    m_ClockBaseUs = us;
                    m_ClockRealUs = NowUs();
                    return us;
                }
                // StandaloneMediaClock: real time at the playback speed.
                return m_Playing && m_State.state == media::PlaybackState::Ready
                           ? m_ClockBaseUs + std::int64_t(double(NowUs() - m_ClockRealUs) * m_Params.speed)
                           : m_ClockBaseUs;
            }

            void SetPlaying(bool play) {
                bool started = play && m_State.state == media::PlaybackState::Ready;
                if (started == m_Playing) return;
                m_ClockBaseUs = ClockUs();
                m_ClockRealUs = NowUs();
                m_Playing = started;
                if (m_Stream) {
                    if (started) {
                        AAudioStream_requestStart(m_Stream);
                        m_StreamStarted = true;
                        m_StreamStartNs = NowNs();
                    } else {
                        AAudioStream_requestPause(m_Stream);
                        m_StreamStarted = false;
                    }
                }
            }

            // --- decoding ------------------------------------------------------------------------
            void FeedInput(Track& t) {
                if (!t.codec) return;
                for (int i = 0; i < 4 && !t.inputDone; ++i) {
                    ssize_t idx = AMediaCodec_dequeueInputBuffer(t.codec, 0);
                    if (idx < 0) return;
                    size_t capacity = 0;
                    std::uint8_t* buffer = AMediaCodec_getInputBuffer(t.codec, size_t(idx), &capacity);
                    ssize_t size = buffer ? AMediaExtractor_readSampleData(t.extractor, buffer, capacity) : -1;
                    if (size < 0) {
                        AMediaCodec_queueInputBuffer(t.codec, size_t(idx), 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                        t.inputDone = true;
                        return;
                    }
                    AMediaCodec_queueInputBuffer(t.codec, size_t(idx), 0, size_t(size), std::uint64_t(AMediaExtractor_getSampleTime(t.extractor)), 0);
                    AMediaExtractor_advance(t.extractor);
                }
            }

            void OpenAudio() {
                CloseAudio();
                // A new output mid-stream (DISCONNECTED): the position goes on from where the old
                // one got to, counted in the new stream's frames.
                if (m_AudioBaseFrame >= 0) {
                    m_AudioBaseFrame = -1;
                    m_Rebase = true;
                }
                AAudioStreamBuilder* b = nullptr;
                if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return;
                AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
                AAudioStreamBuilder_setChannelCount(b, m_Channels);
                AAudioStreamBuilder_setSampleRate(b, m_SampleRate);
                AAudioStreamBuilder_setUsage(b, aaudio_usage_t(m_Usage.load()));
                AAudioStreamBuilder_setAllowedCapturePolicy(b, aaudio_allowed_capture_policy_t(m_CapturePolicy.load()));
                if (__builtin_available(android 32, *))
                    AAudioStreamBuilder_setSpatializationBehavior(b, m_Spatialization.load() == 1 ? AAUDIO_SPATIALIZATION_BEHAVIOR_NEVER
                                                                                                    : AAUDIO_SPATIALIZATION_BEHAVIOR_AUTO);
                const int content = m_ContentType.load();
                AAudioStreamBuilder_setContentType(b, content ? aaudio_content_type_t(content) : m_Video ? AAUDIO_CONTENT_TYPE_MOVIE : AAUDIO_CONTENT_TYPE_MUSIC);
                if (AAudioStreamBuilder_openStream(b, &m_Stream) != AAUDIO_OK) {
                    m_Stream = nullptr;
                    __android_log_print(ANDROID_LOG_WARN, "kiln", "media: no audio output; playing without sound");
                }
                AAudioStreamBuilder_delete(b);
                m_StreamStarted = false;
                if (m_Stream && m_Playing) {
                    AAudioStream_requestStart(m_Stream);
                    m_StreamStarted = true;
                    m_StreamStartNs = NowNs();
                }
            }
            void CloseAudio() {
                if (m_Stream) AAudioStream_close(m_Stream);
                m_Stream = nullptr;
                m_StreamStarted = false;
                m_AudioOpened = false;
            }

            void DrainAudio() {
                Track& t = *m_Audio;
                while (m_PendingPcmOffset >= m_PendingPcm.size() && !t.outputDone) {
                    AMediaCodecBufferInfo info{};
                    ssize_t idx = AMediaCodec_dequeueOutputBuffer(t.codec, &info, 0);
                    if (idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                        AMediaFormat* f = AMediaCodec_getOutputFormat(t.codec);
                        AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_SAMPLE_RATE, &m_SampleRate);
                        AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &m_Channels);
                        AMediaFormat_delete(f);
                        m_AudioOpened = false;
                        ConfigureSonic();
                        continue;
                    }
                    if (idx < 0) return;
                    if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) t.outputDone = true;
                    size_t size = 0;
                    std::uint8_t* data = AMediaCodec_getOutputBuffer(t.codec, size_t(idx), &size);
                    std::int64_t frames = info.size / std::int64_t(2 * std::max(m_Channels, 1));
                    std::int64_t endUs = info.presentationTimeUs + frames * 1'000'000 / std::max(m_SampleRate, 1);
                    if (data && info.size > 0 && endUs > t.seekTargetUs) {
                        const auto* pcm = reinterpret_cast<const std::int16_t*>(data + info.offset);
                        m_PendingPcmOffset = 0;
                        if (Stretching()) {
                            // Through Sonic; its output comes in pitch periods, not buffer by buffer.
                            if (m_SonicFirst) {
                                m_PendingPts = info.presentationTimeUs;
                                m_SonicFirst = false;
                            }
                            m_Sonic.QueueInput(pcm, int(frames));
                            m_PendingPcm.clear();
                            m_Sonic.GetOutput(m_PendingPcm);
                        } else {
                            m_PendingPcm.assign(pcm, pcm + info.size / 2);
                            m_PendingPts = info.presentationTimeUs;
                        }
                        if (m_Volume < 1) for (auto& s : m_PendingPcm) s = std::int16_t(float(s) * m_Volume);
                    }
                    AMediaCodec_releaseOutputBuffer(t.codec, size_t(idx), false);
                }
                // The stream ended: what Sonic still holds.
                if (t.outputDone && Stretching() && !m_SonicEnded && m_PendingPcmOffset >= m_PendingPcm.size()) {
                    m_Sonic.QueueEndOfStream();
                    m_SonicEnded = true;
                    m_PendingPcm.clear();
                    m_PendingPcmOffset = 0;
                    m_Sonic.GetOutput(m_PendingPcm);
                    if (m_Volume < 1) for (auto& s : m_PendingPcm) s = std::int16_t(float(s) * m_Volume);
                }
                if (!m_AudioOpened && m_PendingPcmOffset < m_PendingPcm.size()) {
                    OpenAudio();
                    m_AudioOpened = true;
                }
                if (m_PendingPcmOffset < m_PendingPcm.size()) {
                    if (!m_Stream) {
                        // No output: the sound's time passes on the standalone clock.
                        if (m_PendingPts <= ClockUs() || !m_Playing) {
                            m_AudioReady = true;
                            if (m_Playing) m_PendingPcmOffset = m_PendingPcm.size();
                        }
                        return;
                    }
                    if (m_AudioBaseFrame < 0) {
                        m_AudioBaseFrame = AAudioStream_getFramesWritten(m_Stream);
                        if (std::exchange(m_Rebase, false)) {
                            // Unstretched: the next sample's time; stretched: where playback got to,
                            // with Sonic's time mapping counted from here.
                            const std::int64_t offsetFrames = std::int64_t(m_PendingPcmOffset) / std::max(m_Channels, 1);
                            m_AudioBasePtsUs = Stretching() ? m_AudioPositionUs : m_PendingPts + offsetFrames * 1'000'000 / std::max(m_SampleRate, 1);
                            m_Sonic.RestartDurationCounts();
                        } else {
                            m_AudioBasePtsUs = m_PendingPts;
                        }
                        m_AllowDiscontinuity = true;
                    }
                    int channels = std::max(m_Channels, 1);
                    std::int64_t left = std::int64_t(m_PendingPcm.size() - m_PendingPcmOffset) / channels;
                    aaudio_result_t n = AAudioStream_write(m_Stream, m_PendingPcm.data() + m_PendingPcmOffset, std::int32_t(left), 0);
                    if (n > 0) m_PendingPcmOffset += std::size_t(n) * std::size_t(channels);
                    else if (n == AAUDIO_ERROR_DISCONNECTED) m_AudioOpened = false;   // a new output (headphones)
                    m_AudioReady = true;
                }
                if (t.outputDone) m_AudioReady = true;
            }

            void DrainVideo() {
                Track& t = *m_Video;
                if (!t.codec) return;
                while (!t.outputDone) {
                    if (m_PendingVideo < 0) {
                        AMediaCodecBufferInfo info{};
                        ssize_t idx = AMediaCodec_dequeueOutputBuffer(t.codec, &info, 0);
                        if (idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                            AMediaFormat* f = AMediaCodec_getOutputFormat(t.codec);
                            DecodedVideoSize(f);
                            AMediaFormat_delete(f);
                            continue;
                        }
                        if (idx < 0) return;
                        m_PendingVideo = idx;
                        m_PendingVideoPts = info.presentationTimeUs;
                        m_PendingVideoEos = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
                        m_PendingVideoEmpty = info.size == 0;
                    }
                    size_t idx = size_t(m_PendingVideo);
                    if (m_PendingVideoEmpty) {
                        AMediaCodec_releaseOutputBuffer(t.codec, idx, false);
                        m_PendingVideo = -1;
                        if (m_PendingVideoEos) t.outputDone = true;
                        continue;
                    }
                    // Decode-only frames between the keyframe and the seek target.
                    if (m_PendingVideoPts < t.seekTargetUs && !m_PendingVideoEos) {
                        AMediaCodec_releaseOutputBuffer(t.codec, idx, false);
                        m_PendingVideo = -1;
                        continue;
                    }
                    media::FrameTiming timing;
                    // calculateEarlyTimeUs: media time ahead, in real time at the playback speed.
                    timing.earlyUs = std::int64_t(double(m_PendingVideoPts - ClockUs()) / m_Params.speed);
                    timing.started = m_Playing;
                    timing.firstFrameRendered = m_FirstFrameRendered;
                    timing.lastFrame = m_PendingVideoEos;
                    timing.sinceLastReleaseUs = NowUs() - m_LastReleaseUs;
                    switch (media::DecideFrameRelease(timing)) {
                        case media::FrameRelease::TryAgainLater:
                            return;
                        case media::FrameRelease::Immediately:
                            AMediaCodec_releaseOutputBuffer(t.codec, idx, true);
                            OnFrameRendered();
                            m_LastReleaseUs = NowUs();
                            break;
                        case media::FrameRelease::Scheduled:
                            AMediaCodec_releaseOutputBufferAtTime(t.codec, idx, NowNs() + timing.earlyUs * 1000);
                            OnFrameRendered();
                            m_LastReleaseUs = NowUs();
                            break;
                        case media::FrameRelease::Drop:
                            AMediaCodec_releaseOutputBuffer(t.codec, idx, false);
                            break;
                    }
                    m_PendingVideo = -1;
                    if (m_PendingVideoEos) t.outputDone = true;
                }
            }

            // onOutputFormatChanged: the decoder's crop, turned by the rotation it applies on the
            // way to the surface (90 and 270 swap the sides and invert the pixel aspect ratio).
            void DecodedVideoSize(AMediaFormat* f) {
                std::int32_t w = 0, h = 0, left = 0, top = 0, right = 0, bottom = 0;
                if (AMediaFormat_getRect(f, AMEDIAFORMAT_KEY_DISPLAY_CROP, &left, &top, &right, &bottom)) {
                    w = right - left + 1;
                    h = bottom - top + 1;
                } else {
                    AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_WIDTH, &w);
                    AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_HEIGHT, &h);
                }
                if (w <= 0 || h <= 0) return;   // transient invalid formats some decoders report
                float ratio = m_PixelRatio;
                if (m_State.rotationDegrees == 90 || m_State.rotationDegrees == 270) {
                    std::swap(w, h);
                    ratio = 1 / ratio;
                }
                m_DecodedVideoSize = { .width = w, .height = h, .pixelWidthHeightRatio = ratio };
            }
            // A frame went to the surface: the size it was decoded at (maybeNotifyVideoSizeChanged)
            // and, the first since a load, seek or new surface, onRenderedFirstFrame.
            void OnFrameRendered() {
                if (m_DecodedVideoSize.width == 0 && m_Video && m_Video->codec) {
                    AMediaFormat* f = AMediaCodec_getOutputFormat(m_Video->codec);
                    if (f) {
                        DecodedVideoSize(f);
                        AMediaFormat_delete(f);
                    }
                }
                if (m_DecodedVideoSize.width > 0) m_ReportedVideoSize = m_DecodedVideoSize;
                if (!m_FirstFrameRendered) {
                    m_FirstFrameRendered = true;
                    m_RenderedFirstFrame = true;
                }
            }

            void DoSomeWork() {
                if (m_Video) FeedInput(*m_Video);
                if (m_Audio) FeedInput(*m_Audio);
                if (m_Audio) DrainAudio();
                if (m_Video) DrainVideo();
                bool videoReady = !m_Video || !m_Video->codec || m_FirstFrameRendered || m_Video->outputDone;
                if (m_State.state == media::PlaybackState::Buffering && videoReady && m_AudioReady) {
                    m_State.state = media::PlaybackState::Ready;
                    m_ClockRealUs = NowUs();
                    SetPlaying(m_PlayingWanted);
                }
                bool videoDone = !m_Video || !m_Video->codec || m_Video->outputDone;
                bool audioDone = !m_Audio || (m_Audio->outputDone && m_PendingPcmOffset >= m_PendingPcm.size() && (!Stretching() || m_SonicEnded));
                // The media's end, or the item's clipped end (ClippingMediaSource).
                const std::int64_t endUs = m_EndUs >= 0 && (m_DurationUs < 0 || m_EndUs < m_DurationUs) ? m_EndUs : m_DurationUs;
                const bool clippedEnd = m_EndUs >= 0 && ClockUs() >= m_EndUs;
                if (m_State.state == media::PlaybackState::Ready && ((videoDone && audioDone && ClockUs() >= m_DurationUs - 1000) || clippedEnd)) {
                    if (m_Repeat) {
                        Seek(m_StartClipUs);
                        if (onLooped) onLooped(m_LoadId);
                    } else {
                        m_ClockBaseUs = std::max<std::int64_t>(endUs, 0);
                        SetPlaying(false);
                        m_State.state = media::PlaybackState::Ended;
                    }
                }
            }

            void Report() {
                media::PlayerState s = m_State;
                s.videoSize = m_ReportedVideoSize;
                s.playWhenReady = m_PlayingWanted;
                s.isPlaying = m_Playing;
                if (m_Prepared) {
                    std::int64_t us = ClockUs();
                    if (m_DurationUs > 0) us = std::min(us, m_DurationUs);
                    s.positionMs = std::max<std::int64_t>(us, 0) / 1000;
                    // How far the source is read ahead: all of a local file; what the extractor
                    // has cached of a stream.
                    std::int64_t cached = -1;
                    if (m_Network) {
                        Track* t = m_Audio ? m_Audio.get() : m_Video.get();
                        if (t) cached = AMediaExtractor_getCachedDuration(t->extractor);
                    }
                    if (!m_Network) s.bufferedPositionMs = m_DurationUs >= 0 ? m_DurationUs / 1000 : s.positionMs;
                    else s.bufferedPositionMs = cached >= 0 ? s.positionMs + cached / 1000 : s.positionMs;
                    if (m_DurationUs >= 0) s.bufferedPositionMs = std::min<std::int64_t>(s.bufferedPositionMs, m_DurationUs / 1000);
                    s.isLoading = m_Network && cached >= 0 && (m_DurationUs < 0 || s.bufferedPositionMs < m_DurationUs / 1000);
                }
                std::int64_t now = NowUs();
                media::PlayerState withoutPosition = s, lastWithoutPosition = m_Reported;
                withoutPosition.positionMs = lastWithoutPosition.positionMs = 0;
                withoutPosition.bufferedPositionMs = lastWithoutPosition.bufferedPositionMs = 0;
                const bool moved = s.positionMs != m_Reported.positionMs || s.bufferedPositionMs != m_Reported.bufferedPositionMs;
                if (withoutPosition != lastWithoutPosition || m_LoadId != m_ReportedLoad || (moved && now - m_ReportedUs >= 250'000)) {
                    m_Reported = s;
                    m_ReportedUs = now;
                    m_ReportedLoad = m_LoadId;
                    if (onState) onState(s, m_LoadId);
                }
                if (std::exchange(m_RenderedFirstFrame, false) && onRenderedFirstFrame) onRenderedFirstFrame(m_LoadId);
            }

            // Shared with the main thread (m_Mutex).
            std::mutex m_Mutex;
            std::condition_variable m_Cv;
            bool m_Quit = false;
            media::EngineLoad m_Load;
            bool m_UriChanged = false;
            bool m_PlayWhenReady = false;
            std::optional<std::int64_t> m_SeekUs;
            bool m_RepeatWanted = false;
            float m_VolumeWanted = 1;
            std::atomic<int> m_Usage{ AAUDIO_USAGE_MEDIA }, m_ContentType{ 0 }, m_CapturePolicy{ AAUDIO_ALLOW_CAPTURE_BY_ALL }, m_Spatialization{ 0 };
            ANativeWindow* m_Window = nullptr;
            std::uint64_t m_SurfaceGeneration = 0, m_AppliedSurface = 0;

            // The playback thread's.
            bool m_PlayingWanted = false, m_Repeat = false;
            float m_Volume = 1;
            std::unique_ptr<Source> m_Source;
            std::unique_ptr<Track> m_Video, m_Audio;
            ANativeWindow* m_LastWindow = nullptr;
            bool m_Prepared = false, m_Playing = false, m_FirstFrameRendered = false, m_AudioReady = false;
            std::int64_t m_DurationUs = -1;
            std::int64_t m_ClockBaseUs = 0, m_ClockRealUs = 0, m_LastReleaseUs = 0;
            ssize_t m_PendingVideo = -1;
            std::int64_t m_PendingVideoPts = 0;
            bool m_PendingVideoEos = false, m_PendingVideoEmpty = false;
            AAudioStream* m_Stream = nullptr;
            bool m_StreamStarted = false, m_AudioOpened = false;
            std::int64_t m_StreamStartNs = 0;
            std::int32_t m_SampleRate = 44100, m_Channels = 2;
            std::vector<std::int16_t> m_PendingPcm;
            std::size_t m_PendingPcmOffset = 0;
            std::int64_t m_PendingPts = 0, m_AudioBaseFrame = -1, m_AudioBasePtsUs = 0;
            media::PlayerState m_State, m_Reported;
            std::int64_t m_ReportedUs = 0;
            std::uint64_t m_LoadId = 0, m_ReportedLoad = 0;
            std::int64_t m_StartUs = 0, m_EndUs = -1;   // this load's start and clipped end (-1: none)
            std::int64_t m_StartClipUs = 0;              // where a repeat starts again
            bool m_Network = false;
            media::PlaybackParameters m_ParamsWanted, m_Params;   // wanted: shared (m_Mutex)
            media::TrackSelectionParameters m_TrackSelectionWanted, m_TrackSelection;   // wanted: shared
            bool m_TrackSelectionChanged = false;                                         // shared
            std::string m_Uri;
            std::vector<media::Format> m_Formats;   // the media's tracks
            std::vector<media::TrackType> m_Types;
            int m_SelectedVideo = -1, m_SelectedAudio = -1;
            media::SonicProcessor m_Sonic;
            bool m_SonicEnded = false, m_SonicFirst = true;
            bool m_AllowDiscontinuity = true, m_Rebase = false;
            media::VideoSize m_DecodedVideoSize, m_ReportedVideoSize;
            float m_PixelRatio = 1;
            bool m_RenderedFirstFrame = false;   // to report (onRenderedFirstFrame)
            std::int64_t m_AudioPositionUs = 0;
            std::thread m_Thread;   // last: starts once the members exist
        };
    }

    void InstallMedia() {
        media::SetPlayerEngineFactory([] { return std::unique_ptr<media::PlayerEngine>(std::make_unique<Engine>()); });
    }

}
