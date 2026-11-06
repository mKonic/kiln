#include "kiln/foundation/Scroll.h"
#include "kiln/ui/Locals.h"

#include <cmath>

namespace kiln::foundation {

    namespace {
        struct NestedParams {
            std::shared_ptr<NestedScrollConnection> connection;
            bool operator==(const NestedParams& o) const { return connection == o.connection; }
        };
        struct ConnectionNode final : ui::NestedScrollNode {
            NestedParams params;
            explicit ConnectionNode(NestedParams p) : params(std::move(p)) { connection = params.connection.get(); }
            void OnParamsChanged() { connection = params.connection.get(); }
        };

        // NestedScrollInteropConnection (NestedScrollInteropConnection.android.kt).
        constexpr float kAxesThreshold = 0.5f;   // ScrollingAxesThreshold

        int Axes(Offset o) {
            int axes = 0;
            if (std::abs(o.x) >= kAxesThreshold) axes |= 1;
            if (std::abs(o.y) >= kAxesThreshold) axes |= 2;
            return axes;
        }
        // View pixels are whole and point the other way.
        int ToView(float v) { return -int(std::lround(v)); }
        float Reverse(int v) { return -float(v); }

        // toOffset: what the View side consumed, back in Compose terms, bounded by `available`;
        // the rounding overflow counts as consumed when anything was.
        Offset ToOffset(int dx, int dy, const int consumed[2], Offset available) {
            const float overflowX = consumed[0] == 0 ? 0.0f : available.x - Reverse(dx);
            const float overflowY = consumed[1] == 0 ? 0.0f : available.y - Reverse(dy);
            const float x = Reverse(consumed[0]) + overflowX, y = Reverse(consumed[1]) + overflowY;
            return { available.x >= 0 ? std::min(x, available.x) : std::max(x, available.x),
                     available.y >= 0 ? std::min(y, available.y) : std::max(y, available.y) };
        }

        class InteropConnection final : public NestedScrollConnection {
        public:
            explicit InteropConnection(ui::HostNestedScrollParent* host) : m_Host(host) {}
            Offset OnPreScroll(Offset available, NestedScrollSource source) override {
                const bool touch = source == NestedScrollSource::UserInput;
                if (!m_Host || !m_Host->Start(Axes(available), touch)) return {};
                int consumed[2] = { 0, 0 };
                const int dx = ToView(available.x), dy = ToView(available.y);
                m_Host->PreScroll(dx, dy, touch, consumed);
                return ToOffset(dx, dy, consumed, available);
            }
            Offset OnPostScroll(Offset consumedByChild, Offset available, NestedScrollSource source) override {
                const bool touch = source == NestedScrollSource::UserInput;
                if (!m_Host || !m_Host->Start(Axes(available), touch)) return {};
                int consumed[2] = { 0, 0 };
                const int dx = ToView(available.x), dy = ToView(available.y);
                m_Host->Scroll(ToView(consumedByChild.x), ToView(consumedByChild.y), dx, dy, touch, consumed);
                return ToOffset(dx, dy, consumed, available);
            }
            // View flings happen in the pre phase: a parent that pre-flings or flings takes it all.
            Offset OnPreFling(Offset available) override {
                if (!m_Host) return {};
                if (m_Host->PreFling(-available.x, -available.y)) return available;
                return m_Host->Fling(-available.x, -available.y, true) ? available : Offset{};
            }
            Offset OnPostFling(Offset, Offset) override {
                if (m_Host) m_Host->StopAll();
                return {};
            }
        private:
            ui::HostNestedScrollParent* m_Host;
        };
    }

    Modifier NestedScroll(Modifier m, std::shared_ptr<NestedScrollConnection> connection) {
        return m.With<ConnectionNode>(NestedParams{ std::move(connection) });
    }

    std::shared_ptr<NestedScrollConnection> RememberNestedScrollInteropConnection() {
        auto* c = rt::Composition::Current();
        ui::HostNestedScrollParent* host = c ? c->Read(ui::LocalHostNestedScrollParent) : nullptr;
        auto make = [host] { return std::shared_ptr<NestedScrollConnection>(std::make_shared<InteropConnection>(host)); };
        if (!c) return make();
        return c->Remember<std::shared_ptr<NestedScrollConnection>>(make, rt::Keys(reinterpret_cast<std::uintptr_t>(host)));
    }

}
