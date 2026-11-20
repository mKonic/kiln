#include "kiln/ui/Shortcuts.h"

namespace kiln::ui {

    namespace {
        ShortcutHost& Host() { static auto* h = new ShortcutHost; return *h; }
        // ShortcutInfoCompat.Builder.build: a conversation is long-lived, its id its locus id.
        Shortcut Built(Shortcut s) {
            if (s.isConversation) {
                if (s.locusId.empty()) s.locusId = s.id;
                s.longLived = true;
            }
            return s;
        }
        std::vector<std::string> Run(ShortcutOp op, const std::vector<Shortcut>& s = {}, const std::vector<std::string>& ids = {}) {
            if (!Host()) return {};
            std::vector<Shortcut> built;
            built.reserve(s.size());
            for (const Shortcut& one : s) built.push_back(Built(one));
            return Host()(op, built, ids);
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
    void RemoveLongLivedShortcuts(const std::vector<std::string>& ids) { Run(ShortcutOp::RemoveLongLived, {}, ids); }
    void SetShortcutHost(ShortcutHost host) { Host() = std::move(host); }

}
