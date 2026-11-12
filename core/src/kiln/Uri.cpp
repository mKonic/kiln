#include "kiln/Uri.h"

#include <algorithm>
#include <cctype>

namespace kiln {

    Uri::Uri(std::string uri) : m_Uri(std::move(uri)) {}

    std::string Uri::Decode(const std::string& s) {
        std::string out;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '%' && i + 2 < s.size() && std::isxdigit((unsigned char)s[i + 1]) && std::isxdigit((unsigned char)s[i + 2])) {
                out += char(std::stoi(s.substr(i + 1, 2), nullptr, 16));
                i += 2;
            } else {
                out += s[i];
            }
        }
        return out;
    }

    std::optional<std::string> Uri::Query() const {
        const auto q = m_Uri.find('?');
        if (q == std::string::npos) return std::nullopt;
        const auto f = m_Uri.find('#', q);
        return m_Uri.substr(q + 1, f == std::string::npos ? std::string::npos : f - q - 1);
    }

    std::optional<std::string> Uri::Fragment() const {
        const auto f = m_Uri.find('#');
        if (f == std::string::npos) return std::nullopt;
        return m_Uri.substr(f + 1);
    }

    std::vector<std::string> Uri::PathSegments() const {
        // The path: after the scheme and authority, before the query and fragment.
        std::size_t at = 0;
        if (auto scheme = Scheme()) at = scheme->size() + 1;
        if (m_Uri.compare(at, 2, "//") == 0) {
            at = m_Uri.find_first_of("/?#", at + 2);
            if (at == std::string::npos) at = m_Uri.size();
        }
        std::string rest = m_Uri.substr(at);
        rest = rest.substr(0, rest.find_first_of("?#"));
        std::vector<std::string> out;
        std::size_t start = 0;
        while (start <= rest.size()) {
            const auto end = rest.find('/', start);
            const std::string seg = rest.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!seg.empty()) out.push_back(Decode(seg));
            if (end == std::string::npos) break;
            start = end + 1;
        }
        return out;
    }

    namespace {
        std::vector<std::pair<std::string, std::string>> QueryPairs(const std::optional<std::string>& q) {
            std::vector<std::pair<std::string, std::string>> out;
            if (!q || q->empty()) return out;
            std::size_t start = 0;
            while (start <= q->size()) {
                const auto end = q->find('&', start);
                const std::string part = q->substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (!part.empty()) {
                    const auto eq = part.find('=');
                    auto decode = [](std::string s) { std::replace(s.begin(), s.end(), '+', ' '); return Uri::Decode(s); };
                    if (eq == std::string::npos) out.emplace_back(decode(part), std::string());
                    else out.emplace_back(decode(part.substr(0, eq)), decode(part.substr(eq + 1)));
                }
                if (end == std::string::npos) break;
                start = end + 1;
            }
            return out;
        }
    }

    std::vector<std::string> Uri::QueryParameterNames() const {
        std::vector<std::string> out;
        for (const auto& [k, v] : QueryPairs(Query()))
            if (std::find(out.begin(), out.end(), k) == out.end()) out.push_back(k);
        return out;
    }

    std::vector<std::string> Uri::QueryParameters(const std::string& name) const {
        std::vector<std::string> out;
        for (const auto& [k, v] : QueryPairs(Query()))
            if (k == name) out.push_back(v);
        return out;
    }

    std::optional<std::string> Uri::Scheme() const {
        // scheme ":" ... (RFC 3986): letters, digits, + - . before the first ':', no '/', '?' or '#' first.
        const auto colon = m_Uri.find(':');
        if (colon == std::string::npos || colon == 0) return std::nullopt;
        if (m_Uri.find_first_of("/?#") < colon) return std::nullopt;
        return m_Uri.substr(0, colon);
    }

    std::optional<std::string> Uri::Authority() const {
        // "//" authority, after the scheme (or at the start: a network-path reference).
        std::size_t at = 0;
        if (auto scheme = Scheme()) at = scheme->size() + 1;
        if (m_Uri.compare(at, 2, "//") != 0) return std::nullopt;
        at += 2;
        const auto end = m_Uri.find_first_of("/?#", at);
        return m_Uri.substr(at, end == std::string::npos ? std::string::npos : end - at);
    }

}
