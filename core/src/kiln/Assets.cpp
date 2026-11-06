#include "kiln/Assets.h"

namespace kiln {

    namespace { AssetReader& Reader() { static AssetReader r; return r; } }

    void SetAssetReader(AssetReader reader) { Reader() = std::move(reader); }

    std::optional<std::vector<std::uint8_t>> ReadAsset(std::string_view path) {
        if (!Reader()) return std::nullopt;
        return Reader()(path);
    }

}
