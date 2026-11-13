#include <kiln/ui/Volition.h>

#include <kiln/ui/LayoutNode.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <tuple>
#include <type_traits>

namespace kiln::ui::volition {

    namespace {
        constexpr std::size_t kKindWidth = 9;
        constexpr std::size_t kMaxShown = 80;
        constexpr float kRowSlackDp = 8;

        std::string Trim(std::string_view s) {
            const auto b = s.find_first_not_of(" \t\n\r\f\v");
            if (b == std::string_view::npos) return {};
            return std::string(s.substr(b, s.find_last_not_of(" \t\n\r\f\v") - b + 1));
        }
        std::string Lower(std::string_view s) {
            std::string o(s);
            for (char& c : o) c = char(std::tolower(static_cast<unsigned char>(c)));
            return o;
        }
        bool EqualsIgnoreCase(std::string_view a, std::string_view b) { return Lower(a) == Lower(b); }
        bool ContainsIgnoreCase(std::string_view hay, std::string_view needle) { return Lower(hay).find(Lower(needle)) != std::string::npos; }
        // Characters as Kotlin counts them closely enough for a debug listing: code points.
        std::size_t CodePoints(std::string_view s) {
            std::size_t n = 0;
            for (unsigned char c : s) n += (c & 0xC0) != 0x80;
            return n;
        }
        std::string TakeCodePoints(std::string_view s, std::size_t count) {
            std::size_t i = 0, seen = 0;
            for (; i < s.size(); ++i) {
                if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) {
                    if (seen == count) break;
                    ++seen;
                }
            }
            return std::string(s.substr(0, i));
        }
        // Float.plain(): a whole number without its ".0".
        std::string Plain(float v) {
            if (std::fmod(v, 1.0f) == 0.0f) return std::to_string(static_cast<long long>(v));
            char buf[32];
            std::snprintf(buf, sizeof buf, "%g", double(v));
            return buf;
        }
        std::string KindOf(Role role) {
            switch (role) {
                case Role::Button: return "button";
                case Role::Checkbox: return "checkbox";
                case Role::Switch: return "switch";
                case Role::RadioButton: return "radio";
                case Role::Tab: return "tab";
                case Role::Image: return "image";
                case Role::DropdownList: return "dropdown";
                case Role::ValuePicker: return "picker";
                case Role::Carousel: return "carousel";
                case Role::None: break;
            }
            return {};
        }

        std::optional<Element> ElementOf(const SemanticsNode& node, int ox, int oy) {
            // Clipped to what is visible: a list item scrolled out of its viewport has nothing left to show.
            const Rect& box = node.boundsInRoot;
            if (box.Width() <= 0 || box.Height() <= 0) return std::nullopt;
            const SemanticsConfiguration& c = node.config;
            std::vector<std::string> texts;
            auto add = [&](std::string_view s) {
                std::string t = Trim(s);
                if (!t.empty() && std::find(texts.begin(), texts.end(), t) == texts.end()) texts.push_back(std::move(t));
            };
            for (const auto& t : c.text) add(t);
            for (const auto& d : c.contentDescription) add(d);

            const auto& range = c.progressBarRangeInfo;
            const auto& toggle = c.toggleableState;
            std::string kind;
            if (c.role != Role::None) kind = KindOf(c.role);
            else if (c.setText) kind = "field";
            else if (c.setProgress) kind = "slider";
            else if (range) kind = "progress";
            else if (toggle) kind = "toggle";
            else if (c.onClick) kind = "clickable";
            else if (!c.text.empty()) kind = "text";
            else if (!texts.empty()) kind = "icon";
            else return std::nullopt;

            // A spinner reports a range with nothing in it.
            const bool spinning = range && (range->indeterminate || range->start == range->end);
            Element e;
            e.kind = std::move(kind);
            e.texts = std::move(texts);
            if (c.testTag) e.names.push_back(*c.testTag);
            if (toggle) e.flags.push_back(*toggle == 1 ? "on" : *toggle == 0 ? "off" : "mixed");
            if (c.selected.value_or(false)) e.flags.push_back("selected");
            if (c.focused.value_or(false)) e.flags.push_back("focused");
            if (range) e.flags.push_back(spinning ? std::string("indeterminate") : Plain(range->start) + ".." + Plain(range->end));
            if (c.disabled) e.flags.push_back("disabled");
            if (c.error) e.flags.push_back("error: " + *c.error);
            if (c.editableText) e.value = c.password ? [&] {
                std::string dots;
                for (std::size_t i = CodePoints(*c.editableText); i > 0; --i) dots += "•";
                return dots;
            }() : *c.editableText;
            else if (range && !spinning) e.value = Plain(range->current);
            e.left = int(box.left) + ox;
            e.top = int(box.top) + oy;
            e.right = int(box.right) + ox;
            e.bottom = int(box.bottom) + oy;
            e.enabled = !c.disabled;
            e.config = c;
            return e;
        }

        int CenterX(const Element& e) { return (e.left + e.right) >> 1; }
        int CenterY(const Element& e) { return (e.top + e.bottom) >> 1; }
        bool Focused(const Element& e) { return std::find(e.flags.begin(), e.flags.end(), "focused") != e.flags.end(); }

        bool AnswersTo(const Element& e, std::string_view query, bool exact) {
            const std::string wanted = Trim(query);
            if (wanted.empty()) return false;
            auto hit = [&](const std::string& s) { return exact ? EqualsIgnoreCase(s, wanted) : ContainsIgnoreCase(s, wanted); };
            return std::any_of(e.texts.begin(), e.texts.end(), hit) || std::any_of(e.names.begin(), e.names.end(), hit);
        }

        std::string Quoted(std::string_view text) {
            std::string line(text);
            std::replace(line.begin(), line.end(), '\n', ' ');
            if (CodePoints(line) > kMaxShown) line = TakeCodePoints(line, kMaxShown - 1) + "…";
            return "\"" + line + "\"";
        }
        std::string Label(const Element& e, const std::vector<std::string>& shown) {
            std::string out;
            if (shown.empty()) out = "(unlabeled)";
            else {
                std::string joined;
                for (std::size_t i = 0; i < shown.size(); ++i) joined += (i ? " · " : "") + shown[i];
                out = Quoted(joined);
            }
            if (!e.names.empty()) {
                out += " [";
                for (std::size_t i = 0; i < e.names.size(); ++i) out += (i ? ", " : "") + e.names[i];
                out += ']';
            }
            return out;
        }
        // `button "Retry"`: enough to say which one was meant.
        std::string Summary(const Element& e) {
            std::vector<std::string> first;
            if (!e.texts.empty()) first.push_back(e.texts.front());
            return e.kind + " " + Label(e, first);
        }
        // One line of `ui`: what it is, what it says, its state and where its middle is.
        std::string Describe(const Element& e) {
            std::string out = e.kind;
            if (out.size() < kKindWidth) out.append(kKindWidth - out.size(), ' ');
            out += ' ' + Label(e, e.texts);
            if (e.value) out += " = " + Quoted(*e.value);
            for (const auto& f : e.flags) out += "  " + f;
            out += "  @" + std::to_string(CenterX(e)) + "," + std::to_string(CenterY(e));
            return out;
        }

        struct Shown {
            std::string name;
            std::vector<Element> elements;
        };
        // Every window with something in it, the one on top first.
        std::vector<Shown> Windows(const std::vector<Window>& windows) {
            std::vector<Shown> out;
            for (auto it = windows.rbegin(); it != windows.rend(); ++it) {
                auto elements = ElementsOf(*it);
                if (!elements.empty()) out.push_back({ it->name, std::move(elements) });
            }
            return out;
        }

        struct Refused { std::string message; };

        struct Lookup {
            std::vector<const Element*> found;
            std::string query;
            std::optional<int> index;
        };

        std::optional<std::vector<const Element*>> Matches(const std::vector<Shown>& windows, const std::string& query) {
            for (const auto& w : windows) {
                for (const bool exact : { true, false }) {
                    std::vector<const Element*> hit;
                    for (const auto& e : w.elements)
                        if (AnswersTo(e, query, exact)) hit.push_back(&e);
                    if (!hit.empty()) return hit;
                }
            }
            return std::nullopt;
        }

        // The window on top is asked first and a window that answers settles it; within a window
        // an exact label beats one that only contains the query. A trailing `#2` is an index only
        // when the whole query matches nothing; a kind names every element of that kind.
        Lookup Find(const std::vector<Shown>& windows, const std::string& query) {
            std::vector<std::pair<std::string, std::optional<int>>> attempts{ { query, std::nullopt } };
            // name#2
            if (const auto hash = query.rfind('#'); hash != std::string::npos && hash + 1 < query.size() &&
                std::all_of(query.begin() + std::ptrdiff_t(hash + 1), query.end(), [](char ch) { return ch >= '0' && ch <= '9'; }) &&
                query.size() - hash - 1 <= 9)
                attempts.push_back({ query.substr(0, hash), std::stoi(query.substr(hash + 1)) });
            for (const auto& [name, index] : attempts)
                if (auto hit = Matches(windows, name)) return { *hit, name, index };
            for (const auto& [kind, index] : attempts) {
                for (const auto& w : windows) {
                    std::vector<const Element*> hit;
                    for (const auto& e : w.elements)
                        if (EqualsIgnoreCase(e.kind, Trim(kind))) hit.push_back(&e);
                    if (!hit.empty()) return { hit, kind, index };
                }
            }
            throw Refused{ "nothing on screen is called '" + query + "' - try ui" };
        }

        template<typename Can>
        const Element& Pick(const std::vector<Shown>& windows, const std::string& query, const std::string& verb, Can can) {
            const Lookup l = Find(windows, query);
            const Element* chosen = nullptr;
            if (l.index) {
                if (*l.index < 1 || *l.index > int(l.found.size()))
                    throw Refused{ "only " + std::to_string(l.found.size()) + " match '" + l.query + "'" };
                chosen = l.found[std::size_t(*l.index - 1)];
            } else {
                std::vector<const Element*> usable;
                for (const Element* e : l.found)
                    if (can(*e)) usable.push_back(e);
                if (usable.empty()) chosen = l.found.front();
                else if (usable.size() == 1) chosen = usable.front();
                else {
                    std::string msg = std::to_string(usable.size()) + " match '" + l.query + "' - say which:";
                    for (std::size_t i = 0; i < l.found.size(); ++i)
                        if (can(*l.found[i])) msg += "\n  " + l.query + "#" + std::to_string(i + 1) + "  " + Describe(*l.found[i]);
                    throw Refused{ msg };
                }
            }
            if (!can(*chosen)) throw Refused{ Summary(*chosen) + " cannot be " + verb };
            if (!chosen->enabled) throw Refused{ Summary(*chosen) + " is disabled" };
            return *chosen;
        }

        const Element* FocusedElement(const std::vector<Shown>& windows) {
            for (const auto& w : windows)
                for (auto it = w.elements.rbegin(); it != w.elements.rend(); ++it)
                    if (Focused(*it)) return &*it;
            return nullptr;
        }
        bool CanFill(const Element& e) { return bool(e.config.setText) || bool(e.config.setProgress); }
        const Element& FocusedField(const std::vector<Shown>& windows) {
            const Element* f = FocusedElement(windows);
            if (!f || !CanFill(*f)) throw Refused{ "no field has focus - name one" };
            return *f;
        }

        std::string List(const std::vector<Shown>& windows) {
            if (windows.empty()) return "error: nothing on screen";
            std::string out;
            for (std::size_t i = 0; i < windows.size(); ++i) {
                if (windows.size() > 1) {
                    out += "-- " + windows[i].name;
                    if (i == 0) out += " (on top)";
                    out += '\n';
                }
                for (const auto& e : windows[i].elements) out += Describe(e) + '\n';
            }
            while (!out.empty() && out.back() == '\n') out.pop_back();
            return out;
        }

        std::string FindAnswer(const std::vector<Shown>& windows, const std::string& query) {
            const Lookup l = Find(windows, query);
            if (l.index) {
                if (*l.index < 1 || *l.index > int(l.found.size()))
                    throw Refused{ "only " + std::to_string(l.found.size()) + " match '" + l.query + "'" };
                return Describe(*l.found[std::size_t(*l.index - 1)]);
            }
            if (l.found.size() == 1) return Describe(*l.found.front());
            std::string out;
            for (std::size_t i = 0; i < l.found.size(); ++i)
                out += (i ? "\n" : "") + l.query + "#" + std::to_string(i + 1) + "  " + Describe(*l.found[i]);
            return out;
        }

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

        const char* const kHelp =
            "help          what this app answers\n"
            "destinations  where it can be told to go\n"
            "where         what is on screen, and whether it is ready to be driven\n"
            "check <cond>  whether where says so: route=details, stack~Settings\n"
            "go <name>     open a destination, from wherever the app is\n"
            "ui            every button, field, switch and line of text on screen\n"
            "find <name>   the element a name picks, and where it is\n"
            "click <name>  click it - longclick for a long press\n"
            "fill <name>   set a field's text: <name>, a unit separator (0x1f), then the text\n"
            "submit <name> run a field's keyboard action - search, go, done";

        // Registered adapters, oldest first.
        struct Registered { std::uint64_t id; Adapter adapter; };
        std::vector<Registered>& Adapters() { static std::vector<Registered> a; return a; }

        std::string Number(double d) {
            if (std::isfinite(d) && std::fmod(d, 1.0) == 0.0 && std::fabs(d) < 1e15) return std::to_string(static_cast<long long>(d));
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.17g", d);
            return buf;
        }
        std::string Text(const Value& v) {
            if (std::holds_alternative<std::string>(v.v)) return std::get<std::string>(v.v);
            if (std::holds_alternative<double>(v.v)) return Number(std::get<double>(v.v));
            return ToJson(v);
        }

        // A condition: a path into where's answer, an operator, what it should be.
        struct Condition { std::string path, op, wanted; };
        std::optional<Condition> Split(std::string_view e) {
            for (const char* op : { ">=", "<=", "!=", "=", ">", "<", "~" }) {
                const auto at = e.find(op);
                // A path comes first, so an expression starting with the operator is not one.
                if (at == std::string_view::npos || at == 0) continue;
                std::string path = Trim(e.substr(0, at)), wanted = Trim(e.substr(at + std::string_view(op).size()));
                if (!path.empty() && !wanted.empty()) return Condition{ std::move(path), op, std::move(wanted) };
            }
            return std::nullopt;
        }
        // `route`, `stack.0`: keys through objects, indices through lists.
        const Value* Follow(const Value& state, const std::string& path) {
            const Value* here = &state;
            std::size_t start = 0;
            while (true) {
                const auto dot = path.find('.', start);
                const std::string step = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
                if (const auto* o = std::get_if<Object>(&here->v)) {
                    auto it = std::find_if(o->begin(), o->end(), [&](const auto& kv) { return kv.first == step; });
                    if (it == o->end() || std::holds_alternative<std::nullptr_t>(it->second.v)) return nullptr;
                    here = &it->second;
                } else if (const auto* l = std::get_if<std::vector<Value>>(&here->v)) {
                    char* end = nullptr;
                    const long i = std::strtol(step.c_str(), &end, 10);
                    if (step.empty() || *end || i < 0 || i >= long(l->size())) return nullptr;
                    here = &(*l)[std::size_t(i)];
                } else return nullptr;
                if (dot == std::string::npos) return here;
                start = dot + 1;
            }
        }
        int CompareIgnoreCase(std::string_view a, std::string_view b) { return Lower(a).compare(Lower(b)); }
        bool Holds(const Value& actual, const std::string& op, const std::string& wanted) {
            if (const auto* d = std::get_if<double>(&actual.v)) {
                char* end = nullptr;
                const double right = std::strtod(wanted.c_str(), &end);
                if (end && *end == '\0' && end != wanted.c_str()) {
                    const double left = *d;
                    if (op == "=") return left == right;
                    if (op == "!=") return left != right;
                    if (op == ">") return left > right;
                    if (op == "<") return left < right;
                    if (op == ">=") return left >= right;
                    if (op == "<=") return left <= right;
                    return ContainsIgnoreCase(Number(left), wanted);
                }
            }
            const std::string text = Text(actual);
            if (op == "=") return EqualsIgnoreCase(text, wanted);
            if (op == "!=") return !EqualsIgnoreCase(text, wanted);
            if (op == "~") {
                // A list holds what it holds; text contains what it contains.
                if (const auto* l = std::get_if<std::vector<Value>>(&actual.v))
                    return std::any_of(l->begin(), l->end(), [&](const Value& item) { return EqualsIgnoreCase(Text(item), wanted); });
                return ContainsIgnoreCase(text, wanted);
            }
            const int c = CompareIgnoreCase(text, wanted);
            if (op == ">") return c > 0;
            if (op == "<") return c < 0;
            if (op == ">=") return c >= 0;
            if (op == "<=") return c <= 0;
            return false;
        }

        Value State(const std::vector<Shown>& windows, const Context& context) {
            Object o{ { "activity", context.activity.empty() ? std::string("none") : context.activity },
                      { "ready", !context.activity.empty() } };
            if (const Element* f = FocusedElement(windows)) o.push_back({ "focused", Summary(*f) });
            for (const auto& r : Adapters()) {
                if (!r.adapter.state) continue;
                Object fields;
                r.adapter.state(fields);
                for (auto& [k, v] : fields) {
                    // A field with nothing to say says nothing.
                    if (std::holds_alternative<std::nullptr_t>(v.v)) continue;
                    auto it = std::find_if(o.begin(), o.end(), [&](const auto& kv) { return kv.first == k; });
                    if (it != o.end()) it->second = std::move(v);
                    else o.push_back({ k, std::move(v) });
                }
            }
            return o;
        }
    }

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
            a.erase(std::remove_if(a.begin(), a.end(), [id](const Registered& r) { return r.id == id; }), a.end());
        });
    }

    std::vector<Element> ElementsOf(const Window& window) {
        if (!window.owner) return {};
        std::vector<Element> found;
        for (const auto& node : GetAllSemanticsNodes(*window.owner))
            if (auto e = ElementOf(node, window.originX, window.originY)) found.push_back(std::move(*e));
        // Text drawn twice in one place - an outline under a fill - is one thing to anyone reading it.
        std::vector<Element> distinct;
        for (auto& e : found) {
            const bool seen = std::any_of(distinct.begin(), distinct.end(), [&](const Element& d) {
                return d.kind == e.kind && d.texts == e.texts &&
                       std::tie(d.left, d.top, d.right, d.bottom) == std::tie(e.left, e.top, e.right, e.bottom);
            });
            if (!seen) distinct.push_back(std::move(e));
        }
        // Top to bottom, then left to right along a row: middles within the slack of the row's first.
        const int slack = int(kRowSlackDp * window.owner->GetDensity().density);
        std::stable_sort(distinct.begin(), distinct.end(), [](const Element& a, const Element& b) { return CenterY(a) < CenterY(b); });
        std::vector<Element> ordered;
        ordered.reserve(distinct.size());
        for (std::size_t i = 0; i < distinct.size();) {
            std::size_t j = i + 1;
            while (j < distinct.size() && CenterY(distinct[j]) - CenterY(distinct[i]) <= slack) ++j;
            std::stable_sort(distinct.begin() + std::ptrdiff_t(i), distinct.begin() + std::ptrdiff_t(j),
                             [](const Element& a, const Element& b) { return a.left < b.left; });
            for (std::size_t k = i; k < j; ++k) ordered.push_back(std::move(distinct[k]));
            i = j;
        }
        return ordered;
    }

    std::string Call(const std::vector<Window>& windowList, const Context& context, std::string_view method,
                     std::string_view argumentView) {
        const std::string argument(argumentView);
        try {
            if (method == "help" || method == "commands") return kHelp;
            const auto windows = Windows(windowList);
            if (method == "where" || method == "state") return ToJson(State(windows, context));
            if (method == "check") {
                const auto c = Split(argument);
                if (!c) return "error: '" + argument + "' is not a condition - try route=home or stack~details";
                const Value state = State(windows, context);
                const Value* actual = Follow(state, c->path);
                if (!actual) return "error: nothing at '" + c->path + "' - where says " + ToJson(state);
                return (Holds(*actual, c->op, c->wanted) ? "ok: " : "error: ") + c->path + " is " + Text(*actual);
            }
            if (method == "destinations") {
                std::string out;
                for (auto it = Adapters().rbegin(); it != Adapters().rend(); ++it)
                    if (it->adapter.destinations)
                        for (const auto& line : it->adapter.destinations()) out += (out.empty() ? "" : "\n") + line;
                return out.empty() ? "error: this app has registered no destinations" : out;
            }
            if (method == "go") {
                if (Trim(argument).empty()) return "error: go needs a destination";
                const auto colon = argument.find(':');
                const std::string name = Trim(argument.substr(0, colon));
                const std::optional<std::string> arg = colon == std::string::npos ? std::nullopt
                                                                                   : std::optional<std::string>(Trim(argument.substr(colon + 1)));
                // The newest first: an inner NavHost before the one around it.
                const auto adapters = Adapters();
                for (auto it = adapters.rbegin(); it != adapters.rend(); ++it)
                    if (it->adapter.go)
                        if (auto answer = it->adapter.go(name, arg)) return *answer;
                return "error: no destination '" + name + "' - try destinations";
            }
            if (method == "ui") return List(windows);
            if (method == "find") return FindAnswer(windows, argument);
            if (method == "click" || method == "longclick") {
                const bool longPress = method == "longclick";
                const std::string verb = longPress ? "long-clicked" : "clicked";
                const Element& e = Pick(windows, argument, verb, [&](const Element& x) {
                    return longPress ? bool(x.config.onLongClick) : bool(x.config.onClick);
                });
                const bool done = longPress ? e.config.onLongClick() : e.config.onClick();
                return done ? "ok: " + verb + " " + Summary(e) : "error: " + Summary(e) + " did not take it";
            }
            if (method == "fill") {
                const auto sep = argument.find(kSeparator);
                if (sep == std::string::npos) return "error: fill needs a field and the text for it";
                const std::string query = argument.substr(0, sep), text = argument.substr(sep + 1);
                const Element& field = Trim(query).empty() ? FocusedField(windows) : Pick(windows, query, "filled", CanFill);
                bool done = false;
                if (field.config.setText) done = field.config.setText(text);
                else {
                    // A slider takes a number.
                    char* end = nullptr;
                    const float v = std::strtof(text.c_str(), &end);
                    done = end && end != text.c_str() && *end == '\0' && field.config.setProgress(v);
                }
                return done ? "ok: filled " + Summary(field) : "error: " + Summary(field) + " refused '" + text + "'";
            }
            if (method == "submit") {
                const Element& field = Trim(argument).empty() ? FocusedField(windows)
                                                              : Pick(windows, argument, "submitted", [](const Element& x) { return bool(x.config.onImeAction); });
                if (!field.config.onImeAction) return "error: " + Summary(field) + " has nothing to submit";
                return field.config.onImeAction() ? "ok: submitted " + Summary(field) : "error: " + Summary(field) + " did not submit";
            }
            return "error: no command '" + std::string(method) + "' - try help";
        } catch (const Refused& r) {
            return "error: " + r.message;
        }
    }

}
