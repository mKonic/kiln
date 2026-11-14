#pragma once

// HTTP requests as tasks. On Android they go through HttpURLConnection (the platform's TLS and
// trust store, proxies, the network security config and its cleartext rules); on the desktop
// through libcurl. A request runs on the IO pool and the task resumes on its own dispatcher;
// cancelling the task (leaving the composition, a scope ending) aborts the transfer.
//
//   rt::Task<> Load(rt::State<std::string>& text) {
//       try {
//           net::HttpResponse r = co_await net::Fetch({ .url = "https://example.com/notes.json" });
//           if (r.Ok()) text.Set(r.body);
//       } catch (const net::HttpError& e) {
//           text.Set(e.what());   // no connection, a timeout, a TLS failure
//       }
//   }
//
// An Android app declares android.permission.INTERNET (PERMISSIONS in kiln_add_app).

#include "kiln/runtime/Flow.h"
#include "kiln/runtime/Task.h"
#include <kiln/Function.h>

#include <atomic>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kiln::net {

    // Header fields in order; names compare without case.
    class Headers {
    public:
        Headers() = default;
        Headers(std::initializer_list<std::pair<std::string, std::string>> fields) : m_Fields(fields) {}
        // The first value of the field, if present.
        std::optional<std::string> Get(std::string_view name) const;
        std::vector<std::string> All(std::string_view name) const;
        bool Contains(std::string_view name) const { return Get(name).has_value(); }
        // Replaces the field's values with one.
        void Set(std::string name, std::string value);
        // Adds another value for the field.
        void Add(std::string name, std::string value) { m_Fields.emplace_back(std::move(name), std::move(value)); }
        void Remove(std::string_view name);
        const std::vector<std::pair<std::string, std::string>>& Fields() const { return m_Fields; }
        bool operator==(const Headers&) const = default;

    private:
        std::vector<std::pair<std::string, std::string>> m_Fields;
    };

    struct HttpRequest {
        std::string url;
        std::string method = "GET";
        Headers headers;
        // The request body (bytes), or a file to send instead. Set Content-Type in headers;
        // without one the body goes out as application/x-www-form-urlencoded.
        std::string body;
        std::string bodyFile;
        // Writes the response body to this file instead of HttpResponse::body (downloads).
        std::string responseFile;
        // Milliseconds; 0 waits without limit. The read timeout is between two reads, not for
        // the whole transfer.
        int connectTimeoutMs = 10000;
        int readTimeoutMs = 10000;
        // Follows up to 20 redirects (301, 302, 303 turn a POST into a GET; 307, 308 keep it).
        bool followRedirects = true;
        // Bytes of the response body received so far, and its length when the server said (-1
        // otherwise). Called on the task's dispatcher.
        kiln::Function<void(std::int64_t received, std::int64_t total)> onProgress;
    };

    struct HttpResponse {
        int status = 0;
        std::string statusText;
        Headers headers;
        std::string body;
        // The URL that answered, after redirects.
        std::string url;
        // 200 to 299. Other statuses are responses, not errors.
        bool Ok() const { return status >= 200 && status < 300; }
    };

    // The request got no response.
    class HttpError : public std::runtime_error {
    public:
        enum class Kind {
            InvalidUrl,
            UnknownHost,   // the name does not resolve (often: no connection)
            Connect,       // refused or unreachable
            Timeout,
            Tls,           // certificate or handshake failure
            Cleartext,     // http:// refused by the network security config
            Permission,    // android.permission.INTERNET is missing
            TooManyRedirects,
            Io,            // the connection broke, a file could not be read or written
            Cancelled,
            NoEngine,
        };
        HttpError(Kind kind, const std::string& message) : std::runtime_error(message), kind(kind) {}
        Kind kind;
    };

    // One request in progress, shared by the task waiting for it and the engine running it.
    class HttpCall {
    public:
        bool Cancelled() const { return m_Cancelled.load(std::memory_order_relaxed); }
        // From any thread: marks the call cancelled and runs the engine's abort hook.
        void Cancel();
        // Engines: how to interrupt the transfer (close the socket); cleared with nullptr.
        void SetAbort(kiln::Function<void()> abort);
        // Engines: report body bytes received; false when the call was cancelled.
        bool Progress(std::int64_t received, std::int64_t total);

        // Set by Fetch.
        kiln::Function<void(std::int64_t, std::int64_t)> progressSink;

    private:
        std::atomic<bool> m_Cancelled{ false };
        std::mutex m_Mutex;
        kiln::Function<void()> m_Abort;
    };

    // What performs a request, on the calling (IO) thread. Throws HttpError.
    class HttpEngine {
    public:
        virtual ~HttpEngine() = default;
        virtual HttpResponse Execute(const HttpRequest& request, HttpCall& call) = 0;
    };
    // The platform's engine (Android installs its own; the desktop's is libcurl), or a test's.
    void SetHttpEngine(std::shared_ptr<HttpEngine> engine);
    std::shared_ptr<HttpEngine> GetHttpEngine();

    namespace detail {
        struct FetchState {
            std::optional<HttpResponse> response;
            std::exception_ptr error;
            std::atomic<bool> abandoned{ false };
            std::shared_ptr<HttpCall> call = std::make_shared<HttpCall>();
        };
        void StartFetch(HttpRequest request, std::shared_ptr<FetchState> state, std::shared_ptr<rt::JobState> job, std::coroutine_handle<> h);
    }

    struct FetchAwaitable {
        HttpRequest request;
        std::shared_ptr<detail::FetchState> state = std::make_shared<detail::FetchState>();
        bool await_ready() const noexcept { return false; }
        template<typename P>
        void await_suspend(std::coroutine_handle<P> h) { detail::StartFetch(std::move(request), state, h.promise().job, h); }
        HttpResponse await_resume() {
            if (state->error) std::rethrow_exception(state->error);
            return std::move(*state->response);
        }
    };

    // co_await Fetch(request): the response, whatever its status; throws HttpError when there
    // is none.
    inline FetchAwaitable Fetch(HttpRequest request) { return { std::move(request) }; }
    // GET into a file (written as it arrives; removed when the request fails).
    inline FetchAwaitable Download(std::string url, std::string file, kiln::Function<void(std::int64_t, std::int64_t)> onProgress = nullptr) {
        HttpRequest r;
        r.url = std::move(url);
        r.responseFile = std::move(file);
        r.onProgress = std::move(onProgress);
        return { std::move(r) };
    }

    // The default network (ConnectivityManager's default network callback). An Android app needs
    // android.permission.ACCESS_NETWORK_STATE for it; without the permission, and on the
    // desktop, it reports a connected, validated network of unknown transport.
    struct NetworkState {
        enum class Transport { Unknown, Wifi, Cellular, Ethernet, Bluetooth, Vpn };
        bool connected = true;
        // The system found the network reaches the internet (not a captive portal).
        bool validated = true;
        bool metered = false;
        Transport transport = Transport::Unknown;
        bool operator==(const NetworkState&) const = default;
    };
    // The state now and each change: collect it, or read it in content with
    // rt::CollectAsState(net::Connectivity()).
    rt::MutableStateFlow<NetworkState>& Connectivity();

    // application/x-www-form-urlencoded: percent-encodes a value, and joins name=value pairs.
    std::string UrlEncode(std::string_view value);
    std::string FormBody(const std::vector<std::pair<std::string, std::string>>& fields);

}
