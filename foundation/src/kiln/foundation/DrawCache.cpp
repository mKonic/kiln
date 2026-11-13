#include "kiln/foundation/DrawCache.h"
#include <kiln/Function.h>

#include <stdexcept>

namespace kiln::foundation {

    layer::GraphicsLayerPtr CacheDrawScope::ObtainGraphicsLayer() {
        layer::GraphicsContext& g = m_Graphics ? *m_Graphics : layer::GraphicsContext::Default();
        auto l = g.CreateGraphicsLayer();
        m_Layers.push_back(l);
        return l;
    }

    void CacheDrawScope::Record(layer::GraphicsLayer& layer, const kiln::Function<void(ui::ContentDrawScope&)>& block, std::optional<IntSize> size) {
        if (!m_Content) throw std::logic_error("CacheDrawScope.record outside a cache build");
        const IntSize s = size ? *size : IntSize{ int(m_Params.size.width), int(m_Params.size.height) };
        layer.Record(*m_Content, s, block);
    }

    DrawResult CacheDrawScope::OnDrawBehind(kiln::Function<void(ui::DrawScope&)> block) {
        return OnDrawWithContent([block = std::move(block)](ui::ContentDrawScope& s) {
            block(s);
            s.DrawContent();
        });
    }

    DrawResult CacheDrawScope::OnDrawWithContent(kiln::Function<void(ui::ContentDrawScope&)> block) {
        m_Result.emplace(std::move(block));
        return *m_Result;
    }

    void CacheDrawModifierNode::ReleaseLayers() {
        layer::GraphicsContext& g = m_Scope.m_Graphics ? *m_Scope.m_Graphics : layer::GraphicsContext::Default();
        for (auto& l : m_Scope.m_Layers) g.ReleaseGraphicsLayer(*l);
        m_Scope.m_Layers.clear();
    }

    void CacheDrawModifierNode::InvalidateDrawCache() {
        ReleaseLayers();
        m_Valid = false;
        m_Scope.m_Result.reset();
        InvalidateDraw();
    }

    void CacheDrawModifierNode::Draw(ui::ContentDrawScope& s) {
        // The size, density and direction it is drawn with: a change rebuilds (onMeasureResultChanged,
        // onDensityChange, onLayoutDirectionChange).
        const BuildDrawCacheParams now{ s.GetSize(), s.Direction(), static_cast<const ui::Density&>(s) };
        const BuildDrawCacheParams& was = m_Scope.m_Params;
        if (m_Valid && (!(now.size == was.size) || now.layoutDirection != was.layoutDirection || now.density.density != was.density.density ||
                        now.density.fontScale != was.density.fontScale)) {
            ReleaseLayers();
            m_Valid = false;
        }
        if (!m_Valid) {
            m_Scope.m_Params = now;
            static_cast<ui::Density&>(m_Scope) = now.density;
            m_Scope.m_Content = &s;
            m_Scope.m_Graphics = Owner() ? &layer::RequireGraphicsContext(*this) : nullptr;
            m_Scope.m_Result.reset();
            ObserveReads([&] { if (m_Block) m_Block(m_Scope); });
            m_Scope.m_Content = nullptr;
            if (!m_Scope.m_Result) throw std::logic_error("DrawResult not defined, did you forget to call onDraw?");
            m_Valid = true;
        }
        m_Scope.m_Result->Draw(s);
    }

    namespace {
        struct CacheParams {
            CacheDrawModifierNode::Block block;
            bool operator==(const CacheParams&) const { return false; }   // lambdas: by identity
        };
        struct CacheNode final : CacheDrawModifierNode {
            CacheParams params;
            explicit CacheNode(CacheParams p) : CacheDrawModifierNode(p.block), params(std::move(p)) {}
            void OnParamsChanged() { SetBlock(params.block); }
        };
    }

    Modifier DrawWithCache(Modifier m, kiln::Function<DrawResult(CacheDrawScope&)> onBuildDrawCache) {
        return m.With<CacheNode>(CacheParams{ std::move(onBuildDrawCache) });
    }

}
