#pragma once

// Handwriting gestures on a text field (Compose's HandwritingGestureApi34): select, delete,
// select / delete a range, join or split, insert and remove space; select and delete preview as
// highlights. Areas arrive in root pixels and map into the text layout node.

#include "kiln/foundation/text/TextFieldState.h"
#include "kiln/foundation/text/input/TextLayoutState.h"
#include "kiln/ui/TextInput.h"

#include <functional>

namespace kiln { class TextBackend; }

namespace kiln::foundation {

    struct HandwritingGestureContext {
        TransformedTextFieldState& state;
        TextLayoutState& layout;
        TextBackend* backend = nullptr;              // character types (whitespace, punctuation)
        float lineMargin = 16;                       // ViewConfiguration.handwritingGestureLineMargin
        std::function<void()> updateSelectionState;  // after a select: the toolbar shows for it
    };

    int PerformHandwritingGesture(const HandwritingGestureContext& c, const ui::HandwritingGesture& g);
    bool PreviewHandwritingGesture(const HandwritingGestureContext& c, const ui::HandwritingGesture& g);
    // The preview's cancellation signal: the highlight goes.
    void CancelHandwritingPreview(TransformedTextFieldState& state);

}
