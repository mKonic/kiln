#pragma once

// dragAndDropSource / dragAndDropTarget (foundation/draganddrop): a source starts a platform drag
// after a long press (or a mouse drag past the slop) with the data transferData gives for the
// offset; the shadow is its own content unless drawDragDecoration draws one. A target accepts the
// drags shouldStartDragAndDrop admits and then hears enter / move / exit / drop.

#include "kiln/foundation/Compose.h"
#include "kiln/ui/DragAndDrop.h"

#include <functional>
#include <memory>
#include <optional>

namespace kiln::foundation {

    using ui::DragAndDropEvent;
    using ui::DragAndDropTransferData;

    using DragTransferData = std::function<std::optional<DragAndDropTransferData>(Offset)>;
    Modifier DragAndDropSource(Modifier m, DragTransferData transferData);
    Modifier DragAndDropSource(Modifier m, std::function<void(DrawScope&)> drawDragDecoration, DragTransferData transferData);

    Modifier DragAndDropTarget(Modifier m, std::function<bool(const DragAndDropEvent&)> shouldStartDragAndDrop,
                               std::shared_ptr<ui::DragAndDropTarget> target);

}
