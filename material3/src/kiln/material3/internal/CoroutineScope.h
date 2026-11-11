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
        auto& c = *rt::Composition::Current();
        auto scope = c.Remember<std::shared_ptr<CoroutineScope>>([] { return std::make_shared<CoroutineScope>(); }, {}, key);
        // Cancel on leaving the composition, not only on destruction: a job may hold an owner of
        // the scope.
        std::weak_ptr<CoroutineScope> weak = scope;
        c.DisposableEffect({}, [weak] {
            return std::function<void()>([weak] {
                if (auto s = weak.lock()) {
                    auto jobs = std::move(s->jobs);
                    s->jobs.clear();
                }
            });
        }, key);
        return scope;
    }

}
