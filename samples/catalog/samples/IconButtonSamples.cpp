// IconButtonSamples.kt: every sample is an icon button with a tooltip (for accessibility).
#include "IconButtonSamples.h"
#include <kiln/Function.h>
#include "Common.h"
#include "Shared.h"

#include <kiln/foundation/VectorPainter.h>
#include <kiln/material3/IconButton.h>

namespace catalog::samples {
    namespace ID = m3::IconButtonDefaults;

    namespace {
        const std::string description = "Localized description";

        using ButtonFn = void (*)(kiln::Function<void()>, const Modifier&, const m3::IconButtonOptions&, kiln::Function<void()>);
        using ToggleFn = void (*)(bool, kiln::Function<void(bool)>, const Modifier&, const m3::IconToggleButtonOptions&, kiln::Function<void()>);

        void Lock(const Modifier& modifier = {}) { m3::Icon(Icons::Filled::Lock(), description, modifier); }

        void Button(ButtonFn button, bool animatedShape) {
            PlainTooltipAbove(description, [button, animatedShape] {
                m3::IconButtonOptions o;
                if (animatedShape) o.shapes = ID::shapes();
                button([] {}, {}, o, [] { Lock(); });
            });
        }

        void Toggle(ToggleFn toggle, bool animatedShape) {
            auto& checked = RememberSaveableState(false);
            PlainTooltipAbove(description, [toggle, animatedShape, &checked] {
                m3::IconToggleButtonOptions o;
                if (animatedShape) o.shapes = ID::toggleableShapes();
                toggle(checked.Get(), [&checked](bool v) { checked.Set(v); }, {}, o, [&checked] {
                    m3::Icon(checked.Get() ? Icons::Filled::Lock() : Icons::Outlined::Lock(), description);
                });
            });
        }
    }

    void IconButtonSample() { Button(m3::IconButton, false); }
    void IconButtonWithAnimatedShapeSample() { Button(m3::IconButton, true); }

    void ExtraSmallNarrowSquareIconButtonsSample() {
        PlainTooltipAbove(description, [] {
            // Small narrow round icon button
            m3::IconButtonOptions o;
            o.shape = ID::extraSmallSquareShape();
            const DpSize size = ID::extraSmallContainerSize(m3::IconButtonWidthOption::Narrow);
            m3::FilledIconButton([] {}, SizeMod(m3::MinimumInteractiveComponentSize({}), size.width, size.height), o,
                                 [] { Lock(Size({}, ID::ExtraSmallIconSize)); });
        });
    }

    void MediumRoundWideIconButtonSample() {
        PlainTooltipAbove(description, [] {
            m3::IconButtonOptions o;
            o.shape = ID::mediumRoundShape();
            const DpSize size = ID::mediumContainerSize(m3::IconButtonWidthOption::Wide);
            m3::IconButton([] {}, SizeMod({}, size.width, size.height), o, [] { Lock(Size({}, ID::MediumIconSize)); });
        });
    }

    void LargeRoundUniformOutlinedIconButtonSample() {
        PlainTooltipAbove(description, [] {
            m3::IconButtonOptions o;
            o.shape = ID::largeRoundShape();
            const DpSize size = ID::largeContainerSize();
            m3::OutlinedIconButton([] {}, SizeMod({}, size.width, size.height), o, [] { Lock(Size({}, ID::LargeIconSize)); });
        });
    }

    void TintedIconButtonSample() {
        PlainTooltipAbove(description, [] {
            m3::IconButton([] {}, {}, {}, [] { m3::Icon(RememberVectorPainter(Icons::Filled::Lock()), description, {}, Color(0xFFFF0000u)); });
        });
    }

    void IconToggleButtonSample() { Toggle(m3::IconToggleButton, false); }
    void IconToggleButtonWithAnimatedShapeSample() { Toggle(m3::IconToggleButton, true); }
    void FilledIconButtonSample() { Button(m3::FilledIconButton, false); }
    void FilledIconButtonWithAnimatedShapeSample() { Button(m3::FilledIconButton, true); }
    void FilledIconToggleButtonSample() { Toggle(m3::FilledIconToggleButton, false); }
    void FilledIconToggleButtonWithAnimatedShapeSample() { Toggle(m3::FilledIconToggleButton, true); }
    void FilledTonalIconButtonSample() { Button(m3::FilledTonalIconButton, false); }
    void FilledTonalIconButtonWithAnimatedShapeSample() { Button(m3::FilledTonalIconButton, true); }
    void FilledTonalIconToggleButtonSample() { Toggle(m3::FilledTonalIconToggleButton, false); }
    void FilledTonalIconToggleButtonWithAnimatedShapeSample() { Toggle(m3::FilledTonalIconToggleButton, true); }
    void OutlinedIconButtonSample() { Button(m3::OutlinedIconButton, false); }
    void OutlinedIconButtonWithAnimatedShapeSample() { Button(m3::OutlinedIconButton, true); }
    void OutlinedIconToggleButtonSample() { Toggle(m3::OutlinedIconToggleButton, false); }
    void OutlinedIconToggleButtonWithAnimatedShapeSample() { Toggle(m3::OutlinedIconToggleButton, true); }
}
