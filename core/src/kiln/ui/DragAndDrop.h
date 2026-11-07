#pragma once

// Drag and drop (androidx.compose.ui.draganddrop): the platform drag events a node sees, the data a
// drag carries, DragAndDropNode (sources start transfers; targets accept a session and then
// receive enter / move / exit / drop for the innermost interested target under the pointer), and
// the owner's manager (AndroidDragAndDropManager) that the host view's drag listener drives.

#include "kiln/ui/Clip.h"
#include "kiln/ui/Geometry.h"
#include "kiln/ui/Modifier.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kiln::ui {

    class DrawScope;
    class Owner;

    // DragAndDropTransferData: ClipData, a local state for drags inside the app, View.DRAG_FLAG_*.
    struct DragAndDropTransferData {
        ClipEntry clip;
        ClipMetadata metadata;
        std::shared_ptr<void> localState;
        int flags = 0;
    };

    // DragAndDropEvent (DragEvent): its action, position in root pixels, the clip's MIME types,
    // and on a drop the clip itself and the drag's local state.
    struct DragAndDropEvent {
        enum class Action : std::uint8_t { Started = 1, Location = 2, Drop = 3, Ended = 4, Entered = 5, Exited = 6 };
        Action action = Action::Started;
        Offset positionInRoot;
        std::vector<std::string> mimeTypes;
        std::optional<ClipEntry> clip;
        std::optional<ClipMetadata> metadata;
        std::shared_ptr<void> localState;
        bool result = false;   // ACTION_DRAG_ENDED: whether a target took the drop
        // Activity.requestDragAndDropPermissions for this drop's content URIs (true when granted).
        std::function<bool()> requestPermissions;
    };

    class DragAndDropTarget {
    public:
        virtual ~DragAndDropTarget() = default;
        virtual bool OnDrop(const DragAndDropEvent& event) = 0;
        virtual void OnStarted(const DragAndDropEvent&) {}
        virtual void OnEntered(const DragAndDropEvent&) {}
        virtual void OnMoved(const DragAndDropEvent&) {}
        virtual void OnExited(const DragAndDropEvent&) {}
        virtual void OnChanged(const DragAndDropEvent&) {}
        virtual void OnEnded(const DragAndDropEvent&) {}
    };

    class DragAndDropStartTransferScope {
    public:
        virtual ~DragAndDropStartTransferScope() = default;
        // Starts the platform drag with a shadow of `decorationSize` px drawn by `drawDecoration`.
        virtual bool StartDragAndDropTransfer(const DragAndDropTransferData& data, Size decorationSize,
                                              std::function<void(DrawScope&)> drawDecoration) = 0;
    };

    // DragAndDropNode: a source (onStartTransfer) and / or a target (onDropTargetValidate).
    class DragAndDropNode : public ModifierNode, public DragAndDropTarget {
    public:
        std::function<void(DragAndDropStartTransferScope&, Offset)> onStartTransfer;
        std::function<DragAndDropTarget*(const DragAndDropEvent&)> onDropTargetValidate;

        DragAndDropNode* AsDragAndDrop() override { return this; }
        void OnDetach() override;
        bool HasEligibleDropTarget() const { return m_LastChild || m_ThisTarget; }

        // requestDragAndDropTransfer: offset in this node's space (NaN: unspecified).
        void RequestDragAndDropTransfer(Offset offset);
        // startDragAndDropTransfer: the first source here or below containing the offset.
        void StartDragAndDropTransfer(DragAndDropStartTransferScope& scope, Offset offset, const std::function<bool()>& isTransferStarted);
        // acceptDragAndDropTransfer: each attached node here and below decides; true if any did.
        bool AcceptDragAndDropTransfer(const DragAndDropEvent& start);

        // DragAndDropTarget: routed to the current child target, else this node's own.
        bool OnDrop(const DragAndDropEvent& event) override;
        void OnStarted(const DragAndDropEvent& event) override;
        void OnEntered(const DragAndDropEvent& event) override;
        void OnMoved(const DragAndDropEvent& event) override;
        void OnExited(const DragAndDropEvent& event) override;
        void OnChanged(const DragAndDropEvent& event) override;
        void OnEnded(const DragAndDropEvent& event) override;

        // Rect in root pixels (attached nodes only).
        std::optional<Rect> BoundsInRoot() const;

    protected:
        // The drag and drop nodes below this one, nearest first in tree order (direct ones; each
        // sees its own below). The root manager overrides it with the whole tree's.
        virtual std::vector<DragAndDropNode*> Children() const;
        virtual bool Attached() const { return Owner() != nullptr; }
        virtual ui::Owner* OwnerOf() const;

    private:
        friend class DragAndDropManager;
        template<typename F> void TraverseSelfAndDescendants(F&& block);
        DragAndDropNode* FirstDescendant(const std::function<bool(DragAndDropNode&)>& predicate);
        bool Contains(Offset positionInRoot) const;
        DragAndDropNode* m_LastChild = nullptr;
        DragAndDropTarget* m_ThisTarget = nullptr;
    };

    // The host's drag starter (View.startDragAndDrop with a shadow image) and permissions.
    struct DragAndDropHost {
        // The shadow as RGBA (premultiplied) of width x height; false when the drag did not start.
        std::function<bool(const DragAndDropTransferData& data, int width, int height, std::vector<std::uint8_t> shadowRgba)> startDrag;
    };

    // AndroidDragAndDropManager with its root node over the whole tree.
    class DragAndDropManager {
    public:
        explicit DragAndDropManager(Owner& owner);
        ~DragAndDropManager();
        void SetHost(DragAndDropHost host) { m_Host = std::move(host); }
        void RequestDragAndDropTransfer(DragAndDropNode& node, Offset offset);
        void RegisterTargetInterest(DragAndDropTarget* target);
        bool IsInterestedTarget(DragAndDropTarget* target) const;
        // View.OnDragListener.onDrag: the result the listener returns.
        bool OnDrag(const DragAndDropEvent& event);

    private:
        class Root;
        Owner& m_Owner;
        DragAndDropHost m_Host;
        std::unique_ptr<Root> m_Root;
        std::vector<DragAndDropTarget*> m_InterestedTargets;
    };

}
