// What every build keeps of Volition: adapter registrations (a NavHost registers itself
// whether or not the app answers), JSON values, and Call, which kiln_volition answers when a
// debug build links it.
#include <kiln/ui/Volition.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace kiln::ui::volition {

    namespace {
        std::vector<detail::Registered>& Adapters() { static std::vector<detail::Registered> a; return a; }
        detail::Handler& InstalledHandler() { static detail::Handler h = nullptr; return h; }

        std::string JsonString(std::string_view s) {
            std::string out = "\"";
            for (const char ch : s) {
                switch (ch) {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    case '/': out += "\\/"; break;   // as org.json writes it
                    default:
                        if (static_cast<unsigned char>(ch) < 0x20) {
                            char buf[8];
                            std::snprintf(buf, sizeof buf, "\\u%04x", ch);
                            out += buf;
                        } else out += ch;
                }
            }
            return out + "\"";
        }

        std::string Number(double d) {
            if (std::isfinite(d) && std::fmod(d, 1.0) == 0.0 && std::fabs(d) < 1e15) return std::to_string(static_cast<long long>(d));
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.17g", d);
            return buf;
        }
    }

    const std::vector<detail::Registered>& detail::RegisteredAdapters() { return Adapters(); }
    void detail::SetHandler(Handler handler) { InstalledHandler() = handler; }

    std::string ToJson(const Value& value) {
        return std::visit([](const auto& x) -> std::string {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>) return "null";
            else if constexpr (std::is_same_v<T, bool>) return x ? "true" : "false";
            else if constexpr (std::is_same_v<T, double>) return Number(x);
            else if constexpr (std::is_same_v<T, std::string>) return JsonString(x);
            else if constexpr (std::is_same_v<T, std::vector<Value>>) {
                std::string out = "[";
                for (std::size_t i = 0; i < x.size(); ++i) out += (i ? "," : "") + ToJson(x[i]);
                return out + "]";
            } else {
                std::string out = "{";
                for (std::size_t i = 0; i < x.size(); ++i) out += (i ? "," : "") + JsonString(x[i].first) + ":" + ToJson(x[i].second);
                return out + "}";
            }
        }, value.v);
    }

    std::shared_ptr<void> Register(Adapter adapter) {
        static std::uint64_t next = 0;
        const std::uint64_t id = ++next;
        Adapters().push_back({ id, std::move(adapter) });
        return std::shared_ptr<void>(nullptr, [id](void*) {
            auto& a = Adapters();
            a.erase(std::remove_if(a.begin(), a.end(), [id](const detail::Registered& r) { return r.id == id; }), a.end());
        });
    }

    std::string Call(const std::vector<Window>& windows, const Context& context, std::string_view method, std::string_view argument) {
        if (const detail::Handler h = InstalledHandler()) return h(windows, context, method, argument);
        return "error: this build does not answer Volition (a release build); build it with --debug";
    }

}
