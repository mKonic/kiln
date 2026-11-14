// net::Fetch against a server on the loopback interface (the desktop engine), and against a
// test engine for what a real server cannot be made to do.
#include "Check.h"

#include "kiln/net/Http.h"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace kiln;
using namespace kiln::net;

namespace {
    // One request per connection; answers by path.
    class Server {
    public:
        Server() {
            m_Listen = ::socket(AF_INET, SOCK_STREAM, 0);
            int one = 1;
            ::setsockopt(m_Listen, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            ::bind(m_Listen, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
            socklen_t len = sizeof addr;
            ::getsockname(m_Listen, reinterpret_cast<sockaddr*>(&addr), &len);
            m_Port = ntohs(addr.sin_port);
            ::listen(m_Listen, 16);
            m_Thread = std::thread([this] { Run(); });
        }
        ~Server() {
            m_Stop = true;
            ::shutdown(m_Listen, SHUT_RDWR);
            ::close(m_Listen);
            m_Thread.join();
        }
        std::string Url(const std::string& path) const { return "http://127.0.0.1:" + std::to_string(m_Port) + path; }
        std::atomic<int> slowAborted{ 0 };

    private:
        void Run() {
            while (!m_Stop) {
                const int fd = ::accept(m_Listen, nullptr, nullptr);
                if (fd < 0) break;
                Serve(fd);
                ::close(fd);
            }
        }
        static void Send(int fd, const std::string& s) {
            std::size_t done = 0;
            while (done < s.size()) {
                const ssize_t n = ::send(fd, s.data() + done, s.size() - done, MSG_NOSIGNAL);
                if (n <= 0) return;
                done += std::size_t(n);
            }
        }
        static std::string Reply(int status, const std::string& text, const std::string& body, const std::string& extra = "") {
            return "HTTP/1.1 " + std::to_string(status) + " " + text + "\r\nContent-Length: " + std::to_string(body.size()) +
                   "\r\nConnection: close\r\n" + extra + "\r\n" + body;
        }
        void Serve(int fd) {
            std::string in;
            char buf[4096];
            std::size_t headEnd = std::string::npos;
            while (headEnd == std::string::npos) {
                const ssize_t n = ::recv(fd, buf, sizeof buf, 0);
                if (n <= 0) return;
                in.append(buf, std::size_t(n));
                headEnd = in.find("\r\n\r\n");
            }
            const std::string head = in.substr(0, headEnd);
            std::string body = in.substr(headEnd + 4);
            std::size_t length = 0;
            if (const auto at = head.find("Content-Length: "); at != std::string::npos) length = std::stoul(head.substr(at + 16));
            while (body.size() < length) {
                const ssize_t n = ::recv(fd, buf, sizeof buf, 0);
                if (n <= 0) return;
                body.append(buf, std::size_t(n));
            }
            const std::string method = head.substr(0, head.find(' '));
            const std::string path = head.substr(method.size() + 1, head.find(' ', method.size() + 1) - method.size() - 1);
            if (path == "/hello") Send(fd, Reply(200, "OK", "hello", "Content-Type: text/plain\r\nX-Twice: a\r\nx-twice: b\r\n"));
            else if (path == "/echo") {
                std::string token;
                if (const auto at = head.find("X-Token: "); at != std::string::npos) token = head.substr(at + 9, head.find("\r\n", at) - at - 9);
                std::string type;
                if (const auto at = head.find("Content-Type: "); at != std::string::npos) type = head.substr(at + 14, head.find("\r\n", at) - at - 14);
                Send(fd, Reply(200, "OK", method + "|" + token + "|" + type + "|" + body));
            }
            else if (path == "/missing") Send(fd, Reply(404, "Not Found", "nothing here"));
            else if (path == "/redirect") Send(fd, Reply(302, "Found", "", "Location: /hello\r\n"));
            else if (path == "/loop") Send(fd, Reply(302, "Found", "", "Location: /loop\r\n"));
            else if (path == "/big") {
                const std::string big(300000, 'x');
                Send(fd, Reply(200, "OK", big));
            }
            else if (path == "/slow") {
                // Headers, then nothing until the client goes away.
                Send(fd, "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\n");
                for (int i = 0; i < 300 && !m_Stop; ++i) {
                    char c;
                    if (::recv(fd, &c, 1, MSG_DONTWAIT) == 0) { ++slowAborted; break; }
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
            else Send(fd, Reply(500, "Server Error", ""));
        }
        int m_Listen = -1;
        int m_Port = 0;
        std::atomic<bool> m_Stop{ false };
        std::thread m_Thread;
    };

    struct Outcome {
        std::optional<HttpResponse> response;
        std::optional<HttpError::Kind> error;
        std::string message;
    };
    rt::Task<> Run(HttpRequest request, std::shared_ptr<Outcome> out) {
        try {
            out->response = co_await Fetch(std::move(request));
        } catch (const HttpError& e) {
            out->error = e.kind;
            out->message = e.what();
        }
    }
    void Pump(const rt::Job& job, int millis = 5000) {
        for (int i = 0; i < millis && !job.Done(); ++i) {
            rt::Dispatcher::Main().RunPending();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    Outcome Do(HttpRequest request) {
        auto out = std::make_shared<Outcome>();
        rt::Job job = rt::Launch(rt::Dispatcher::Main(), Run(std::move(request), out));
        Pump(job);
        CHECK(job.Done());
        return *out;
    }
}

TEST(HttpHeadersCompareNamesWithoutCase) {
    Headers h{ { "Content-Type", "text/plain" }, { "X-A", "1" } };
    h.Add("x-a", "2");
    CHECK(h.Get("content-type") == std::optional<std::string>("text/plain"));
    CHECK(h.All("X-A") == (std::vector<std::string>{ "1", "2" }));
    h.Set("X-a", "3");
    CHECK(h.All("x-a") == std::vector<std::string>{ "3" } && !h.Contains("missing"));
    CHECK(FormBody({ { "q", "a b&c" }, { "n", "é" } }) == "q=a+b%26c&n=%C3%A9");
}

// Against the loopback server: the desktop engine (libcurl). On Android the engine is the
// platform's, which only an app process has.
#if !defined(__ANDROID__)
TEST(HttpFetchGetsAResponse) {
    Server server;
    Outcome o = Do({ .url = server.Url("/hello") });
    CHECK(o.response && o.response->status == 200 && o.response->Ok());
    if (!o.response) return;
    CHECK(o.response->body == "hello" && o.response->statusText == "OK");
    CHECK(o.response->headers.Get("content-type") == std::optional<std::string>("text/plain"));
    CHECK(o.response->headers.All("X-Twice") == (std::vector<std::string>{ "a", "b" }));
    CHECK(o.response->url == server.Url("/hello"));
}

TEST(HttpFetchSendsMethodHeadersAndBody) {
    Server server;
    Outcome post = Do({ .url = server.Url("/echo"), .method = "POST", .headers = { { "X-Token", "abc" }, { "Content-Type", "application/json" } }, .body = "{\"a\":1}" });
    CHECK(post.response && post.response->body == "POST|abc|application/json|{\"a\":1}");
    Outcome put = Do({ .url = server.Url("/echo"), .method = "PUT", .body = "data" });
    CHECK(put.response && put.response->body == "PUT||application/x-www-form-urlencoded|data");
    Outcome del = Do({ .url = server.Url("/echo"), .method = "DELETE" });
    CHECK(del.response && del.response->body == "DELETE|||");
    Outcome patch = Do({ .url = server.Url("/echo"), .method = "PATCH", .body = "p" });
    CHECK(patch.response && patch.response->body == "PATCH||application/x-www-form-urlencoded|p");
    // A body from a file.
    const std::string file = "/tmp/kiln-http-upload-" + std::to_string(::getpid());
    if (FILE* f = std::fopen(file.c_str(), "wb")) { std::fputs("from a file", f); std::fclose(f); }
    Outcome upload = Do({ .url = server.Url("/echo"), .method = "PUT", .bodyFile = file });
    CHECK(upload.response && upload.response->body == "PUT||application/x-www-form-urlencoded|from a file");
    std::remove(file.c_str());
}

TEST(HttpStatusesAreResponsesAndRedirectsAreFollowed) {
    Server server;
    Outcome missing = Do({ .url = server.Url("/missing") });
    CHECK(missing.response && missing.response->status == 404 && !missing.response->Ok() && missing.response->body == "nothing here");
    CHECK(missing.response && missing.response->statusText == "Not Found");
    Outcome followed = Do({ .url = server.Url("/redirect") });
    CHECK(followed.response && followed.response->status == 200 && followed.response->body == "hello" && followed.response->url == server.Url("/hello"));
    CHECK(followed.response && !followed.response->headers.Contains("Location"));
    Outcome kept = Do({ .url = server.Url("/redirect"), .followRedirects = false });
    CHECK(kept.response && kept.response->status == 302 && kept.response->headers.Get("location") == std::optional<std::string>("/hello"));
    Outcome loop = Do({ .url = server.Url("/loop") });
    CHECK(loop.error == std::optional(HttpError::Kind::TooManyRedirects));
}

TEST(HttpFailuresThrowByKind) {
    Outcome refused = Do({ .url = "http://127.0.0.1:1/" });
    CHECK(refused.error == std::optional(HttpError::Kind::Connect) && !refused.message.empty());
    Outcome bad = Do({ .url = "notaurl://x" });
    CHECK(bad.error == std::optional(HttpError::Kind::InvalidUrl));
    Outcome unknown = Do({ .url = "http://no-such-host.invalid/" });
    CHECK(unknown.error == std::optional(HttpError::Kind::UnknownHost));
    Server server;
    Outcome slow = Do({ .url = server.Url("/slow"), .readTimeoutMs = 1000 });
    CHECK(slow.error == std::optional(HttpError::Kind::Timeout));
}

TEST(HttpDownloadWritesAFileAndReportsProgress) {
    Server server;
    const std::string file = "/tmp/kiln-http-download-" + std::to_string(::getpid());
    auto out = std::make_shared<Outcome>();
    std::int64_t last = 0, total = 0;
    int calls = 0;
    rt::Job job = rt::Launch(rt::Dispatcher::Main(), [](std::string url, std::string file, std::shared_ptr<Outcome> out, std::int64_t& last, std::int64_t& total, int& calls) -> rt::Task<> {
        out->response = co_await Download(url, file, [&](std::int64_t received, std::int64_t of) { last = received; total = of; ++calls; });
    }(server.Url("/big"), file, out, last, total, calls));
    Pump(job);
    rt::Dispatcher::Main().RunPending();
    CHECK(out->response && out->response->status == 200 && out->response->body.empty());
    CHECK(calls > 0 && last == 300000 && total == 300000);
    std::string read;
    if (FILE* f = std::fopen(file.c_str(), "rb")) { char b[65536]; std::size_t n; while ((n = std::fread(b, 1, sizeof b, f)) > 0) read.append(b, n); std::fclose(f); }
    CHECK(read.size() == 300000 && read.find_first_not_of('x') == std::string::npos);
    std::remove(file.c_str());
    // A failed download leaves no file.
    Outcome failed = Do({ .url = "http://127.0.0.1:1/", .responseFile = file });
    CHECK(failed.error && ::access(file.c_str(), F_OK) != 0);
}

TEST(HttpCancellingTheTaskAbortsTheTransfer) {
    Server server;
    auto out = std::make_shared<Outcome>();
    {
        rt::Job job = rt::Launch(rt::Dispatcher::Main(), Run({ .url = server.Url("/slow"), .readTimeoutMs = 0 }, out));
        for (int i = 0; i < 100; ++i) {
            rt::Dispatcher::Main().RunPending();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        CHECK(!job.Done());
    }   // the job is cancelled here
    for (int i = 0; i < 3000 && server.slowAborted == 0; ++i) {
        rt::Dispatcher::Main().RunPending();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(server.slowAborted == 1);            // the connection was closed, not left to finish
    CHECK(!out->response && !out->error);      // and the cancelled task did not resume
}
#endif

TEST(HttpEngineCanBeReplaced) {
    struct Fake final : HttpEngine {
        HttpResponse Execute(const HttpRequest& request, HttpCall&) override {
            if (request.url == "fail") throw HttpError(HttpError::Kind::Tls, "bad certificate");
            HttpResponse r;
            r.status = 201;
            r.body = request.method + " " + request.url;
            return r;
        }
    };
    auto real = GetHttpEngine();
    SetHttpEngine(std::make_shared<Fake>());
    Outcome ok = Do({ .url = "x", .method = "POST" });
    CHECK(ok.response && ok.response->status == 201 && ok.response->body == "POST x");
    Outcome fail = Do({ .url = "fail" });
    CHECK(fail.error == std::optional(HttpError::Kind::Tls) && fail.message == "bad certificate");
    SetHttpEngine(real);
}

TEST(ConnectivityIsAStateFlow) {
    auto& c = Connectivity();
    const NetworkState before = c.Value();
    CHECK(before.connected && before.validated);
    std::vector<NetworkState> seen;
    rt::Job job = rt::Launch(rt::Dispatcher::Main(), c.Collect(kiln::Function<void(const NetworkState&)>([&](const NetworkState& s) { seen.push_back(s); })));
    c.SetValue({ .connected = false, .validated = false });
    rt::Dispatcher::Main().RunPending();
    c.SetValue({ .connected = true, .validated = true, .metered = true, .transport = NetworkState::Transport::Cellular });
    rt::Dispatcher::Main().RunPending();
    CHECK(seen.size() == 3 && !seen[1].connected && seen[2].metered && seen[2].transport == NetworkState::Transport::Cellular);
    c.SetValue(before);
}
