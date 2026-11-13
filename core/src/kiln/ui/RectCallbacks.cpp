#include "kiln/ui/RectCallbacks.h"
#include <kiln/Function.h>

#include "kiln/ui/LayoutNode.h"

#include <algorithm>
#include <climits>
#include <cmath>

namespace kiln::ui {

    struct RectRegistry::Entry {
        LayoutNode* node = nullptr;
        std::weak_ptr<const char> alive;
        bool global = false;
        std::int64_t throttle = 0, debounce = 0;
        kiln::Function<void(const RelativeLayoutBounds&)> callback;
        IntRect rect;
        bool hasRect = false;
        std::int64_t lastInvoke = LLONG_MIN, lastUninvoked = -1;
        bool removed = false;
    };

    namespace {
        std::int64_t NowMillis() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(rt::Dispatcher::Main().Now().time_since_epoch()).count();
        }
        IntRect RectOf(const LayoutNode& n) {
            const Rect r = n.OuterCoordinates().BoundsInRoot();
            return { int(std::lround(r.left)), int(std::lround(r.top)), int(std::lround(r.right)), int(std::lround(r.bottom)) };
        }
        float FractionInside(const IntRect& e, int left, int top, int right, int bottom) {
            const int w = std::min(e.right, right) - std::max(e.left, left);
            const int h = std::min(e.bottom, bottom) - std::max(e.top, top);
            if (w <= 0 || h <= 0) return 0;
            const long long clipped = (long long)w * h;
            const long long maxArea = std::min((long long)(right - left) * (bottom - top), (long long)e.Width() * e.Height());
            if (clipped == maxArea) return 1;
            return float(clipped) / float(maxArea);
        }
    }

    float RelativeLayoutBounds::FractionVisibleInRect(int left, int top, int right, int bottom) const { return FractionInside(m_Bounds, left, top, right, bottom); }
    float RelativeLayoutBounds::FractionVisibleIn(const RelativeLayoutBounds& v) const {
        return FractionInside(m_Bounds, v.m_Bounds.left, v.m_Bounds.top, v.m_Bounds.right, v.m_Bounds.bottom);
    }

    std::vector<IntRect> RelativeLayoutBounds::CalculateOcclusions() const {
        // RectManager.isTargetDrawnFirst: below the common ancestor, the branch with the lower
        // zIndex (then place order) draws first; ancestors and descendants never occlude.
        std::vector<IntRect> out;
        if (!m_Node || !m_Node->IsPlaced()) return out;
        const LayoutNode* root = m_Node;
        while (root->Parent()) root = root->Parent();
        auto drawnFirst = [](const LayoutNode* a, const LayoutNode* b) {
            if (a->Depth() == 0 || b->Depth() == 0) return false;
            while (a->Depth() > b->Depth()) a = a->Parent();
            if (a == b) return false;
            while (b->Depth() > a->Depth()) b = b->Parent();
            if (a == b) return false;
            const LayoutNode *la = a, *lb = b;
            while (a != b) { la = a; lb = b; a = a->Parent(); b = b->Parent(); if (!a || !b) return false; }
            return la->ZIndex() == lb->ZIndex() ? la->PlaceOrder() < lb->PlaceOrder() : la->ZIndex() < lb->ZIndex();
        };
        kiln::Function<void(const LayoutNode*)> walk = [&](const LayoutNode* n) {
            if (n != m_Node && n->IsPlaced() && drawnFirst(m_Node, n)) {
                const IntRect r = RectOf(*n);
                const IntRect c{ std::max(r.left, m_Bounds.left), std::max(r.top, m_Bounds.top), std::min(r.right, m_Bounds.right), std::min(r.bottom, m_Bounds.bottom) };
                if (!c.Empty()) out.push_back(c);
            }
            for (const LayoutNode* ch : n->Children()) walk(ch);
        };
        walk(root);
        return out;
    }

    RegistrationHandle RectRegistry::Register(const ModifierNode& node, bool global, std::int64_t throttle, std::int64_t debounce,
                                              kiln::Function<void(const RelativeLayoutBounds&)> callback) {
        auto e = std::make_shared<Entry>();
        e->node = node.Owner();
        e->alive = node.AliveToken();
        e->global = global;
        e->throttle = throttle;
        e->debounce = debounce == 0 ? throttle : debounce;
        e->callback = std::move(callback);
        m_Entries.push_back(e);
        std::weak_ptr<Entry> we = e;
        std::weak_ptr<char> alive = m_Alive;
        return RegistrationHandle([this, we, alive] {
            if (alive.expired()) return;
            if (auto e = we.lock()) { e->removed = true; std::erase(m_Entries, e); }
        });
    }

    void RectRegistry::Fire(Owner& owner, Entry& e, std::int64_t now) {
        e.lastUninvoked = -1;
        e.lastInvoke = now;
        e.callback(RelativeLayoutBounds(e.rect, owner.WindowSize(), m_Screen, e.node));
    }

    void RectRegistry::Schedule(Owner& owner, std::int64_t deadline) {
        if (m_Deadline > 0 && m_Deadline <= deadline) return;
        m_Deadline = deadline;
        std::weak_ptr<char> alive = m_Alive;
        Owner* o = &owner;
        m_Timer = rt::Launch(rt::Dispatcher::Main(), [](RectRegistry* self, Owner* o, std::weak_ptr<char> alive, std::int64_t wait) -> rt::Task<> {
            co_await rt::Delay(wait);
            if (!alive.expired()) self->TriggerDebounced(*o);
        }(this, o, alive, std::max<std::int64_t>(0, deadline - NowMillis())));
    }

    void RectRegistry::TriggerDebounced(Owner& owner) {
        const std::int64_t now = NowMillis();
        if (m_Deadline > now) return;
        m_Deadline = -1;
        std::int64_t next = LLONG_MAX;
        auto entries = m_Entries;
        for (auto& e : entries) {
            if (e->removed || e->lastUninvoked < 0) continue;
            const std::int64_t due = e->lastUninvoked + e->debounce;
            if (due <= now) Fire(owner, *e, now);
            else next = std::min(next, due);
        }
        if (next != LLONG_MAX) Schedule(owner, next);
    }

    void RectRegistry::Dispatch(Owner& owner, bool laidOut) {
        const std::int64_t now = NowMillis();
        std::erase_if(m_Entries, [](const auto& e) { return e->alive.expired(); });
        auto entries = m_Entries;
        std::int64_t next = LLONG_MAX;
        for (auto& e : entries) {
            if (e->removed || !e->node || !e->node->IsPlaced()) continue;
            const IntRect r = RectOf(*e->node);
            const bool fresh = e->lastInvoke == LLONG_MIN && e->lastUninvoked < 0;
            const bool changed = !e->hasRect || !(r == e->rect);
            if (!fresh && !changed && !(e->global && laidOut)) continue;
            e->rect = r;
            e->hasRect = true;
            // ThrottledCallbacks.fireWithUpdatedRect.
            const bool pastThrottle = e->lastInvoke == LLONG_MIN || now - e->lastInvoke >= e->throttle;
            const bool zeroDebounce = e->debounce == 0, zeroThrottle = e->throttle == 0;
            const bool canInvoke = zeroDebounce || !zeroThrottle;
            // forEachNewCallbackNeverInvoked: a new registration hears at once.
            if ((pastThrottle && canInvoke) || fresh) Fire(owner, *e, now);
            else if (!zeroDebounce) { e->lastUninvoked = now; next = std::min(next, now + e->debounce); }
        }
        if (next != LLONG_MAX) Schedule(owner, next);
    }

    RegistrationHandle RegisterOnLayoutRectChanged(const ModifierNode& node, std::int64_t throttle, std::int64_t debounce,
                                                   kiln::Function<void(const RelativeLayoutBounds&)> callback) {
        Owner* o = node.Owner() ? node.Owner()->GetOwner() : nullptr;
        if (!o) return {};
        return o->Rects().Register(node, false, throttle, debounce, std::move(callback));
    }
    RegistrationHandle RegisterOnGlobalLayoutListener(const ModifierNode& node, std::int64_t throttle, std::int64_t debounce,
                                                      kiln::Function<void(const RelativeLayoutBounds&)> callback) {
        Owner* o = node.Owner() ? node.Owner()->GetOwner() : nullptr;
        if (!o) return {};
        return o->Rects().Register(node, true, throttle, debounce, std::move(callback));
    }

}
