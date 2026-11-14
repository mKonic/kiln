// The desktop's HTTP engine: libcurl's easy interface, one handle per request.
#if !defined(__ANDROID__)

#include "kiln/net/Http.h"

#include <curl/curl.h>

#include <cstdio>
#include <mutex>

namespace kiln::net {

    namespace {
        struct Transfer {
            const HttpRequest* request = nullptr;
            HttpCall* call = nullptr;
            HttpResponse response;
            FILE* out = nullptr;
            FILE* in = nullptr;
            std::int64_t received = 0;
            bool writeFailed = false;
        };

        std::size_t OnBody(char* data, std::size_t size, std::size_t count, void* user) {
            auto* t = static_cast<Transfer*>(user);
            const std::size_t n = size * count;
            if (t->out) {
                if (std::fwrite(data, 1, n, t->out) != n) { t->writeFailed = true; return 0; }
            } else {
                t->response.body.append(data, n);
            }
            t->received += std::int64_t(n);
            return n;
        }
        // A header block per response: with redirects, the last one is the answer's.
        std::size_t OnHeader(char* data, std::size_t size, std::size_t count, void* user) {
            auto* t = static_cast<Transfer*>(user);
            const std::size_t n = size * count;
            std::string_view line(data, n);
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.remove_suffix(1);
            if (line.starts_with("HTTP/")) {
                t->response.headers = {};
                // "HTTP/1.1 404 Not Found"
                const auto a = line.find(' ');
                const auto b = a == std::string_view::npos ? a : line.find(' ', a + 1);
                t->response.statusText = b == std::string_view::npos ? std::string() : std::string(line.substr(b + 1));
            } else if (const auto colon = line.find(':'); colon != std::string_view::npos) {
                std::string_view value = line.substr(colon + 1);
                while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
                t->response.headers.Add(std::string(line.substr(0, colon)), std::string(value));
            }
            return n;
        }
        int OnProgress(void* user, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
            auto* t = static_cast<Transfer*>(user);
            if (t->call->Cancelled()) return 1;
            if (now > 0 && !t->call->Progress(std::int64_t(now), total > 0 ? std::int64_t(total) : -1)) return 1;
            return 0;
        }
        std::size_t OnRead(char* buffer, std::size_t size, std::size_t count, void* user) {
            auto* t = static_cast<Transfer*>(user);
            return std::fread(buffer, 1, size * count, t->in);
        }

        HttpError::Kind KindOf(CURLcode code) {
            switch (code) {
                case CURLE_URL_MALFORMAT:
                case CURLE_UNSUPPORTED_PROTOCOL: return HttpError::Kind::InvalidUrl;
                case CURLE_COULDNT_RESOLVE_HOST:
                case CURLE_COULDNT_RESOLVE_PROXY: return HttpError::Kind::UnknownHost;
                case CURLE_COULDNT_CONNECT: return HttpError::Kind::Connect;
                case CURLE_OPERATION_TIMEDOUT: return HttpError::Kind::Timeout;
                case CURLE_SSL_CONNECT_ERROR:
                case CURLE_PEER_FAILED_VERIFICATION:
                case CURLE_SSL_CERTPROBLEM:
                case CURLE_SSL_CIPHER:
                case CURLE_SSL_CACERT_BADFILE: return HttpError::Kind::Tls;
                case CURLE_TOO_MANY_REDIRECTS: return HttpError::Kind::TooManyRedirects;
                default: return HttpError::Kind::Io;
            }
        }

        class CurlEngine final : public HttpEngine {
        public:
            CurlEngine() {
                static std::once_flag once;
                std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
            }
            HttpResponse Execute(const HttpRequest& request, HttpCall& call) override {
                CURL* curl = curl_easy_init();
                if (!curl) throw HttpError(HttpError::Kind::Io, "curl: no handle");
                Transfer t;
                t.request = &request;
                t.call = &call;
                curl_slist* headers = nullptr;
                char error[CURL_ERROR_SIZE] = "";
                struct Cleanup {
                    CURL* curl; curl_slist*& headers; Transfer& t;
                    ~Cleanup() {
                        curl_easy_cleanup(curl);
                        curl_slist_free_all(headers);
                        if (t.out) std::fclose(t.out);
                        if (t.in) std::fclose(t.in);
                    }
                } cleanup{ curl, headers, t };

                if (!request.responseFile.empty()) {
                    t.out = std::fopen(request.responseFile.c_str(), "wb");
                    if (!t.out) throw HttpError(HttpError::Kind::Io, "cannot write " + request.responseFile);
                }
                curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
                curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
                curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
                curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
                curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
                curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");   // gzip, decoded, as the platform does
                curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, request.followRedirects ? 1L : 0L);
                curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 20L);
                curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, long(request.connectTimeoutMs));
                if (request.readTimeoutMs > 0) {
                    // Nothing received for the read timeout: under 1 byte/s for that long.
                    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
                    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, long((request.readTimeoutMs + 999) / 1000));
                }
                const bool hasBody = !request.body.empty() || !request.bodyFile.empty();
                if (request.method == "HEAD") {
                    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
                } else if (!request.bodyFile.empty()) {
                    t.in = std::fopen(request.bodyFile.c_str(), "rb");
                    if (!t.in) throw HttpError(HttpError::Kind::Io, "cannot read " + request.bodyFile);
                    std::fseek(t.in, 0, SEEK_END);
                    const long size = std::ftell(t.in);
                    std::fseek(t.in, 0, SEEK_SET);
                    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
                    curl_easy_setopt(curl, CURLOPT_READFUNCTION, &OnRead);
                    curl_easy_setopt(curl, CURLOPT_READDATA, &t);
                    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, curl_off_t(size));
                } else if (hasBody || request.method == "POST" || request.method == "PUT" || request.method == "PATCH") {
                    curl_easy_setopt(curl, CURLOPT_POST, 1L);
                    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.data());
                    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, curl_off_t(request.body.size()));
                }
                if (request.method != "GET" && request.method != "HEAD") curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.method.c_str());
                bool contentType = false;
                for (const auto& [name, value] : request.headers.Fields()) {
                    headers = curl_slist_append(headers, (name + ": " + value).c_str());
                    if (name.size() == 12 && curl_strequal(name.c_str(), "Content-Type")) contentType = true;
                }
                // A body without a type is sent as a form, as HttpURLConnection does. No
                // "Expect: 100-continue".
                if (hasBody && !contentType) headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
                headers = curl_slist_append(headers, "Expect:");
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &OnBody);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &t);
                curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, &OnHeader);
                curl_easy_setopt(curl, CURLOPT_HEADERDATA, &t);
                curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &OnProgress);
                curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &t);
                curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

                const CURLcode code = curl_easy_perform(curl);
                if (call.Cancelled()) throw HttpError(HttpError::Kind::Cancelled, "cancelled");
                if (code != CURLE_OK) {
                    if (t.writeFailed) throw HttpError(HttpError::Kind::Io, "cannot write " + request.responseFile);
                    throw HttpError(KindOf(code), error[0] ? error : curl_easy_strerror(code));
                }
                long status = 0;
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
                char* url = nullptr;
                curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &url);
                t.response.status = int(status);
                t.response.url = url ? url : request.url;
                // The body arrives decoded: these two no longer describe it.
                if (t.response.headers.Contains("Content-Encoding")) {
                    t.response.headers.Remove("Content-Encoding");
                    t.response.headers.Remove("Content-Length");
                }
                return std::move(t.response);
            }
        };
    }

    std::shared_ptr<HttpEngine> PlatformHttpEngine() { return std::make_shared<CurlEngine>(); }

}

#endif
