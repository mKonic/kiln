// SplitButtonSamples.kt
#include "SplitButtonSamples.h"
#include <kiln/Function.h>
#include "Common.h"
#include "Shared.h"

#include <kiln/foundation/Animate.h>
#include <kiln/foundation/Semantics.h>
#include <kiln/material3/Button.h>
#include <kiln/material3/Divider.h>
#include <kiln/material3/Menu.h>
#include <kiln/material3/SplitButton.h>

namespace catalog::samples {
    namespace SD = m3::SplitButtonDefaults;
    namespace BD = m3::ButtonDefaults;

    namespace {
        using LeadingFn = void (*)(kiln::Function<void()>, const Modifier&, const m3::SplitButtonOptions&, kiln::Function<void()>);
        using TrailingFn = void (*)(bool, kiln::Function<void(bool)>, const Modifier&, const m3::SplitButtonOptions&, kiln::Function<void()>);

        // The edit icon, a spacer and the label (sized for `height` when given).
        kiln::Function<void()> EditAndLabel(const char* label, std::optional<Dp> height = std::nullopt) {
            return [label, height] {
                const Dp iconSize = height ? SD::leadingButtonIconSizeFor(*height) : SD::LeadingIconSize;
                m3::Icon(Icons::Filled::Edit(), std::string("Localized description"), Size({}, iconSize));
                Spacer(Size({}, height ? BD::iconSpacingFor(*height) : BD::IconSpacing));
                if (height) TextStyled(label, BD::textStyleFor(*height));
                else m3::Text(label);
            };
        }

        // The checkable trailing button: its arrow turns over while checked; a tooltip names it.
        void ToggleTrailing(TrailingFn trailing, kiln::rt::State<bool>& checked, Modifier modifier = {}, const m3::SplitButtonOptions& options = {},
                            std::optional<Dp> height = std::nullopt) {
            static const std::string description = "Toggle Button";
            PlainTooltipAbove(description, [trailing, &checked, modifier, options, height] {
                const bool c = checked.Get();
                trailing(c, [&checked](bool v) { checked.Set(v); }, Semantics(modifier, [c](SemanticsConfiguration& s) {
                             s.stateDescription = c ? "Expanded" : "Collapsed";
                             s.contentDescription = { description };
                         }),
                         options, [c, height] {
                             const float rotation = AnimateFloatAsState(c ? 180.f : 0.f);
                             const Dp size = height ? SD::trailingButtonIconSizeFor(*height) : SD::TrailingIconSize;
                             m3::Icon(Icons::Filled::KeyboardArrowDown(), std::string("Localized description"),
                                      GraphicsLayer(Size({}, size), [rotation](GraphicsLayerParams& p) { p.rotationZ = rotation; }));
                         });
            });
        }

        // The variant samples: an edit + label leading button and a checkable trailing one.
        void Variant(LeadingFn leading, TrailingFn trailing) {
            auto& checked = RememberSaveableState(false);
            m3::SplitButtonLayout([leading] { leading([] { /* Do Nothing */ }, {}, {}, EditAndLabel("My Button")); },
                                  [trailing, &checked] { ToggleTrailing(trailing, checked); });
        }

        // The size samples: both buttons at `size` high.
        void Sized(Dp size, bool fillMaxHeight = false) {
            auto& checked = RememberSaveableState(false);
            m3::SplitButtonLayout(
                [size] {
                    m3::SplitButtonOptions o;
                    o.shapes = SD::leadingButtonShapesFor(size);
                    o.contentPadding = SD::leadingButtonContentPaddingFor(size);
                    SD::LeadingButton([] { /* Do Nothing */ }, HeightIn({}, size), o, EditAndLabel("Label", size));
                },
                [size, fillMaxHeight, &checked] {
                    m3::SplitButtonOptions o;
                    o.shapes = SD::trailingButtonShapesFor(size);
                    o.contentPadding = SD::trailingButtonContentPaddingFor(size);
                    const Modifier m = fillMaxHeight ? HeightIn(FillMax({}, Axis::Vertical), size) : HeightIn({}, size);
                    ToggleTrailing(SD::TrailingButton, checked, m, o, size);
                });
        }
    }

    void FilledSplitButtonSample() { Variant(SD::LeadingButton, SD::TrailingButton); }

    void SplitButtonWithUnCheckableTrailingButtonSample() {
        m3::SplitButtonLayout([] { SD::LeadingButton([] { /* Do Nothing */ }, {}, {}, EditAndLabel("My Button")); },
                              [] {
                                  static const std::string description = "Toggle Button";
                                  PlainTooltipAbove(description, [] {
                                      SD::TrailingButton([] { /* Do Nothing */ }, {}, {}, [] {
                                          m3::Icon(Icons::Filled::KeyboardArrowDown(), description, Size({}, SD::TrailingIconSize));
                                      });
                                  });
                              });
    }

    void SplitButtonWithDropdownMenuSample() {
        auto& checked = RememberSaveableState(false);
        Box(WrapContent(FillMax({}, Axis::Both), Axis::Both), Alignment::TopStart(), false, [&checked] {
            m3::SplitButtonLayout([] { SD::LeadingButton([] { /* Do Nothing */ }, {}, {}, EditAndLabel("My Button")); },
                                  [&checked] { ToggleTrailing(SD::TrailingButton, checked); });
            m3::DropdownMenu(checked.Get(), [&checked] { checked.Set(false); }, {}, {}, [] {
                const auto item = [](const char* text, const ImageVectorPtr* icon, kiln::Function<void()> trailing) {
                    m3::DropdownMenuItemOptions o;
                    o.leadingIcon = [icon] { m3::Icon(*icon, std::nullopt); };
                    o.trailingIcon = std::move(trailing);
                    m3::DropdownMenuItem([text] { m3::Text(text); }, [] {}, {}, o);
                };
                item("Edit", &Icons::Outlined::Edit(), {});
                item("Settings", &Icons::Outlined::Settings(), {});
                m3::HorizontalDivider();
                item("Send Feedback", &Icons::Outlined::Email(), [] {
                    m3::TextOptions t;
                    t.textAlign = kiln::text::TextAlign::Center;
                    m3::Text("F11", {}, t);
                });
            });
        });
    }

    void TonalSplitButtonSample() { Variant(SD::TonalLeadingButton, SD::TonalTrailingButton); }
    void ElevatedSplitButtonSample() { Variant(SD::ElevatedLeadingButton, SD::ElevatedTrailingButton); }
    void OutlinedSplitButtonSample() { Variant(SD::OutlinedLeadingButton, SD::OutlinedTrailingButton); }

    void SplitButtonWithTextSample() {
        auto& checked = RememberSaveableState(false);
        m3::SplitButtonLayout([] { SD::LeadingButton([] { /* Do Nothing */ }, {}, {}, [] { m3::Text("My Button"); }); },
                              [&checked] { ToggleTrailing(SD::TrailingButton, checked); });
    }

    void SplitButtonWithIconSample() {
        auto& checked = RememberSaveableState(false);
        m3::SplitButtonLayout(
            [] {
                static const std::string description = "Button";
                PlainTooltipAbove(description, [] {
                    SD::LeadingButton([] { /* Do Nothing */ }, {}, {},
                                      [] { m3::Icon(Icons::Filled::Edit(), description, Size({}, SD::LeadingIconSize)); });
                });
            },
            [&checked] { ToggleTrailing(SD::TrailingButton, checked); });
    }

    void XSmallFilledSplitButtonSample() { Sized(SD::ExtraSmallContainerHeight); }
    void MediumFilledSplitButtonSample() { Sized(SD::MediumContainerHeight); }
    void LargeFilledSplitButtonSample() { Sized(SD::LargeContainerHeight); }
    void ExtraLargeFilledSplitButtonSample() { Sized(SD::ExtraLargeContainerHeight, true); }
}
