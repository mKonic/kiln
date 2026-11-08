#include "kiln/material3/ripple/Ripple.h"

#include "kiln/animation/Animatable.h"
#include "kiln/graphics/Path.h"
#include "kiln/graphics/Shape.h"
#include "kiln/material3/ripple/RippleHost.h"
#include "kiln/runtime/Task.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <variant>

namespace kiln::material3::ripple {

    using foundation::Interaction;
    using ui::Color;
    using ui::Dp;
    using ui::Offset;
    using ui::Rect;
    using ui::Size;

    float RippleEndRadius(const ui::Density& density, bool bounded, Size size) {
        const float radiusCoveringBounds = std::hypot(size.width, size.height) / 2.0f;
        return bounded ? radiusCoveringBounds + density.ToPx(Dp(10)) : radiusCoveringBounds;
    }

    namespace {
        // Border.kt's BorderLogic: an inside border (inset from the edge) for each outline kind.
        // Widths and insets are read every draw, so the ring animates them.
        void DrawBorder(ui::DrawScope& s, Dp width, Dp inset, Color color, const gfx::Outline& outline) {
            const Size size = s.GetSize();
            const float minDimension = std::min(size.width, size.height);
            const float insetPx = std::ceil(s.ToPx(inset));
            // Dp.Hairline (0 dp) is one pixel; at most half of what is left inside the inset.
            const float strokeWidth = std::max(0.0f, std::min(width.value == 0 ? 1.0f : std::ceil(s.ToPx(width)),
                                                              std::ceil((minDimension - insetPx * 2) / 2)));
            const float halfStroke = strokeWidth / 2;
            // A stroke larger than the area: a filled shape.
            const bool fillArea = insetPx == 0 && strokeWidth * 2 > minDimension;
            const Offset topLeft{ halfStroke + insetPx, halfStroke + insetPx };
            const Size borderSize{ size.width - strokeWidth - insetPx * 2, size.height - strokeWidth - insetPx * 2 };
            auto shrink = [](Offset r, float v) { return Offset{ std::max(0.0f, r.x - v), std::max(0.0f, r.y - v) }; };
            const gfx::Stroke stroke{ strokeWidth };

            if (std::holds_alternative<Rect>(outline.value)) {
                if (fillArea) {
                    s.DrawRect(color, {}, size);
                } else {
                    gfx::Path p;
                    p.AddRect({ topLeft.x, topLeft.y, topLeft.x + borderSize.width, topLeft.y + borderSize.height });
                    s.DrawPath(p, color, &stroke);
                }
                return;
            }
            if (const auto* rr = std::get_if<gfx::RoundRect>(&outline.value)) {
                const bool simple = rr->topLeft.x == rr->topLeft.y && rr->topLeft == rr->topRight && rr->topLeft == rr->bottomRight &&
                                    rr->topLeft == rr->bottomLeft;
                if (simple) {
                    const Offset corner = shrink(rr->topLeft, insetPx);
                    gfx::Path p;
                    if (fillArea) {
                        p.AddRoundRect({ { 0, 0, size.width, size.height }, corner, corner, corner, corner });
                        s.DrawPath(p, color);
                    } else if (corner.x < halfStroke) {
                        // The stroke's inside edge would be sharp: the filled shape with the inner
                        // rect clipped out (ClipOp.Difference).
                        gfx::Path outer, inner;
                        outer.AddRoundRect({ { 0, 0, size.width, size.height }, corner, corner, corner, corner });
                        inner.AddRect({ strokeWidth, strokeWidth, size.width - strokeWidth, size.height - strokeWidth });
                        s.DrawPath(gfx::Path::Combine(gfx::PathOperation::Difference, outer, inner), color);
                    } else {
                        const Offset r = shrink(corner, halfStroke);
                        p.AddRoundRect({ { topLeft.x, topLeft.y, topLeft.x + borderSize.width, topLeft.y + borderSize.height }, r, r, r, r });
                        s.DrawPath(p, color, &stroke);
                    }
                } else {
                    const float w = rr->rect.Width(), h = rr->rect.Height();
                    auto insetRect = [&](float v) {
                        return gfx::RoundRect{ { v, v, w - v, h - v }, shrink(rr->topLeft, v), shrink(rr->topRight, v),
                                               shrink(rr->bottomRight, v), shrink(rr->bottomLeft, v) };
                    };
                    gfx::Path p;
                    p.AddRoundRect(insetRect(insetPx));
                    if (!fillArea) {
                        gfx::Path inner;
                        inner.AddRoundRect(insetRect(strokeWidth + insetPx));
                        p = gfx::Path::Combine(gfx::PathOperation::Difference, p, inner);
                    }
                    s.DrawPath(p, color);
                }
                return;
            }
            const gfx::Path& path = std::get<gfx::Path>(outline.value);
            if (fillArea) {
                s.DrawPath(path, color);
                return;
            }
            // Generic: in an offscreen layer over the path bounds, a stroke twice the width
            // (inset included), the inset cleared, then everything outside the path cleared.
            const Rect bounds = path.Bounds();
            gfx::Path boundsPath;
            boundsPath.AddRect(bounds);
            const gfx::Path mask = gfx::Path::Combine(gfx::PathOperation::Difference, boundsPath, path);
            const float layerWidth = std::ceil(bounds.Width()), layerHeight = std::ceil(bounds.Height());
            s.Save();
            s.BeginLayer({ bounds.left, bounds.top, bounds.left + layerWidth, bounds.top + layerHeight }, 1);
            const gfx::Stroke outerStroke{ (strokeWidth + insetPx) * 2 };
            s.DrawPath(path, color, &outerStroke);
            s.SetBlendMode(gfx::BlendMode::Clear);
            const gfx::Stroke insetStroke{ insetPx * 2 };   // Stroke(0) is a hairline
            s.DrawPath(path, color, &insetStroke);
            // Scaled slightly to also clear what anti-aliasing leaves outside the path; the pivot
            // is the centre of the layer's size in the path's own coordinates.
            s.Scale((layerWidth + 1) / layerWidth, (layerHeight + 1) / layerHeight, { layerWidth / 2, layerHeight / 2 });
            s.DrawPath(mask, color);
            s.EndLayer();
            s.Restore();
        }

        anim::SpecPtr DefaultTweenSpec() { return anim::Tween(15, 0, anim::Linear()); }
        anim::SpecPtr IncomingSpec(Interaction::Kind k) {
            if (k == Interaction::Kind::Focus || k == Interaction::Kind::Drag) return anim::Tween(45, 0, anim::Linear());
            return DefaultTweenSpec();
        }
        anim::SpecPtr OutgoingSpec(std::optional<Interaction::Kind> k) {
            if (k == Interaction::Kind::Drag) return anim::Tween(150, 0, anim::Linear());
            return DefaultTweenSpec();
        }

        struct RippleParams {
            std::shared_ptr<foundation::MutableInteractionSource> source;
            RippleNodeConfiguration config;
            // The configuration holds colour producers that read the theme: always update.
            bool operator==(const RippleParams&) const { return false; }
        };

        // RippleNode with AndroidRippleNode's press handling.
        struct RippleNode final : ui::ModifierNode, ui::DrawModifier, RippleHostKey {
            RippleParams params;
            explicit RippleNode(RippleParams p) : params(std::move(p)) {}
            ui::DrawModifier* AsDraw() override { return this; }

            std::shared_ptr<foundation::MutableInteractionSource> subscribed;
            std::uint64_t token = 0;
            bool hasValidSize = false;
            Size rippleSize;
            float targetRadius = 0;
            std::vector<Interaction> pendingInteractions;
            anim::Animatable animatedAlpha{ 0.0f }, animatedFocusRing{ 0.0f };
            rt::Job alphaJob, ringJob;
            struct Entry { Interaction::Kind kind; std::uint64_t id; };
            std::vector<Entry> interactions;
            std::optional<Entry> currentInteraction;
            bool isFocused = false;
            std::shared_ptr<RippleContainer> rippleContainer;
            RippleHostView* rippleHostView = nullptr;

            void OnParamsChanged() {
                if (Owner() && params.source != subscribed) {
                    Unsubscribe();
                    Subscribe();
                }
                if (hasValidSize) UpdateTargetRadius();
                InvalidateDraw();
            }
            void OnAttach() override { Subscribe(); }
            void OnDetach() override {
                if (rippleContainer) rippleContainer->DisposeRippleIfNeeded(*this);
                Unsubscribe();
                alphaJob.Cancel();
                ringJob.Cancel();
                hasValidSize = false;
                rippleSize = {};
                targetRadius = 0;
                pendingInteractions.clear();
            }
            ~RippleNode() override {
                dying = true;
                if (rippleContainer) rippleContainer->DisposeRippleIfNeeded(*this);
                Unsubscribe();
            }
            void OnResetRippleHostView() override {
                rippleHostView = nullptr;
                if (!dying) InvalidateDraw();
            }

            void Subscribe() {
                if (!params.source) return;
                subscribed = params.source;
                token = subscribed->Subscribe([this](const Interaction& i) { OnInteraction(i); });
            }
            void Unsubscribe() {
                if (subscribed) subscribed->Unsubscribe(token);
                subscribed.reset();
            }

            ui::Density density;   // the last draw's
            bool dying = false;
            void UpdateTargetRadius() {
                const auto& c = params.config;
                targetRadius = c.radius.Specified() ? density.ToPx(c.radius) : RippleEndRadius(density, c.isBounded, rippleSize);
            }

            float PressAlpha() const { return params.config.press.enabled ? params.config.press.alpha : 0.0f; }
            Color RippleColor() const { return params.config.color ? params.config.color() : Color(0xff000000); }

            void OnInteraction(const Interaction& interaction) {
                const bool wasFocused = isFocused;
                const bool removal = interaction.phase != Interaction::Phase::Start;
                if (interaction.kind == Interaction::Kind::Press) {
                    if (hasValidSize) HandlePressInteraction(interaction);
                    else pendingInteractions.push_back(interaction);   // until there is a size
                }
                auto remove = [&](std::uint64_t id) { std::erase_if(interactions, [id](const Entry& e) { return e.id == id; }); };
                switch (interaction.kind) {
                    case Interaction::Kind::Hover:
                    case Interaction::Kind::Drag:
                        if (removal) remove(interaction.id);
                        else interactions.push_back({ interaction.kind, interaction.id });
                        break;
                    case Interaction::Kind::Focus:
                        if (removal) {
                            remove(interaction.id);
                            if (std::none_of(interactions.begin(), interactions.end(), [](const Entry& e) { return e.kind == Interaction::Kind::Focus; }))
                                isFocused = false;
                        } else {
                            interactions.push_back({ interaction.kind, interaction.id });
                            isFocused = true;
                        }
                        break;
                    default: return;
                }
                // The most recent interaction is the one shown.
                const std::optional<Entry> newInteraction = interactions.empty() ? std::nullopt : std::optional(interactions.back());
                const bool changed = newInteraction.has_value() != currentInteraction.has_value() ||
                                     (newInteraction && newInteraction->id != currentInteraction->id);
                if (!changed) return;
                const auto& config = params.config;
                if (newInteraction) {
                    float target = 0;
                    switch (newInteraction->kind) {
                        case Interaction::Kind::Hover: target = config.hover.enabled ? config.hover.alpha : 0; break;
                        case Interaction::Kind::Focus: target = config.focus.kind == RippleNodeConfiguration::Focus::Kind::Opacity ? config.focus.alpha : 0; break;
                        case Interaction::Kind::Drag: target = config.drag.enabled ? config.drag.alpha : 0; break;
                        default: break;
                    }
                    AnimateAlpha(target, IncomingSpec(newInteraction->kind));
                } else {
                    AnimateAlpha(0, OutgoingSpec(currentInteraction ? std::optional(currentInteraction->kind) : std::nullopt));
                }
                if (interaction.kind == Interaction::Kind::Focus) {
                    if (config.focus.kind == RippleNodeConfiguration::Focus::Kind::InsetRing) {
                        // Only when the focused state changed.
                        if (wasFocused != isFocused) {
                            anim::SpecPtr spec = isFocused ? config.focus.ring.focusingAnimationSpec : config.focus.ring.unfocusingAnimationSpec;
                            ringJob = rt::Launch(rt::Dispatcher::Main(), [](anim::Animatable* a, float t, anim::SpecPtr spec) -> rt::Task<> {
                                co_await a->AnimateTo(t, spec ? spec : anim::Spring());
                            }(&animatedFocusRing, isFocused ? 1.0f : 0.0f, std::move(spec)));
                        }
                    } else {
                        ringJob.Cancel();
                        animatedFocusRing.SnapTo(0.0f);
                    }
                }
                currentInteraction = newInteraction;
            }

            void AnimateAlpha(float target, anim::SpecPtr spec) {
                alphaJob = rt::Launch(rt::Dispatcher::Main(), [](anim::Animatable* a, float t, anim::SpecPtr spec) -> rt::Task<> {
                    co_await a->AnimateTo(t, std::move(spec));
                }(&animatedAlpha, target, std::move(spec)));
            }

            void HandlePressInteraction(const Interaction& press) {
                if (press.phase == Interaction::Phase::Start) AddRipple(press);
                else if (rippleHostView) rippleHostView->RemoveRipple();
            }

            void AddRipple(const Interaction& press) {
                if (!rippleContainer) rippleContainer = RippleContainer::For(Owner() ? static_cast<const void*>(Owner()->GetOwner()) : nullptr);
                RippleHostView& host = rippleContainer->GetRippleHostView(*this);
                std::weak_ptr<const char> alive = AliveToken();
                host.AddRipple(press.position, params.config.isBounded, rippleSize, int(std::lround(targetRadius)), RippleColor(), PressAlpha(),
                               [this, alive] { if (!alive.expired()) InvalidateDraw(); });
                rippleHostView = &host;
                InvalidateDraw();
            }

            void Draw(ui::ContentDrawScope& s) override {
                const Size size = s.GetSize();
                // onRemeasured: the size the ripple fills, then any presses waiting for it.
                if (!hasValidSize || !(size == rippleSize) || density != static_cast<const ui::Density&>(s)) {
                    density = s;
                    hasValidSize = true;
                    rippleSize = size;
                    UpdateTargetRadius();
                    auto pending = std::move(pendingInteractions);
                    pendingInteractions.clear();
                    for (const auto& p : pending) HandlePressInteraction(p);
                }
                s.DrawContent();
                // drawRipples. Colour and alpha changes reach only ripples started after them.
                if (rippleHostView) {
                    rippleHostView->SetRippleProperties(rippleSize, int(std::lround(targetRadius)), RippleColor(), PressAlpha());
                    rippleHostView->Draw(s);
                }
                DrawStateLayers(s);
            }

            void DrawStateLayers(ui::ContentDrawScope& s) {
                const auto& config = params.config;
                const float alpha = animatedAlpha.Float();   // reading it redraws as it animates
                if (alpha > 0) {
                    const Size size = s.GetSize();
                    const Offset center{ size.width / 2, size.height / 2 };
                    s.Save();
                    if (config.isBounded) s.ClipRect({ 0, 0, size.width, size.height });
                    // color.copy(alpha = alpha): the alpha unquantised.
                    s.MultiplyAlpha(alpha);
                    s.DrawRoundRect(RippleColor().WithAlpha(1), { center.x - targetRadius, center.y - targetRadius },
                                    { 2 * targetRadius, 2 * targetRadius }, targetRadius);
                    s.Restore();
                }
                const float ring = animatedFocusRing.Float();
                if (ring > 0 && config.focus.kind == RippleNodeConfiguration::Focus::Kind::InsetRing && config.focus.ring.shape) {
                    const auto& r = config.focus.ring;
                    const gfx::Outline outline = r.shape->CreateOutline(s.GetSize(), s.Direction(), s);
                    DrawBorder(s, Dp(r.innerStrokeWidth.value * ring), Dp(r.innerStrokeInset.value * ring),
                               r.innerStrokeColor ? r.innerStrokeColor() : Color(0xff000000), outline);
                    DrawBorder(s, Dp(r.outerStrokeWidth.value * ring), Dp(r.outerStrokeInset.value * ring),
                               r.outerStrokeColor ? r.outerStrokeColor() : Color(0xff000000), outline);
                }
            }
        };
    }

    ui::Modifier RippleModifier(ui::Modifier m, std::shared_ptr<foundation::MutableInteractionSource> source,
                                RippleNodeConfiguration configuration) {
        return m.With<RippleNode>(RippleParams{ std::move(source), std::move(configuration) });
    }

}
