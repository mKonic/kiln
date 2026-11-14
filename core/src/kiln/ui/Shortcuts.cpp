#include "kiln/ui/Shortcuts.h"

namespace kiln::ui {

    namespace {
        ShortcutHost& Host() { static auto* h = new ShortcutHost; return *h; }
        std::vector<std::string> Run(ShortcutOp op, const std::vector<Shortcut>& s = {}, const std::vector<std::string>& ids = {}) {
            return Host() ? Host()(op, s, ids) : std::vector<std::string>{};
        }
        bool Ok(const std::vector<std::string>& r) { return !r.empty() && r.front() == "ok"; }
    }

    bool SetDynamicShortcuts(const std::vector<Shortcut>& s) { return Ok(Run(ShortcutOp::Set, s)); }
    bool PushDynamicShortcut(const Shortcut& s) { return Ok(Run(ShortcutOp::Push, { s })); }
    void RemoveDynamicShortcuts(const std::vector<std::string>& ids) { Run(ShortcutOp::Remove, {}, ids); }
    void RemoveAllDynamicShortcuts() { Run(ShortcutOp::RemoveAll); }
    void ReportShortcutUsed(const std::string& id) { Run(ShortcutOp::ReportUsed, {}, { id }); }
    bool RequestPinShortcut(const Shortcut& s) { return Ok(Run(ShortcutOp::RequestPin, { s })); }
    std::vector<std::string> DynamicShortcutIds() { return Run(ShortcutOp::List); }
    void SetShortcutHost(ShortcutHost host) { Host() = std::move(host); }

}
