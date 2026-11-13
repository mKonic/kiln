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

#include <optional>
#include <string>
#include <string_view>
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

    // Runs one command: help, where / state, ui, find, click, longclick, fill, submit. A failure
    // is an answer starting with "error:".
    std::string Call(const std::vector<Window>& windows, const Context& context, std::string_view method,
                     std::string_view argument);

}
