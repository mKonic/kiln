// BottomSheetSamples.kt
#include "BottomSheetSamples.h"
#include "Common.h"

#include <kiln/foundation/Focus.h>
#include <kiln/foundation/Gestures.h>
#include <kiln/foundation/Lazy.h>
#include <kiln/foundation/Scroll.h>
#include <kiln/material3/AppBar.h>
#include <kiln/material3/BottomSheetScaffold.h>
#include <kiln/material3/Button.h>
#include <kiln/material3/Checkbox.h>
#include <kiln/material3/IconButton.h>
#include <kiln/material3/ListItem.h>
#include <kiln/material3/MaterialTheme.h>
#include <kiln/material3/ModalBottomSheet.h>
#include <kiln/material3/SheetState.h>
#include <kiln/material3/TextField.h>

namespace catalog::samples {
    namespace {
        // ListItems "Item n" with a favourite icon, on surfaceContainerLow.
        void FavoriteItems(LazyListScope& scope, int count) {
            scope.Items(count, [](int i) {
                m3::ListItemColors c;
                c.containerColor = m3::MaterialTheme::colorScheme().surfaceContainerLow;
                m3::ListItemOptions o;
                o.leadingContent = [] { m3::Icon(Icons::Filled::Favorite(), std::string("Localized description")); };
                o.colors = m3::ListItemDefaults::colors(c);
                m3::ListItem({}, o, [i] { m3::Text("Item " + std::to_string(i)); });
            });
        }

        // scope.launch { hide() }.invokeOnCompletion { if (!isVisible) openBottomSheet = false }
        kiln::rt::Task<> HideThenClose(std::shared_ptr<m3::SheetState> state, kiln::rt::State<bool>* openBottomSheet) {
            try {
                co_await state->Hide();
            } catch (...) {
            }
            if (!state->IsVisible()) openBottomSheet->Set(false);
        }
    }

    void ModalBottomSheetSample() {
        auto& openBottomSheet = RememberSaveableState(false);
        auto& skipPartiallyExpanded = RememberSaveableState(false);
        const LaunchScope scope = RememberCoroutineScope();
        m3::SheetValues enabled;
        enabled.partiallyExpanded = !skipPartiallyExpanded.Get();
        auto bottomSheetState = m3::RememberBottomSheetState(m3::SheetValue::Hidden, enabled);
        Column({}, Arrangement::SpacedBy(4_dp), AlignmentH::Start(), [&openBottomSheet, &skipPartiallyExpanded] {
            ClickableOptions toggle;
            toggle.role = Role::Checkbox;
            const bool skip = skipPartiallyExpanded.Get();
            Row(Toggleable({}, skip, [&skipPartiallyExpanded](bool checked) { skipPartiallyExpanded.Set(checked); }, toggle), [skip] {
                m3::Checkbox(skip, nullptr);
                Spacer(Width({}, 16_dp));
                m3::Text("Skip partially expanded State");
            });
            m3::Button([&openBottomSheet] { openBottomSheet.Set(!openBottomSheet.Peek()); }, AlignInColumn({}, AlignmentH::CenterHorizontally()), {},
                       [] { m3::Text("Show Bottom Sheet"); });
        });
        if (openBottomSheet.Get()) {
            m3::ModalBottomSheetOptions o;
            o.sheetState = bottomSheetState;
            m3::ModalBottomSheet([&openBottomSheet] { openBottomSheet.Set(false); }, {}, o, [&openBottomSheet, bottomSheetState, scope] {
                Row(FillMax({}, Axis::Horizontal), Arrangement::Center(), AlignmentV::Top(), [&openBottomSheet, bottomSheetState, scope] {
                    m3::Button([&openBottomSheet, bottomSheetState, scope] { scope.Launch(HideThenClose(bottomSheetState, &openBottomSheet)); }, {}, {},
                               [] { m3::Text("Hide Bottom Sheet"); });
                });
                m3::TextFieldOptions field;
                field.label = [](const m3::TextFieldLabelScope&) { m3::Text("Text field"); };
                m3::OutlinedTextField(RememberTextFieldState(), Padding({}, 16_dp, 0_dp), field);
                LazyColumn({}, RememberLazyListState(), [](LazyListScope& s) { FavoriteItems(s, 25); });
            });
        }
    }

    void SimpleBottomSheetScaffoldSample() {
        const LaunchScope scope = RememberCoroutineScope();
        auto scaffoldState = m3::RememberBottomSheetScaffoldState();
        m3::BottomSheetScaffoldOptions o;
        o.scaffoldState = scaffoldState;
        o.sheetPeekHeight = 128_dp;
        m3::BottomSheetScaffold(
            [scaffoldState, scope] {
                Column(FillMax({}, Axis::Horizontal), Arrangement::Top(), AlignmentH::CenterHorizontally(), [scaffoldState, scope] {
                    Box(Height(FillMax({}, Axis::Horizontal), 128_dp), Alignment::Center(), false, [] { m3::Text("Swipe up to expand sheet"); });
                    m3::Text("Sheet content");
                    const bool expanded = scaffoldState->bottomSheetState->CurrentValue() == m3::SheetValue::Expanded;
                    m3::Button([scaffoldState, scope] { scope.Launch(scaffoldState->bottomSheetState->PartialExpand()); },
                               FocusPropertiesMod(Padding({}, PaddingValues{ 0_dp, 0_dp, 0_dp, 64_dp }), [expanded](FocusProperties& p) { p.canFocus = expanded; }),
                               {}, [] { m3::Text("Click to collapse sheet"); });
                });
            },
            {}, o, [](const PaddingValues& innerPadding) {
                Box(Padding(FillMax({}, Axis::Both), innerPadding), Alignment::Center(), false, [] { m3::Text("Scaffold Content"); });
            });
    }

    void BottomSheetScaffoldNestedScrollSample() {
        static const Color colors[5] = { Color(0xFFffd7d7u), Color(0xFFffe9d6u), Color(0xFFfffbd0u), Color(0xFFe3ffd9u), Color(0xFFd0fff8u) };
        auto scaffoldState = m3::RememberBottomSheetScaffoldState();
        auto scrollBehavior = m3::TopAppBarDefaults::pinnedScrollBehavior();
        m3::BottomSheetScaffoldOptions o;
        o.scaffoldState = scaffoldState;
        o.sheetPeekHeight = 128_dp;
        o.topBar = [scrollBehavior] {
            m3::TopAppBarOptions t;
            t.navigationIcon = [] {
                m3::IconButton([] { /*TODO*/ }, {}, {}, [] { m3::Icon(Icons::Filled::Menu(), std::string("Localized description")); });
            };
            t.scrollBehavior = scrollBehavior;
            m3::TopAppBar([] { m3::Text("Bottom sheet scaffold"); }, {}, t);
        };
        m3::BottomSheetScaffold([] { LazyColumn({}, RememberLazyListState(), [](LazyListScope& s) { FavoriteItems(s, 50); }); },
                                NestedScroll({}, scrollBehavior->NestedScrollConnection()), o, [](const PaddingValues& innerPadding) {
                                    LazyListOptions l;
                                    l.contentPadding = innerPadding;
                                    LazyColumn({}, RememberLazyListState(), [](LazyListScope& s) {
                                        s.Items(100, [](int i) { Box(Background(Height(FillMax({}, Axis::Horizontal), 50_dp), colors[i % 5])); });
                                    }, l);
                                });
    }
}
