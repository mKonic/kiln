#include "kiln/foundation/contextmenu/ContextMenuUi.h"
#include "kiln/foundation/Shapes.h"

#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Scroll.h"
#include "kiln/ui/Configuration.h"
#include "kiln/ui/LayoutNode.h"
#include "kiln/ui/Locals.h"

namespace kiln::foundation {

    TextStyle ContextMenuSpec::LabelTextStyle(Color color) {
        TextStyle s(text::Sp(14), color);
        s.span.fontWeight = text::FontWeight::Medium();
        s.span.letterSpacing = text::Sp(0.1f);
        s.paragraph.lineHeight = text::Sp(20);
        s.paragraph.textAlign = text::TextAlign::Start;
        return s;
    }

    ContextMenuColors DefaultContextMenuColors() {
        const Color black{ 0xFF000000u }, disabled{ 0x61000000u };   // 0.38 alpha
        return { Color{ 0xFFFFFFFFu }, black, black, disabled, disabled };
    }

    ContextMenuColors ComputeContextMenuColors() {
        auto* c = rt::Composition::Current();
        ui::Owner* owner = c ? c->Read(ui::LocalOwner) : nullptr;
        const ui::Configuration config = c ? c->Read(ui::LocalConfiguration) : ui::Configuration{};
        auto& cached = c->Remember<ContextMenuColors>([owner] {
            ContextMenuColors colors = DefaultContextMenuColors();
            if (owner && owner->ContextMenu().colors) {
                const auto h = owner->ContextMenu().colors();
                if (h.background) colors.backgroundColor = Color(h.background);
                if (h.text) colors.textColor = colors.iconColor = Color(h.text);
                if (h.disabledText) colors.disabledTextColor = colors.disabledIconColor = Color(h.disabledText);
            }
            return colors;
        }, rt::Keys(reinterpret_cast<std::uintptr_t>(owner), config.uiMode, config.densityDpi, config.locales.empty() ? std::string() : config.locales.front()));
        return cached;
    }

    void ContextMenuScope::Item(std::string label, std::function<void()> onClick, bool enabled, Modifier modifier,
                                std::function<void(Color)> leadingIcon) {
        if (label.find_first_not_of(" \t\n") == std::string::npos) throw std::logic_error("Label must not be blank");
        m_Entries.push_back({ false, std::move(label), enabled, std::move(modifier), std::move(leadingIcon), std::move(onClick) });
    }

    void ContextMenuScope::Separator() { m_Entries.push_back({ true }); }

    void ContextMenuScope::Content(const ContextMenuColors& colors) const {
        int i = 0;
        for (const Entry& e : m_Entries) {
            const rt::Key key(i++);
            if (e.separator) {
                Box(Background(Height(FillMax(Padding(Modifier{}, { 0_dp, Dp(ContextMenuSpec::DividerVerticalPaddingDp), 0_dp, Dp(ContextMenuSpec::DividerVerticalPaddingDp) }),
                                              Axis::Horizontal), Dp(ContextMenuSpec::DividerHeightDp)), colors.iconColor),
                    Alignment::TopStart(), false, {}, key);
            } else {
                ContextMenuItem(e.label, e.enabled, colors, e.modifier, e.leadingIcon, e.onClick);
            }
        }
    }

    void ContextMenuColumnBuilder(const Modifier& modifier, const ContextMenuColors& colors,
                                  const std::function<void(ContextMenuScope&)>& builder) {
        ContextMenuScope scope;
        builder(scope);
        auto& scroll = RememberScrollState();
        // ContextMenuColumn: a 3 dp shadow of the rounded 4 dp container (which clips to it), as
        // wide as its widest item, scrolling when taller than the window.
        Modifier m = ShadowMod(modifier, Dp(ContextMenuSpec::MenuContainerElevationDp), RoundedCorner(Dp(ContextMenuSpec::CornerRadiusDp)));
        m = Background(std::move(m), colors.backgroundColor);
        m = Width(std::move(m), IntrinsicSize::Max);
        m = Padding(std::move(m), 0_dp, Dp(ContextMenuSpec::VerticalPaddingDp));
        m = VerticalScroll(std::move(m), scroll);
        Column(m, [scope, colors] { scope.Content(colors); });
    }

    void ContextMenuItem(const std::string& label, bool enabled, const ContextMenuColors& colors, const Modifier& modifier,
                         const std::function<void(Color)>& leadingIcon, std::function<void()> onClick) {
        ClickableOptions opts;
        opts.enabled = enabled;
        opts.onClickLabel = label;
        Modifier m = Clickable(modifier, [enabled, onClick] { if (enabled && onClick) onClick(); }, opts);
        m = FillMax(std::move(m), Axis::Horizontal);
        m = SizeIn(std::move(m), Dp(ContextMenuSpec::ContainerWidthMinDp), Dp(ContextMenuSpec::ListItemHeightDp),
                   Dp(ContextMenuSpec::ContainerWidthMaxDp), Dp(ContextMenuSpec::ListItemHeightDp));
        m = Padding(std::move(m), Dp(ContextMenuSpec::HorizontalPaddingDp), 0_dp);
        Row(m, Arrangement::SpacedBy(Dp(ContextMenuSpec::HorizontalPaddingDp)), AlignmentV::CenterVertically(),
            [label, enabled, colors, leadingIcon] {
            if (leadingIcon) {
                const Color iconColor = enabled ? colors.iconColor : colors.disabledIconColor;
                Box(SizeIn(Modifier{}, Dp(ContextMenuSpec::IconSizeDp), Dp::Unspecified(), Dp(ContextMenuSpec::IconSizeDp),
                           Dp(ContextMenuSpec::IconSizeDp), false),
                    Alignment::TopStart(), false, [leadingIcon, iconColor] { leadingIcon(iconColor); });
            }
            BasicTextOptions o;
            o.maxLines = 1;
            BasicText(label, Weight(Modifier{}, 1, true),
                      ContextMenuSpec::LabelTextStyle(enabled ? colors.textColor : colors.disabledTextColor), o);
        });
    }

    void ContextMenuPopup(std::shared_ptr<const PopupPositionProvider> positionProvider, std::function<void()> onDismiss,
                          const Modifier& modifier, std::function<void(ContextMenuScope&)> builder, Key key) {
        rt::Composition::Current()->Child([positionProvider, onDismiss, modifier, builder] {
            const ContextMenuColors colors = ComputeContextMenuColors();
            PopupProperties p;
            p.focusable = true;
            Popup(positionProvider, onDismiss, p, [modifier, colors, builder] { ContextMenuColumnBuilder(modifier, colors, builder); });
        }, key);
    }

    namespace {
        bool FitsBetweenPositionAndStartEdge(int position, int popupLength, int windowLength, bool closeAffinity) {
            return closeAffinity ? popupLength <= position : windowLength - popupLength > position;
        }
        int AlignStartEdgeToPosition(int position, int popupLength, bool closeAffinity) {
            return closeAffinity ? position : position - popupLength;
        }
        int AlignStartEdges(int popupLength, int windowLength, bool closeAffinity) {
            return closeAffinity ? 0 : windowLength - popupLength;
        }
    }

    int AlignPopupAxis(int position, int popupLength, int windowLength, bool closeAffinity) {
        if (popupLength >= windowLength) return AlignStartEdges(popupLength, windowLength, closeAffinity);
        if (FitsBetweenPositionAndStartEdge(position, popupLength, windowLength, !closeAffinity))
            return AlignStartEdgeToPosition(position, popupLength, closeAffinity);
        if (FitsBetweenPositionAndStartEdge(position, popupLength, windowLength, closeAffinity))
            return AlignStartEdgeToPosition(position, popupLength, !closeAffinity);
        return AlignStartEdges(popupLength, windowLength, !closeAffinity);
    }

    IntOffset ContextMenuPopupPositionProvider::CalculatePosition(ui::IntRect anchorBounds, ui::IntSize windowSize, LayoutDirection dir,
                                                                  ui::IntSize popupContentSize) const {
        const IntOffset anchor = m_AnchorPosition ? m_AnchorPosition() : IntOffset{};
        const IntOffset result{
            AlignPopupAxis(anchorBounds.left + anchor.x, popupContentSize.width, windowSize.width, dir == LayoutDirection::Ltr),
            AlignPopupAxis(anchorBounds.top + anchor.y, popupContentSize.height, windowSize.height),
        };
        if (m_OnPositionCalculated)
            m_OnPositionCalculated(anchor, { result.x, result.y, result.x + popupContentSize.width, result.y + popupContentSize.height });
        return result;
    }

}
