// media::Player's engine on Android: an extractor and an AMediaCodec decoder per track, sound
// through AAudio, on a playback thread that works every 10 ms while it plays (ExoPlayer's
// doSomeWork loop). Video frames are released against the audio clock, or a standalone clock
// without sound (StandaloneMediaClock), as DecideFrameRelease says. HLS and DASH come from
// media::stream's loaders: each track reads their pieces (runs of one rendition's segments)
// through an extractor per piece, switching renditions at segment boundaries.
#include "AndroidMedia.h"

#include "kiln/jvm/Jvm.h"
#include "kiln/media/Player.h"
#include "kiln/media/stream/AdaptiveSource.h"
#include "kiln/media/stream/BandwidthMeter.h"
#include "kiln/media/stream/SegmentStream.h"
#include "kiln/net/Http.h"
#include "kiln/ui/ContentResolver.h"

#include <aaudio/AAudio.h>
#include "kiln/media/Sonic.h"
#include "kiln/media/TrackSelection.h"
#include "kiln/ui/Configuration.h"
#include <android/log.h>
#include <dlfcn.h>
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
#include <deque>
#include <map>
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

        // An extractor's view of a stream piece (AMediaDataSource's user data).
        struct PieceReader {
            std::shared_ptr<media::stream::StreamPiece> piece;
            int reader = 0;
        };
        ssize_t ReadPiece(void* user, off64_t offset, void* buffer, size_t size) {
            auto* r = static_cast<PieceReader*>(user);
            return ssize_t(r->piece->ReadAt(r->reader, offset, static_cast<std::uint8_t*>(buffer), size));
        }
        ssize_t PieceSize(void* user) {
            const std::int64_t size = static_cast<PieceReader*>(user)->piece->Size();
            return size < 0 ? -1 : ssize_t(size);
        }
        void PieceClose(void*) {}

        // RendererCapabilities' format support from the platform's decoders
        // (KilnMedia.decoderSupport): 2 handled, 1 the type but not these properties, 0 no
        // decoder. Unknown types count as handled (the decoder decides).
        int DecoderSupport(const media::Format& f) {
            if (f.sampleMimeType.empty()) return 2;
            const std::string key = f.sampleMimeType + "|" + f.codecs + "|" + std::to_string(f.width) + "x" + std::to_string(f.height) + "@" +
                                    std::to_string(f.frameRate) + "|" + std::to_string(f.channelCount) + "|" + std::to_string(f.sampleRate);
            static std::mutex mutex;
            static std::map<std::string, int> cache;
            {
                std::lock_guard lock(mutex);
                if (auto it = cache.find(key); it != cache.end()) return it->second;
            }
            int support = 2;
            try {
                JNIEnv* env = jvm::Env();
                static jvm::Global<jclass> media = jvm::FindClass("dev/kiln/KilnMedia");
                static jmethodID method = jvm::detail::StaticMethod(env, media.get(), "decoderSupport", "(Ljava/lang/String;Ljava/lang/String;IIFII)I");
                jvm::Local<jstring> mime = jvm::ToJava(env, f.sampleMimeType);
                jvm::Local<jstring> codecs = jvm::ToJava(env, f.codecs);
                support = env->CallStaticIntMethod(media.get(), method, mime.get(), codecs.get(), jint(f.width), jint(f.height), jfloat(f.frameRate),
                                                   jint(f.channelCount), jint(f.sampleRate));
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    support = 2;
                }
            } catch (const std::exception&) {
                support = 2;
            }
            std::lock_guard lock(mutex);
            cache[key] = support;
            return support;
        }

        // DefaultBandwidthMeter.getSingletonInstance: one estimate for every player.
        media::stream::DefaultBandwidthMeter& BandwidthMeter() {
            static std::unique_ptr<media::stream::DefaultBandwidthMeter> meter = media::stream::DefaultBandwidthMeter::Builder().Build();
            return *meter;
        }

        struct FormatDeleter {
            void operator()(AMediaFormat* f) const { AMediaFormat_delete(f); }
        };
        using FormatPtr = std::unique_ptr<AMediaFormat, FormatDeleter>;

        // An adaptive stream track's extraction, on a thread of its own (ExoPlayer extracts on its
        // loading thread into sample queues): it reads the stream's pieces in turn, an extractor
        // each, into a short queue the playback thread takes from without waiting. The platform's
        // extractors read ahead of the samples they return, so a read may wait for the next
        // segment; only this thread waits.
        class TrackReader {
        public:
            struct Item {
                enum class Kind { Format, Sample, End, NoTrack } kind = Kind::Sample;
                FormatPtr format;   // Format: the piece's track format
                std::int64_t minPresentationUs = media::stream::kTimeUnset;
                std::vector<std::uint8_t> data;   // Sample
                std::int64_t timeUs = 0;
            };

            TrackReader(std::shared_ptr<media::stream::SegmentStream> stream, int reader, bool video)
                : m_Stream(std::move(stream)), m_Reader(reader), m_Video(video), m_Thread([this] { Run(); }) {}
            ~TrackReader() {
                {
                    std::lock_guard lock(m_Mutex);
                    m_Quit = true;
                    if (m_Piece) m_Piece->Cancel();   // a read that waits gives up
                }
                m_Cv.notify_all();
                m_Thread.join();
            }

            std::optional<Item> Pop() {
                std::optional<Item> item;
                {
                    std::lock_guard lock(m_Mutex);
                    if (m_Queue.empty()) return std::nullopt;
                    item = std::move(m_Queue.front());
                    m_Queue.pop_front();
                    m_QueuedBytes -= item->data.size();
                }
                m_Cv.notify_all();
                return item;
            }
            // The first item (the first piece's format, or that it has no such track); nullopt
            // until the deadline.
            std::optional<Item> PopFirst(std::int64_t timeoutUs) {
                std::unique_lock lock(m_Mutex);
                m_Cv.wait_for(lock, std::chrono::microseconds(timeoutUs), [this] { return !m_Queue.empty() || m_Finished; });
                lock.unlock();
                return Pop();
            }
            // Nothing queued, and the extractor waits for bytes not loaded yet: playback will stall.
            bool Starving() const {
                std::lock_guard lock(m_Mutex);
                return m_Queue.empty() && !m_Finished && (!m_Piece || m_Piece->IsWaiting(m_Reader) || !m_Piece->CanOpen());
            }

        private:
            // An extractor on a piece (released in reverse order).
            struct Extraction {
                PieceReader pieceReader;
                AMediaDataSource* dataSource = nullptr;
                AMediaExtractor* extractor = nullptr;
                ~Extraction() {
                    if (extractor) AMediaExtractor_delete(extractor);
                    if (dataSource) AMediaDataSource_delete(dataSource);
                }
            };

            bool Quit() const {
                std::lock_guard lock(m_Mutex);
                return m_Quit;
            }
            // Waits a little; false once told to quit.
            bool Pause() {
                std::unique_lock lock(m_Mutex);
                m_Cv.wait_for(lock, std::chrono::milliseconds(10), [this] { return m_Quit; });
                return !m_Quit;
            }
            void Push(Item item) {
                {
                    std::lock_guard lock(m_Mutex);
                    m_QueuedBytes += item.data.size();
                    m_Queue.push_back(std::move(item));
                }
                m_Cv.notify_all();
            }
            void Finish() {
                {
                    std::lock_guard lock(m_Mutex);
                    m_Finished = true;
                }
                m_Cv.notify_all();
            }
            // Room in the queue: about two seconds of samples, at most 8 MB.
            bool AwaitRoom() {
                std::unique_lock lock(m_Mutex);
                const std::size_t maxItems = m_Video ? 60 : 100;
                m_Cv.wait(lock, [&] { return m_Quit || (m_Queue.size() < maxItems && m_QueuedBytes < (8u << 20)); });
                return !m_Quit;
            }
            void SetPiece(std::shared_ptr<media::stream::StreamPiece> piece) {
                std::lock_guard lock(m_Mutex);
                m_Piece = std::move(piece);
                if (m_Quit && m_Piece) m_Piece->Cancel();
            }

            // The piece's track of this type selected; its format, or none.
            FormatPtr Open(Extraction& e, const std::shared_ptr<media::stream::StreamPiece>& piece) {
                e.pieceReader = PieceReader{ piece, m_Reader };
                e.dataSource = AMediaDataSource_new();
                AMediaDataSource_setUserdata(e.dataSource, &e.pieceReader);
                AMediaDataSource_setReadAt(e.dataSource, &ReadPiece);
                AMediaDataSource_setGetSize(e.dataSource, &PieceSize);
                AMediaDataSource_setClose(e.dataSource, &PieceClose);
                // The TS extractor probes for tracks for two seconds of wall time unless its reads
                // fail at the end of what is loaded; any extractor's sniffing does not count as
                // reading on.
                const bool ts = piece->Container() == media::stream::SegmentContainer::Ts;
                if (ts) piece->SetNonBlocking(m_Reader, true);
                piece->SetOpening(m_Reader, true);
                e.extractor = AMediaExtractor_new();
                const media_status_t r = AMediaExtractor_setDataSourceCustom(e.extractor, e.dataSource);
                piece->SetOpening(m_Reader, false);
                if (ts) piece->SetNonBlocking(m_Reader, false);
                if (r != AMEDIA_OK) {
                    if (!Quit()) __android_log_print(ANDROID_LOG_WARN, "kiln", "media: cannot read a %s piece (%d)", m_Video ? "video" : "audio", int(r));
                    return nullptr;
                }
                const char* prefix = m_Video ? "video/" : "audio/";
                for (size_t i = 0, n = AMediaExtractor_getTrackCount(e.extractor); i < n; ++i) {
                    FormatPtr f(AMediaExtractor_getTrackFormat(e.extractor, i));
                    const char* mime = nullptr;
                    if (AMediaFormat_getString(f.get(), AMEDIAFORMAT_KEY_MIME, &mime) && mime && std::strncmp(mime, prefix, 6) == 0) {
                        AMediaExtractor_selectTrack(e.extractor, i);
                        return f;
                    }
                }
                return nullptr;
            }

            // Waits until the piece's successor is known; false once told to quit.
            bool AwaitEnd(const std::shared_ptr<media::stream::StreamPiece>& piece) {
                while (!piece->Ended())
                    if (!Pause()) return false;
                return true;
            }

            void Run() {
                std::shared_ptr<media::stream::StreamPiece> piece;
                bool first = true;
                while (!Quit()) {
                    if (!piece) piece = m_Stream->FirstPiece();
                    if (!piece || !piece->CanOpen()) {
                        if (!Pause()) break;
                        continue;
                    }
                    SetPiece(piece);
                    Extraction e;
                    FormatPtr format = Open(e, piece);
                    if (!format) {
                        if (first) {
                            Push({ .kind = Item::Kind::NoTrack });
                            break;
                        }
                        // A piece without this track (a gap): on to the next.
                        if (!AwaitEnd(piece)) break;
                        if (piece->IsLast()) {
                            Push({ .kind = Item::Kind::End });
                            break;
                        }
                        piece = piece->Next();
                        continue;
                    }
                    first = false;
                    Push({ .kind = Item::Kind::Format, .format = std::move(format), .minPresentationUs = piece->MinPresentationUs() });
                    // Known once the piece can open (CanOpen waits for TS and packed audio's).
                    const std::int64_t offsetUs = piece->TimeOffsetUs().value_or(piece->StartUs());
                    for (;;) {
                        if (!AwaitRoom()) break;
                        const std::int64_t sampleUs = AMediaExtractor_getSampleTime(e.extractor);
                        if (sampleUs < 0) break;
                        Item sample;
                        sample.data.resize(std::size_t(std::max<ssize_t>(AMediaExtractor_getSampleSize(e.extractor), 0)));
                        const ssize_t n = AMediaExtractor_readSampleData(e.extractor, sample.data.data(), sample.data.size());
                        if (n < 0) break;
                        sample.data.resize(std::size_t(n));
                        sample.timeUs = sampleUs + offsetUs;
                        Push(std::move(sample));
                        AMediaExtractor_advance(e.extractor);
                    }
                    if (Quit() || !AwaitEnd(piece)) break;
                    if (piece->IsLast()) {
                        Push({ .kind = Item::Kind::End });
                        break;
                    }
                    piece = piece->Next();
                }
                Finish();
            }

            const std::shared_ptr<media::stream::SegmentStream> m_Stream;
            const int m_Reader;
            const bool m_Video;
            mutable std::mutex m_Mutex;
            std::condition_variable m_Cv;
            bool m_Quit = false, m_Finished = false;
            std::deque<Item> m_Queue;
            std::size_t m_QueuedBytes = 0;
            std::shared_ptr<media::stream::StreamPiece> m_Piece;
            std::thread m_Thread;   // last
        };

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

            // An adaptive stream's track: its samples come from a reader thread.
            std::shared_ptr<media::stream::SegmentStream> stream;
            int reader = 0;
            bool video = false;
            std::unique_ptr<TrackReader> sampleReader;
            std::optional<TrackReader::Item> pending;   // taken from the reader, waiting for an input buffer
            bool fed = false;                            // samples went to a decoder since the start or a seek
            // A new rendition: its codec-specific data goes in before its samples, or the decoder
            // drains and starts again with its format.
            std::vector<std::vector<std::uint8_t>> pendingConfig;
            AMediaFormat* reinitFormat = nullptr;

            void ReleaseCodec() {
                if (codec) {
                    AMediaCodec_stop(codec);
                    AMediaCodec_delete(codec);
                    codec = nullptr;
                }
            }
            ~Track() {
                sampleReader.reset();
                ReleaseCodec();
                if (format) AMediaFormat_delete(format);
                if (reinitFormat) AMediaFormat_delete(reinitFormat);
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
            // An adaptive stream's groups and tracks that play.
            struct StreamSelection {
                int videoGroup = -1, audioGroup = -1;   // audioGroup == videoGroup: muxed
                std::vector<int> videoTracks, audioTracks;
                bool operator==(const StreamSelection&) const = default;
            };

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
                    // A stream that buffers or loads keeps working too (its buffer decides when to play,
                    // and its buffered position moves while paused).
                    bool busy = m_Prepared && (m_Playing || !m_FirstFrameRendered || NeedsDecoding() || StreamsBusy());
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
                        m_MimeType = load->mimeType;
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
                    // Positions from the player are in the window (an adaptive stream's moves).
                    if (seek && m_Prepared) Seek(m_WindowStartUs + std::min(*seek, m_DurationUs > 0 ? m_DurationUs - m_WindowStartUs : *seek));
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
                m_VideoStream.reset();
                m_AudioStream.reset();
                m_Adaptive.reset();
                m_WindowStartUs = 0;
                m_Rebuffering = false;
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
                if (media::stream::AdaptiveSource::InferProtocol(uri, m_MimeType)) return OpenAdaptive(uri, startUs);
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
                m_State.durationMs = m_DurationUs >= 0 ? m_DurationUs / 1000 : -1;
                m_Network = uri.starts_with("http://") || uri.starts_with("https://");
                FinishOpen(std::max<std::int64_t>(startUs, 0));
            }

            // Both kinds of source: the decoders and the start (startUs on the clock's timeline).
            void FinishOpen(std::int64_t startUs) {
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
                if (m_Video && m_LastWindow) CreateVideoCodec(m_LastWindow);
                StartFrom(startUs);
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
                // MediaCodecVideoRenderer.getCodecMaxValues: room for the largest rendition the
                // stream may switch to, so a switch needs no new decoder.
                if (m_Video->stream && m_VideoTracks.size() > 1) {
                    std::int32_t maxWidth = 0, maxHeight = 0;
                    AMediaFormat_getInt32(m_Video->format, AMEDIAFORMAT_KEY_WIDTH, &maxWidth);
                    AMediaFormat_getInt32(m_Video->format, AMEDIAFORMAT_KEY_HEIGHT, &maxHeight);
                    for (int track : m_VideoTracks) {
                        const media::Format& f = m_Adaptive->Groups()[std::size_t(m_VideoGroup)].formats[std::size_t(track)];
                        maxWidth = std::max(maxWidth, std::int32_t(f.width));
                        maxHeight = std::max(maxHeight, std::int32_t(f.height));
                    }
                    if (maxWidth > 0 && maxHeight > 0) {
                        AMediaFormat_setInt32(m_Video->format, AMEDIAFORMAT_KEY_MAX_WIDTH, maxWidth);
                        AMediaFormat_setInt32(m_Video->format, AMEDIAFORMAT_KEY_MAX_HEIGHT, maxHeight);
                    }
                }
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
                if (window && m_Video->stream) {
                    if (!CreateVideoCodec(window)) return;
                    m_FirstFrameRendered = false;
                    // Nothing read yet (no surface since the start): decoding starts where the
                    // track is, showing from the clock on. Otherwise a stream reads on, not back:
                    // everything starts again from the clock.
                    if (!m_Video->fed) m_Video->seekTargetUs = std::max(m_Video->seekTargetUs, ClockUs());
                    else Seek(ClockUs());
                    return;
                }
                if (window && CreateVideoCodec(window)) {
                    // Decode from the keyframe before the clock, showing from the clock on.
                    std::int64_t now = ClockUs();
                    AMediaExtractor_seekTo(m_Video->extractor, now, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
                    m_Video->inputDone = m_Video->outputDone = false;
                    m_Video->seekTargetUs = now;
                    m_FirstFrameRendered = false;
                }
            }

            // --- adaptive streams --------------------------------------------------------------
            // HlsMediaSource / DashMediaSource preparing: the manifest, the track groups, the
            // loaders from the start position (a live stream's default one when none is given),
            // then the tracks of their first pieces.
            void OpenAdaptive(const std::string& uri, std::int64_t startUs) {
                using namespace media::stream;
                try {
                    auto fetcher = std::make_shared<SegmentFetcher>(net::GetHttpEngine(), &BandwidthMeter());
                    m_Adaptive = AdaptiveSource::Open(uri, m_MimeType, std::move(fetcher));
                } catch (const HttpStatusError& e) {
                    return Fail(e.what(), media::PlaybackException::IoBadHttpStatus);
                } catch (const ParserException& e) {
                    return Fail(e.what(), media::PlaybackException::ParsingManifestMalformed);
                } catch (const std::exception& e) {
                    return Fail(e.what(), media::PlaybackException::IoNetworkConnectionFailed);
                }
                const StreamWindow w = m_Adaptive->Window();
                m_WindowStartUs = w.startUs;
                m_TargetLiveOffsetUs = w.targetLiveOffsetUs;
                m_DurationUs = w.dynamic || w.durationUs == kTimeUnset ? -1 : w.startUs + w.durationUs;
                m_State.durationMs = w.durationUs == kTimeUnset ? -1 : w.durationUs / 1000;
                const std::int64_t positionUs = w.startUs + (w.dynamic && startUs <= 0 ? w.defaultPositionUs : std::max<std::int64_t>(startUs, 0));
                m_StreamSelection = SelectStreamGroups();
                const StreamSelection& sel = m_StreamSelection;
                ReportStreamTracks();
                if (sel.videoGroup < 0 && sel.audioGroup < 0) return Fail("no audio or video in " + uri, media::PlaybackException::ParsingContainerUnsupported);
                const bool muxed = sel.audioGroup >= 0 && sel.audioGroup == sel.videoGroup;
                auto makeStream = [&](int group, const std::vector<int>& tracks, int readers) {
                    SegmentStream::Config config;
                    config.group = group;
                    config.tracks = tracks;
                    config.readers = readers;
                    config.bandwidthMeter = &BandwidthMeter();
                    auto stream = std::make_shared<SegmentStream>(m_Adaptive, config);
                    stream->Start(positionUs);
                    return stream;
                };
                m_VideoGroup = sel.videoGroup;
                m_VideoTracks = sel.videoTracks;
                if (sel.videoGroup >= 0) m_VideoStream = makeStream(sel.videoGroup, sel.videoTracks, muxed ? 2 : 1);
                if (muxed) m_AudioStream = m_VideoStream;
                else if (sel.audioGroup >= 0) m_AudioStream = makeStream(sel.audioGroup, sel.audioTracks, 1);
                // The first pieces (blocking, as an extractor on a URL does while it prepares).
                for (const auto& s : { m_VideoStream, m_AudioStream }) {
                    if (!s) continue;
                    const std::int64_t deadline = NowUs() + 30'000'000;
                    for (;;) {
                        if (auto piece = s->FirstPiece(); piece && piece->CanOpen()) break;
                        if (const std::string error = s->Error(); !error.empty())
                            return Fail(error, error.starts_with("HTTP ") ? media::PlaybackException::IoBadHttpStatus : media::PlaybackException::IoNetworkConnectionFailed);
                        if (NowUs() > deadline) return Fail("timed out loading " + uri, media::PlaybackException::IoNetworkConnectionTimeout);
                        {
                            std::lock_guard lock(m_Mutex);
                            if (m_Quit || m_UriChanged) return Close();
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                }
                auto openTrack = [&](const std::shared_ptr<SegmentStream>& stream, int reader, bool video) -> std::unique_ptr<Track> {
                    auto t = std::make_unique<Track>();
                    t->stream = stream;
                    t->reader = reader;
                    t->video = video;
                    t->seekTargetUs = positionUs;
                    t->sampleReader = std::make_unique<TrackReader>(stream, reader, video);
                    std::optional<TrackReader::Item> first = t->sampleReader->PopFirst(30'000'000);
                    if (!first || first->kind != TrackReader::Item::Kind::Format) return nullptr;
                    t->format = first->format.release();
                    const char* mime = nullptr;
                    AMediaFormat_getString(t->format, AMEDIAFORMAT_KEY_MIME, &mime);
                    t->mime = mime ? mime : "";
                    if (first->minPresentationUs != kTimeUnset) t->seekTargetUs = std::max(t->seekTargetUs, first->minPresentationUs);
                    return t;
                };
                if (m_VideoStream && sel.videoGroup >= 0 && m_Adaptive->Groups()[std::size_t(sel.videoGroup)].type == media::TrackType::Video)
                    m_Video = openTrack(m_VideoStream, 0, true);
                if (m_AudioStream) m_Audio = openTrack(m_AudioStream, muxed ? 1 : 0, false);
                // A reader without its track (the variants' segments hold no audio after all)
                // must not hold segments back.
                if (muxed && !m_Audio) m_VideoStream->DetachReader(1);
                if (muxed && !m_Video) m_VideoStream->DetachReader(0);
                if (!m_Video && !m_Audio) return Fail("no audio or video in " + uri, media::PlaybackException::ParsingContainerUnsupported);
                m_Network = true;
                FinishOpen(positionUs);
            }

            // DefaultTrackSelector over the stream's groups: the first video group (adaptive over
            // its tracks), and the audio AudioTrackInfo ranks first among the audio groups and the
            // variants' muxed audio; an override picks a group and its tracks.
            StreamSelection SelectStreamGroups() const {
                StreamSelection out;
                const auto& groups = m_Adaptive->Groups();
                auto overrideFor = [&](media::TrackType type) -> const media::TrackSelectionOverride* {
                    for (const auto& o : m_TrackSelection.overrides)
                        if (o.type == type) return &o;
                    return nullptr;
                };
                auto groupIndex = [&](const std::string& id) {
                    for (std::size_t i = 0; i < groups.size(); ++i)
                        if (groups[i].id == id) return int(i);
                    return -1;
                };
                auto all = [&](int group) {
                    std::vector<int> tracks;
                    for (int i = 0; i < int(groups[std::size_t(group)].formats.size()); ++i) tracks.push_back(i);
                    return tracks;
                };
                // DefaultTrackSelector: the tracks within the decoders' capabilities, else those
                // that exceed them (exceedRendererCapabilitiesIfNecessary), else all.
                // Trick-play (I-frame only) tracks are never selected; adaptation stays within one
                // MIME type (isCompatibleForAdaptationWith without allowMixedMimeTypes): the type
                // with the most such tracks.
                auto supported = [&](int group) {
                    std::vector<int> handled, exceeding, any;
                    const auto& formats = groups[std::size_t(group)].formats;
                    for (int i = 0; i < int(formats.size()); ++i) {
                        if (formats[std::size_t(i)].roleFlags & media::kRoleFlagTrickPlay) continue;
                        any.push_back(i);
                        const int support = DecoderSupport(formats[std::size_t(i)]);
                        if (support == 2) handled.push_back(i);
                        else if (support == 1) exceeding.push_back(i);
                    }
                    std::vector<int> tracks = !handled.empty() ? handled : !exceeding.empty() ? exceeding : !any.empty() ? any : all(group);
                    std::map<std::string, int> perMime;
                    std::string mime;
                    for (int i : tracks)
                        if (++perMime[formats[std::size_t(i)].sampleMimeType] > perMime[mime] || mime.empty()) mime = formats[std::size_t(i)].sampleMimeType;
                    std::erase_if(tracks, [&](int i) { return formats[std::size_t(i)].sampleMimeType != mime; });
                    return tracks;
                };
                int primary = -1;
                for (std::size_t i = 0; i < groups.size(); ++i)
                    if (groups[i].primary) primary = int(i);
                if (!m_TrackSelection.disabledTrackTypes.contains(media::TrackType::Video)) {
                    if (const auto* o = overrideFor(media::TrackType::Video)) {
                        const int g = groupIndex(o->groupId);
                        if (g >= 0 && !o->trackIndices.empty()) {
                            out.videoGroup = g;
                            out.videoTracks = o->trackIndices;
                        }
                    } else {
                        for (std::size_t i = 0; i < groups.size(); ++i)
                            if (groups[i].type == media::TrackType::Video) {
                                out.videoGroup = int(i);
                                out.videoTracks = supported(int(i));
                                break;
                            }
                    }
                }
                if (!m_TrackSelection.disabledTrackTypes.contains(media::TrackType::Audio)) {
                    // Candidates: the audio groups (by their first format) and the muxed audio.
                    std::vector<media::Format> formats;
                    std::vector<int> owners;   // the group; for muxed audio, the primary group
                    for (std::size_t i = 0; i < groups.size(); ++i) {
                        if (groups[i].type == media::TrackType::Audio && !groups[i].formats.empty()) {
                            formats.push_back(groups[i].formats.front());
                            owners.push_back(int(i));
                        }
                        if (groups[i].embeddedAudio) {
                            formats.push_back(*groups[i].embeddedAudio);
                            owners.push_back(int(i));
                        }
                    }
                    if (const auto* o = overrideFor(media::TrackType::Audio)) {
                        const std::string id = o->groupId;
                        const int g = groupIndex(id.ends_with(":audio") ? id.substr(0, id.size() - 6) : id);
                        if (g >= 0 && !o->trackIndices.empty()) {
                            out.audioGroup = g;
                            out.audioTracks = id.ends_with(":audio") ? out.videoTracks : o->trackIndices;
                        }
                    } else if (!formats.empty()) {
                        // Only formats a decoder takes (within its capabilities when any is).
                        std::vector<int> support(formats.size());
                        bool anyHandled = false;
                        for (std::size_t i = 0; i < formats.size(); ++i) {
                            support[i] = DecoderSupport(formats[i]);
                            anyHandled = anyHandled || support[i] == 2;
                        }
                        std::vector<bool> playable(formats.size());
                        for (std::size_t i = 0; i < formats.size(); ++i) playable[i] = anyHandled ? support[i] == 2 : support[i] >= 1;
                        const int best = media::SelectAudioTrack(formats, playable, m_TrackSelection, ui::PlatformLocaleList());
                        if (best >= 0) {
                            out.audioGroup = owners[std::size_t(best)];
                            out.audioTracks = groups[std::size_t(out.audioGroup)].type == media::TrackType::Audio ? supported(out.audioGroup) : out.videoTracks;
                        }
                    }
                    // Muxed audio with the video disabled: the variants still load, for the audio.
                    if (out.audioGroup >= 0 && groups[std::size_t(out.audioGroup)].type != media::TrackType::Audio && out.audioTracks.empty())
                        out.audioTracks = all(out.audioGroup);
                    if (out.audioGroup >= 0 && out.audioGroup == primary && out.videoGroup < 0) {
                        out.videoGroup = out.audioGroup;   // its loader
                        out.videoTracks = out.audioTracks;
                    }
                }
                return out;
            }

            // getCurrentTracks for a stream: its groups (the variants' muxed audio as a group of
            // its own), the selection's tracks selected.
            void ReportStreamTracks() {
                m_State.tracks = {};
                const auto& groups = m_Adaptive->Groups();
                const StreamSelection& sel = m_StreamSelection;
                for (std::size_t i = 0; i < groups.size(); ++i) {
                    const auto& g = groups[i];
                    media::Tracks::Group out;
                    out.id = g.id;
                    out.type = g.type;
                    out.formats = g.formats;
                    for (const auto& f : g.formats) out.trackSupported.push_back(DecoderSupport(f) == 2);
                    out.trackSelected.assign(g.formats.size(), false);
                    const bool videoHere = int(i) == sel.videoGroup && g.type == media::TrackType::Video;
                    const bool audioHere = int(i) == sel.audioGroup && g.type == media::TrackType::Audio;
                    for (int t : videoHere ? sel.videoTracks : audioHere ? sel.audioTracks : std::vector<int>{})
                        if (t >= 0 && t < int(g.formats.size())) out.trackSelected[std::size_t(t)] = true;
                    m_State.tracks.groups.push_back(std::move(out));
                    if (g.embeddedAudio) {
                        media::Tracks::Group audio;
                        audio.id = g.id + ":audio";
                        audio.type = media::TrackType::Audio;
                        audio.formats = { *g.embeddedAudio };
                        audio.trackSupported = { DecoderSupport(*g.embeddedAudio) == 2 };
                        audio.trackSelected = { int(i) == sel.audioGroup };
                        m_State.tracks.groups.push_back(std::move(audio));
                    }
                }
            }

            static std::vector<std::uint8_t> FormatBuffer(AMediaFormat* f, const char* key) {
                void* data = nullptr;
                size_t size = 0;
                if (!AMediaFormat_getBuffer(f, key, &data, &size) || !data) return {};
                const auto* bytes = static_cast<const std::uint8_t*>(data);
                return { bytes, bytes + size };
            }
            // MediaCodecRenderer.onInputFormatChanged / canReuseCodec: video of the same type
            // keeps its decoder (the new codec-specific data first); other changes drain it.
            void ApplyNewFormat(Track& t, AMediaFormat* format, const std::string& mime) {
                if (t.video && mime == t.mime) {
                    for (const char* key : { "csd-0", "csd-1" }) {
                        std::vector<std::uint8_t> csd = FormatBuffer(format, key);
                        if (!csd.empty()) t.pendingConfig.push_back(std::move(csd));
                    }
                    AMediaFormat_delete(format);
                    return;
                }
                if (!t.video && mime == t.mime) {
                    std::int32_t rate = 0, channels = 0, oldRate = 0, oldChannels = 0;
                    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, &rate);
                    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channels);
                    AMediaFormat_getInt32(t.format, AMEDIAFORMAT_KEY_SAMPLE_RATE, &oldRate);
                    AMediaFormat_getInt32(t.format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &oldChannels);
                    if (rate == oldRate && channels == oldChannels && FormatBuffer(format, "csd-0") == FormatBuffer(t.format, "csd-0")) {
                        AMediaFormat_delete(format);
                        return;
                    }
                }
                if (t.reinitFormat) AMediaFormat_delete(t.reinitFormat);
                t.reinitFormat = format;
            }

            // The drained decoder gives way to one for the new format.
            void Reinitialize(Track& t) {
                AMediaFormat_delete(t.format);
                t.format = std::exchange(t.reinitFormat, nullptr);
                const char* mime = nullptr;
                AMediaFormat_getString(t.format, AMEDIAFORMAT_KEY_MIME, &mime);
                t.mime = mime ? mime : "";
                t.ReleaseCodec();
                t.inputDone = t.outputDone = false;
                t.pendingConfig.clear();
                if (&t == m_Video.get()) {
                    m_PendingVideo = -1;
                    if (m_LastWindow) CreateVideoCodec(m_LastWindow);
                    return;
                }
                t.codec = AMediaCodec_createDecoderByType(t.mime.c_str());
                if (!t.codec || AMediaCodec_configure(t.codec, t.format, nullptr, nullptr, 0) != AMEDIA_OK || AMediaCodec_start(t.codec) != AMEDIA_OK)
                    return Fail("no decoder for " + t.mime, media::PlaybackException::DecoderInitFailed);
                AMediaFormat_getInt32(t.format, AMEDIAFORMAT_KEY_SAMPLE_RATE, &m_SampleRate);
                AMediaFormat_getInt32(t.format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &m_Channels);
                m_AudioOpened = false;
                ConfigureSonic();
            }

            // The reader's items into the decoder: formats (codec-specific data, or a drain for a
            // new decoder), samples, and the end.
            void FeedStreamInput(Track& t) {
                for (int i = 0; i < 8 && !t.inputDone; ++i) {
                    if (t.reinitFormat) {
                        // A decoder to replace drains first: the end of its input.
                        const ssize_t idx = AMediaCodec_dequeueInputBuffer(t.codec, 0);
                        if (idx < 0) return;
                        AMediaCodec_queueInputBuffer(t.codec, size_t(idx), 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                        t.inputDone = true;
                        return;
                    }
                    if (!t.pending) {
                        t.pending = t.sampleReader->Pop();
                        if (!t.pending) return;
                    }
                    TrackReader::Item& item = *t.pending;
                    if (item.kind == TrackReader::Item::Kind::Format) {
                        if (item.minPresentationUs != media::stream::kTimeUnset) t.seekTargetUs = std::max(t.seekTargetUs, item.minPresentationUs);
                        const char* mime = nullptr;
                        AMediaFormat_getString(item.format.get(), AMEDIAFORMAT_KEY_MIME, &mime);
                        const std::string m = mime ? mime : "";
                        ApplyNewFormat(t, item.format.release(), m);
                        t.pending.reset();
                        continue;
                    }
                    if (item.kind == TrackReader::Item::Kind::NoTrack) {
                        t.pending.reset();
                        continue;
                    }
                    const ssize_t idx = AMediaCodec_dequeueInputBuffer(t.codec, 0);
                    if (idx < 0) return;
                    size_t capacity = 0;
                    std::uint8_t* buffer = AMediaCodec_getInputBuffer(t.codec, size_t(idx), &capacity);
                    if (!t.pendingConfig.empty()) {
                        const std::vector<std::uint8_t> csd = std::move(t.pendingConfig.front());
                        t.pendingConfig.erase(t.pendingConfig.begin());
                        const size_t n = buffer ? std::min(capacity, csd.size()) : 0;
                        if (n) std::memcpy(buffer, csd.data(), n);
                        AMediaCodec_queueInputBuffer(t.codec, size_t(idx), 0, n, 0, AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG);
                        continue;
                    }
                    if (item.kind == TrackReader::Item::Kind::End) {
                        AMediaCodec_queueInputBuffer(t.codec, size_t(idx), 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                        t.inputDone = true;
                        t.pending.reset();
                        return;
                    }
                    const size_t n = buffer ? std::min(capacity, item.data.size()) : 0;
                    if (n) std::memcpy(buffer, item.data.data(), n);
                    AMediaCodec_queueInputBuffer(t.codec, size_t(idx), 0, n, std::uint64_t(std::max<std::int64_t>(item.timeUs, 0)), 0);
                    t.fed = true;
                    t.pending.reset();
                }
            }

            // A renderer that is not ready (MediaCodecRenderer.isReady): its reader has nothing and
            // waits for the network, and it has nothing left to play either: no audio pending in
            // the sink (AudioRenderer's audioSink.hasPendingData), no video frame to show.
            bool StreamsStarving() const {
                for (const Track* t : { m_Video.get(), m_Audio.get() }) {
                    if (!t || !t->stream || !t->codec || t->inputDone || t->pending || !t->sampleReader || !t->sampleReader->Starving()) continue;
                    if (t == m_Audio.get()) {
                        if (m_PendingPcmOffset < m_PendingPcm.size()) continue;
                        if (m_Stream && AAudioStream_getFramesWritten(m_Stream) - AAudioStream_getFramesRead(m_Stream) > m_SampleRate / 50) continue;
                        return true;
                    }
                    if (m_PendingVideo >= 0 || NowUs() - m_LastReleaseUs < 200'000) continue;
                    return true;
                }
                return false;
            }

            bool StreamsBusy() const {
                if (!m_Adaptive) return false;
                if (m_State.state == media::PlaybackState::Buffering) return true;
                for (const auto& st : { m_VideoStream, m_AudioStream })
                    if (st && st->IsLoading()) return true;
                return false;
            }

            std::string StreamError() const {
                for (const auto& s : { m_VideoStream, m_AudioStream })
                    if (s)
                        if (std::string e = s->Error(); !e.empty()) return e;
                return {};
            }
            bool StreamsEnded() const {
                for (const auto& s : { m_VideoStream, m_AudioStream })
                    if (s && !s->IsEnded()) return false;
                return true;
            }
            // How far every stream is loaded (kTimeUnset while one has nothing).
            std::int64_t StreamsBufferedUs() const {
                std::int64_t buffered = INT64_MAX;
                for (const auto& s : { m_VideoStream, m_AudioStream }) {
                    if (!s || s->IsEnded()) continue;
                    const std::int64_t b = s->BufferedPositionUs();
                    if (b == media::stream::kTimeUnset) return media::stream::kTimeUnset;
                    buffered = std::min(buffered, b);
                }
                return buffered == INT64_MAX ? media::stream::kTimeUnset : buffered;
            }
            // DefaultLoadControl.shouldStartPlayback: 2.5 s loaded ahead (5 s after a rebuffer, at
            // most half a live stream's target offset), or everything.
            bool StreamsReadyToPlay() const {
                if (!m_Adaptive || StreamsEnded()) return true;
                if (StreamsStarving()) return false;
                const std::int64_t buffered = StreamsBufferedUs();
                if (buffered == media::stream::kTimeUnset) return false;
                std::int64_t minUs = m_Rebuffering ? 5'000'000 : 2'500'000;
                if (m_TargetLiveOffsetUs > 0) minUs = std::min(minUs, m_TargetLiveOffsetUs / 2);
                minUs = std::int64_t(double(minUs) * m_Params.speed);
                return buffered - m_ClockBaseUs >= minUs;
            }
            // A live window moves as the playlists refresh: positions follow it once a second.
            void RefreshWindow() {
                const std::int64_t now = NowUs();
                if (now - m_WindowCheckedUs < 1'000'000) return;
                m_WindowCheckedUs = now;
                const media::stream::StreamWindow w = m_Adaptive->Window();
                if (!w.dynamic) {
                    if (m_DurationUs < 0 && w.durationUs != media::stream::kTimeUnset) {
                        // A live stream that ended (its end tag arrived).
                        m_DurationUs = w.startUs + w.durationUs;
                    }
                }
                m_WindowStartUs = w.startUs;
                m_State.durationMs = w.durationUs == media::stream::kTimeUnset ? -1 : w.durationUs / 1000;
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
                if (m_Adaptive) {
                    const StreamSelection selection = SelectStreamGroups();
                    if (selection == m_StreamSelection) return;
                    Open(m_Uri, std::max<std::int64_t>(ClockUs() - m_WindowStartUs, 0));
                    return;
                }
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
                // Streams load again from the segment containing the position.
                // The readers stop before their pieces go; they start again on the new ones.
                for (Track* t : { m_Video.get(), m_Audio.get() })
                    if (t && t->stream) t->sampleReader.reset();
                // HlsMediaPeriod.seekToUs: the timestamp adjusters start again with the new chunks.
                if (m_Adaptive) m_Adaptive->Adjusters().Reset();
                if (m_VideoStream) m_VideoStream->Start(us);
                if (m_AudioStream && m_AudioStream != m_VideoStream) m_AudioStream->Start(us);
                for (Track* t : { m_Video.get(), m_Audio.get() }) {
                    if (!t) continue;
                    if (t->stream) {
                        t->sampleReader = std::make_unique<TrackReader>(t->stream, t->reader, t->video);
                        t->pending.reset();
                        t->pendingConfig.clear();
                        if (t->reinitFormat) AMediaFormat_delete(t->reinitFormat);
                        t->reinitFormat = nullptr;
                        t->fed = false;
                    } else {
                        AMediaExtractor_seekTo(t->extractor, us, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
                    }
                    if (t->codec) AMediaCodec_flush(t->codec);
                    t->inputDone = t->outputDone = false;
                    t->seekTargetUs = us;
                }
                m_Rebuffering = false;
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
                if (t.stream) return FeedStreamInput(t);
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
                // AAudioStreamBuilder_setSpatializationBehavior: API 32, above kiln's minimum.
                using SetSpatialization = void (*)(AAudioStreamBuilder*, aaudio_spatialization_behavior_t);
                static const auto setSpatialization = reinterpret_cast<SetSpatialization>(dlsym(RTLD_DEFAULT, "AAudioStreamBuilder_setSpatializationBehavior"));
                if (setSpatialization)
                    setSpatialization(b, m_Spatialization.load() == 1 ? AAUDIO_SPATIALIZATION_BEHAVIOR_NEVER : AAUDIO_SPATIALIZATION_BEHAVIOR_AUTO);
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
                    const bool endOfStream = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
                    if (endOfStream && !t.reinitFormat) t.outputDone = true;
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
                    if (endOfStream && t.reinitFormat) {
                        Reinitialize(t);
                        return;
                    }
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
                        if (m_PendingVideoEos) {
                            if (t.reinitFormat) return Reinitialize(t);
                            t.outputDone = true;
                        }
                        continue;
                    }
                    // A drain for a new decoder: its last frame shows, the rest of the stream goes on.
                    if (m_PendingVideoEos && t.reinitFormat) {
                        AMediaCodec_releaseOutputBuffer(t.codec, idx, true);
                        m_PendingVideo = -1;
                        return Reinitialize(t);
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
                if (m_Adaptive) {
                    if (const std::string error = StreamError(); !error.empty())
                        return Fail(error, error.starts_with("HTTP ") ? media::PlaybackException::IoBadHttpStatus : media::PlaybackException::IoNetworkConnectionFailed);
                    const std::int64_t now = ClockUs();
                    for (const auto& s : { m_VideoStream, m_AudioStream })
                        if (s) s->SetPlaybackPosition(now, m_Params.speed);
                    RefreshWindow();
                }
                if (m_Video) FeedInput(*m_Video);
                if (m_Audio) FeedInput(*m_Audio);
                if (m_Audio) DrainAudio();
                if (m_Video) DrainVideo();
                bool videoReady = !m_Video || !m_Video->codec || m_FirstFrameRendered || m_Video->outputDone;
                if (m_State.state == media::PlaybackState::Buffering && videoReady && m_AudioReady && StreamsReadyToPlay()) {
                    m_State.state = media::PlaybackState::Ready;
                    m_Rebuffering = false;
                    m_ClockRealUs = NowUs();
                    SetPlaying(m_PlayingWanted);
                }
                // ExoPlayerImplInternal: playing with nothing loaded for the position: buffering
                // again (until DefaultLoadControl's buffer after a rebuffer is there).
                if (m_Adaptive && m_State.state == media::PlaybackState::Ready && !StreamsEnded()) {
                    const std::int64_t buffered = StreamsBufferedUs();
                    if ((buffered != media::stream::kTimeUnset && buffered <= ClockUs()) || StreamsStarving()) {
                        m_State.state = media::PlaybackState::Buffering;
                        m_Rebuffering = true;
                        SetPlaying(m_PlayingWanted);
                    }
                }
                bool videoDone = !m_Video || !m_Video->codec || m_Video->outputDone;
                bool audioDone = !m_Audio || (m_Audio->outputDone && m_PendingPcmOffset >= m_PendingPcm.size() && (!Stretching() || m_SonicEnded));
                // The media's end, or the item's clipped end (ClippingMediaSource).
                const std::int64_t clipEndUs = m_EndUs >= 0 ? m_WindowStartUs + m_EndUs : -1;
                const std::int64_t endUs = clipEndUs >= 0 && (m_DurationUs < 0 || clipEndUs < m_DurationUs) ? clipEndUs : m_DurationUs;
                const bool clippedEnd = clipEndUs >= 0 && ClockUs() >= clipEndUs;
                if (m_State.state == media::PlaybackState::Ready && ((videoDone && audioDone && ClockUs() >= m_DurationUs - 1000) || clippedEnd)) {
                    if (m_Repeat) {
                        Seek(m_WindowStartUs + m_StartClipUs);
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
                if (m_Prepared && m_Adaptive) {
                    std::int64_t us = ClockUs();
                    if (m_DurationUs > 0) us = std::min(us, m_DurationUs);
                    s.positionMs = std::max<std::int64_t>(us - m_WindowStartUs, 0) / 1000;
                    const std::int64_t buffered = StreamsEnded() && m_DurationUs > 0 ? m_DurationUs : StreamsBufferedUs();
                    s.bufferedPositionMs = buffered == media::stream::kTimeUnset ? s.positionMs : std::max<std::int64_t>(buffered - m_WindowStartUs, 0) / 1000;
                    s.bufferedPositionMs = std::max(s.bufferedPositionMs, s.positionMs);
                    s.isLoading = false;
                    for (const auto& st : { m_VideoStream, m_AudioStream })
                        if (st && st->IsLoading()) s.isLoading = true;
                } else if (m_Prepared) {
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
            std::string m_Uri, m_MimeType;
            // An adaptive stream: its manifest, the loaders the tracks read from (one for both
            // when the audio is muxed into the video's segments), and its window on the period's
            // timeline (the clock runs on that timeline; the player sees positions in the window).
            std::shared_ptr<media::stream::AdaptiveSource> m_Adaptive;
            std::shared_ptr<media::stream::SegmentStream> m_VideoStream, m_AudioStream;
            std::int64_t m_WindowStartUs = 0, m_WindowCheckedUs = 0, m_TargetLiveOffsetUs = -1;
            bool m_Rebuffering = false;
            StreamSelection m_StreamSelection;
            int m_VideoGroup = -1;
            std::vector<int> m_VideoTracks;
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
