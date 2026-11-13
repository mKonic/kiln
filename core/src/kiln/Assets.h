#pragma once

// Read-only app assets: the APK's assets/ directory on Android (AAssetManager), a directory
// next to the executable on the dev host (KILN_ASSETS overrides it).

#include <cstdint>
#include <kiln/Function.h>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace kiln {

    std::optional<std::vector<std::uint8_t>> ReadAsset(std::string_view path);

    // Installed by the host.
    using AssetReader = kiln::Function<std::optional<std::vector<std::uint8_t>>(std::string_view)>;
    void SetAssetReader(AssetReader reader);

}
