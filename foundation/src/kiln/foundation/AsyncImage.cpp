#include "kiln/foundation/AsyncImage.h"

#include "kiln/runtime/Composition.h"
#include "kiln/runtime/Flow.h"
#include "kiln/runtime/State.h"

namespace kiln::foundation {

    using namespace ui;

    namespace {
        // Draws nothing and has no intrinsic size (a null painter in Coil).
        class EmptyPainter final : public Painter {
        public:
            std::optional<Size> IntrinsicSize() const override { return std::nullopt; }
            void Draw(DrawScope&, Size, float, const gfx::ColorFilter*) override {}
            bool Equals(const Painter& other) const override { return dynamic_cast<const EmptyPainter*>(&other) != nullptr; }
        };

        struct Holder {
            // The first constraints the layout measured with (ConstraintsSizeResolver); 0 for
            // an unbounded side.
            rt::MutableStateFlow<std::optional<std::pair<int, int>>> size{ std::nullopt };
            rt::State<AsyncImageState, rt::NeverEqual> state{ AsyncImageState{} };
            bool posted = false;
        };

        // ConstraintsSizeResolver as a layout modifier: records the constraints, measures as is.
        struct SizeResolverParams {
            std::shared_ptr<Holder> holder;
            bool operator==(const SizeResolverParams&) const = default;
        };
        struct SizeResolverNode final : ModifierNode, LayoutModifier {
            SizeResolverParams params;
            explicit SizeResolverNode(SizeResolverParams p) : params(std::move(p)) {}
            void OnParamsChanged() {}
            LayoutModifier* AsLayout() override { return this; }
            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& c) override {
                auto& h = params.holder;
                if (!h->size.Value() && !h->posted && !c.IsZero()) {
                    // Handed over after layout: the loading task must not run inside measure.
                    h->posted = true;
                    std::pair<int, int> px{ c.HasBoundedWidth() ? c.maxWidth : 0, c.HasBoundedHeight() ? c.maxHeight : 0 };
                    rt::Dispatcher::Main().Post([weak = std::weak_ptr<Holder>(h), px] {
                        if (auto held = weak.lock()) held->size.SetValue(px);
                    });
                }
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

        image::Scale ScaleOf(ContentScale s) {
            using K = ContentScale::Kind;
            return s.kind == K::Fit || s.kind == K::Inside ? image::Scale::Fit : image::Scale::Fill;
        }

        rt::Task<> Load(std::shared_ptr<Holder> h, image::ImageRequest request, AsyncImageOptions options) {
            auto set = [&](AsyncImageState s) {
                h->state.Set(s);
                if (options.onState) options.onState(s);
            };
            if (request.data.empty()) {
                AsyncImageState s;
                s.kind = AsyncImageState::Kind::Error;
                s.painter = options.fallback ? options.fallback : options.error;
                s.result.error = "no data";
                set(std::move(s));
                co_return;
            }
            const bool sized = request.width > 0 || request.height > 0;
            if (!sized && options.contentScale.kind != ContentScale::Kind::None) {
                std::optional<std::pair<int, int>> size = h->size.Value();
                if (!size) size = co_await rt::First(rt::Filter(h->size.AsFlow(), [](const std::optional<std::pair<int, int>>& v) { return v.has_value(); }));
                request.width = size->first;
                request.height = size->second;
                request.precision = image::Precision::Inexact;   // scaled to the canvas when drawn
            }
            request.scale = ScaleOf(options.contentScale);
            AsyncImageState loading;
            loading.kind = AsyncImageState::Kind::Loading;
            loading.painter = options.placeholder;
            set(std::move(loading));
            image::ImageLoader& loader = options.imageLoader ? *options.imageLoader : image::ImageLoader::Default();
            image::ImageResult result = co_await loader.Execute(std::move(request));
            AsyncImageState done;
            if (result.image) {
                done.kind = AsyncImageState::Kind::Success;
                done.painter = std::make_shared<BitmapPainter>(result.image, options.filterQuality);
            } else {
                done.kind = AsyncImageState::Kind::Error;
                done.painter = options.error;
            }
            done.result = std::move(result);
            set(std::move(done));
        }
    }

    void AsyncImage(const std::string& model, const std::optional<std::string>& contentDescription, const Modifier& modifier,
                    const AsyncImageOptions& options) {
        image::ImageRequest request;
        request.data = model;
        AsyncImage(request, contentDescription, modifier, options);
    }

    void AsyncImage(const image::ImageRequest& request, const std::optional<std::string>& contentDescription, const Modifier& modifier,
                    const AsyncImageOptions& options) {
        auto& c = *rt::Composition::Current();
        // A new holder (size and state) per request and loader, as Coil restarts on a new input.
        const rt::Keys keys(request.data, request.memoryCacheKey, request.diskCacheKey, request.width, request.height,
                            reinterpret_cast<std::uintptr_t>(options.imageLoader), int(options.contentScale.kind));
        auto& holder = c.Remember<std::shared_ptr<Holder>>([] { return std::make_shared<Holder>(); }, keys);
        std::shared_ptr<Holder> h = holder;
        c.LaunchedEffect(keys, [h, request, options] { return Load(h, request, options); });
        static const auto empty = std::make_shared<EmptyPainter>();
        std::shared_ptr<Painter> painter = h->state.Get().painter;
        Image(painter ? painter : empty, contentDescription, modifier.With<SizeResolverNode>(SizeResolverParams{ h }),
              options.alignment, options.contentScale, options.alpha, options.colorFilter);
    }

}
