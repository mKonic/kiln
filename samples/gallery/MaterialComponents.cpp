#include "MaterialComponents.h"

#include <kiln/foundation/Compose.h>
#include <kiln/foundation/Flow.h>
#include <kiln/material3/Badge.h>
#include <kiln/material3/Button.h>
#include <kiln/material3/Divider.h>
#include <kiln/material3/DynamicColorScheme.h>
#include <kiln/material3/IconButton.h>
#include <kiln/material3/MaterialTheme.h>
#include <kiln/material3/Surface.h>
#include <kiln/material3/ToggleButton.h>
#include <kiln/material3/Text.h>
#include <kiln/material3/internal/Icons.h>

namespace kiln::gallery {

    using namespace kiln::foundation;
    using namespace kiln::ui;
    namespace m3 = kiln::material3;

    namespace {
        void Buttons() {
            auto& clicks = RememberState(0);
            auto* count = &clicks;
            FlowRow({}, Arrangement::SpacedBy(8_dp), Arrangement::SpacedBy(8_dp), AlignmentV::CenterVertically(), [count] {
                auto onClick = [count] { count->Set(count->Peek() + 1); };
                m3::Button(onClick, {}, {}, [] { m3::Text("Filled"); });
                m3::ElevatedButton(onClick, {}, {}, [] { m3::Text("Elevated"); });
                m3::FilledTonalButton(onClick, {}, {}, [] { m3::Text("Tonal"); });
                m3::OutlinedButton(onClick, {}, {}, [] { m3::Text("Outlined"); });
                m3::TextButton(onClick, {}, {}, [] { m3::Text("Text"); });
                m3::ButtonOptions expressive;
                expressive.shapes = m3::ButtonDefaults::shapesFor(m3::ButtonDefaults::MediumContainerHeight);
                expressive.contentPadding = m3::ButtonDefaults::contentPaddingFor(m3::ButtonDefaults::MediumContainerHeight);
                // A minimum height, not a fixed height: let the label grow at large font scales.
                m3::Button(onClick, DefaultMinSize({}, Dp::Unspecified(), m3::ButtonDefaults::MediumContainerHeight), expressive, [] {
                    m3::TextOptions text;
                    text.style = m3::ButtonDefaults::textStyleFor(m3::ButtonDefaults::MediumContainerHeight);
                    m3::Text("Expressive", {}, text);
                });
                m3::ButtonOptions disabled;
                disabled.enabled = false;
                m3::Button(onClick, {}, disabled, [] { m3::Text("Disabled"); });
                m3::BadgedBox([count] { m3::Badge({}, std::nullopt, std::nullopt, [count] { m3::Text(std::to_string(count->Get())); }); },
                              Padding({}, 8_dp, 16_dp), [] { m3::Icon(m3::internal::Icons::Filled::Check(), std::string("Clicks")); });
            });
        }

        void ToggleButtons() {
            auto& checked = RememberState(false);
            auto* on = &checked;
            FlowRow({}, Arrangement::SpacedBy(8_dp), Arrangement::SpacedBy(8_dp), AlignmentV::CenterVertically(), [on] {
                auto change = [on](bool value) { on->Set(value); };
                m3::ToggleButtonOptions icon;
                icon.icon = [] { m3::Icon(m3::internal::Icons::Filled::Check(), std::nullopt); };
                m3::ToggleButton(on->Get(), change, {}, icon, [] { m3::Text("Filled toggle"); });
                m3::ElevatedToggleButton(on->Get(), change, {}, {}, [] { m3::Text("Elevated toggle"); });
                m3::ToggleButtonOptions medium;
                medium.buttonSize = m3::ToggleButtonSize::Medium;
                m3::FilledTonalToggleButton(on->Get(), change, {}, medium, [] { m3::Text("Tonal toggle"); });
                m3::OutlinedToggleButton(on->Get(), change, {}, {}, [] { m3::Text("Outlined toggle"); });
                m3::ToggleButtonOptions disabled;
                disabled.enabled = false;
                m3::ToggleButton(on->Get(), change, {}, disabled, [] { m3::Text("Disabled toggle"); });
            });
            m3::Text(checked.Get() ? "Text toggles: on" : "Text toggles: off");
        }

        void IconButtons() {
            auto& checked = RememberState(false);
            auto* on = &checked;
            auto icon = [](const char* description) {
                return [description] { m3::Icon(m3::internal::Icons::Filled::Check(), std::string(description)); };
            };
            FlowRow({}, Arrangement::SpacedBy(8_dp), Arrangement::SpacedBy(8_dp), AlignmentV::CenterVertically(), [on, icon] {
                auto change = [on](bool value) { on->Set(value); };
                m3::IconButton([] {}, {}, {}, icon("Standard icon button"));
                m3::FilledIconButton([] {}, {}, {}, icon("Filled icon button"));
                m3::FilledTonalIconButton([] {}, {}, {}, icon("Tonal icon button"));
                m3::OutlinedIconButton([] {}, {}, {}, icon("Outlined icon button"));
                m3::IconToggleButtonOptions toggle;
                toggle.shapes = m3::IconButtonDefaults::toggleableShapes();
                m3::IconToggleButton(on->Get(), change, {}, toggle, icon("Standard icon toggle"));
                m3::FilledIconToggleButton(on->Get(), change, {}, toggle, icon("Filled icon toggle"));
                m3::FilledTonalIconToggleButton(on->Get(), change, {}, toggle, icon("Tonal icon toggle"));
                m3::OutlinedIconToggleButton(on->Get(), change, {}, toggle, icon("Outlined icon toggle"));
                m3::IconButtonOptions disabled;
                disabled.enabled = false;
                m3::FilledIconButton([] {}, {}, disabled, icon("Disabled icon button"));
            });
            m3::Text(checked.Get() ? "Icon toggles: on" : "Icon toggles: off");
        }
    }

    void MaterialComponents() {
        auto& theme = RememberState(0);
        const bool dark = IsSystemInDarkTheme();
        m3::MaterialThemeArgs args;
        if (theme.Get() == 1) args.colorScheme = m3::DarkColorScheme();
        else if (theme.Get() == 2) {
            auto dynamic = dark ? m3::DynamicDarkColorScheme() : m3::DynamicLightColorScheme();
            args.colorScheme = dynamic.value_or(dark ? m3::DarkColorScheme() : m3::LightColorScheme());
        } else args.colorScheme = m3::LightColorScheme();
        auto* selection = &theme;
        m3::MaterialTheme(args, [selection] {
            m3::Surface(FillMax({}, Axis::Horizontal), {}, [selection] {
                Column(Padding({}, 16_dp), Arrangement::SpacedBy(8_dp), AlignmentH::Start(), [selection] {
                    FlowRow({}, Arrangement::SpacedBy(8_dp), Arrangement::SpacedBy(8_dp), AlignmentV::CenterVertically(), [selection] {
                        m3::TextButton([selection] { selection->Set(0); }, {}, {}, [] { m3::Text("Light"); });
                        m3::TextButton([selection] { selection->Set(1); }, {}, {}, [] { m3::Text("Dark"); });
                        m3::TextButton([selection] { selection->Set(2); }, {}, {}, [] { m3::Text("Dynamic"); });
                    });
                    m3::Text("Toggle buttons");
                    ToggleButtons();
                    m3::HorizontalDivider();
                    m3::Text("Material buttons");
                    Buttons();
                    m3::HorizontalDivider();
                    m3::Text("Icon buttons");
                    IconButtons();
                });
            });
        });
    }

}
