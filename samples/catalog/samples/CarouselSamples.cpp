// CarouselSamples.kt
#include "CarouselSamples.h"
#include "Common.h"

#include <kiln/Assets.h>
#include <kiln/foundation/Back.h>
#include <kiln/foundation/Border.h>
#include <kiln/foundation/Image.h>
#include <kiln/foundation/Lazy.h>
#include <kiln/foundation/Scroll.h>
#include <kiln/material3/Button.h>
#include <kiln/material3/Carousel.h>
#include <kiln/material3/Chip.h>
#include <kiln/material3/MaterialTheme.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string>

namespace catalog::samples {
    namespace {
        // R.drawable.carousel_image_N and R.string.carousel_image_N_description.
        struct CarouselItem {
            int id;
            const kiln::gfx::ImageBitmap* image;
            const char* contentDescription;
        };
        const std::array<CarouselItem, 5>& Items() {
            static const std::array<kiln::gfx::ImageBitmap, 5> images = [] {
                std::array<kiln::gfx::ImageBitmap, 5> out;
                for (int i = 0; i < 5; ++i) {
                    const auto bytes = kiln::ReadAsset("carousel_image_" + std::to_string(i + 1) + ".jpg");
                    if (bytes) out[i] = kiln::gfx::ImageBitmap::Decode(*bytes);
                }
                return out;
            }();
            static const std::array<CarouselItem, 5> items{ {
                { 0, &images[0], "A racecar" },
                { 1, &images[1], "Dotonburi, Osaka" },
                { 2, &images[2], "The Grand Canyon" },
                { 3, &images[3], "A rock structure with its reflection mirrored over the sea" },
                { 4, &images[4], "A car on a long stretch of desert road" },
            } };
            return items;
        }
        int ItemCount() { return int(Items().size()); }

        void CropImage(const CarouselItem& item, const Modifier& modifier) {
            Image(*item.image, std::string(item.contentDescription), modifier, Alignment::Center(), ContentScale::Crop());
        }

        m3::CarouselOptions Spaced() {
            m3::CarouselOptions o;
            o.itemSpacing = 8_dp;
            o.contentPadding = PaddingValues{ 16_dp, 0_dp, 16_dp, 0_dp };
            return o;
        }

        // clickable(true, "Tap to focus", Role.Image) { animationScope.launch { state.animateScrollToItem(i) } }
        Modifier TapToFocus(Modifier m, m3::CarouselState& state, const LaunchScope& scope, int i) {
            ClickableOptions c;
            c.onClickLabel = "Tap to focus";
            c.role = Role::Image;
            return Clickable(std::move(m), [&state, scope, i] { scope.Launch(state.AnimateScrollToItem(i)); }, c);
        }

        // The plain multi-browse carousel of the last two samples.
        void MultiBrowse(const Modifier& modifier) {
            auto state = m3::RememberCarouselState(ItemCount);
            m3::HorizontalMultiBrowseCarousel(*state, 186_dp, modifier, [](const m3::CarouselItemScope& scope, int i) {
                CropImage(Items()[i], scope.MaskClip(Height({}, 205_dp), m3::MaterialTheme::shapes().extraLarge));
            }, Spaced());
        }
    }

    void HorizontalMultiBrowseCarouselSample() {
        auto state = m3::RememberCarouselState(ItemCount);
        const LaunchScope animationScope = RememberCoroutineScope();
        m3::HorizontalMultiBrowseCarousel(*state, 186_dp, Height(FillMax({}, Axis::Horizontal), 221_dp),
                                          [state, animationScope](const m3::CarouselItemScope& scope, int i) {
                                              CropImage(Items()[i], scope.MaskClip(TapToFocus(FillMax(Height({}, 205_dp), Axis::Horizontal), *state, animationScope, i),
                                                                                   m3::MaterialTheme::shapes().extraLarge));
                                          },
                                          Spaced());
    }

    void HorizontalUncontainedCarouselSample() {
        auto state = m3::RememberCarouselState(ItemCount);
        m3::HorizontalUncontainedCarousel(*state, 186_dp, Height(Width({}, 412_dp), 221_dp), [](const m3::CarouselItemScope& scope, int i) {
            CropImage(Items()[i], scope.MaskClip(Height({}, 205_dp), m3::MaterialTheme::shapes().extraLarge));
        }, Spaced());
    }

    void HorizontalCenteredHeroCarouselSample() {
        auto state = m3::RememberCarouselState(ItemCount);
        const LaunchScope animationScope = RememberCoroutineScope();
        m3::HorizontalCenteredHeroCarousel(*state, Padding(Height(FillMax({}, Axis::Horizontal), 221_dp), 24_dp, 0_dp),
                                           [state, animationScope](const m3::CarouselItemScope& scope, int i) {
                                               CropImage(Items()[i], TapToFocus(scope.MaskClip(Height(FillMax({}, Axis::Horizontal), 205_dp),
                                                                                               m3::MaterialTheme::shapes().extraLarge),
                                                                                *state, animationScope, i));
                                           },
                                           Spaced());
    }

    void MultiAspectCarouselRowSample() {
        static constexpr Dp mainAxisSizes[5] = { Dp(305), Dp(205), Dp(275), Dp(350), Dp(100) };
        LazyListState& state = RememberLazyListState();
        const auto effectState = Here().Remember<std::shared_ptr<m3::CarouselParallaxScrollEffectState>>(
            [&state] { return m3::MakeCarouselParallaxScrollEffectState(state); }, kiln::rt::Keys(&state));
        LazyListOptions o;
        o.contentPadding = PaddingValues{ 16_dp, 16_dp, 16_dp, 16_dp };
        o.arrangement = Arrangement::SpacedBy(8_dp);
        LazyRow(Height(FillMax({}, Axis::Horizontal), 221_dp), state, [effectState](LazyListScope& scope) {
            scope.Items(ItemCount(), [effectState](int i) {
                CropImage(Items()[i], m3::CarouselParallaxScrollEffect(Height(Width({}, mainAxisSizes[i]), 205_dp), i, effectState,
                                                                       m3::MaterialTheme::shapes().extraLarge));
            });
        }, o);
    }

    void FadingHorizontalMultiBrowseCarouselSample() {
        auto state = m3::RememberCarouselState(ItemCount);
        m3::HorizontalMultiBrowseCarousel(*state, 186_dp, Height(Width({}, 412_dp), 221_dp), [](const m3::CarouselItemScope& scope, int i) {
            const CarouselItem& item = Items()[i];
            // For item 1 and 4, create a stacked item layout that clips two images independently
            // to the item's mask
            if (i == 1 || i == 4) {
                Column(Height({}, 205_dp), Arrangement::SpacedBy(8_dp), AlignmentH::Start(), [&scope, &item] {
                    const auto extraLarge = m3::MaterialTheme::shapes().extraLarge;
                    CropImage(item, scope.MaskBorder(scope.MaskClip(FillMax(FillMax({}, Axis::Horizontal), Axis::Vertical, 0.5f), extraLarge),
                                                     BorderStroke{ 3_dp, Color::Magenta() }, extraLarge));
                    const auto small = RoundedCorner(8_dp);
                    CropImage(item, scope.MaskBorder(scope.MaskClip(FillMax({}, Axis::Both), small), BorderStroke{ 5_dp, Color::Green() }, small));
                });
            } else {
                // Mask using a generic path shape
                const auto pathShape = Here().Remember<std::shared_ptr<const kiln::gfx::Shape>>([] {
                    return std::make_shared<kiln::gfx::GenericShape>([](kiln::gfx::Path& path, kiln::ui::Size size, kiln::ui::LayoutDirection) {
                        path.AddRoundRect(kiln::gfx::RoundRect::Uniform({ 0, 0, size.width, size.height }, 30));
                    });
                });
                const float eightDp = Here().Read(LocalDensity).ToPx(8_dp);
                Box(scope.MaskBorder(scope.MaskClip(Height({}, 205_dp), pathShape), BorderStroke{ 5_dp, Color::Red() }, pathShape), Alignment::TopStart(),
                    false, [&scope, &item, i, eightDp] {
                        CropImage(item, FillMax({}, Axis::Both));
                        m3::ChipOptions o;
                        o.leadingIcon = [] {
                            m3::Icon(Icons::Filled::Image(), std::string("Localized description"), SizeMod({}, m3::AssistChipDefaults::IconSize, m3::AssistChipDefaults::IconSize));
                        };
                        const m3::CarouselItemDrawInfo* info = &scope.DrawInfo();
                        m3::ElevatedAssistChip([] { /* Do something! */ }, [i] { m3::Text("Image " + std::to_string(i)); },
                                               GraphicsLayer({}, [info, eightDp](GraphicsLayerParams& l) {
                                                   // Fade the chip in once the carousel item's size is large enough to
                                                   // display the entire chip
                                                   const float w = l.size.width;
                                                   l.alpha = std::max(w - info->MaxSize() + info->Size(), 0.0f) / w;
                                                   // Translate the chip to be pinned to the left side of the item's mask
                                                   l.translationX = info->MaskRect().left + eightDp;
                                               }),
                                               o);
                    });
            }
        }, Spaced());
    }

    void CarouselWithShowAllButtonSample() {
        auto& showAllItems = RememberSaveableState(false);
        BackHandler(showAllItems.Get(), [&showAllItems] { showAllItems.Set(false); });
        if (showAllItems.Get()) {
            // Shows the grid page directly. For better user experience and navigation patterns,
            // consider navigating to a dedicated screen
            LazyGridOptions o;
            o.contentPadding = PaddingValues{ 16_dp, 16_dp, 16_dp, 16_dp };
            o.mainAxisArrangement = Arrangement::SpacedBy(8_dp);
            o.crossAxisArrangement = Arrangement::SpacedBy(8_dp);
            LazyVerticalGrid(Padding(FillMax({}, Axis::Both), PaddingValues{ 0_dp, 8_dp, 0_dp, 0_dp }), GridCells::Fixed(2), RememberLazyGridState(),
                             [](LazyGridScope& scope) {
                                 scope.Items(ItemCount(), [](int i) {
                                     CropImage(Items()[i], Clip(Height({}, 205_dp), m3::MaterialTheme::shapes().extraLarge));
                                 });
                             }, o);
        } else {
            Column(Padding(VerticalScroll(FillMax({}, Axis::Both), RememberScrollState()), 0_dp, 16_dp), [&showAllItems] {
                MultiBrowse(Height(Width({}, 412_dp), 221_dp));
                m3::TextButton([&showAllItems] { showAllItems.Set(true); }, Padding(AlignInColumn({}, AlignmentH::End()), 16_dp, 4_dp), {},
                               [] { m3::Text("Show all"); });
                // An example of the content for the scrollable page
                for (int index = 0; index <= 4; ++index) {
                    Spacer(Height({}, 16_dp));
                    Box(Background(Padding(Height(FillMax({}, Axis::Horizontal), 140_dp), 16_dp, 0_dp), m3::MaterialTheme::colorScheme().surfaceVariant,
                                   RoundedCorner(16_dp)),
                        Alignment::TopStart(), false, [] {});
                }
            });
        }
    }
}
