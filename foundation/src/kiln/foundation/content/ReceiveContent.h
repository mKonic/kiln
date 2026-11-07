#pragma once

// contentReceiver (Compose's foundation.content): content the keyboard commits (images, GIFs,
// stickers) or the clipboard pastes reaches the nearest receivers around a text field, innermost
// first; each takes what it wants and passes on the rest. Text left over is inserted as text.

#include "kiln/foundation/Compose.h"
#include "kiln/ui/Clip.h"
#include "kiln/ui/DragAndDrop.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace kiln::foundation {

    // MediaType: a MIME type, wildcards allowed.
    struct MediaType {
        std::string representation;
        bool operator==(const MediaType&) const = default;
        static MediaType Text() { return { "text/*" }; }
        static MediaType PlainText() { return { "text/plain" }; }
        static MediaType HtmlText() { return { "text/html" }; }
        static MediaType Image() { return { "image/*" }; }
        static MediaType All() { return { "*/*" }; }
    };

    class TransferableContent {
    public:
        enum class Source : std::uint8_t { Keyboard, DragAndDrop, Clipboard };
        TransferableContent(ui::ClipEntry clip, ui::ClipMetadata metadata, Source source, std::optional<std::string> linkUri = std::nullopt,
                            int permission = 0)
            : clipEntry(std::move(clip)), clipMetadata(std::move(metadata)), source(source), linkUri(std::move(linkUri)), permission(permission) {}
        ui::ClipEntry clipEntry;
        ui::ClipMetadata clipMetadata;
        Source source;
        // PlatformTransferableContent: the keyboard's link (InputContentInfo.getLinkUri) and its
        // URI permission (Owner::ReleaseContentPermission when done with the content).
        std::optional<std::string> linkUri;
        int permission = 0;

        bool HasMediaType(const MediaType& t) const { return clipMetadata.HasMimeType(t.representation); }
        // consume: drops the items `predicate` takes; null when it took them all, this when none.
        std::shared_ptr<const TransferableContent> Consume(const std::function<bool(const ui::ClipItem&)>& predicate,
                                                           const std::shared_ptr<const TransferableContent>& self) const;
        // ClipEntry.readPlainText: the items' texts joined with line breaks, null without text.
        std::optional<std::string> ReadPlainText() const;
    };
    using TransferableContentPtr = std::shared_ptr<const TransferableContent>;

    // dragAndDropRequestPermission: asks the platform for access to a drop's content URIs.
    bool ClipContainsContentUri(const ui::ClipEntry& clip);
    void DragAndDropRequestPermission(const ui::DragAndDropEvent& e);
    // DragAndDropEvent.toTransferableContent.
    TransferableContentPtr TransferableContentOf(const ui::DragAndDropEvent& e);

    class ReceiveContentListener {
    public:
        virtual ~ReceiveContentListener() = default;
        virtual void OnDragStart() {}
        virtual void OnDragEnd() {}
        virtual void OnDragEnter() {}
        virtual void OnDragExit() {}
        // What is left for the receivers further out (the same pointer: nothing taken; null: all).
        virtual TransferableContentPtr OnReceive(TransferableContentPtr content) = 0;
    };
    std::shared_ptr<ReceiveContentListener> ReceiveContentListenerOf(std::function<TransferableContentPtr(TransferableContentPtr)> onReceive);

    Modifier ContentReceiver(Modifier m, std::shared_ptr<ReceiveContentListener> listener);

    // ReceiveContentConfiguration: the receivers in effect at a node (its own and every one
    // further out, chained).
    class ReceiveContentConfiguration {
    public:
        virtual ~ReceiveContentConfiguration() = default;
        virtual ReceiveContentListener& Listener() = 0;
        // onCommitContent: whether any receiver took something.
        bool OnCommitContent(const TransferableContentPtr& content) { return Listener().OnReceive(content) != content; }
    };
    // ModifierLocalReceiveContent.current: the nearest receiver before `node` in its chain or
    // around its layout node; null when there is none or `node` is detached.
    ReceiveContentConfiguration* FindReceiveContentConfiguration(const ui::ModifierNode& node);

}
