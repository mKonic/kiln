#pragma once

// A layout's configuration as functions run at measure time (FlexBoxConfig, FlexConfig and
// Grid's config block), so they can read the constraints.

#include <functional>
#include <memory>
#include <type_traits>
#include <vector>

namespace kiln::foundation {

    // A config: functions applied in order to a scope (FlexBoxConfig, FlexConfig, GridConfig). Equal when
    // made of the same functions (copies of one config).
    template<typename Scope>
    class LayoutConfig {
    public:
        using Fn = std::function<void(Scope&)>;
        LayoutConfig() = default;   // the empty config
        template<typename F, typename = std::enable_if_t<std::is_invocable_v<F&, Scope&> && !std::is_same_v<std::decay_t<F>, LayoutConfig>>>
        LayoutConfig(F fn) { m_Parts.push_back(std::make_shared<const Fn>(std::move(fn))); }
        // then: this, then `other`.
        LayoutConfig Then(const LayoutConfig& other) const {
            LayoutConfig r = *this;
            r.m_Parts.insert(r.m_Parts.end(), other.m_Parts.begin(), other.m_Parts.end());
            return r;
        }
        void Configure(Scope& s) const { for (const auto& p : m_Parts) (*p)(s); }
        bool Empty() const { return m_Parts.empty(); }
        bool operator==(const LayoutConfig& o) const { return m_Parts == o.m_Parts; }

    private:
        std::vector<std::shared_ptr<const Fn>> m_Parts;
    };

}
