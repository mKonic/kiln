#pragma once
// Volition's protocol (github.com/mKonic/volition), answered natively from the semantics tree:
// debug builds take `adb shell content call --uri content://<package>.volition --method ui`
// and the rest of Volition's commands, so its CLI and MCP tools drive a kiln app by name.
//
// The elements are what Volition's ComposeElements reads from a Compose root (the merged
// semantics tree: a button's text belongs to the button), listed and matched the way its
// Elements does, so answers read the same for a kiln app as for a Compose one.
#include <kiln/Function.h>
#include <kiln/ui/Semantics.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace kiln::ui {
    class Owner;
}

namespace kiln::ui::volition {

    // One of the app's windows, bottom to top as the window manager stacks them.
    struct Window {
        std::string name;           // an activity's name, "dialog" or "popup"
        Owner* owner = nullptr;
        int originX = 0, originY = 0;   // the window's content on screen, in pixels
    };

    // What the app shows besides its windows: `where` reports these.
    struct Context {
        std::string activity;       // the resumed activity's simple name, empty for none
    };

    // `where`'s answer, as JSON: null, a boolean, a number, text, a list or an object.
    struct Value;
    using Object = std::vector<std::pair<std::string, Value>>;
    struct Value {
        std::variant<std::nullptr_t, bool, double, std::string, std::vector<Value>, Object> v = nullptr;
        Value() = default;
        Value(std::nullptr_t) {}
        Value(bool b) : v(b) {}
        Value(int i) : v(double(i)) {}
        Value(double d) : v(d) {}
        Value(std::string s) : v(std::move(s)) {}
        Value(const char* s) : v(std::string(s)) {}
        Value(std::vector<Value> list) : v(std::move(list)) {}
        Value(Object object) : v(std::move(object)) {}
    };
    std::string ToJson(const Value& value);

    // What the app says about itself and where it can be sent (Volition's state and destination
    // registrations): a NavHost adds its route, previous route and back stack, and `go`es to
    // its routes. Registered while the handle lives; the newest is asked first.
    struct Adapter {
        // Fields for `where` (a later adapter's field replaces an earlier one's).
        kiln::Function<void(Object&)> state;
        // `destinations`: one line each, `name` or `name:<argument>`, then two spaces and what it does.
        kiln::Function<std::vector<std::string>()> destinations;
        // `go name[:argument]`: the answer, or nothing when the name is not one of its destinations.
        kiln::Function<std::optional<std::string>(const std::string& name, const std::optional<std::string>& argument)> go;
    };
    [[nodiscard]] std::shared_ptr<void> Register(Adapter adapter);

    // Something on screen a caller can name.
    struct Element {
        std::string kind;
        std::vector<std::string> texts;   // what it says, its label first
        std::vector<std::string> names;   // test tag
        std::optional<std::string> value;
        std::vector<std::string> flags;
        int left = 0, top = 0, right = 0, bottom = 0;   // on screen, pixels
        bool enabled = true;
        SemanticsConfiguration config;    // its actions
    };

    // A window's elements in reading order (top to bottom, rows left to right).
    std::vector<Element> ElementsOf(const Window& window);

    // Separates a field's name from its text in fill's single argument.
    inline constexpr char kSeparator = '\x1f';

    // Runs one command: help, where / state, check, destinations, go, ui, find, click, longclick,
    // fill, submit. A failure is an answer starting with "error:".
    std::string Call(const std::vector<Window>& windows, const Context& context, std::string_view method,
                     std::string_view argument);

}
