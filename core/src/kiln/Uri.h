#pragma once

// A URI, scheme://authority/path?query#fragment, read as android.net.Uri reads one (the parts
// deep links match on). Path segments and query parameters come decoded.

#include <cstdint>
#include <optional>
#include <string_view>
#include <string>
#include <vector>

namespace kiln {

    class Uri {
    public:
        explicit Uri(std::string uri);
        const std::string& ToString() const { return m_Uri; }
        std::optional<std::string> Scheme() const;
        std::optional<std::string> Authority() const;
        std::vector<std::string> PathSegments() const;
        std::vector<std::string> QueryParameterNames() const;
        std::vector<std::string> QueryParameters(const std::string& name) const;
        std::optional<std::string> Query() const;
        std::optional<std::string> Fragment() const;
        // Percent-decoding.
        static std::string Decode(const std::string& s);
        // Uri.encode: UTF-8 bytes other than letters, digits, _-!.~'()* and those in `allow`,
        // percent-encoded (uppercase hex).
        static std::string Encode(const std::string& s, const std::string& allow = {});
        bool operator==(const Uri&) const = default;

    private:
        std::string m_Uri;
    };

    // A data: URI's bytes (data:[<mediatype>][;base64],<data>, as Coil's DataUriFetcher reads
    // it), nullopt when malformed.
    std::optional<std::vector<std::uint8_t>> DecodeDataUri(std::string_view uri);

}
