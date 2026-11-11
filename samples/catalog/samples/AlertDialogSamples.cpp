// AlertDialogSamples.kt
#include "AlertDialogSamples.h"
#include "Common.h"

#include <kiln/material3/AlertDialog.h>
#include <kiln/material3/Button.h>
#include <kiln/material3/Surface.h>

namespace catalog::samples {
    namespace {
        // The open button, then the dialog while open (it starts open).
        kiln::rt::State<bool>& OpenDialogButton() {
            auto& openDialog = RememberState(true);
            m3::Button([&openDialog] { openDialog.Set(true); }, {}, {}, [] { m3::Text("Open dialog"); });
            return openDialog;
        }
        // Dismiss the dialog when the user clicks outside the dialog or on the back button. If
        // you want to disable that functionality, simply use an empty onDismissRequest.
        void Alert(kiln::rt::State<bool>& openDialog, m3::AlertDialogOptions o) {
            o.dismissButton = [&openDialog] { m3::TextButton([&openDialog] { openDialog.Set(false); }, {}, {}, [] { m3::Text("Dismiss"); }); };
            m3::AlertDialog([&openDialog] { openDialog.Set(false); },
                            [&openDialog] { m3::TextButton([&openDialog] { openDialog.Set(false); }, {}, {}, [] { m3::Text("Confirm"); }); }, {}, o);
        }
    }

    void AlertDialogSample() {
        auto& openDialog = OpenDialogButton();
        if (!openDialog.Get()) return;
        m3::AlertDialogOptions o;
        o.title = [] { m3::Text("Title"); };
        o.text = [] { m3::Text("Turned on by default"); };
        Alert(openDialog, o);
    }

    void AlertDialogWithIconSample() {
        auto& openDialog = OpenDialogButton();
        if (!openDialog.Get()) return;
        m3::AlertDialogOptions o;
        o.icon = [] { m3::Icon(Icons::Filled::Favorite(), std::nullopt, Size({}, m3::AlertDialogDefaults::IconSize)); };
        o.title = [] { m3::Text("Title"); };
        o.text = [] { m3::Text("This area typically contains the supportive text which presents the details regarding the Dialog's purpose."); };
        Alert(openDialog, o);
    }

    void BasicAlertDialogSample() {
        auto& openDialog = OpenDialogButton();
        if (!openDialog.Get()) return;
        m3::BasicAlertDialog([&openDialog] { openDialog.Set(false); }, {}, {}, [&openDialog] {
            m3::SurfaceOptions s;
            s.shape = m3::MaterialTheme::shapes().large;
            s.tonalElevation = m3::AlertDialogDefaults::TonalElevation;
            m3::Surface(WrapContent(WrapContent({}, Axis::Horizontal), Axis::Vertical), s, [&openDialog] {
                Column(Padding({}, 16_dp), Arrangement::Top(), AlignmentH::Start(), [&openDialog] {
                    m3::Text("This area typically contains the supportive text which presents the details regarding the Dialog's purpose.");
                    Spacer(Height({}, 24_dp));
                    m3::TextButton([&openDialog] { openDialog.Set(false); }, AlignInColumn({}, AlignmentH::End()), {}, [] { m3::Text("Confirm"); });
                });
            });
        });
    }
}
