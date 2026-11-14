#pragma once

// AndroidView (ui viewinterop AndroidView.android.kt): an Android View, such as a WebView or a
// map, inside kiln content. The View is a real View of the window, laid out where its element is,
// so touches, focus, the keyboard and accessibility work as they do for any View; it is drawn
// into the element through an embedded surface, so kiln's clips, alpha and content drawn over it
// apply. A View that scrolls takes part in nested scrolling with kiln's scrollables around it.
//
//   AndroidView(AndroidViewClass("android.webkit.WebView"), FillMax(Modifier{}, Axis::Both), {
//       .update = [](void* env, void* view) { /* JNI: view.loadUrl(...) */ },
//   });
//
// The callbacks get the JNIEnv* and the View (a jobject) as void*, so this header needs no JNI.
// On a host without Views (the desktop dev host) the element is empty, at its minimum size.

#include "kiln/foundation/Compose.h"
#include "kiln/ui/InteropView.h"

#include <string>

namespace kiln::foundation {

    struct AndroidViewOptions {
        // After the View is made, after each recomposition, and when state it reads changes.
        ui::InteropViewBlock update;
        // Set: when the element is kept for reuse (a lazy item that scrolled away) the View is
        // kept with it and reset here, then updated for its new content. Unset: it is released.
        ui::InteropViewBlock onReset;
        // The View leaves for good.
        ui::InteropViewBlock onRelease;
    };

    // factory(env, context): a new View (a local reference) for the window's Context.
    // It can run outside a Java call, where FindClass only finds platform classes: load the app's
    // own classes (androidx, a map SDK) with context.getClassLoader().loadClass(name).
    void AndroidView(ui::InteropViewFactory factory, const Modifier& modifier = {}, AndroidViewOptions options = {}, rt::Key key = {});

    // A factory that constructs `className` (a View with a Context constructor), such as
    // "android.webkit.WebView" or "android.widget.SeekBar".
    ui::InteropViewFactory AndroidViewClass(std::string className);

}
