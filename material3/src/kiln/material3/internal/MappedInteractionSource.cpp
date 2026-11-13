#include "kiln/material3/internal/MappedInteractionSource.h"
#include <kiln/Function.h>

#include <unordered_map>

namespace kiln::material3::internal {

    using foundation::Interaction;
    using foundation::MutableInteractionSource;

    namespace {
        struct Mapping {
            std::shared_ptr<MutableInteractionSource> mapped = std::make_shared<MutableInteractionSource>();
            kiln::Function<ui::Offset()> delta;
            std::unordered_map<std::uint64_t, std::uint64_t> ids;   // the source's id -> the mapped one
        };
    }

    std::shared_ptr<MutableInteractionSource> RememberMappedInteractionSource(const std::shared_ptr<MutableInteractionSource>& source,
                                                                               kiln::Function<ui::Offset()> delta) {
        auto& c = *rt::Composition::Current();
        auto& mapping = c.Remember<std::shared_ptr<Mapping>>([] { return std::make_shared<Mapping>(); },
                                                             rt::Keys(std::uintptr_t(source.get())));
        mapping->delta = std::move(delta);
        const auto m = mapping;
        c.DisposableEffect(rt::Keys(std::uintptr_t(source.get())), [source, m]() -> kiln::Function<void()> {
            const auto token = source->Subscribe([m](const Interaction& i) {
                if (i.phase == Interaction::Phase::Start) {
                    const ui::Offset at = i.kind == Interaction::Kind::Press && m->delta ? i.position - m->delta() : i.position;
                    m->ids[i.id] = m->mapped->Start(i.kind, at);
                    return;
                }
                const auto it = m->ids.find(i.id);
                if (it == m->ids.end()) return;   // started before the mapping
                if (i.phase == Interaction::Phase::End) m->mapped->End(i.kind, it->second);
                else m->mapped->Cancel(i.kind, it->second);
                m->ids.erase(it);
            });
            return [source, token] { source->Unsubscribe(token); };
        });
        return mapping->mapped;
    }

}
