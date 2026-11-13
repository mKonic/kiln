// DrawerSamples.kt
#include "DrawerSamples.h"
#include <kiln/Function.h>
#include "Common.h"

#include <kiln/foundation/Focus.h>
#include <kiln/foundation/Scroll.h>
#include <kiln/material3/Button.h>
#include <kiln/material3/NavigationDrawer.h>

namespace catalog::samples {
    namespace {
        const ImageVectorPtr& Item(int i) {
            static const ImageVectorPtr* const items[18] = {
                &Icons::Filled::AccountCircle(), &Icons::Filled::Bookmarks(), &Icons::Filled::CalendarMonth(), &Icons::Filled::Dashboard(),
                &Icons::Filled::Email(),         &Icons::Filled::Favorite(),  &Icons::Filled::Group(),         &Icons::Filled::Headphones(),
                &Icons::Filled::Image(),         &Icons::Filled::JoinFull(),  &Icons::Filled::Keyboard(),      &Icons::Filled::Laptop(),
                &Icons::Filled::Map(),           &Icons::Filled::Navigation(), &Icons::Filled::Outbox(),       &Icons::Filled::PushPin(),
                &Icons::Filled::QrCode(),        &Icons::Filled::Radio(),
            };
            return *items[i];
        }
        // item.name.substringAfterLast(".")
        std::string NameOf(const ImageVectorPtr& icon) {
            const std::string& name = icon->name;
            const auto dot = name.rfind('.');
            return dot == std::string::npos ? name : name.substr(dot + 1);
        }

        // The drawer's items in a scrolling column, 12 dp from the top.
        void Items(kiln::rt::State<int>& selectedIndex, Modifier itemModifier, kiln::Function<void()> onPick) {
            Column(VerticalScroll({}, RememberScrollState()), [&selectedIndex, itemModifier, onPick] {
                Spacer(Height({}, 12_dp));
                for (int index = 0; index < 18; ++index) {
                    m3::NavigationDrawerItemOptions o;
                    o.icon = [index] { m3::Icon(Item(index), std::nullopt); };
                    m3::NavigationDrawerItem([index] { m3::Text(NameOf(Item(index))); }, index == selectedIndex.Get(),
                                             [&selectedIndex, onPick, index] {
                                                 if (onPick) onPick();
                                                 selectedIndex.Set(index);
                                             },
                                             itemModifier, o);
                }
            });
        }

        // The modal and dismissible samples' content: a swipe hint and a button that opens it.
        void OpenContent(const std::shared_ptr<m3::DrawerState>& drawerState, const LaunchScope& scope, const std::shared_ptr<FocusRequester>& focusRequester) {
            Column(Padding(FillMax({}, Axis::Both), 16_dp), Arrangement::Top(), AlignmentH::CenterHorizontally(), [drawerState, scope, focusRequester] {
                m3::Text(drawerState->IsClosed() ? ">>> Swipe >>>" : "<<< Swipe <<<");
                Spacer(Height({}, 20_dp));
                m3::Button([drawerState, scope] { scope.Launch(drawerState->Open()); }, FocusRequesterModifier({}, *focusRequester), {},
                           [] { m3::Text("Click to open"); });
            });
        }
        // LaunchedEffect(drawerState.isClosed): focus the button again once the drawer closes.
        void FocusWhenClosed(const std::shared_ptr<m3::DrawerState>& drawerState, const std::shared_ptr<FocusRequester>& focusRequester) {
            const bool closed = drawerState->IsClosed();
            Here().LaunchedEffect(kiln::rt::Keys(closed), [closed, focusRequester]() -> kiln::rt::Task<> {
                if (closed) focusRequester->RequestFocus();
                co_return;
            });
        }
        std::shared_ptr<FocusRequester> RememberFocusRequester() {
            return Here().Remember<std::shared_ptr<FocusRequester>>([] { return std::make_shared<FocusRequester>(); });
        }
    }

    void ModalNavigationDrawerSample() {
        auto drawerState = m3::RememberDrawerState(m3::DrawerValue::Closed);
        const LaunchScope scope = RememberCoroutineScope();
        auto focusRequester = RememberFocusRequester();
        auto& selectedIndex = RememberSaveableState(0);
        m3::ModalNavigationDrawerOptions o;
        o.drawerState = drawerState;
        m3::ModalNavigationDrawer(
            [drawerState, scope, &selectedIndex] {
                m3::ModalDrawerSheet({}, {}, [drawerState, scope, &selectedIndex] {
                    Items(selectedIndex, Padding({}, m3::NavigationDrawerItemDefaults::ItemPadding), [drawerState, scope] { scope.Launch(drawerState->Close()); });
                }, drawerState);
            },
            {}, o, [drawerState, scope, focusRequester] { OpenContent(drawerState, scope, focusRequester); });
        FocusWhenClosed(drawerState, focusRequester);
    }

    void PermanentNavigationDrawerSample() {
        auto& selectedIndex = RememberSaveableState(0);
        m3::PermanentNavigationDrawer(
            [&selectedIndex] { m3::PermanentDrawerSheet(Width({}, 240_dp), {}, [&selectedIndex] { Items(selectedIndex, Padding({}, 12_dp, 0_dp), {}); }); },
            {}, [] {
                Column(Padding(FillMax({}, Axis::Both), 16_dp), Arrangement::Top(), AlignmentH::CenterHorizontally(),
                       [] { m3::Text("Application content"); });
            });
    }

    void DismissibleNavigationDrawerSample() {
        auto drawerState = m3::RememberDrawerState(m3::DrawerValue::Closed);
        const LaunchScope scope = RememberCoroutineScope();
        auto focusRequester = RememberFocusRequester();
        auto& selectedIndex = RememberSaveableState(0);
        m3::DismissibleNavigationDrawerOptions o;
        o.drawerState = drawerState;
        m3::DismissibleNavigationDrawer(
            [drawerState, scope, &selectedIndex] {
                m3::DismissibleDrawerSheet({}, {}, [drawerState, scope, &selectedIndex] {
                    Items(selectedIndex, Padding({}, 12_dp, 0_dp), [drawerState, scope] { scope.Launch(drawerState->Close()); });
                }, drawerState);
            },
            {}, o, [drawerState, scope, focusRequester] { OpenContent(drawerState, scope, focusRequester); });
        FocusWhenClosed(drawerState, focusRequester);
    }
}
