#include "kiln/ui/ActivityResult.h"

namespace kiln::ui {

    namespace {
        kiln::Function<bool(const std::string&)>& PermissionCheck() {
            static kiln::Function<bool(const std::string&)> f;
            return f;
        }
    }
    bool CheckSelfPermission(const std::string& permission) { return PermissionCheck() ? PermissionCheck()(permission) : true; }
    void SetPermissionCheck(kiln::Function<bool(const std::string&)> check) { PermissionCheck() = std::move(check); }

}
