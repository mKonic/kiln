#include "kiln/navigation/NavDestination.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <set>
#include <stdexcept>

namespace kiln::navigation {

    // --- NavType --------------------------------------------------------------------------------

    const char* NavTypeName(NavType t) {
        switch (t) {
            case NavType::String: return "string";
            case NavType::Int: return "integer";
            case NavType::Long: return "long";
            case NavType::Float: return "float";
            case NavType::Bool: return "boolean";
        }
        return "string";
    }

    NavValue ParseNavValue(NavType type, const std::string& value) {
        auto fail = [&] { throw std::invalid_argument(std::string("cannot parse \"") + value + "\" as " + NavTypeName(type)); };
        try {
            switch (type) {
                case NavType::String:
                    if (value == "null") return std::monostate{};
                    return value;
                case NavType::Int: {
                    std::size_t used = 0;
                    const bool hex = value.starts_with("0x");
                    const long long v = std::stoll(hex ? value.substr(2) : value, &used, hex ? 16 : 10);
                    if (used != (hex ? value.size() - 2 : value.size()) || v < INT32_MIN || v > INT32_MAX) fail();
                    return std::int32_t(v);
                }
                case NavType::Long: {
                    // The L suffix is optional at runtime.
                    std::string local = value.ends_with("L") ? value.substr(0, value.size() - 1) : value;
                    const bool hex = value.starts_with("0x");
                    if (hex) local = local.substr(2);
                    std::size_t used = 0;
                    const long long v = std::stoll(local, &used, hex ? 16 : 10);
                    if (used != local.size()) fail();
                    return std::int64_t(v);
                }
                case NavType::Float: {
                    std::size_t used = 0;
                    const float v = std::stof(value, &used);
                    if (used != value.size()) fail();
                    return v;
                }
                case NavType::Bool:
                    if (value == "true") return true;
                    if (value == "false") return false;
                    throw std::invalid_argument("A boolean NavType only accepts \"true\" or \"false\" values.");
            }
        } catch (const std::out_of_range&) {
            fail();
        } catch (const std::invalid_argument&) {
            throw;
        }
        fail();
        return {};
    }

    bool NavValueHasType(NavType type, const NavValue& v) {
        switch (type) {
            case NavType::String: return std::holds_alternative<std::string>(v);
            case NavType::Int: return std::holds_alternative<std::int32_t>(v);
            case NavType::Long: return std::holds_alternative<std::int64_t>(v);
            case NavType::Float: return std::holds_alternative<float>(v);
            case NavType::Bool: return std::holds_alternative<bool>(v);
        }
        return false;
    }

    NamedNavArgument NavArgumentOf(std::string name, NavType type, bool nullable, std::optional<NavValue> defaultValue) {
        if (nullable && type != NavType::String) throw std::invalid_argument(std::string(NavTypeName(type)) + " does not allow nullable values");
        if (!nullable && defaultValue && std::holds_alternative<std::monostate>(*defaultValue))
            throw std::invalid_argument(std::string("Argument with type ") + NavTypeName(type) + " has null value but is not nullable.");
        return { std::move(name), NavArgument{ type, nullable, std::move(defaultValue) } };
    }

    namespace {
        std::vector<std::string> MissingRequiredArguments(const std::map<std::string, NavArgument>& arguments,
                                                          const std::function<bool(const std::string&)>& isMissing) {
            std::vector<std::string> out;
            for (const auto& [k, a] : arguments)
                if (!a.isNullable && !a.IsDefaultValuePresent() && isMissing(k)) out.push_back(k);
            return out;
        }

        std::string RegexEscape(const std::string& s) {
            static const std::string special = R"(\^$.|?*+()[]{}/)";
            std::string out;
            for (char ch : s) {
                if (special.find(ch) != std::string::npos) out += '\\';
                out += ch;
            }
            return out;
        }

        // saveWildcardInRegex: a literal ".*" in the pattern stays a wildcard.
        std::string SaveWildcard(std::string r) {
            for (std::size_t p; (p = r.find("\\.\\*")) != std::string::npos;) r.replace(p, 4, ".*");
            return r;
        }

        const char* const kPathRegex = "([^/]*?|)";
        const char* const kAnySymbolsInTheTail = "([\\s\\S]+?)?";

        // buildRegex: literal text escaped, each {arg} a capturing group.
        void BuildRegex(const std::string& uri, std::vector<std::string>& args, std::string& out) {
            static const std::regex fillIn(R"(\{(.+?)\})");
            std::size_t appendPos = 0;
            for (auto it = std::sregex_iterator(uri.begin(), uri.end(), fillIn); it != std::sregex_iterator(); ++it) {
                args.push_back((*it)[1].str());
                const std::size_t start = std::size_t(it->position());
                if (start > appendPos) out += RegexEscape(uri.substr(appendPos, start - appendPos));
                out += kPathRegex;
                appendPos = start + std::size_t(it->length());
            }
            if (appendPos < uri.size()) out += RegexEscape(uri.substr(appendPos));
        }

        void ParseArgument(NavArgs& out, const std::string& name, const std::string& value, const NavArgument* argument) {
            if (argument) out[name] = ParseNavValue(argument->type, value);
            else out[name] = value;
        }
    }

    // --- NavUri ---------------------------------------------------------------------------------

    NavUri::NavUri(std::string uri) : m_Uri(std::move(uri)) {}

    std::string NavUri::Decode(const std::string& s) {
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

    std::optional<std::string> NavUri::Query() const {
        const auto q = m_Uri.find('?');
        if (q == std::string::npos) return std::nullopt;
        const auto f = m_Uri.find('#', q);
        return m_Uri.substr(q + 1, f == std::string::npos ? std::string::npos : f - q - 1);
    }

    std::optional<std::string> NavUri::Fragment() const {
        const auto f = m_Uri.find('#');
        if (f == std::string::npos) return std::nullopt;
        return m_Uri.substr(f + 1);
    }

    std::vector<std::string> NavUri::PathSegments() const {
        std::string rest = m_Uri;
        if (const auto s = rest.find("://"); s != std::string::npos) {
            rest = rest.substr(s + 3);
            const auto slash = rest.find('/');
            rest = slash == std::string::npos ? std::string() : rest.substr(slash);
        }
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
                    auto decode = [](std::string s) { std::replace(s.begin(), s.end(), '+', ' '); return NavUri::Decode(s); };
                    if (eq == std::string::npos) out.emplace_back(decode(part), std::string());
                    else out.emplace_back(decode(part.substr(0, eq)), decode(part.substr(eq + 1)));
                }
                if (end == std::string::npos) break;
                start = end + 1;
            }
            return out;
        }
    }

    std::vector<std::string> NavUri::QueryParameterNames() const {
        std::vector<std::string> out;
        for (const auto& [k, v] : QueryPairs(Query()))
            if (std::find(out.begin(), out.end(), k) == out.end()) out.push_back(k);
        return out;
    }

    std::vector<std::string> NavUri::QueryParameters(const std::string& name) const {
        std::vector<std::string> out;
        for (const auto& [k, v] : QueryPairs(Query()))
            if (k == name) out.push_back(v);
        return out;
    }

    // --- NavDeepLink ----------------------------------------------------------------------------

    NavDeepLink::NavDeepLink(std::string uriPattern) : m_UriPattern(std::move(uriPattern)) {
        // parsePath
        static const std::regex scheme(R"(^[a-zA-Z]+[+\w\-.]*:)");
        std::string regex = "^";
        if (!std::regex_search(m_UriPattern, scheme)) regex += "http[s]?://";
        const std::size_t split = std::min(m_UriPattern.find_first_of("?#"), m_UriPattern.size());
        BuildRegex(m_UriPattern.substr(0, split), m_PathArgs, regex);
        regex += "($|(\\?(.)*)|(#(.)*))";
        m_PathRegex = SaveWildcard(regex);
        m_IsExact = m_PathRegex.find(".*") == std::string::npos && m_PathRegex.find(kPathRegex) == std::string::npos;
        // parseQuery
        static const std::regex queryPattern(R"(^[^?#]+\?([^#]*).*)");
        m_IsParameterizedQuery = std::regex_match(m_UriPattern, queryPattern);
        if (m_IsParameterizedQuery) {
            const NavUri uri(m_UriPattern);
            static const std::regex fillIn(R"(\{(.+?)\})");
            for (const auto& name : uri.QueryParameterNames()) {
                const auto params = uri.QueryParameters(name);
                if (params.size() > 1)
                    throw std::invalid_argument("Query parameter " + name + " must only be present once in " + m_UriPattern + ".");
                std::string queryParam = params.empty() ? name : params.front();
                if (params.empty()) m_SingleQueryParamValueOnly = true;
                ParamQuery param;
                std::string argRegex;
                std::size_t appendPos = 0;
                for (auto it = std::sregex_iterator(queryParam.begin(), queryParam.end(), fillIn); it != std::sregex_iterator(); ++it) {
                    param.arguments.push_back((*it)[1].str());
                    const std::size_t start = std::size_t(it->position());
                    if (start > appendPos) argRegex += RegexEscape(queryParam.substr(appendPos, start - appendPos));
                    argRegex += kAnySymbolsInTheTail;
                    appendPos = start + std::size_t(it->length());
                }
                if (appendPos < queryParam.size()) argRegex += RegexEscape(queryParam.substr(appendPos));
                argRegex += "$";
                param.regex = SaveWildcard(argRegex);
                m_QueryArgs.emplace_back(name, std::move(param));
            }
        }
        // parseFragment
        if (const auto frag = NavUri(m_UriPattern).Fragment()) BuildRegex(*frag, m_FragArgs, m_FragRegex);
    }

    std::vector<std::string> NavDeepLink::ArgumentsNames() const {
        std::vector<std::string> out = m_PathArgs;
        for (const auto& [n, q] : m_QueryArgs) out.insert(out.end(), q.arguments.begin(), q.arguments.end());
        out.insert(out.end(), m_FragArgs.begin(), m_FragArgs.end());
        return out;
    }

    bool NavDeepLink::Matches(const NavUri& uri) const {
        return std::regex_match(uri.ToString(), std::regex(m_PathRegex, std::regex::icase));
    }

    std::optional<NavArgs> NavDeepLink::GetMatchingArguments(const NavUri& uri, const std::map<std::string, NavArgument>& arguments) const {
        std::smatch result;
        const std::string s = uri.ToString();
        if (!std::regex_match(s, result, std::regex(m_PathRegex, std::regex::icase))) return std::nullopt;
        NavArgs out;
        auto argOf = [&](const std::string& name) -> const NavArgument* { auto it = arguments.find(name); return it == arguments.end() ? nullptr : &it->second; };
        // Path arguments: one that does not parse means no match.
        for (std::size_t i = 0; i < m_PathArgs.size(); ++i) {
            const std::string value = NavUri::Decode(result[i + 1].str());
            try {
                ParseArgument(out, m_PathArgs[i], value, argOf(m_PathArgs[i]));
            } catch (const std::invalid_argument&) {
                return std::nullopt;
            }
        }
        // Query arguments.
        if (m_IsParameterizedQuery) {
            for (const auto& [paramName, stored] : m_QueryArgs) {
                std::vector<std::string> inputs = uri.QueryParameters(paramName);
                if (m_SingleQueryParamValueOnly) {
                    const auto q = uri.Query();
                    if (q && *q != s) inputs = { *q };
                }
                NavArgs temp;
                const std::regex pattern(stored.regex);
                for (const auto& input : inputs) {
                    std::smatch m;
                    if (!std::regex_match(input, m, pattern)) return std::nullopt;
                    for (std::size_t i = 0; i < stored.arguments.size(); ++i) {
                        const std::string& name = stored.arguments[i];
                        if (temp.contains(name)) continue;   // repeated params: collections only
                        try {
                            ParseArgument(temp, name, m[i + 1].str(), argOf(name));
                        } catch (const std::invalid_argument&) {
                        }
                    }
                }
                for (auto& [k, v] : temp) out[k] = v;
            }
        }
        // Fragment arguments.
        if (!m_FragRegex.empty()) {
            const std::string fragment = uri.Fragment().value_or("null");
            std::smatch m;
            if (std::regex_match(fragment, m, std::regex(m_FragRegex, std::regex::icase))) {
                for (std::size_t i = 0; i < m_FragArgs.size(); ++i) {
                    try {
                        ParseArgument(out, m_FragArgs[i], NavUri::Decode(m[i + 1].str()), argOf(m_FragArgs[i]));
                    } catch (const std::invalid_argument&) {
                        break;
                    }
                }
            }
        }
        if (!MissingRequiredArguments(arguments, [&](const std::string& n) { return !out.contains(n); }).empty()) return std::nullopt;
        return out;
    }

    int NavDeepLink::CalculateMatchingPathSegments(const NavUri& uri) const {
        const auto requested = uri.PathSegments();
        const auto pattern = NavUri(m_UriPattern).PathSegments();
        std::set<std::string> a(requested.begin(), requested.end()), b(pattern.begin(), pattern.end());
        int n = 0;
        for (const auto& s : a) if (b.contains(s)) ++n;
        return n;
    }

    // --- DeepLinkMatch --------------------------------------------------------------------------

    bool DeepLinkMatch::HasMatchingArgs(const std::optional<NavArgs>& arguments) const {
        if (!arguments || !matchingArgs) return false;
        for (const auto& [key, value] : *matchingArgs) {
            auto it = arguments->find(key);
            if (it == arguments->end()) return false;
            if (destination->Arguments().contains(key) && !(it->second == value)) return false;
        }
        return true;
    }

    bool DeepLinkMatch::operator<(const DeepLinkMatch& o) const {
        auto compare = [&]() -> int {
            if (isExactDeepLink != o.isExactDeepLink) return isExactDeepLink ? 1 : -1;
            if (matchingPathSegments != o.matchingPathSegments) return matchingPathSegments > o.matchingPathSegments ? 1 : -1;
            if (matchingArgs.has_value() != o.matchingArgs.has_value()) return matchingArgs ? 1 : -1;
            if (matchingArgs && matchingArgs->size() != o.matchingArgs->size()) return matchingArgs->size() > o.matchingArgs->size() ? 1 : -1;
            if (hasMatchingAction != o.hasMatchingAction) return hasMatchingAction ? 1 : -1;
            return mimeTypeMatchLevel - o.mimeTypeMatchLevel;
        };
        return compare() < 0;
    }

    std::string CreateRoute(const std::string& route) { return "android-app://androidx.navigation/" + route; }

    int RouteId(const std::string& route) {
        // String.hashCode of createRoute(route).
        std::int32_t h = 0;
        for (unsigned char ch : CreateRoute(route)) h = std::int32_t(std::uint32_t(h) * 31u + ch);
        return h;
    }

    // --- NavDestination -------------------------------------------------------------------------

    void NavDestination::SetRoute(std::optional<std::string> route) {
        if (!route) {
            m_Id = 0;
            m_RouteDeepLink.reset();
            m_Route.reset();
            return;
        }
        if (route->find_first_not_of(" \t\n") == std::string::npos) throw std::invalid_argument("Cannot have an empty route");
        NavDeepLink link(CreateRoute(*route));
        const auto names = link.ArgumentsNames();
        const auto missing = MissingRequiredArguments(m_Arguments, [&](const std::string& k) { return std::find(names.begin(), names.end(), k) == names.end(); });
        if (!missing.empty()) throw std::invalid_argument("Cannot set route \"" + *route + "\": required arguments are missing");
        m_RouteDeepLink = std::move(link);
        m_Id = RouteId(*route);
        m_Route = std::move(route);
    }

    std::string NavDestination::DisplayName() const { return m_Route ? *m_Route : std::to_string(m_Id); }

    std::optional<DeepLinkMatch> NavDestination::MatchRoute(const std::string& route) {
        if (!m_RouteDeepLink) return std::nullopt;
        const NavUri uri(CreateRoute(route));
        auto args = m_RouteDeepLink->GetMatchingArguments(uri, m_Arguments);
        if (!args) return std::nullopt;
        return DeepLinkMatch{ this, std::move(args), m_RouteDeepLink->IsExactDeepLink(), m_RouteDeepLink->CalculateMatchingPathSegments(uri), false, -1 };
    }

    bool NavDestination::HasRoute(const std::string& route, const std::optional<NavArgs>& arguments) {
        if (m_Route == route) return true;
        const auto match = MatchRoute(route);
        if (!match || match->destination != this) return false;
        return match->HasMatchingArgs(arguments);
    }

    std::optional<NavArgs> NavDestination::AddInDefaultArgs(const std::optional<NavArgs>& args) const {
        if (!args && m_Arguments.empty()) return std::nullopt;
        NavArgs out;
        for (const auto& [k, a] : m_Arguments)
            if (a.defaultValue && !std::holds_alternative<std::monostate>(*a.defaultValue)) out[k] = *a.defaultValue;
        if (args) {
            for (const auto& [k, v] : *args) out[k] = v;
            for (const auto& [k, a] : m_Arguments) {
                auto it = out.find(k);
                if (it == out.end()) continue;
                const bool ok = std::holds_alternative<std::monostate>(it->second) ? a.isNullable : NavValueHasType(a.type, it->second);
                if (!ok) throw std::invalid_argument("Wrong argument type for '" + k + "' in argument savedState. " + NavTypeName(a.type) + " expected.");
            }
        }
        return out;
    }

    std::vector<NavDestination*> NavDestination::Hierarchy() {
        std::vector<NavDestination*> out;
        for (NavDestination* d = this; d; d = d->Parent()) out.push_back(d);
        return out;
    }

    // --- NavGraph -------------------------------------------------------------------------------

    NavDestination* NavGraph::NodeById(int id) const {
        for (const auto& n : m_Nodes) if (n->Id() == id) return n.get();
        return nullptr;
    }

    void NavGraph::AddDestination(NavDestinationPtr node) {
        const int id = node->Id();
        if (id == 0 && !node->Route()) throw std::invalid_argument("Destinations must have an id or route.");
        if (Route() && node->Route() == Route()) throw std::invalid_argument("Destination cannot have the same route as graph");
        if (id == Id()) throw std::invalid_argument("Destination cannot have the same id as graph");
        for (auto& n : m_Nodes) {
            if (n->Id() != id) continue;
            if (n == node) return;
            n->SetParent(nullptr);
            if (node->Parent()) throw std::logic_error("Destination already has a parent set. Call NavGraph.remove() to remove the previous parent.");
            node->SetParent(this);
            n = std::move(node);
            return;
        }
        if (node->Parent()) throw std::logic_error("Destination already has a parent set. Call NavGraph.remove() to remove the previous parent.");
        node->SetParent(this);
        m_Nodes.push_back(std::move(node));
    }

    void NavGraph::Remove(NavDestination* node) {
        for (auto it = m_Nodes.begin(); it != m_Nodes.end(); ++it)
            if ((*it)->Id() == node->Id()) { (*it)->SetParent(nullptr); m_Nodes.erase(it); return; }
    }

    void NavGraph::Clear() {
        for (auto& n : m_Nodes) n->SetParent(nullptr);
        m_Nodes.clear();
    }

    std::optional<DeepLinkMatch> NavGraph::MatchRouteComprehensive(const std::string& route, bool searchChildren, bool searchParent, NavDestination* lastVisited) {
        std::optional<DeepLinkMatch> best = MatchRoute(route);
        auto consider = [&](std::optional<DeepLinkMatch> m) { if (m && (!best || *best < *m)) best = std::move(m); };
        if (searchChildren) {
            std::optional<DeepLinkMatch> bestChild;
            for (auto& child : m_Nodes) {
                if (child.get() == lastVisited) continue;
                std::optional<DeepLinkMatch> m = child->IsGraph()
                    ? static_cast<NavGraph&>(*child).MatchRouteComprehensive(route, true, false, this)
                    : child->MatchRoute(route);
                if (m && (!bestChild || *bestChild < *m)) bestChild = std::move(m);
            }
            consider(std::move(bestChild));
        }
        if (searchParent && Parent() && Parent() != lastVisited) consider(Parent()->MatchRouteComprehensive(route, searchChildren, true, this));
        return best;
    }

    NavDestination* NavGraph::FindNodeComprehensive(int id, NavDestination* lastVisited, bool searchChildren, NavDestination* matchingDest) {
        NavDestination* destination = NodeById(id);
        if (matchingDest) {
            if (destination == matchingDest && destination->Parent() == matchingDest->Parent()) return destination;
            destination = nullptr;
        } else if (destination) {
            return destination;
        }
        if (searchChildren)
            for (auto& child : m_Nodes)
                if (child->IsGraph() && child.get() != lastVisited)
                    if ((destination = static_cast<NavGraph&>(*child).FindNodeComprehensive(id, this, true, matchingDest))) break;
        if (destination) return destination;
        if (Parent() && Parent() != lastVisited) return Parent()->FindNodeComprehensive(id, this, searchChildren, matchingDest);
        return nullptr;
    }

    NavDestination* NavGraph::FindNode(const std::string& route, bool searchParents) {
        if (route.find_first_not_of(" \t\n") == std::string::npos) return nullptr;
        for (auto& n : m_Nodes)
            if (n->Route() == route || n->MatchRoute(route)) return n.get();
        if (searchParents && Parent()) return Parent()->FindNode(route);
        return nullptr;
    }

    void NavGraph::SetStartDestination(const std::string& route) {
        if (route.find_first_not_of(" \t\n") == std::string::npos) throw std::invalid_argument("Start destination route cannot be empty");
        if (Route() == route) throw std::invalid_argument("Start destination " + route + " cannot use the same route as the graph");
        m_StartRoute = route;
        m_StartId = RouteId(route);
    }

    std::string NavGraph::StartDestDisplayName() const { return m_StartRoute ? *m_StartRoute : std::to_string(m_StartId); }

    std::string NavGraph::DisplayName() const { return Id() != 0 ? NavDestination::DisplayName() : "the root navigation"; }

    NavDestination* NavGraph::FindStartDestination() {
        NavDestination* d = m_StartRoute ? FindNode(*m_StartRoute, false) : NodeById(m_StartId);
        while (d && d->IsGraph()) {
            auto& g = static_cast<NavGraph&>(*d);
            d = g.m_StartRoute ? g.FindNode(*g.m_StartRoute, false) : g.NodeById(g.m_StartId);
        }
        return d;
    }

    std::vector<NavDestination*> NavGraph::ChildHierarchy() {
        std::vector<NavDestination*> out{ this };
        NavDestination* d = this;
        while (d && d->IsGraph()) {
            auto& g = static_cast<NavGraph&>(*d);
            d = g.m_StartRoute ? g.FindNode(*g.m_StartRoute, false) : g.NodeById(g.m_StartId);
            if (d) out.push_back(d);
        }
        return out;
    }

}
