// CardSamples.kt
#include "CardSamples.h"
#include "Common.h"

#include <kiln/material3/Card.h>

namespace catalog::samples {
    namespace {
        Modifier CardSize() { return SizeMod({}, 180_dp, 100_dp); }
        std::function<void()> Centered(const char* text) {
            return [text] { Box(FillMax({}, Axis::Both), Alignment::TopStart(), false, [text] { m3::Text(text, AlignInBox({}, Alignment::Center())); }); };
        }
    }

    void CardSample() { m3::Card(CardSize(), {}, Centered("Card content")); }
    void ClickableCardSample() { m3::Card([] {}, CardSize(), {}, Centered("Clickable")); }
    void ElevatedCardSample() { m3::ElevatedCard(CardSize(), {}, Centered("Card content")); }
    void ClickableElevatedCardSample() { m3::ElevatedCard([] {}, CardSize(), {}, Centered("Clickable")); }
    void OutlinedCardSample() { m3::OutlinedCard(CardSize(), {}, Centered("Card content")); }
    void ClickableOutlinedCardSample() { m3::OutlinedCard([] {}, CardSize(), {}, Centered("Clickable")); }
}
