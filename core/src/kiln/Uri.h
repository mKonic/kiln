#pragma once

// A URI, scheme://authority/path?query#fragment, read as android.net.Uri reads one (the parts
// deep links match on). Path segments and query parameters come decoded.

#include <optional>
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
        bool operator==(const Uri&) const = default;

    private:
        std::string m_Uri;
    };

}
