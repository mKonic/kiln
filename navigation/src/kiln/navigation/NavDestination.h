#pragma once

// navigation-common: arguments (NavType, NavArgument), route patterns (NavDeepLink's uri pattern
// matching, as routes use it), NavDestination and NavGraph. Routes are strings with {argument}
// placeholders in the path or query ("detail/{id}?tab={tab}"). Deep links match a uri pattern,
// an action and a mime type (NavDeepLinkRequest). Type-safe (kotlinx.serialization) routes have
// no C++ counterpart.

#include "kiln/Uri.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace kiln::navigation {

    // An argument value (what a SavedState holds for the argument types here).
    using NavValue = std::variant<std::monostate, bool, std::int32_t, std::int64_t, float, std::string>;
    // Arguments by name (SavedState).
    using NavArgs = std::map<std::string, NavValue>;

    // NavType: how an argument parses from a route and what it holds.
    enum class NavType : std::uint8_t { String, Int, Long, Float, Bool };
    const char* NavTypeName(NavType t);
    // parseValue: throws std::invalid_argument when `value` does not parse.
    NavValue ParseNavValue(NavType type, const std::string& value);
    bool NavValueHasType(NavType type, const NavValue& v);

    // NavArgument: an argument's type, nullability and default value.
    struct NavArgument {
        NavType type = NavType::String;
        bool isNullable = false;
        std::optional<NavValue> defaultValue;   // set: a default is present (monostate: null)
        bool IsDefaultValuePresent() const { return defaultValue.has_value(); }
        bool operator==(const NavArgument&) const = default;
    };
    // navArgument(name) { type; nullable; defaultValue }
    struct NamedNavArgument {
        std::string name;
        NavArgument argument;
    };
    NamedNavArgument NavArgumentOf(std::string name, NavType type, bool nullable = false, std::optional<NavValue> defaultValue = std::nullopt);

    using NavUri = kiln::Uri;

    // NavDeepLinkRequest: what a deep link is asked to match: a uri, an action and a mime type,
    // any of them absent.
    struct NavDeepLinkRequest {
        std::optional<NavUri> uri;
        std::optional<std::string> action;
        std::optional<std::string> mimeType;
        // Builder.fromUri / fromAction / fromMimeType (an empty action or a mime type not
        // "type/subtype" throws std::invalid_argument).
        static NavDeepLinkRequest FromUri(NavUri uri) { return { std::move(uri), std::nullopt, std::nullopt }; }
        static NavDeepLinkRequest FromAction(std::string action);
        static NavDeepLinkRequest FromMimeType(std::string mimeType);
        std::string ToString() const;
    };

    // NavDeepLink: a uri pattern ({argument} placeholders in the path, query and fragment), an
    // action and a mime type ("type/subtype", either part "*"), each optional. A request matches
    // when every part the deep link has matches; the uri gives the arguments.
    class NavDeepLink {
    public:
        explicit NavDeepLink(std::string uriPattern) : NavDeepLink(std::optional<std::string>(std::move(uriPattern)), std::nullopt, std::nullopt) {}
        // An empty action or a mime type that is not "type/subtype" throws std::invalid_argument.
        NavDeepLink(std::optional<std::string> uriPattern, std::optional<std::string> action, std::optional<std::string> mimeType);
        const std::optional<std::string>& UriPattern() const { return m_UriPattern; }
        const std::optional<std::string>& Action() const { return m_Action; }
        const std::optional<std::string>& MimeType() const { return m_MimeType; }
        bool IsExactDeepLink() const { return m_IsExact; }
        std::vector<std::string> ArgumentsNames() const;
        bool Matches(const NavUri& uri) const { return Matches(NavDeepLinkRequest::FromUri(uri)); }
        bool Matches(const NavDeepLinkRequest& request) const;
        // The arguments `uri` gives, or nullopt when it does not match or misses a required one.
        std::optional<NavArgs> GetMatchingArguments(const NavUri& uri, const std::map<std::string, NavArgument>& arguments) const;
        // The path and query arguments that parse (none when the uri does not match).
        NavArgs GetMatchingPathAndQueryArgs(const std::optional<NavUri>& uri, const std::map<std::string, NavArgument>& arguments) const;
        int CalculateMatchingPathSegments(const std::optional<NavUri>& uri) const;
        // getMimeTypeMatchRating: -1 no match; else 2 for the same type plus 1 for the same subtype.
        int GetMimeTypeMatchRating(const std::string& mimeType) const;
        bool operator==(const NavDeepLink& o) const { return m_UriPattern == o.m_UriPattern && m_Action == o.m_Action && m_MimeType == o.m_MimeType; }
    private:
        struct ParamQuery { std::string regex; std::vector<std::string> arguments; };
        bool QueryArguments(const NavUri& uri, const std::string& whole, const std::map<std::string, NavArgument>& arguments, NavArgs& out) const;
        std::optional<std::string> m_UriPattern, m_Action, m_MimeType;
        std::string m_MimeRegex;
        std::vector<std::string> m_PathArgs;
        std::string m_PathRegex;
        bool m_IsExact = false;
        bool m_IsParameterizedQuery = false;
        bool m_SingleQueryParamValueOnly = false;
        std::vector<std::pair<std::string, ParamQuery>> m_QueryArgs;
        std::vector<std::string> m_FragArgs;
        std::string m_FragRegex;
    };

    // navDeepLink { uriPattern = ...; action = ...; mimeType = ... }
    struct NavDeepLinkDslBuilder {
        std::optional<std::string> uriPattern, action, mimeType;
        NavDeepLink Build() const;   // one of the three is required (std::invalid_argument)
    };
    NavDeepLink NavDeepLinkOf(const std::function<void(NavDeepLinkDslBuilder&)>& deepLinkBuilder);

    class NavGraph;

    // DeepLinkMatch: a destination a route matched, ordered by how well.
    struct DeepLinkMatch {
        class NavDestination* destination = nullptr;
        std::optional<NavArgs> matchingArgs;
        bool isExactDeepLink = false;
        int matchingPathSegments = 0;
        bool hasMatchingAction = false;
        int mimeTypeMatchLevel = -1;
        bool HasMatchingArgs(const std::optional<NavArgs>& arguments) const;
        bool operator<(const DeepLinkMatch& o) const;   // compareTo < 0
    };

    // "android-app://androidx.navigation/" + route
    std::string CreateRoute(const std::string& route);
    // The id a route gives a destination (createRoute(route).hashCode()).
    int RouteId(const std::string& route);

    // NavDestination: a place in the graph, handled by the navigator named navigatorName.
    class NavDestination {
    public:
        explicit NavDestination(std::string navigatorName) : m_NavigatorName(std::move(navigatorName)) {}
        virtual ~NavDestination() = default;
        const std::string& NavigatorName() const { return m_NavigatorName; }
        NavGraph* Parent() const { return m_Parent; }
        void SetParent(NavGraph* p) { m_Parent = p; }
        std::optional<std::string> label;
        const std::map<std::string, NavArgument>& Arguments() const { return m_Arguments; }
        int Id() const { return m_Id; }
        void SetId(int id) { m_Id = id; }
        const std::optional<std::string>& Route() const { return m_Route; }
        void SetRoute(std::optional<std::string> route);
        virtual std::string DisplayName() const;
        void AddDeepLink(const std::string& uriPattern) { m_DeepLinks.emplace_back(uriPattern); }
        // addDeepLink: a deep link with arguments not declared on the destination throws
        // std::invalid_argument.
        void AddDeepLink(NavDeepLink navDeepLink);
        const std::vector<NavDeepLink>& DeepLinks() const { return m_DeepLinks; }
        // matchDeepLink: the best of this destination's deep links for the request (a graph's
        // also searches its children).
        virtual std::optional<DeepLinkMatch> MatchDeepLink(const NavDeepLinkRequest& request);
        bool HasDeepLink(const NavUri& deepLink) { return HasDeepLink(NavDeepLinkRequest::FromUri(deepLink)); }
        bool HasDeepLink(const NavDeepLinkRequest& request) { return MatchDeepLink(request).has_value(); }
        std::optional<DeepLinkMatch> MatchRoute(const std::string& route);
        bool HasRoute(const std::string& route, const std::optional<NavArgs>& arguments);
        void AddArgument(const std::string& name, NavArgument argument) { m_Arguments[name] = std::move(argument); }
        void RemoveArgument(const std::string& name) { m_Arguments.erase(name); }
        // addInDefaultArgs: the defaults overlaid with `args` (nullopt when both are empty).
        std::optional<NavArgs> AddInDefaultArgs(const std::optional<NavArgs>& args) const;
        // hierarchy: this, then each parent graph.
        std::vector<NavDestination*> Hierarchy();
        virtual bool IsGraph() const { return false; }
        // FloatingWindow (dialogs) and SupportingPane markers.
        virtual bool IsFloatingWindow() const { return false; }
        virtual bool IsSupportingPane() const { return false; }

    private:
        std::string m_NavigatorName;
        NavGraph* m_Parent = nullptr;
        std::map<std::string, NavArgument> m_Arguments;
        int m_Id = 0;
        std::optional<std::string> m_Route;
        std::optional<NavDeepLink> m_RouteDeepLink;
        std::vector<NavDeepLink> m_DeepLinks;
    };

    using NavDestinationPtr = std::shared_ptr<NavDestination>;

    // NavGraph: a destination holding destinations, entered at its start destination.
    class NavGraph : public NavDestination {
    public:
        explicit NavGraph(std::string navigatorName = "navigation") : NavDestination(std::move(navigatorName)) {}
        bool IsGraph() const override { return true; }
        const std::vector<NavDestinationPtr>& Nodes() const { return m_Nodes; }
        NavDestination* NodeById(int id) const;
        void AddDestination(NavDestinationPtr node);
        void Remove(NavDestination* node);
        void Clear();
        std::optional<DeepLinkMatch> MatchRouteComprehensive(const std::string& route, bool searchChildren, bool searchParent, NavDestination* lastVisited);
        std::optional<DeepLinkMatch> MatchDeepLink(const NavDeepLinkRequest& request) override;
        std::optional<DeepLinkMatch> MatchDeepLinkComprehensive(const NavDeepLinkRequest& request, bool searchChildren, bool searchParent,
                                                                NavDestination* lastVisited);
        NavDestination* FindNodeComprehensive(int id, NavDestination* lastVisited, bool searchChildren, NavDestination* matchingDest = nullptr);
        NavDestination* FindNode(int id) { return FindNodeComprehensive(id, this, false); }
        NavDestination* FindNode(const std::string& route, bool searchParents = true);
        int StartDestinationId() const { return m_StartId; }
        const std::optional<std::string>& StartDestinationRoute() const { return m_StartRoute; }
        void SetStartDestination(const std::string& route);
        std::string StartDestDisplayName() const;
        std::string DisplayName() const override;
        // findStartDestination: the start destination, descending through nested graphs.
        NavDestination* FindStartDestination();
        // childHierarchy: this, then the start destination of each nested graph.
        std::vector<NavDestination*> ChildHierarchy();

    private:
        std::vector<NavDestinationPtr> m_Nodes;
        int m_StartId = 0;
        std::optional<std::string> m_StartRoute;
    };

}
