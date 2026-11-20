#include "kiln/foundation/AsyncImage.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Subcompose.h"
#include "kiln/runtime/Composition.h"
#include "kiln/runtime/Flow.h"
#include "kiln/runtime/State.h"

namespace kiln::foundation {

    using namespace ui;

    namespace {
        // ConstraintsSizeResolver as a layout modifier: records the constraints, measures as is.
        struct SizeResolverParams {
            std::shared_ptr<AsyncImagePainter> painter;
            bool operator==(const SizeResolverParams&) const = default;
        };
        struct SizeResolverNode final : ModifierNode, LayoutModifier {
            SizeResolverParams params;
            explicit SizeResolverNode(SizeResolverParams p) : params(std::move(p)) {}
            void OnParamsChanged() {}
            LayoutModifier* AsLayout() override { return this; }
            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& c) override {
                if (!c.IsZero()) params.painter->SetSize(c.HasBoundedWidth() ? c.maxWidth : 0, c.HasBoundedHeight() ? c.maxHeight : 0);
                Placeable& p = m.Measure(c);
                return s.Layout(p.Width(), p.Height(), [&p](PlacementScope& place) { place.Place(p, 0, 0); });
            }
            // Intrinsics pass through: the default measures with stand-in constraints, which
            // must not be taken for the layout's.
            int MinIntrinsicWidth(MeasureScope&, Measurable& m, int height) override { return m.MinIntrinsicWidth(height); }
            int MaxIntrinsicWidth(MeasureScope&, Measurable& m, int height) override { return m.MaxIntrinsicWidth(height); }
            int MinIntrinsicHeight(MeasureScope&, Measurable& m, int width) override { return m.MinIntrinsicHeight(width); }
            int MaxIntrinsicHeight(MeasureScope&, Measurable& m, int width) override { return m.MaxIntrinsicHeight(width); }
        };

        bool Specified(const std::optional<Size>& s) { return s && s->Specified(); }

        image::Scale ScaleOf(ContentScale s) {
            using K = ContentScale::Kind;
            return s.kind == K::Fit || s.kind == K::Inside ? image::Scale::Fit : image::Scale::Fill;
        }

    }

    struct AsyncImagePainter::Impl {
        image::ImageRequest request;
        AsyncImageOptions options;
        // The size to load at (px, 0 unbounded), once known.
        rt::MutableStateFlow<std::optional<std::pair<int, int>>> size{ std::nullopt };
        rt::State<AsyncImageState, rt::NeverEqual> state{ AsyncImageState{} };
        bool sizePosted = false, loaded = false;

        void Set(AsyncImageState s) {
            state.Set(s);
            if (options.onState) options.onState(s);
        }
        bool NeedsSize() const {
            return request.width <= 0 && request.height <= 0 && options.contentScale.kind != ContentScale::Kind::None;
        }
        image::ImageRequest Sized(std::pair<int, int> px) const {
            image::ImageRequest r = request;
            if (NeedsSize()) {
                r.width = px.first;
                r.height = px.second;
                r.precision = image::Precision::Inexact;   // scaled to the canvas when drawn
            }
            r.scale = ScaleOf(options.contentScale);
            return r;
        }
        image::ImageLoader& Loader() const { return options.imageLoader ? *options.imageLoader : image::ImageLoader::Default(); }
        // A memory cache hit, set at once (AsyncImagePainter's synchronous memory cache check).
        bool TryMemory(std::pair<int, int> px) {
            if (loaded || request.data.empty()) return loaded;
            if (auto hit = Loader().Peek(Sized(px))) {
                loaded = true;
                AsyncImageState s;
                s.kind = AsyncImageState::Kind::Success;
                s.painter = std::make_shared<BitmapPainter>(hit->image, options.filterQuality);
                s.result = std::move(*hit);
                Set(std::move(s));
            }
            return loaded;
        }
    };

    namespace {
        rt::Task<> Load(std::shared_ptr<AsyncImagePainter::Impl> h) {
            if (h->loaded) co_return;
            const AsyncImageOptions& options = h->options;
            if (h->request.data.empty()) {
                AsyncImageState s;
                s.kind = AsyncImageState::Kind::Error;
                s.painter = options.fallback ? options.fallback : options.error;
                s.result.error = "no data";
                h->Set(std::move(s));
                co_return;
            }
            std::pair<int, int> px{ 0, 0 };
            if (h->NeedsSize()) {
                std::optional<std::pair<int, int>> size = h->size.Value();
                if (!size) size = co_await rt::First(rt::Filter(h->size.AsFlow(), [](const std::optional<std::pair<int, int>>& v) { return v.has_value(); }));
                px = *size;
            }
            if (h->TryMemory(px)) co_return;
            AsyncImageState loading;
            loading.kind = AsyncImageState::Kind::Loading;
            loading.painter = options.placeholder;
            h->Set(std::move(loading));
            image::ImageResult result = co_await h->Loader().Execute(h->Sized(px));
            h->loaded = true;
            AsyncImageState done;
            if (result.image) {
                done.kind = AsyncImageState::Kind::Success;
                done.painter = std::make_shared<BitmapPainter>(result.image, options.filterQuality);
                // maybeNewCrossfadePainter with CrossfadeTransition.Factory: not from memory.
                if (options.crossfadeMillis > 0 && result.dataSource != image::DataSource::MemoryCache)
                    done.painter = std::make_shared<CrossfadePainter>(options.placeholder, done.painter, options.contentScale, options.crossfadeMillis);
            } else {
                done.kind = AsyncImageState::Kind::Error;
                done.painter = options.error;
            }
            done.result = std::move(result);
            h->Set(std::move(done));
        }
    }

    const AsyncImageState& AsyncImagePainter::State() const { return m_Impl->state.Get(); }
    std::optional<Size> AsyncImagePainter::IntrinsicSize() const {
        const auto& p = m_Impl->state.Get().painter;
        return p ? p->IntrinsicSize() : std::nullopt;
    }
    void AsyncImagePainter::Draw(DrawScope& scope, Size size, float alpha, const gfx::ColorFilter* colorFilter) {
        // DrawScopeSizeResolver: the first drawn size, when nothing else gave one.
        if (size.width > 0 && size.height > 0) SetSize(int(std::lround(size.width)), int(std::lround(size.height)));
        if (const auto& p = m_Impl->state.Get().painter) p->Draw(scope, size, alpha, colorFilter);
    }
    void AsyncImagePainter::SetSize(int width, int height) {
        Impl& h = *m_Impl;
        if (h.size.Value() || h.sizePosted) return;
        h.sizePosted = true;
        // Handed over after layout or drawing: the loading task must not run inside them.
        rt::Dispatcher::Main().Post([weak = std::weak_ptr<Impl>(m_Impl), px = std::pair{ width, height }] {
            if (auto held = weak.lock()) held->size.SetValue(px);
        });
    }

    std::shared_ptr<AsyncImagePainter> RememberAsyncImagePainter(const image::ImageRequest& request, const AsyncImageOptions& options) {
        auto& c = *rt::Composition::Current();
        // A new painter (size and state) per request and loader, as Coil restarts on a new input.
        const rt::Keys keys(request.data, request.memoryCacheKey, request.diskCacheKey, request.width, request.height,
                            request.transformations.CacheKey(), reinterpret_cast<std::uintptr_t>(options.imageLoader), int(options.contentScale.kind));
        auto& painter = c.Remember<std::shared_ptr<AsyncImagePainter>>([&] {
            auto impl = std::make_shared<AsyncImagePainter::Impl>();
            impl->request = request;
            impl->options = options;
            // Sized already: a memory cache hit is the first state.
            if (!impl->NeedsSize()) impl->TryMemory({ 0, 0 });
            return std::make_shared<AsyncImagePainter>(impl);
        }, keys);
        // The latest callbacks and painters (they are not part of the request's identity).
        std::shared_ptr<AsyncImagePainter> p = painter;
        c.LaunchedEffect(keys, [impl = p->GetImpl()] { return Load(impl); });
        return p;
    }

    CrossfadePainter::CrossfadePainter(std::shared_ptr<Painter> start, std::shared_ptr<Painter> end, ContentScale contentScale, int durationMillis,
                                       bool fadeStart, bool preferExactIntrinsicSize)
        : m_Start(std::move(start)), m_End(std::move(end)), m_ContentScale(contentScale), m_Duration(std::max(durationMillis, 1)), m_FadeStart(fadeStart) {
        // computeIntrinsicSize: the larger of both when both are known; with
        // preferExactIntrinsicSize, the one that is; else unspecified.
        const std::optional<Size> s = m_Start ? m_Start->IntrinsicSize() : std::optional<Size>(Size{ 0, 0 });
        const std::optional<Size> e = m_End ? m_End->IntrinsicSize() : std::optional<Size>(Size{ 0, 0 });
        if (Specified(s) && Specified(e)) m_Intrinsic = Size{ std::max(s->width, e->width), std::max(s->height, e->height) };
        else if (preferExactIntrinsicSize && Specified(s)) m_Intrinsic = s;
        else if (preferExactIntrinsicSize && Specified(e)) m_Intrinsic = e;
    }

    void CrossfadePainter::DrawPainter(DrawScope& scope, Painter* painter, Size size, float alpha, const gfx::ColorFilter* colorFilter) {
        if (!painter || alpha <= 0) return;
        // computeDrawSize: the painter's intrinsic size by the content scale, centred.
        Size drawSize = size;
        const std::optional<Size> src = painter->IntrinsicSize();
        if (Specified(src) && src->width > 0 && src->height > 0 && size.width > 0 && size.height > 0)
            drawSize = *src * m_ContentScale.ComputeScaleFactor(*src, size);
        const Offset at{ (size.width - drawSize.width) / 2, (size.height - drawSize.height) / 2 };
        scope.WithTransform([at](DrawScope& d) { d.Translate(at); }, [&](DrawScope& d) { painter->Draw(d, drawSize, alpha, colorFilter); });
    }

    void CrossfadePainter::Draw(DrawScope& scope, Size size, float alpha, const gfx::ColorFilter* colorFilter) {
        (void)m_Tick.Get();   // redrawn when the tick moves
        if (m_Done) {
            DrawPainter(scope, m_End.get(), size, alpha, colorFilter);
            return;
        }
        const std::int64_t now = rt::Dispatcher::Main().LastFrameTimeNanos();
        if (!m_StartNanos) m_StartNanos = now;
        const float percent = float(double(now - *m_StartNanos) / 1e6 / double(m_Duration));
        const float endAlpha = std::clamp(percent, 0.0f, 1.0f) * alpha;
        const float startAlpha = m_FadeStart ? alpha - endAlpha : alpha;
        m_Done = percent >= 1;
        DrawPainter(scope, m_Start.get(), size, startAlpha, colorFilter);
        DrawPainter(scope, m_End.get(), size, endAlpha, colorFilter);
        if (m_Done) m_Start.reset();
        else rt::Dispatcher::Main().Post([tick = &m_Tick, alive = std::weak_ptr<int>(m_Alive)] {
            if (alive.lock()) tick->Set(tick->Peek() + 1);
        });
    }

    std::shared_ptr<AsyncImagePainter> RememberAsyncImagePainter(const std::string& model, const AsyncImageOptions& options) {
        image::ImageRequest request;
        request.data = model;
        return RememberAsyncImagePainter(request, options);
    }

    void AsyncImage(const std::string& model, const std::optional<std::string>& contentDescription, const Modifier& modifier,
                    const AsyncImageOptions& options) {
        image::ImageRequest request;
        request.data = model;
        AsyncImage(request, contentDescription, modifier, options);
    }

    void AsyncImage(const image::ImageRequest& request, const std::optional<std::string>& contentDescription, const Modifier& modifier,
                    const AsyncImageOptions& options) {
        std::shared_ptr<AsyncImagePainter> painter = RememberAsyncImagePainter(request, options);
        // Read here too: a new state recomposes, so the image re-measures with its painter.
        (void)painter->State();
        Image(painter, contentDescription, modifier.With<SizeResolverNode>(SizeResolverParams{ painter }), options.alignment, options.contentScale,
              options.alpha, options.colorFilter);
    }

    void SubcomposeAsyncImageContent(const SubcomposeAsyncImageScope& scope, const Modifier& modifier) {
        Image(scope.painter, scope.contentDescription, modifier, scope.alignment, scope.contentScale, scope.alpha, scope.colorFilter);
    }

    void SubcomposeAsyncImage(const image::ImageRequest& request, const std::optional<std::string>& contentDescription, const Modifier& modifier,
                              const AsyncImageOptions& options, kiln::Function<void(const SubcomposeAsyncImageScope&)> content) {
        std::shared_ptr<AsyncImagePainter> painter = RememberAsyncImagePainter(request, options);
        // Copies: the constraints' content runs later, in measure.
        auto scopeOf = [contentDescription, options](const std::shared_ptr<AsyncImagePainter>& p) {
            SubcomposeAsyncImageScope scope;
            scope.painter = p;
            scope.state = p->State();
            scope.contentDescription = contentDescription;
            scope.alignment = options.alignment;
            scope.contentScale = options.contentScale;
            scope.alpha = options.alpha;
            scope.colorFilter = options.colorFilter;
            return scope;
        };
        const bool sized = request.width > 0 || request.height > 0 || options.contentScale.kind == ContentScale::Kind::None;
        if (sized) {
            // The fast path: no constraints to resolve.
            Box(modifier, options.alignment, true, [&] { content(scopeOf(painter)); });
            return;
        }
        // The constraints size the request before the content runs (a memory cache hit is the
        // first state then).
        BoxWithConstraints(modifier, options.alignment, true, [painter, scopeOf, content = std::move(content)](const BoxWithConstraintsScope& b) {
            const Constraints& c = b.constraints;
            const std::pair<int, int> px{ c.HasBoundedWidth() ? c.maxWidth : 0, c.HasBoundedHeight() ? c.maxHeight : 0 };
            painter->GetImpl()->TryMemory(px);
            painter->SetSize(px.first, px.second);
            content(scopeOf(painter));
        });
    }

    void SubcomposeAsyncImage(const std::string& model, const std::optional<std::string>& contentDescription, const Modifier& modifier,
                              const AsyncImageOptions& options, SubcomposeAsyncImageSlots slots) {
        image::ImageRequest request;
        request.data = model;
        auto shared = std::make_shared<SubcomposeAsyncImageSlots>(std::move(slots));
        SubcomposeAsyncImage(request, contentDescription, modifier, options, [shared](const SubcomposeAsyncImageScope& scope) {
            // contentOf: the state's slot, else the image.
            using K = AsyncImageState::Kind;
            const auto& slot = scope.state.kind == K::Loading ? shared->loading : scope.state.kind == K::Success ? shared->success
                             : scope.state.kind == K::Error ? shared->error : shared->loading;
            if (slot) slot(scope);
            else SubcomposeAsyncImageContent(scope);
        });
    }

}
