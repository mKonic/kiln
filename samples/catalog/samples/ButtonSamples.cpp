// ButtonSamples.kt
#include "ButtonSamples.h"
#include "Common.h"

#include <kiln/material3/Button.h>

namespace catalog::samples {
    namespace BD = m3::ButtonDefaults;

    namespace {
        m3::ButtonOptions Shapes() {
            m3::ButtonOptions o;
            o.shapes = BD::shapes();
            return o;
        }
        void Label(const char* text) {
            m3::Text(text);
        }
        // The sized samples: an Edit icon and "Label" at the container height's sizes.
        void SizedButtonWithIcon(Dp size, bool styledText) {
            m3::ButtonOptions o;
            o.contentPadding = BD::contentPaddingFor(size, true);
            m3::Button([] {}, HeightIn({}, size), o, [size, styledText] {
                m3::Icon(Icons::Filled::Edit(), std::string("Localized description"), Size({}, BD::iconSizeFor(size)));
                Spacer(Size({}, BD::iconSpacingFor(size)));
                if (styledText) TextStyled("Label", BD::textStyleFor(size));
                else m3::Text("Label");
            });
        }
    }

    void ButtonSample() { m3::Button([] {}, {}, {}, [] { Label("Button"); }); }
    void ButtonWithAnimatedShapeSample() { m3::Button([] {}, {}, Shapes(), [] { Label("Button"); }); }
    void SquareButtonSample() {
        m3::ButtonOptions o;
        o.shape = BD::squareShape();
        m3::Button([] {}, {}, o, [] { Label("Button"); });
    }
    void SmallButtonSample() {
        m3::ButtonOptions o;
        o.contentPadding = BD::SmallContentPadding();
        m3::Button([] {}, {}, o, [] { Label("Button"); });
    }
    void ElevatedButtonSample() { m3::ElevatedButton([] {}, {}, {}, [] { Label("Elevated Button"); }); }
    void ElevatedButtonWithAnimatedShapeSample() { m3::ElevatedButton([] {}, {}, Shapes(), [] { Label("Elevated Button"); }); }
    void FilledTonalButtonSample() { m3::FilledTonalButton([] {}, {}, {}, [] { Label("Filled Tonal Button"); }); }
    void FilledTonalButtonWithAnimatedShapeSample() { m3::FilledTonalButton([] {}, {}, Shapes(), [] { Label("Filled Tonal Button"); }); }
    void OutlinedButtonSample() { m3::OutlinedButton([] {}, {}, {}, [] { Label("Outlined Button"); }); }
    void OutlinedButtonWithAnimatedShapeSample() { m3::OutlinedButton([] {}, {}, Shapes(), [] { Label("Outlined Button"); }); }
    void TextButtonSample() { m3::TextButton([] {}, {}, {}, [] { Label("Text Button"); }); }
    void TextButtonWithAnimatedShapeSample() { m3::TextButton([] {}, {}, Shapes(), [] { Label("Text Button"); }); }

    void ButtonWithIconSample() {
        m3::ButtonOptions o;
        o.contentPadding = BD::contentPaddingFor(BD::MinHeight(), true);
        m3::Button([] {}, {}, o, [] {
            m3::Icon(Icons::Filled::Favorite(), std::string("Localized description"), Size({}, BD::iconSizeFor(BD::MinHeight())));
            Spacer(Size({}, BD::iconSpacingFor(BD::MinHeight())));
            m3::Text("Like");
        });
    }
    void XSmallButtonWithIconSample() { SizedButtonWithIcon(BD::ExtraSmallContainerHeight, false); }
    void MediumButtonWithIconSample() { SizedButtonWithIcon(BD::MediumContainerHeight, true); }
    void LargeButtonWithIconSample() { SizedButtonWithIcon(BD::LargeContainerHeight, true); }
    void XLargeButtonWithIconSample() { SizedButtonWithIcon(BD::ExtraLargeContainerHeight, true); }
}
