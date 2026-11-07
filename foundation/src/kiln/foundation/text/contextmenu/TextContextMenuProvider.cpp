#include "kiln/foundation/text/contextmenu/TextContextMenuProvider.h"

#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Image.h"
#include "kiln/foundation/contextmenu/ContextMenuUi.h"
#include "kiln/runtime/Channel.h"
#include "kiln/ui/LayoutNode.h"
#include "kiln/ui/Locals.h"

#include <cmath>

namespace kiln::foundation {

    namespace {
        // BasicTextContextMenuProvider: the session shown while ShowTextContextMenu waits.
        class BasicTextContextMenuProvider final : public TextContextMenuProvider {
        public:
            struct Session final : TextContextMenuSession {
                std::shared_ptr<TextContextMenuDataProvider> dataProvider;
                rt::Channel<int> closed{ 1 };
                void Close() override { closed.TrySend(0); }
            };
            using Block = std::function<void(const std::shared_ptr<Session>&)>;

            explicit BasicTextContextMenuProvider(Block block) : m_Block(std::move(block)) {}

            rt::Task<> ShowTextContextMenu(std::shared_ptr<TextContextMenuDataProvider> dataProvider) override {
                // MutatorMutex.mutate: the session before this one ends.
                if (auto previous = m_Session.Peek()) previous->Close();
                auto session = std::make_shared<Session>();
                session->dataProvider = std::move(dataProvider);
                m_Session.Set(session);
                auto alive = m_Alive;
                co_await session->closed.Next();
                if (alive.expired()) co_return;
                if (m_Session.Peek() == session) m_Session.Set(nullptr);
            }

            void ContextMenu() {
                auto session = m_Session.Get();
                if (session) m_Block(session);
            }
            void Cancel() { if (auto s = m_Session.Peek()) s->Close(); }

        private:
            Block m_Block;
            rt::State<std::shared_ptr<Session>, rt::NeverEqual> m_Session{ nullptr };
            std::shared_ptr<int> m_AliveToken = std::make_shared<int>(0);
            std::weak_ptr<int> m_Alive = m_AliveToken;
        };

        // MaintainWindowPositionPopupPositionProvider: once placed the menu stays put while the
        // window, direction and its size do.
        class MaintainWindowPositionPopupPositionProvider final : public PopupPositionProvider {
        public:
            explicit MaintainWindowPositionPopupPositionProvider(std::shared_ptr<const PopupPositionProvider> p) : m_Provider(std::move(p)) {}
            IntOffset CalculatePosition(ui::IntRect anchor, ui::IntSize window, LayoutDirection dir, ui::IntSize content) const override {
                if (m_Position && m_Window == window && m_Direction == dir && m_Content == content) return *m_Position;
                const IntOffset p = m_Provider->CalculatePosition(anchor, window, dir, content);
                m_Window = window;
                m_Direction = dir;
                m_Content = content;
                m_Position = p;
                return p;
            }
        private:
            std::shared_ptr<const PopupPositionProvider> m_Provider;
            mutable std::optional<IntOffset> m_Position;
            mutable ui::IntSize m_Window, m_Content;
            mutable LayoutDirection m_Direction = LayoutDirection::Ltr;
        };

        // ContextMenuPopupPositionProvider for a point given in root pixels: the point in the
        // anchor's coordinates (dataProvider.position(anchorLayoutCoordinates)).
        class RootPointPositionProvider final : public PopupPositionProvider {
        public:
            explicit RootPointPositionProvider(std::shared_ptr<TextContextMenuDataProvider> d) : m_Data(std::move(d)) {}
            IntOffset CalculatePosition(ui::IntRect anchor, ui::IntSize window, LayoutDirection dir, ui::IntSize content) const override {
                const Offset p = m_Data->PositionInRoot();
                ContextMenuPopupPositionProvider inner([&] {
                    return IntOffset{ int(std::lround(p.x)) - anchor.left, int(std::lround(p.y)) - anchor.top };
                });
                return inner.CalculatePosition(anchor, window, dir, content);
            }
        private:
            std::shared_ptr<TextContextMenuDataProvider> m_Data;
        };

        // A host icon as a 24 dp painter box, tinted (standard and theme icons) or as drawn.
        void IconBox(const std::optional<ui::HostIcon>& icon, std::optional<Color> tint) {
            if (!icon || icon->width <= 0 || icon->height <= 0) return;
            auto& c = *rt::Composition::Current();
            auto& painter = c.Remember<std::shared_ptr<Painter>>([&] {
                return std::make_shared<BitmapPainter>(gfx::ImageBitmap::FromPixels(icon->width, icon->height, icon->rgba, true));
            }, rt::Keys(reinterpret_cast<std::uintptr_t>(icon->rgba.data()), icon->width, icon->height));
            std::optional<gfx::ColorFilter> filter;
            if (tint && tint->argb != Color::Unspecified().argb) filter = gfx::ColorFilter::Tint(*tint);
            Box(Paint(SizeMod(Modifier{}, Dp(ContextMenuSpec::IconSizeDp), Dp(ContextMenuSpec::IconSizeDp)), painter, true,
                      Alignment::Center(), ContentScale::Fit(), 1, filter));
        }

        // DefaultTextContextMenuDropdown: the data's items (standard ones with the platform's
        // strings and action mode icons), classification items and separators.
        void DefaultTextContextMenuDropdown(TextContextMenuSession& session, const TextContextMenuData& data) {
            auto& c = *rt::Composition::Current();
            ui::Owner* owner = c.Read(ui::LocalOwner);
            const ui::ContextMenuHost* host = owner ? &owner->ContextMenu() : nullptr;
            const int iconPx = int(std::lround(ContextMenuSpec::IconSizeDp * (owner ? owner->GetDensity().density : 1.0f)));
            // Icons are fetched once per menu (the host draws them).
            auto& icons = c.Remember<std::shared_ptr<std::map<std::string, std::optional<ui::HostIcon>>>>(
                [] { return std::make_shared<std::map<std::string, std::optional<ui::HostIcon>>>(); });
            auto cachedIcon = [&](const std::string& k, const std::function<std::optional<ui::HostIcon>()>& load) {
                auto it = icons->find(k);
                if (it == icons->end()) it = icons->emplace(k, load()).first;
                return it->second;
            };
            const ContextMenuColors colors = ComputeContextMenuColors();
            TextContextMenuSession* s = &session;
            ContextMenuColumnBuilder(Modifier{}, colors, [&](ContextMenuScope& scope) {
                for (const auto& component : data.components) {
                    if (const auto* item = std::get_if<TextContextMenuItem>(&component)) {
                        const ui::TextToolbarItemId id = TextContextMenuKeys::PlatformId(item->key);
                        std::string label = item->label;
                        if (label.empty() && host && host->standardLabel && id != ui::TextToolbarItemId::None) label = host->standardLabel(id);
                        if (label.empty()) continue;
                        std::optional<ui::HostIcon> icon;
                        if (host && item->leadingIcon != 0 && host->themeIcon)
                            icon = cachedIcon("a" + std::to_string(item->leadingIcon), [&] { return host->themeIcon(item->leadingIcon, iconPx); });
                        else if (host && item->leadingIcon == 0 && id != ui::TextToolbarItemId::None && host->standardIcon)
                            icon = cachedIcon("s" + std::to_string(int(id)), [&] { return host->standardIcon(id, iconPx); });
                        std::function<void(Color)> leading;
                        if (icon) leading = [icon](Color tint) { IconBox(icon, tint); };
                        auto onClick = item->onClick;
                        scope.Item(label, [onClick, s] { if (onClick) onClick(*s); }, true, {}, leading);
                    } else if (const auto* ci = std::get_if<TextContextMenuTextClassificationItem>(&component)) {
                        if (!host || !host->classificationLabel) continue;
                        const std::string label = host->classificationLabel(ci->classification, ci->index);
                        if (label.find_first_not_of(" \t\n") == std::string::npos) continue;
                        std::optional<ui::HostIcon> icon;
                        if (host->classificationIcon)
                            icon = cachedIcon("c" + std::to_string(ci->classification) + ":" + std::to_string(ci->index),
                                              [&] { return host->classificationIcon(ci->classification, ci->index, iconPx); });
                        std::function<void(Color)> leading;
                        if (icon) leading = [icon](Color) { IconBox(icon, std::nullopt); };
                        const int handle = ci->classification, index = ci->index;
                        auto perform = host->performClassification;
                        scope.Item(label, [perform, handle, index] { if (perform) perform(handle, index); }, true, {}, leading);
                    } else {
                        scope.Separator();
                    }
                }
            });
        }

        void OpenContextMenu(const std::shared_ptr<BasicTextContextMenuProvider::Session>& session) {
            rt::Composition::Current()->Child([session] {
                auto& c = *rt::Composition::Current();
                auto& provider = c.Remember<std::shared_ptr<const PopupPositionProvider>>([&] {
                    return std::make_shared<MaintainWindowPositionPopupPositionProvider>(std::make_shared<RootPointPositionProvider>(session->dataProvider));
                }, rt::Keys(reinterpret_cast<std::uintptr_t>(session->dataProvider.get())));
                PopupProperties p;
                p.focusable = true;
                Popup(provider, [session] { session->Close(); }, p, [session] {
                    DefaultTextContextMenuDropdown(*session, session->dataProvider->Data());
                });
            }, rt::Key(0xd20d));
        }
    }

    void ProvideDefaultPlatformTextContextMenuProviders(std::function<Modifier()> modifier, std::function<void()> content) {
        auto& c = *rt::Composition::Current();
        if (c.Read(LocalTextContextMenuDropdownProvider)) {
            Box(modifier ? modifier() : Modifier{}, Alignment::TopStart(), true, content);
            return;
        }
        auto& provider = c.Remember<std::shared_ptr<BasicTextContextMenuProvider>>([] {
            return std::make_shared<BasicTextContextMenuProvider>([](const std::shared_ptr<BasicTextContextMenuProvider::Session>& s) { OpenContextMenu(s); });
        });
        c.DisposableEffect(rt::Keys(provider.get()), [p = provider] { return std::function<void()>([p] { p->Cancel(); }); });
        BasicTextContextMenuProvider* raw = provider.get();
        c.Provide(LocalTextContextMenuDropdownProvider, static_cast<TextContextMenuProvider*>(raw), [modifier, content, raw] {
            Box(modifier ? modifier() : Modifier{}, Alignment::TopStart(), true, [content, raw] {
                content();
                rt::Composition::Current()->Child([raw] { raw->ContextMenu(); }, rt::Key(0xd20c));
            });
        });
    }

    namespace {
        // TextContextMenuGestureNode's data: the menu at the click, with the components around
        // the node.
        struct GestureAnchor {
            ui::ModifierNode* node = nullptr;   // the node collecting the components
            rt::Job job;                        // the node's coroutine scope: the menu it shows
        };
        struct GestureAnchorParams {
            std::shared_ptr<GestureAnchor> anchor;
            bool operator==(const GestureAnchorParams&) const = default;
        };
        struct GestureAnchorNode final : ui::ModifierNode {
            GestureAnchorParams params;
            explicit GestureAnchorNode(GestureAnchorParams p) : params(std::move(p)) {}
            void OnParamsChanged() { if (Owner()) params.anchor->node = this; }
            void OnAttach() override { params.anchor->node = this; }
            void OnDetach() override {
                if (params.anchor->node != this) return;
                params.anchor->node = nullptr;
                params.anchor->job.Cancel();
            }
        };

        class ClickDataProvider final : public TextContextMenuDataProvider {
        public:
            ClickDataProvider(std::shared_ptr<GestureAnchor> a, Offset local) : m_Anchor(std::move(a)), m_Local(local) {}
            Offset PositionInRoot() override {
                ui::ModifierNode* n = m_Anchor->node;
                if (!n || !n->Owner() || !n->Owner()->IsPlaced()) return {};
                return LayoutNode::MapPoint(n->Owner()->SpaceToRoot(n), m_Local);
            }
            Rect ContentBoundsInRoot() override { const Offset p = PositionInRoot(); return { p.x, p.y, p.x, p.y }; }
            TextContextMenuData Data() override {
                ui::ModifierNode* n = m_Anchor->node;
                return n ? CollectTextContextMenuData(*n) : TextContextMenuData{};
            }
        private:
            std::shared_ptr<GestureAnchor> m_Anchor;
            Offset m_Local;
        };

        // onRightClickDown: each gesture that starts with a secondary button press.
        rt::Task<> OnRightClickDown(PointerInputScope& s, std::function<void(Offset)> onDown) {
            for (;;) {
                std::optional<PointerInputChange> down;
                while (!down) {
                    auto e = co_await s.AwaitPointerEvent();
                    if (!e) co_return;
                    bool allDown = !e->changes.empty();
                    for (const auto& ch : e->changes) allDown = allDown && ch.ChangedToDown();
                    if (e->buttons.IsSecondaryPressed() && allDown) down = e->changes[0];
                }
                down->Consume();   // shares the event's consumed flag
                onDown(down->position);
                // waitForUpOrCancellation: the up is consumed; a consumed move ends the wait.
                for (;;) {
                    auto e = co_await s.AwaitPointerEvent();
                    if (!e) co_return;
                    bool allUp = true, consumed = false;
                    for (const auto& ch : e->changes) { allUp = allUp && !ch.pressed; consumed = consumed || ch.IsConsumed(); }
                    if (allUp) { for (auto& ch : e->changes) ch.Consume(); break; }
                    if (consumed) break;
                }
            }
        }
    }

    Modifier ShowTextContextMenuOnSecondaryClick(Modifier m, std::function<rt::Task<>(Offset)> onPreShowContextMenu) {
        auto& c = *rt::Composition::Current();
        TextContextMenuProvider* provider = c.Read(LocalTextContextMenuDropdownProvider);
        auto& anchor = c.Remember<std::shared_ptr<GestureAnchor>>([] { return std::make_shared<GestureAnchor>(); });
        auto pre = std::make_shared<std::function<rt::Task<>(Offset)>>(std::move(onPreShowContextMenu));
        auto a = anchor;
        m = PointerInput(std::move(m), rt::Keys(reinterpret_cast<std::uintptr_t>(provider), reinterpret_cast<std::uintptr_t>(a.get())),
                         [provider, a, pre](PointerInputScope& s) {
            return OnRightClickDown(s, [provider, a, pre](Offset local) {
                if (!provider) return;
                auto data = std::make_shared<ClickDataProvider>(a, local);
                a->job = rt::Launch(rt::Dispatcher::Main(), [](TextContextMenuProvider* p, std::shared_ptr<std::function<rt::Task<>(Offset)>> pre,
                                                      std::shared_ptr<ClickDataProvider> data, Offset local) -> rt::Task<> {
                    if (*pre) co_await (*pre)(local);
                    co_await p->ShowTextContextMenu(data);
                }(provider, pre, data, local));
            });
        });
        return m.With<GestureAnchorNode>(GestureAnchorParams{ anchor });
    }

}
