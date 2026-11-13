#pragma once

// navigation3-runtime deep links (deeplink/*.kt): matchers turn a DeepLinkRequest (a uri and/or
// extras) into a key for the back stack. Compose decodes a matched uri's arguments into a
// @Serializable key; kiln's UriDeepLinkMatcher takes a function from the arguments to the key
// instead (nullopt: the arguments do not make a key, so no match).
//
//   UriDeepLinkMatcher<Route> detail("https://example.com/detail/{id}", [](const DeepLinkArguments& a)
//       -> std::optional<Route> { return Detail{ std::stoi(a.at("id").front()) }; });
//   if (auto m = detail.Match(DeepLinkRequest("https://example.com/detail/7"))) backStack.push_back(m->key);
//
// When several matchers match, the greatest result (operator<) is the best: an exact path beats
// one with path arguments, then more path arguments, then any arguments, then more arguments.

#include "kiln/Uri.h"
#include <kiln/Function.h>

#include <any>
#include <cctype>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace kiln::navigation3 {

    // --- Requests -------------------------------------------------------------------------------

    // RequestExtrasKey<T>: a typed key, identified by the object (declare it once, inline/static).
    template<typename T>
    struct RequestExtrasKey {
        const char* name = "";
    };

    // RequestExtras: typed values a request carries besides its uri (mime type, action, ...).
    class RequestExtras {
    public:
        template<typename T> void Put(const RequestExtrasKey<T>& key, T value) { m_Map[&key] = std::any(std::move(value)); }
        template<typename T> const T* Get(const RequestExtrasKey<T>& key) const {
            auto it = m_Map.find(&key);
            return it == m_Map.end() ? nullptr : std::any_cast<T>(&it->second);
        }
        template<typename T> bool Contains(const RequestExtrasKey<T>& key) const { return m_Map.contains(&key); }
        std::size_t Size() const { return m_Map.size(); }
        bool Empty() const { return m_Map.empty(); }
        // plus: other's values win; minus: without other's keys.
        RequestExtras operator+(const RequestExtras& o) const {
            RequestExtras r = o;
            for (const auto& [k, v] : m_Map) r.m_Map.emplace(k, v);
            return r;
        }
        RequestExtras operator-(const RequestExtras& o) const {
            RequestExtras r = *this;
            for (const auto& [k, v] : o.m_Map) r.m_Map.erase(k);
            return r;
        }

    private:
        std::map<const void*, std::any> m_Map;
    };

    // requestExtras { put(...) }
    inline RequestExtras MakeRequestExtras(const kiln::Function<void(RequestExtras&)>& builder) {
        RequestExtras r;
        builder(r);
        return r;
    }

    class DeepLinkRequest {
    public:
        inline static const RequestExtrasKey<std::string> MimeTypeExtrasKey{ "mimeType" };
        // DeepLinkRequest.ActionExtrasKey (an intent's action).
        inline static const RequestExtrasKey<std::string> ActionExtrasKey{ "action" };

        // A uri, extras, or both (one is required).
        explicit DeepLinkRequest(std::optional<Uri> uri, RequestExtras extras = {}) : m_Uri(std::move(uri)), m_Extras(std::move(extras)) {
            if (!m_Uri && m_Extras.Empty()) throw std::invalid_argument("DeepLinkRequest must have either a uri or extras.");
        }
        explicit DeepLinkRequest(const std::string& uri, RequestExtras extras = {}) : DeepLinkRequest(std::optional<Uri>(Uri(uri)), std::move(extras)) {}
        explicit DeepLinkRequest(RequestExtras extras) : DeepLinkRequest(std::optional<Uri>(), std::move(extras)) {}

        const std::optional<Uri>& GetUri() const { return m_Uri; }
        const RequestExtras& Extras() const { return m_Extras; }

        static RequestExtras MimeTypeExtra(std::string mimeType) {
            RequestExtras r;
            r.Put(MimeTypeExtrasKey, std::move(mimeType));
            return r;
        }
        static RequestExtras ActionExtra(std::string action) {
            RequestExtras r;
            r.Put(ActionExtrasKey, std::move(action));
            return r;
        }

    private:
        std::optional<Uri> m_Uri;
        RequestExtras m_Extras;
    };

    // --- Matching -------------------------------------------------------------------------------

    // Arguments a uri gave, by placeholder name: path and fragment arguments have one value, a
    // query argument one per occurrence.
    using DeepLinkArguments = std::map<std::string, std::vector<std::string>>;

    // DeepLinkMatcher.MatchResult (with UriMatchResult's ordering data when a uri matched).
    template<typename T>
    struct DeepLinkMatch {
        T key;
        DeepLinkArguments arguments;
        bool fromUri = false;               // a UriMatchResult
        bool isExactPath = false;
        int matchingPathArgumentCount = -1;

        // compareTo: > 0 when this is the better match.
        int CompareTo(const DeepLinkMatch& o) const {
            if (!fromUri) return 0;
            if (!o.fromUri) return 1;
            if (isExactPath != o.isExactPath) return isExactPath ? 1 : -1;
            if (matchingPathArgumentCount != o.matchingPathArgumentCount) return matchingPathArgumentCount > o.matchingPathArgumentCount ? 1 : -1;
            if (arguments.empty() != o.arguments.empty()) return arguments.empty() ? -1 : 1;
            if (arguments.size() != o.arguments.size()) return arguments.size() > o.arguments.size() ? 1 : -1;
            return 0;
        }
        bool operator<(const DeepLinkMatch& o) const { return CompareTo(o) < 0; }
    };

    // DeepLinkMatcher.Filter: a request the matcher looks at (all filters must pass).
    using DeepLinkFilter = kiln::Function<bool(const DeepLinkRequest&)>;

    inline DeepLinkFilter MimeTypeFilter(std::string mimeType) {
        return [mimeType](const DeepLinkRequest& r) {
            const std::string* m = r.Extras().Get(DeepLinkRequest::MimeTypeExtrasKey);
            if (!m || m->size() != mimeType.size()) return false;
            for (std::size_t i = 0; i < m->size(); ++i)
                if (std::tolower((unsigned char)(*m)[i]) != std::tolower((unsigned char)mimeType[i])) return false;
            return true;
        };
    }
    inline DeepLinkFilter ActionFilter(std::string action) {
        return [action](const DeepLinkRequest& r) {
            const std::string* a = r.Extras().Get(DeepLinkRequest::ActionExtrasKey);
            return a && *a == action;
        };
    }

    template<typename T, typename R = DeepLinkMatch<T>>
    class DeepLinkMatcher {
    public:
        explicit DeepLinkMatcher(std::vector<DeepLinkFilter> filters = {}) : m_Filters(std::move(filters)) {}
        virtual ~DeepLinkMatcher() = default;
        std::optional<R> Match(const DeepLinkRequest& request) const {
            for (const auto& f : m_Filters)
                if (!f(request)) return std::nullopt;
            return MatchRequest(request);
        }

    protected:
        virtual std::optional<R> MatchRequest(const DeepLinkRequest& request) const = 0;

    private:
        std::vector<DeepLinkFilter> m_Filters;
    };

    // StaticKeyDeepLinkMatcher: `key` for every request the filters let through.
    template<typename T>
    class StaticKeyDeepLinkMatcher final : public DeepLinkMatcher<T> {
    public:
        StaticKeyDeepLinkMatcher(T key, std::vector<DeepLinkFilter> filters) : DeepLinkMatcher<T>(std::move(filters)), m_Key(std::move(key)) {}
        const T& Key() const { return m_Key; }

    protected:
        std::optional<DeepLinkMatch<T>> MatchRequest(const DeepLinkRequest&) const override { return DeepLinkMatch<T>{ m_Key }; }

    private:
        T m_Key;
    };

    namespace detail {
        // Regex.escape, as backslashes (std::regex has no \Q..\E).
        inline std::string RegexEscape(const std::string& s) {
            std::string out;
            for (char c : s) {
                if (std::string_view("\\^$.|?*+()[]{}/-").find(c) != std::string_view::npos) out += '\\';
                out += c;
            }
            return out;
        }
        // saveWildcardInRegex: an escaped ".*" in the pattern stays a wildcard.
        inline std::string SaveWildcard(std::string regex) {
            for (std::size_t at = 0; (at = regex.find("\\.\\*", at)) != std::string::npos;) { regex.replace(at, 4, ".*"); at += 2; }
            return regex;
        }
        inline bool IEquals(const std::string& a, const std::string& b) {
            if (a.size() != b.size()) return false;
            for (std::size_t i = 0; i < a.size(); ++i)
                if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
            return true;
        }
        inline const std::regex& FillIn() { static const std::regex r(R"(\{(.+?)\})"); return r; }
        inline constexpr const char* kPathRegex = "([^/]*?|)";
        inline constexpr const char* kPlaceholderContent = "([\\s\\S]+?)?";

        struct QueryParamPattern {
            std::string paramRegex;
            std::vector<std::string> arguments;
            bool isUnnamedParams = false;
        };
        struct ParsedPattern {
            std::string pathRegex;
            std::vector<std::string> pathArgs;
            std::map<std::string, QueryParamPattern> query;
            std::optional<std::pair<std::string, std::vector<std::string>>> fragment;
        };

        inline void Take(std::set<std::string>& taken, const std::string& name, const std::string& where, const std::string& pattern) {
            if (!taken.insert(name).second) throw std::invalid_argument("Duplicate placeholder name [" + name + "] found in " + where + " [" + pattern + "].");
        }

        // buildRegex: a segment's literals escaped, each {placeholder} a capture.
        inline void BuildRegex(const std::string& pattern, const std::string& segment, std::vector<std::string>& args, std::string& out,
                               std::set<std::string>& taken) {
            if (segment.find('{') == std::string::npos) { out += RegexEscape(segment); return; }
            std::size_t appendPos = 0;
            for (auto it = std::sregex_iterator(segment.begin(), segment.end(), FillIn()); it != std::sregex_iterator(); ++it) {
                const std::string name = (*it)[1].str();
                Take(taken, name, "uri pattern", pattern);
                args.push_back(name);
                const std::size_t first = std::size_t(it->position(0));
                if (first > appendPos) out += RegexEscape(segment.substr(appendPos, first - appendPos));
                out += kPathRegex;
                appendPos = first + std::size_t(it->length(0));
            }
            if (appendPos < segment.size()) out += RegexEscape(segment.substr(appendPos));
        }

        inline ParsedPattern Parse(const Uri& pattern) {
            ParsedPattern p;
            std::set<std::string> taken;
            const std::string text = pattern.ToString();
            // parsePath
            std::string re = "^";
            std::vector<std::string> segments;
            for (auto& s : pattern.PathSegments()) if (!s.empty()) segments.push_back(s);
            const auto scheme = pattern.Scheme();
            if (!scheme || IEquals(*scheme, "http")) re += "http[s]?://";
            else if (IEquals(*scheme, "https")) re += "https://";
            else re += RegexEscape(*scheme) + "://";
            if (auto authority = pattern.Authority()) {
                re += RegexEscape(*authority);
                if (!segments.empty()) re += "/";
            }
            for (std::size_t i = 0; i < segments.size(); ++i) {
                BuildRegex(text, segments[i], p.pathArgs, re, taken);
                if (i + 1 != segments.size()) re += "/";
            }
            const std::string beforeQuery = text.substr(0, text.find_first_of("?#"));
            if (!beforeQuery.empty() && beforeQuery.back() == '/') re += "/";
            re += "($|(\\?(.)*)|(#(.)*))";
            p.pathRegex = SaveWildcard(re);
            // parseQuery
            std::optional<std::string> unnamed;
            for (const auto& name : pattern.QueryParameterNames()) {
                const auto values = pattern.QueryParameters(name);
                if (values.size() > 1) throw std::invalid_argument("Query parameter [" + name + "] must only be present once in the pattern [" + text + "].");
                const std::string first = values.empty() ? std::string() : values.front();
                QueryParamPattern qp;
                const bool isArg = std::regex_match(name, FillIn());
                const bool isFlag = first.empty();
                if (isArg) {
                    if (unnamed) throw std::invalid_argument("DeepLink pattern [" + text + "] already has unnamed query parameter " + *unnamed + ". Cannot add unnamed query parameter " + name + ".");
                    unnamed = name;
                }
                std::string param = first;
                if (isArg || isFlag) { qp.isUnnamedParams = true; param = name; }
                std::string argRegex;
                std::size_t appendPos = 0;
                for (auto it = std::sregex_iterator(param.begin(), param.end(), FillIn()); it != std::sregex_iterator(); ++it) {
                    const std::size_t at = std::size_t(it->position(0));
                    argRegex += RegexEscape(param.substr(appendPos, at - appendPos));
                    argRegex += kPlaceholderContent;
                    qp.arguments.push_back((*it)[1].str());
                    appendPos = at + std::size_t(it->length(0));
                }
                argRegex += RegexEscape(param.substr(appendPos));
                argRegex += "$";
                qp.paramRegex = SaveWildcard(argRegex);
                if (qp.arguments.empty()) {
                    if (const auto w = qp.paramRegex.find(".*"); w != std::string::npos) {
                        qp.paramRegex.replace(w, 2, "(.*)");
                        qp.arguments.push_back(name);
                    } else if (isFlag) {
                        std::string body = qp.paramRegex;
                        if (!body.empty() && body.back() == '$') body.pop_back();
                        qp.paramRegex = "(" + body + ")$";
                        qp.arguments.push_back(name);
                    }
                }
                for (const auto& a : qp.arguments) Take(taken, a, "query of", text);
                p.query[name] = std::move(qp);
            }
            // parseFragment
            if (auto fragment = pattern.Fragment()) {
                std::string fre;
                std::vector<std::string> args;
                BuildRegex(text, *fragment, args, fre, taken);
                p.fragment = std::pair{ SaveWildcard(fre), std::move(args) };
            }
            return p;
        }
    }

    // UriDeepLinkMatcher: matches uris against a pattern ("https://example.com/item/{id}?tab={tab}");
    // `decode` makes the key from the arguments. A pattern without a scheme matches http and
    // https; ".*" in it is a wildcard.
    template<typename T>
    class UriDeepLinkMatcher : public DeepLinkMatcher<T> {
    public:
        using Decode = kiln::Function<std::optional<T>(const DeepLinkArguments&)>;
        UriDeepLinkMatcher(const std::string& uriPattern, Decode decode, std::vector<DeepLinkFilter> filters = {})
            : DeepLinkMatcher<T>(std::move(filters)), m_Pattern(Normalized(uriPattern)), m_Decode(std::move(decode)) {}

    protected:
        std::optional<DeepLinkMatch<T>> MatchRequest(const DeepLinkRequest& request) const override {
            return request.GetUri() ? MatchUri(*request.GetUri()) : std::nullopt;
        }

        virtual std::optional<DeepLinkMatch<T>> MatchUri(const Uri& uri) const {
            const detail::ParsedPattern& p = Parsed();
            const std::string& re = p.pathRegex;
            const std::regex schemeRegex(re.substr(1, re.find("://") - 1), std::regex::icase);
            if (!std::regex_match(uri.Scheme().value_or(""), schemeRegex)) return std::nullopt;
            const auto a = uri.Authority(), b = m_Pattern.Authority();
            if (a.has_value() != b.has_value() || (a && !detail::IEquals(*a, *b))) return std::nullopt;
            // extractPathArgs
            std::smatch m;
            const std::string text = uri.ToString();
            if (!std::regex_match(text, m, std::regex(re, std::regex::icase))) return std::nullopt;
            DeepLinkArguments pathArgs;
            for (std::size_t i = 0; i < p.pathArgs.size(); ++i)
                pathArgs[p.pathArgs[i]] = { m[i + 1].matched ? Uri::Decode(m[i + 1].str()) : std::string() };
            // extractQueryArgs
            DeepLinkArguments queryArgs;
            std::vector<std::string> splitInput;
            if (auto q = uri.Query()) {
                std::size_t start = 0;
                while (true) {
                    const auto end = q->find('&', start);
                    splitInput.push_back(q->substr(start, end == std::string::npos ? std::string::npos : end - start));
                    if (end == std::string::npos) break;
                    start = end + 1;
                }
            }
            std::set<std::string> named;
            for (const auto& [name, qp] : p.query) if (!qp.isUnnamedParams) named.insert(name);
            for (const auto& [name, qp] : p.query) {
                std::vector<std::string> inputs = uri.QueryParameters(name);
                if (qp.isUnnamedParams) {
                    inputs.clear();
                    for (const auto& part : splitInput)
                        if (part.find('=') == std::string::npos && !named.contains(part)) inputs.push_back(part);
                }
                const std::regex paramRegex(qp.paramRegex);
                for (const auto& input : inputs) {
                    std::smatch qm;
                    if (!std::regex_match(input, qm, paramRegex)) continue;
                    for (std::size_t i = 0; i < qp.arguments.size(); ++i)
                        queryArgs[qp.arguments[i]].push_back(qm[i + 1].matched ? qm[i + 1].str() : std::string());
                }
            }
            // extractFragmentArgs
            DeepLinkArguments fragmentArgs;
            if (auto fragment = uri.Fragment(); fragment && p.fragment) {
                std::smatch fm;
                if (std::regex_match(*fragment, fm, std::regex(p.fragment->first, std::regex::icase)))
                    for (std::size_t i = 0; i < p.fragment->second.size(); ++i)
                        fragmentArgs[p.fragment->second[i]] = { fm[i + 1].matched ? Uri::Decode(fm[i + 1].str()) : std::string() };
            }
            return MatchArguments(pathArgs, queryArgs, fragmentArgs);
        }

        virtual std::optional<DeepLinkMatch<T>> MatchArguments(const DeepLinkArguments& pathArgs, const DeepLinkArguments& queryArgs,
                                                               const DeepLinkArguments& fragmentArgs) const {
            // Path arguments win over query ones, which win over fragment ones.
            DeepLinkArguments arguments = pathArgs;
            for (const auto& [k, v] : queryArgs) arguments.emplace(k, v);
            for (const auto& [k, v] : fragmentArgs) arguments.emplace(k, v);
            std::optional<T> key = m_Decode(arguments);
            if (!key) return std::nullopt;
            bool wildcard = false;
            for (const auto& s : m_Pattern.PathSegments()) wildcard |= s == ".*";
            DeepLinkMatch<T> r{ std::move(*key), std::move(arguments) };
            r.fromUri = true;
            r.isExactPath = pathArgs.empty() && !wildcard;
            r.matchingPathArgumentCount = int(pathArgs.size());
            return r;
        }

    private:
        // A pattern without a scheme or "//" is a host and path: "//" goes in front.
        static Uri Normalized(const std::string& pattern) {
            static const std::regex scheme("^[a-zA-Z][a-zA-Z0-9+\\-.]*:");
            if (std::regex_search(pattern, scheme) || pattern.starts_with("//")) return Uri(pattern);
            return Uri("//" + pattern);
        }
        const detail::ParsedPattern& Parsed() const {
            if (!m_Parsed) m_Parsed = std::make_shared<detail::ParsedPattern>(detail::Parse(m_Pattern));
            return *m_Parsed;
        }

        Uri m_Pattern;
        Decode m_Decode;
        mutable std::shared_ptr<detail::ParsedPattern> m_Parsed;   // parsed on first use
    };

    // BackStackMatchResult: a match and the back stack it opens (built on first use).
    template<typename T, typename K>
    struct BackStackMatch {
        DeepLinkMatch<T> match;
        const std::vector<K>& BackStack() const {
            if (!m_BackStack) m_BackStack = std::make_shared<std::vector<K>>(m_Builder(match));
            return *m_BackStack;
        }
        int CompareTo(const BackStackMatch& o) const { return match.CompareTo(o.match); }
        bool operator<(const BackStackMatch& o) const { return CompareTo(o) < 0; }

        kiln::Function<std::vector<K>(const DeepLinkMatch<T>&)> m_Builder;
        mutable std::shared_ptr<std::vector<K>> m_BackStack;
    };

    // BackStackMatcher / withBackStack: `matcher`'s matches, each with the back stack to show.
    template<typename T, typename K>
    class BackStackMatcher final : public DeepLinkMatcher<T, BackStackMatch<T, K>> {
    public:
        using Builder = kiln::Function<std::vector<K>(const DeepLinkMatch<T>&)>;
        BackStackMatcher(std::shared_ptr<const DeepLinkMatcher<T>> matcher, Builder builder) : m_Matcher(std::move(matcher)), m_Builder(std::move(builder)) {}

    protected:
        std::optional<BackStackMatch<T, K>> MatchRequest(const DeepLinkRequest& request) const override {
            auto m = m_Matcher->Match(request);
            if (!m) return std::nullopt;
            return BackStackMatch<T, K>{ std::move(*m), m_Builder, nullptr };
        }

    private:
        std::shared_ptr<const DeepLinkMatcher<T>> m_Matcher;
        Builder m_Builder;
    };
    template<typename K, typename T>
    std::shared_ptr<BackStackMatcher<T, K>> WithBackStack(std::shared_ptr<const DeepLinkMatcher<T>> matcher, typename BackStackMatcher<T, K>::Builder builder) {
        return std::make_shared<BackStackMatcher<T, K>>(std::move(matcher), std::move(builder));
    }

}
