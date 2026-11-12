#pragma once

// drawWithCache (DrawModifier.kt): a draw block in two parts. The build part runs when the
// size, density, direction or a state it read changes, and makes the objects drawing needs
// (paths, brushes, graphics layers); the draw part it returns (onDrawBehind / onDrawWithContent)
// runs every frame with them.

#include "kiln/foundation/GraphicsLayer.h"

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace kiln::foundation {

    // BuildDrawCacheParams: what a cache is built for.
    struct BuildDrawCacheParams {
        ui::Size size;
        ui::LayoutDirection layoutDirection = ui::LayoutDirection::Ltr;
        ui::Density density;
    };

    // DrawResult: the per-frame part, from onDrawBehind / onDrawWithContent.
    class DrawResult {
    public:
        explicit DrawResult(std::function<void(ui::ContentDrawScope&)> block) : m_Block(std::move(block)) {}
        void Draw(ui::ContentDrawScope& s) const { if (m_Block) m_Block(s); }
    private:
        std::function<void(ui::ContentDrawScope&)> m_Block;
    };

    class CacheDrawScope : public ui::Density {
    public:
        ui::Size GetSize() const { return m_Params.size; }
        ui::LayoutDirection Direction() const { return m_Params.layoutDirection; }
        // obtainGraphicsLayer: a layer the cache owns (released when the cache is rebuilt or
        // the node detaches).
        layer::GraphicsLayerPtr ObtainGraphicsLayer();
        // GraphicsLayer.record in the cache: records `block` with the scope being drawn (so it
        // can draw the content), at the cache's size unless given.
        void Record(layer::GraphicsLayer& layer, const std::function<void(ui::ContentDrawScope&)>& block, std::optional<IntSize> size = std::nullopt);
        DrawResult OnDrawBehind(std::function<void(ui::DrawScope&)> block);
        DrawResult OnDrawWithContent(std::function<void(ui::ContentDrawScope&)> block);

    private:
        friend class CacheDrawModifierNode;
        BuildDrawCacheParams m_Params;
        ui::ContentDrawScope* m_Content = nullptr;
        layer::GraphicsContext* m_Graphics = nullptr;
        std::vector<layer::GraphicsLayerPtr> m_Layers;
        std::optional<DrawResult> m_Result;
    };

    // CacheDrawModifierNode: the node behind drawWithCache, for custom nodes too.
    class CacheDrawModifierNode : public ui::ModifierNode, public ui::DrawModifier {
    public:
        using Block = std::function<DrawResult(CacheDrawScope&)>;
        explicit CacheDrawModifierNode(Block block) : m_Block(std::move(block)) {}
        ui::DrawModifier* AsDraw() override { return this; }
        void Draw(ui::ContentDrawScope& s) override;
        void OnDetach() override { ReleaseLayers(); }
        void OnObservedReadsChanged() override { InvalidateDrawCache(); }
        // invalidateDrawCache: rebuild before the next draw.
        void InvalidateDrawCache();
        void SetBlock(Block block) { m_Block = std::move(block); InvalidateDrawCache(); }

    private:
        void ReleaseLayers();
        Block m_Block;
        CacheDrawScope m_Scope;
        bool m_Valid = false;
    };

    Modifier DrawWithCache(Modifier m, std::function<DrawResult(CacheDrawScope&)> onBuildDrawCache);

}
