#include "kiln/net/Http.h"

#include "kiln/runtime/TaskDispatchers.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace kiln::net {

    namespace {
        bool SameName(std::string_view a, std::string_view b) {
            return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](unsigned char x, unsigned char y) { return std::tolower(x) == std::tolower(y); });
        }
        std::mutex g_EngineMutex;
        std::shared_ptr<HttpEngine>& Engine() {
            static auto* e = new std::shared_ptr<HttpEngine>;
            return *e;
        }
    }

    std::optional<std::string> Headers::Get(std::string_view name) const {
        for (const auto& [n, v] : m_Fields) if (SameName(n, name)) return v;
        return std::nullopt;
    }
    std::vector<std::string> Headers::All(std::string_view name) const {
        std::vector<std::string> out;
        for (const auto& [n, v] : m_Fields) if (SameName(n, name)) out.push_back(v);
        return out;
    }
    void Headers::Set(std::string name, std::string value) {
        Remove(name);
        m_Fields.emplace_back(std::move(name), std::move(value));
    }
    void Headers::Remove(std::string_view name) {
        std::erase_if(m_Fields, [&](const auto& f) { return SameName(f.first, name); });
    }

    void HttpCall::Cancel() {
        m_Cancelled = true;
        std::lock_guard lock(m_Mutex);   // the engine clears the hook under it before its state goes
        if (m_Abort) m_Abort();
    }
    void HttpCall::SetAbort(kiln::Function<void()> abort) {
        std::lock_guard lock(m_Mutex);
        m_Abort = std::move(abort);
    }
    bool HttpCall::Progress(std::int64_t received, std::int64_t total) {
        if (Cancelled()) return false;
        if (progressSink) progressSink(received, total);
        return true;
    }

    std::shared_ptr<HttpEngine> PlatformHttpEngine();   // HttpCurl.cpp on the desktop

    void SetHttpEngine(std::shared_ptr<HttpEngine> engine) {
        std::lock_guard lock(g_EngineMutex);
        Engine() = std::move(engine);
    }
    std::shared_ptr<HttpEngine> GetHttpEngine() {
        std::lock_guard lock(g_EngineMutex);
        if (!Engine()) Engine() = PlatformHttpEngine();
        return Engine();
    }
#if defined(__ANDROID__)
    std::shared_ptr<HttpEngine> PlatformHttpEngine() { return nullptr; }   // installed by the host
#endif

    namespace detail {
        namespace {
            // The latest progress, handed to the task's dispatcher at most once per pending post.
            struct ProgressRelay {
                std::atomic<std::int64_t> received{ 0 }, total{ -1 };
                std::atomic<bool> posted{ false };
                kiln::Function<void(std::int64_t, std::int64_t)> sink;
            };
        }

        void StartFetch(HttpRequest request, std::shared_ptr<FetchState> state, std::shared_ptr<rt::JobState> job, std::coroutine_handle<> h) {
            rt::Dispatcher* home = job && job->dispatcher ? job->dispatcher : &rt::Dispatcher::Main();
            std::weak_ptr<rt::JobState> weak = job;
            if (job) job->unregister = [state] {
                state->abandoned = true;
                state->call->Cancel();
            };
            if (request.onProgress) {
                auto relay = std::make_shared<ProgressRelay>();
                relay->sink = std::move(request.onProgress);
                request.onProgress = nullptr;
                state->call->progressSink = [relay, home, state](std::int64_t received, std::int64_t total) {
                    relay->received = received;
                    relay->total = total;
                    if (relay->posted.exchange(true)) return;
                    home->PostFromAnyThread([relay, state] {
                        relay->posted = false;
                        if (!state->abandoned) relay->sink(relay->received, relay->total);
                    });
                };
            }
            std::shared_ptr<HttpEngine> engine = GetHttpEngine();
            rt::TaskDispatchers::Platform().IO().Execute([request = std::move(request), state, engine, home, weak, h]() mutable {
                try {
                    if (!engine) throw HttpError(HttpError::Kind::NoEngine, "kiln: no HTTP engine");
                    if (state->call->Cancelled()) throw HttpError(HttpError::Kind::Cancelled, "cancelled");
                    state->response.emplace(engine->Execute(request, *state->call));
                } catch (...) {
                    state->error = std::current_exception();
                    if (!request.responseFile.empty()) std::remove(request.responseFile.c_str());
                }
                state->call->SetAbort(nullptr);
                state->call->progressSink = nullptr;   // it holds the state: the engine is done with it
                home->PostFromAnyThread([state, weak, h] {
                    if (state->abandoned) return;
                    auto j = weak.lock();
                    if (j) {
                        if (j->cancelled) return;
                        j->unregister = nullptr;
                    }
                    rt::ResumeJob(j, h);
                });
            });
        }
    }

    rt::MutableStateFlow<NetworkState>& Connectivity() {
        static auto* state = new rt::MutableStateFlow<NetworkState>(NetworkState{});
        return *state;
    }

    std::string UrlEncode(std::string_view value) {
        static const char* hex = "0123456789ABCDEF";
        std::string out;
        for (const unsigned char c : value) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += char(c);
            else if (c == ' ') out += '+';
            else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
        }
        return out;
    }
    std::string FormBody(const std::vector<std::pair<std::string, std::string>>& fields) {
        std::string out;
        for (const auto& [name, value] : fields) {
            if (!out.empty()) out += '&';
            out += UrlEncode(name) + "=" + UrlEncode(value);
        }
        return out;
    }

}
