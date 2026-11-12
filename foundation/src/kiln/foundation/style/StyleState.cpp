#include "kiln/foundation/style/StyleState.h"

#include "kiln/runtime/Composition.h"

#include <algorithm>
#include <vector>

namespace kiln::foundation {

    namespace {
        class BooleanPredefinedKey final : public StyleStateKey<bool> {
        public:
            BooleanPredefinedKey(int mask, bool defaultValue) : StyleStateKey<bool>(defaultValue), m_Mask(mask) {}
            bool GetValueFrom(const MutableStyleState& s) const override { return s.Flag(m_Mask); }
            void SetValueTo(bool v, MutableStyleState& s) const override { s.UpdateFlags(m_Mask, v ? m_Mask : 0); }
            bool IsPredefined() const override { return true; }
        private:
            int m_Mask;
        };

        class ToggleKey final : public StyleStateKey<ui::ToggleableState> {
        public:
            ToggleKey() : StyleStateKey<ui::ToggleableState>(ui::ToggleableState::Off) {}
            ui::ToggleableState GetValueFrom(const MutableStyleState& s) const override { return s.TriStateToggle(); }
            void SetValueTo(ui::ToggleableState v, MutableStyleState& s) const override { s.SetTriStateToggle(v); }
            bool IsPredefined() const override { return true; }
        };
    }

    const StyleStateKey<bool>& StyleStateKeys::Pressed() { static const BooleanPredefinedKey k(MutableStyleState::kPressed, false); return k; }
    const StyleStateKey<bool>& StyleStateKeys::Hovered() { static const BooleanPredefinedKey k(MutableStyleState::kHovered, false); return k; }
    const StyleStateKey<bool>& StyleStateKeys::Focused() { static const BooleanPredefinedKey k(MutableStyleState::kFocused, false); return k; }
    const StyleStateKey<bool>& StyleStateKeys::Selected() { static const BooleanPredefinedKey k(MutableStyleState::kSelected, false); return k; }
    const StyleStateKey<bool>& StyleStateKeys::Enabled() { static const BooleanPredefinedKey k(MutableStyleState::kEnabled, true); return k; }
    const StyleStateKey<ui::ToggleableState>& StyleStateKeys::Toggle() { static const ToggleKey k; return k; }

    ui::ToggleableState MutableStyleState::TriStateToggle() const {
        switch (Flags() & kToggleMask) {
            case kToggleOn: return ui::ToggleableState::On;
            case 0: return ui::ToggleableState::Off;
            default: return ui::ToggleableState::Indeterminate;
        }
    }

    void MutableStyleState::SetTriStateToggle(ui::ToggleableState v) {
        UpdateFlags(kToggleMask, v == ui::ToggleableState::On ? kToggleOn : v == ui::ToggleableState::Off ? 0 : kToggleIndeterminate);
    }

    void MutableStyleState::UpdateFlags(int mask, int values) {
        const int current = m_Flags.Peek();
        const int next = (current & ~mask) | values;
        if (current != next) m_Flags.Set(next);
    }

    void MutableStyleState::Remove(const StyleStateKeyBase& key) {
        if (key.IsPredefined()) throw std::logic_error("Cannot remove an internal StyleStateKey");
        if (!m_Custom.Peek().contains(&key)) return;
        m_Custom.Update([&](auto& map) { map.erase(&key); });
    }

    std::function<void()> MutableStyleState::ProcessInteractions(const std::shared_ptr<MutableInteractionSource>& source) {
        if (!source) return [] {};
        // Interactions still in progress, by kind (a press ends with its release or cancel).
        struct Tracker {
            std::vector<std::uint64_t> pressed, hovered, focused;
        };
        auto tracker = std::make_shared<Tracker>();
        SetPressed(false);
        SetHovered(false);
        SetFocused(false);
        const std::uint64_t token = source->Subscribe([this, tracker](const Interaction& i) {
            auto track = [&](std::vector<std::uint64_t>& set) {
                if (i.phase == Interaction::Phase::Start) {
                    if (std::find(set.begin(), set.end(), i.id) == set.end()) set.push_back(i.id);
                } else {
                    std::erase(set, i.id);
                }
                return !set.empty();
            };
            switch (i.kind) {
                case Interaction::Kind::Press: SetPressed(track(tracker->pressed)); break;
                case Interaction::Kind::Hover: SetHovered(track(tracker->hovered)); break;
                case Interaction::Kind::Focus: SetFocused(track(tracker->focused)); break;
                default: {
                    // Other interactions go to the custom keys.
                    std::vector<const StyleStateKeyBase*> keys;
                    for (const auto& [k, v] : m_Custom.Peek()) keys.push_back(k);
                    for (const StyleStateKeyBase* k : keys) k->ProcessInteraction(i, *this);
                    break;
                }
            }
        });
        std::weak_ptr<MutableInteractionSource> weak = source;
        return [weak, token] {
            if (auto s = weak.lock()) s->Unsubscribe(token);
        };
    }

    std::shared_ptr<MutableStyleState> RememberUpdatedStyleState(std::shared_ptr<MutableInteractionSource> interactionSource,
                                                                 const std::function<void(MutableStyleState&)>& block) {
        std::shared_ptr<MutableStyleState> state;
        if (auto* c = rt::Composition::Current()) {
            // remember(interactionSource): a new state for a new source.
            struct Slot {
                std::shared_ptr<MutableInteractionSource> source;
                std::shared_ptr<MutableStyleState> state;
            };
            auto& slot = c->Remember<Slot>([&] { return Slot{ interactionSource, std::make_shared<MutableStyleState>(interactionSource) }; });
            if (slot.source != interactionSource) slot = Slot{ interactionSource, std::make_shared<MutableStyleState>(interactionSource) };
            state = slot.state;
        } else {
            state = std::make_shared<MutableStyleState>(std::move(interactionSource));
        }
        if (block) block(*state);
        return state;
    }

}
