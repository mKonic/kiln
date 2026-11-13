#pragma once

// The dropdown text context menu (text/contextmenu/provider and the Android
// DefaultTextContextMenuDropdownProvider): a text component asks the provider in
// LocalTextContextMenuDropdownProvider to show its menu (a right click does,
// showTextContextMenuOnSecondaryClick); the default provider shows the menu's data in a context
// menu popup at the click until an item or a dismissal closes the session. The floating toolbar
// is the owner's (TextContextMenuToolbarHandler) unless LocalTextContextMenuToolbarProvider has one.

#include "kiln/foundation/text/contextmenu/TextContextMenu.h"
#include <kiln/Function.h>

#include <memory>

namespace kiln::foundation {

    // TextContextMenuDataProvider: where the menu goes and what it holds (root pixels).
    class TextContextMenuDataProvider {
    public:
        virtual ~TextContextMenuDataProvider() = default;
        virtual Offset PositionInRoot() = 0;
        virtual Rect ContentBoundsInRoot() = 0;
        virtual TextContextMenuData Data() = 0;
    };

    // TextContextMenuProvider: shows the menu until its session closes (one at a time: a new one
    // ends the last).
    class TextContextMenuProvider {
    public:
        virtual ~TextContextMenuProvider() = default;
        virtual rt::Task<> ShowTextContextMenu(std::shared_ptr<TextContextMenuDataProvider> dataProvider) = 0;
    };

    inline rt::Local<TextContextMenuProvider*> LocalTextContextMenuDropdownProvider{ nullptr };
    // LocalTextContextMenuToolbarProvider: shows text components' floating toolbar (null: the
    // owner's platform toolbar).
    inline rt::Local<TextContextMenuProvider*> LocalTextContextMenuToolbarProvider{ nullptr };

    // ProvideDefaultPlatformTextContextMenuProviders: the default dropdown provider for `content`
    // (unless one is provided already), in a Box that propagates min constraints. `modifier` is
    // built inside, where the provider is the local (the menu gestures read it).
    void ProvideDefaultPlatformTextContextMenuProviders(kiln::Function<Modifier()> modifier, kiln::Function<void()> content);

    // showTextContextMenuOnSecondaryClick: a right click shows the menu of the components around
    // this point of the chain, at the click; `onPreShowContextMenu` runs first (with the click in
    // the node's space).
    Modifier ShowTextContextMenuOnSecondaryClick(Modifier m, kiln::Function<rt::Task<>(Offset)> onPreShowContextMenu = {});

}
