#pragma once

// magnifier (Compose's Magnifier.android.kt over PlatformMagnifier): the platform magnifier shown
// over a source point in the node's space, refreshed after every frame the node draws. Text
// selection uses the animated variant (SelectionMagnifier.kt): the centre springs between lines
// and snaps along one.

#include "kiln/animation/Animatable.h"
#include <kiln/Function.h>
#include "kiln/foundation/Compose.h"
#include "kiln/ui/LayoutNode.h"

#include <cmath>
#include <functional>
#include <memory>

namespace kiln::foundation {

    struct MagnifierParams {
        // sourceCenter / magnifierCenter in the node's space; NaN hides / uses the default.
        kiln::Function<Offset(const ui::Density&)> sourceCenter;
        kiln::Function<Offset(const ui::Density&)> magnifierCenter;
        kiln::Function<void(ui::Size dpSize)> onSizeChanged;
        float zoom = NAN;
        bool useTextDefault = false;
        Dp width = Dp::Unspecified(), height = Dp::Unspecified(), cornerRadius = Dp::Unspecified(), elevation = Dp::Unspecified();
        bool clippingEnabled = true;
    };
    Modifier MagnifierMod(Modifier m, MagnifierParams params);

    // MagnifierNode's work, for nodes that host a magnifier: Update from their draw (it reads
    // what the centre reads), Detach when they go.
    class MagnifierController {
    public:
        ~MagnifierController() { Detach(); }
        // anchorInRoot: the node's position in the root (NaN: not placed).
        void Update(const MagnifierParams& p, ui::Owner* owner, Offset anchorInRoot, const ui::Density& density);
        void Detach();
        ui::IntSize Size() const { return m_PreviousSize; }
    private:
        void Recreate(const MagnifierParams& p, const ui::Density& density);
        void UpdateSizeIfNecessary(const MagnifierParams& p, const ui::Density& density);
        void ScheduleContentUpdate();
        ui::Owner* m_Owner = nullptr;
        int m_Handle = 0;
        ui::Owner::MagnifierConfig m_Config;
        float m_Zoom = NAN;
        ui::Density m_Density{ -1, -1 };
        ui::IntSize m_PreviousSize{ -1, -1 };
        bool m_ContentPosted = false;
        bool m_ContentRequested = false;
        rt::Job m_ContentJob;   // kept: a dropped Job cancels its task
        std::shared_ptr<char> m_Alive = std::make_shared<char>(0);
    };

    // AnimatedSelectionMagnifierNode with a text-default MagnifierNode.
    class SelectionMagnifier {
    public:
        SelectionMagnifier();
        ~SelectionMagnifier();
        // From the host node's draw: `center` (in the node's space, NaN: hidden) for the
        // magnifier's size.
        void Update(const kiln::Function<Offset(ui::IntSize magnifierSize)>& center, ui::Owner* owner, Offset anchorInRoot, const ui::Density& density);
        void Detach();
    private:
        anim::Animatable m_Center;
        std::optional<Offset> m_Target;
        // Detach does nothing while detached: its snap is a state write, which would ask for a
        // frame on every draw of a field without a magnifier.
        bool m_Attached = true;   // the first Detach resets the centre
        rt::Job m_Animation;
        MagnifierController m_Controller;
        ui::IntSize m_MagnifierSize{ 0, 0 };
    };

}
