#include "kiln/net/Http.h"

#include "kiln/runtime/TaskDispatchers.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <limits>

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
            struct UrlParts {
                std::string_view scheme, authority, path, query, fragment;
                bool hasAuthority = false, hasQuery = false, hasFragment = false;
            };
            UrlParts Split(std::string_view u) {
                UrlParts p;
                if (const auto hash = u.find('#'); hash != std::string_view::npos) {
                    p.fragment = u.substr(hash + 1);
                    p.hasFragment = true;
                    u = u.substr(0, hash);
                }
                if (const auto q = u.find('?'); q != std::string_view::npos) {
                    p.query = u.substr(q + 1);
                    p.hasQuery = true;
                    u = u.substr(0, q);
                }
                const auto colon = u.find(':');
                if (colon != std::string_view::npos && colon > 0 && std::isalpha(static_cast<unsigned char>(u[0])) &&
                    u.substr(0, colon).find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+-.") == std::string_view::npos) {
                    p.scheme = u.substr(0, colon);
                    u = u.substr(colon + 1);
                }
                if (u.starts_with("//")) {
                    u = u.substr(2);
                    const auto slash = u.find('/');
                    p.authority = u.substr(0, slash);
                    p.hasAuthority = true;
                    u = slash == std::string_view::npos ? std::string_view{} : u.substr(slash);
                }
                p.path = u;
                return p;
            }
            std::string Lower(std::string_view s) {
                std::string out(s);
                for (char& c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
                return out;
            }
            // RFC 3986 5.2.4.
            std::string RemoveDotSegments(std::string_view in) {
                std::string out;
                while (!in.empty()) {
                    if (in.starts_with("../")) in.remove_prefix(3);
                    else if (in.starts_with("./")) in.remove_prefix(2);
                    else if (in.starts_with("/./")) in.remove_prefix(2);
                    else if (in == "/.") in = "/";
                    else if (in.starts_with("/../") || in == "/..") {
                        in = in.size() == 3 ? std::string_view("/") : in.substr(3);
                        const auto last = out.rfind('/');
                        out.erase(last == std::string::npos ? 0 : last);
                    } else if (in == "." || in == "..") in = {};
                    else {
                        const auto next = in.find('/', 1);
                        out += in.substr(0, next);
                        in = next == std::string_view::npos ? std::string_view{} : in.substr(next);
                    }
                }
                return out;
            }
            // Host and port, as HttpUrl.canReuseConnectionFor compares them (with the scheme).
            std::string Origin(std::string_view url) {
                const UrlParts p = Split(url);
                const std::string scheme = Lower(p.scheme);
                std::string_view hostPort = p.authority;
                if (const auto at = hostPort.rfind('@'); at != std::string_view::npos) hostPort.remove_prefix(at + 1);
                std::string_view host = hostPort, port;
                const auto close = hostPort.rfind(']');
                if (const auto colon = hostPort.rfind(':'); colon != std::string_view::npos && (close == std::string_view::npos || colon > close)) {
                    host = hostPort.substr(0, colon);
                    port = hostPort.substr(colon + 1);
                }
                if (port.empty()) port = scheme == "https" ? "443" : "80";
                return scheme + "://" + Lower(host) + ":" + std::string(port);
            }
            int RetryAfter(const HttpResponse& response, int defaultDelay) {
                const auto header = response.headers.Get("Retry-After");
                if (!header) return defaultDelay;
                if (header->empty() || header->find_first_not_of("0123456789") != std::string::npos) return std::numeric_limits<int>::max();
                try { return std::stoi(*header); } catch (...) { return std::numeric_limits<int>::max(); }
            }
        }

        std::string ResolveUrl(std::string_view base, std::string_view reference) {
            const UrlParts b = Split(base), r = Split(reference);
            std::string scheme, authority, path, query;
            bool hasAuthority, hasQuery;
            if (!r.scheme.empty()) {
                scheme = Lower(r.scheme);
                authority = r.authority; hasAuthority = r.hasAuthority;
                path = RemoveDotSegments(r.path);
                query = r.query; hasQuery = r.hasQuery;
            } else {
                scheme = Lower(b.scheme);
                if (r.hasAuthority) {
                    authority = r.authority; hasAuthority = true;
                    path = RemoveDotSegments(r.path);
                    query = r.query; hasQuery = r.hasQuery;
                } else {
                    authority = b.authority; hasAuthority = b.hasAuthority;
                    if (r.path.empty()) {
                        path = b.path;
                        if (r.hasQuery) { query = r.query; hasQuery = true; }
                        else { query = b.query; hasQuery = b.hasQuery; }
                    } else {
                        if (r.path.starts_with('/')) path = RemoveDotSegments(r.path);
                        else {
                            // Merge (5.2.3).
                            std::string merged;
                            if (b.hasAuthority && b.path.empty()) merged = "/" + std::string(r.path);
                            else {
                                const auto slash = b.path.rfind('/');
                                merged = std::string(slash == std::string_view::npos ? std::string_view{} : b.path.substr(0, slash + 1)) + std::string(r.path);
                            }
                            path = RemoveDotSegments(merged);
                        }
                        query = r.query; hasQuery = r.hasQuery;
                    }
                }
            }
            if ((scheme != "http" && scheme != "https") || !hasAuthority || authority.empty()) return {};
            if (path.empty()) path = "/";
            std::string out = scheme + "://" + authority + path;
            if (hasQuery) out += "?" + query;
            if (r.hasFragment) out += "#" + std::string(r.fragment);
            return out;
        }

        std::optional<HttpRequest> FollowUp(const HttpRequest& request, const HttpResponse& response, int prior) {
            const int code = response.status;
            const std::string& method = request.method;
            switch (code) {
                case 300: case 301: case 302: case 303: case 307: case 308: {
                    if (!request.followRedirects) return std::nullopt;
                    const auto location = response.headers.Get("Location");
                    if (!location) return std::nullopt;
                    std::string url = ResolveUrl(request.url, *location);   // unsupported schemes stay unfollowed
                    if (url.empty()) return std::nullopt;
                    HttpRequest next = request;
                    if (method != "GET" && method != "HEAD") {
                        const bool toGet = code == 303 ? method != "PROPFIND"
                                         : code == 307 || code == 308 ? false
                                         : method != "PROPFIND" && method != "QUERY";
                        if (toGet) {
                            next.method = "GET";
                            next.body.clear();
                            next.bodyFile.clear();
                            next.headers.Remove("Transfer-Encoding");
                            next.headers.Remove("Content-Length");
                            next.headers.Remove("Content-Type");
                        }
                    }
                    // Across hosts the authentication goes.
                    if (Origin(request.url) != Origin(url)) next.headers.Remove("Authorization");
                    next.url = std::move(url);
                    return next;
                }
                case 408:
                    if (prior == 408 || RetryAfter(response, 0) > 0) return std::nullopt;
                    return request;
                case 503:
                    if (prior == 503 || RetryAfter(response, std::numeric_limits<int>::max()) != 0) return std::nullopt;
                    return request;
                default:
                    return std::nullopt;
            }
        }

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
                    // The engine makes one exchange; redirects and retries are followed here, the
                    // same on every platform.
                    HttpRequest current = request;
                    current.onProgress = nullptr;
                    int prior = 0;
                    for (int followUps = 0;;) {
                        HttpResponse response = engine->Execute(current, *state->call);
                        std::optional<HttpRequest> next = FollowUp(current, response, prior);
                        if (!next) {
                            state->response.emplace(std::move(response));
                            break;
                        }
                        if (++followUps > 20) throw HttpError(HttpError::Kind::TooManyRedirects, "Too many follow-up requests: " + std::to_string(followUps));
                        if (state->call->Cancelled()) throw HttpError(HttpError::Kind::Cancelled, "cancelled");
                        prior = response.status;
                        current = std::move(*next);
                    }
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
