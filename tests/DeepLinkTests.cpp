// navigation3 deep links: cases from UriDeepLinkMatcherTest, DeepLinkMatcherTest and
// DeepLinkRequestTest (navigation3-runtime), with decode functions in place of serializers.
#include "Check.h"

#include "kiln/navigation3/DeepLink.h"

#include <algorithm>
#include <charconv>

using namespace kiln;
using namespace kiln::navigation3;

namespace {
    struct Key {
        std::string name;
        int age = 0;
        bool operator==(const Key&) const = default;
    };
    // TestKey: matches whatever the arguments are.
    std::optional<Key> AnyKey(const DeepLinkArguments&) { return Key{ "test" }; }
    std::optional<int> ToInt(const std::string& s) {
        int v = 0;
        auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
        if (ec != std::errc() || p != s.data() + s.size()) return std::nullopt;
        return v;
    }
    // SimpleKey(name, age): both required, age an int.
    std::optional<Key> NameAge(const DeepLinkArguments& a) {
        auto n = a.find("name"), g = a.find("age");
        if (n == a.end() || g == a.end()) return std::nullopt;
        auto age = ToInt(g->second.front());
        if (!age) return std::nullopt;
        return Key{ n->second.front(), *age };
    }
    bool Matches(const std::string& pattern, const std::string& uri) {
        return UriDeepLinkMatcher<Key>(pattern, AnyKey).Match(DeepLinkRequest(uri)).has_value();
    }
    DeepLinkMatch<Key> MatchOf(const std::string& pattern, const std::string& uri) {
        return *UriDeepLinkMatcher<Key>(pattern, AnyKey).Match(DeepLinkRequest(uri));
    }
}

TEST(DeepLinkSchemeAndAuthority) {
    CHECK(!Matches("http://example.com/path", "ftp://example.com/path"));
    CHECK(Matches("http://example.com/path", "https://example.com/path"));
    CHECK(Matches("http://example.com/path", "http://example.com/path"));
    CHECK(Matches("https://example.com/path", "https://example.com/path"));
    CHECK(!Matches("https://example.com/path", "http://example.com/path"));   // https is strict
    CHECK(Matches("//example.com/path", "http://example.com/path"));
    CHECK(Matches("//example.com/path", "https://example.com/path"));
    CHECK(Matches("www.example.com/path", "https://www.example.com/path"));
    CHECK(Matches("example.com/path", "http://example.com/path"));
    CHECK(Matches("http://example.com/path", "HTTP://example.com/path"));
    CHECK(!Matches("http://example.com/path", "http://example.org/path"));
    CHECK(Matches("http://example.com/path", "http://EXAMPLE.COM/path"));
}

TEST(DeepLinkPathSegments) {
    CHECK(!Matches("http://example.com/path/subpath", "http://example.com/path"));
    CHECK(!Matches("http://example.com/path", "http://example.com/path/subpath"));
    CHECK(Matches("http://example.com/path/subpath", "http://example.com/path/subpath"));
    CHECK(Matches("http://example.com/path/.*", "http://example.com/path/wildcard/segments"));
}

TEST(DeepLinkArgumentsMakeTheKey) {
    UriDeepLinkMatcher<Key> m("https://example.com/user_user_{name}_{age}", NameAge);
    auto r = m.Match(DeepLinkRequest("https://example.com/user_user_john_30"));
    CHECK(r.has_value());
    CHECK(r->key == (Key{ "john", 30 }));
    CHECK(!m.Match(DeepLinkRequest("https://example.com/user_user_john_notAnAge")).has_value());
    UriDeepLinkMatcher<Key> q("https://example.com/user?name={name}&age={age}", NameAge);
    r = q.Match(DeepLinkRequest("https://example.com/user?name=john&age=30"));
    CHECK(r.has_value() && r->key == (Key{ "john", 30 }));
    CHECK(!q.Match(DeepLinkRequest("https://example.com/user?name=john")).has_value());   // a required argument is missing
    // Encoded path arguments come decoded.
    UriDeepLinkMatcher<Key> p("https://example.com/{name}/{age}", NameAge);
    r = p.Match(DeepLinkRequest("https://example.com/ada%20lovelace/36"));
    CHECK(r.has_value() && r->key.name == "ada lovelace");
}

TEST(DeepLinkFragments) {
    UriDeepLinkMatcher<Key> f("https://example.com/path#{name}", [](const DeepLinkArguments& a) -> std::optional<Key> {
        return Key{ a.at("name").front() };
    });
    auto r = f.Match(DeepLinkRequest("https://example.com/path#a"));
    CHECK(r.has_value() && r->key.name == "a");
    auto w = MatchOf("https://example.com/path#.*", "https://example.com/path#123");
    CHECK(w.arguments.empty());
    auto pw = MatchOf("https://example.com/path#a_.*", "https://example.com/path#a_123");
    CHECK(pw.arguments.empty());
}

TEST(DeepLinkBestMatchOrdering) {
    // An exact path beats a wildcard.
    CHECK(MatchOf("https://example.com/.*", "https://example.com/path") < MatchOf("https://example.com/path", "https://example.com/path"));
    // More path arguments win.
    CHECK(MatchOf("https://example.com/{name}/.*", "https://example.com/john/123/anything") <
          MatchOf("https://example.com/{name}/{id}/.*", "https://example.com/john/123/anything"));
    // More query arguments win; fragment arguments count the same as query ones.
    CHECK(MatchOf("https://example.com/{name}", "https://example.com/john?q=abc") <
          MatchOf("https://example.com/{name}?q={q}", "https://example.com/john?q=abc"));
    CHECK(MatchOf("https://example.com/{name}", "https://example.com/john#7") < MatchOf("https://example.com/{name}#{id}", "https://example.com/john#7"));
    CHECK_EQ(MatchOf("https://example.com/{name}?q={q}", "https://example.com/john?q=abc#7")
                 .CompareTo(MatchOf("https://example.com/{name}#{id}", "https://example.com/john?q=abc#7")), 0);
    CHECK_EQ(MatchOf("https://example.com/{id}/{name}", "https://example.com/1/2").CompareTo(MatchOf("https://example.com/{first}/{last}", "https://example.com/1/2")), 0);
    // A uri match beats any other kind.
    CHECK_EQ(MatchOf("https://example.com/path", "https://example.com/path").CompareTo(DeepLinkMatch<Key>{ Key{} }), 1);
}

TEST(DeepLinkDuplicatePlaceholdersThrow) {
    auto throws = [](const std::string& pattern) {
        try { (void)Matches(pattern, "https://example.com/a/b?x=1#c"); } catch (const std::invalid_argument&) { return true; }
        return false;
    };
    CHECK(throws("https://example.com/{a}/{a}"));
    CHECK(throws("https://example.com/path?x={a}&y={a}"));
    CHECK(throws("https://example.com/{a}?x={a}"));
    CHECK(throws("https://example.com/{a}#{a}"));
    CHECK(throws("https://example.com/path?x={a}#{a}"));
}

TEST(DeepLinkFiltersExtrasAndBackStacks) {
    StaticKeyDeepLinkMatcher<Key> image(Key{ "image" }, { MimeTypeFilter("image/png") });
    CHECK(image.Match(DeepLinkRequest(DeepLinkRequest::MimeTypeExtra("IMAGE/PNG"))).has_value());
    CHECK(!image.Match(DeepLinkRequest(DeepLinkRequest::MimeTypeExtra("text/plain"))).has_value());
    StaticKeyDeepLinkMatcher<Key> view(Key{ "view" }, { ActionFilter("android.intent.action.VIEW") });
    CHECK(view.Match(DeepLinkRequest("https://example.com", DeepLinkRequest::ActionExtra("android.intent.action.VIEW"))).has_value());
    bool threw = false;
    try { DeepLinkRequest r{ RequestExtras{} }; } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    const RequestExtras both = DeepLinkRequest::MimeTypeExtra("a") + DeepLinkRequest::ActionExtra("b");
    CHECK_EQ(both.Size(), std::size_t(2));
    CHECK_EQ((both - DeepLinkRequest::ActionExtra("x")).Size(), std::size_t(1));

    auto users = std::make_shared<UriDeepLinkMatcher<Key>>("https://example.com/user?name={name}&age={age}", NameAge);
    auto withStack = WithBackStack<Key, Key>(users, [](const DeepLinkMatch<Key>& m) { return std::vector<Key>{ Key{ "home" }, m.key }; });
    auto r = withStack->Match(DeepLinkRequest("https://example.com/user?name=john&age=30"));
    CHECK(r.has_value());
    CHECK(r->BackStack() == (std::vector<Key>{ Key{ "home" }, Key{ "john", 30 } }));
}
