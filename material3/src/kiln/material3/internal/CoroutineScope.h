#pragma once

// rememberCoroutineScope: tasks launched from callbacks, cancelled when the caller leaves the
// composition.

#include "kiln/runtime/Composition.h"
#include "kiln/runtime/Task.h"

#include <memory>
#include <vector>

namespace kiln::material3::internal {

    struct CoroutineScope {
        std::vector<rt::Job> jobs;
        void Launch(rt::Task<> task) {
            std::erase_if(jobs, [](const rt::Job& j) { return j.Done(); });
            jobs.push_back(rt::Launch(rt::Dispatcher::Main(), std::move(task)));
        }
    };

    inline std::shared_ptr<CoroutineScope> RememberCoroutineScope(rt::Key key = {}) {
        return rt::Composition::Current()->Remember<std::shared_ptr<CoroutineScope>>([] { return std::make_shared<CoroutineScope>(); }, {}, key);
    }

}
