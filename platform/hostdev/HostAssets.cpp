// Dev-host assets: files under $KILN_ASSETS, else "assets" next to the executable.

#include "kiln/Assets.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace kiln::hostdev {

    void InstallAssetReader() {
        std::filesystem::path base;
        if (const char* env = std::getenv("KILN_ASSETS")) base = env;
        else base = std::filesystem::read_symlink("/proc/self/exe").parent_path() / "assets";
        SetAssetReader([base](std::string_view path) -> std::optional<std::vector<std::uint8_t>> {
            std::ifstream f(base / std::filesystem::path(path), std::ios::binary);
            if (!f) return std::nullopt;
            return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f), {});
        });
    }

}
