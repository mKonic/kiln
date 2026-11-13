#pragma once

// rememberCoroutineScope: tasks launched from callbacks run side by side and are cancelled when
// the caller leaves the composition.

#include "kiln/runtime/Composition.h"
#include <kiln/Function.h>
#include "kiln/runtime/Task.h"

#include <memory>
#include <vector>

namespace kiln::rt {

    struct CoroutineScope {
        std::vector<Job> jobs;
        void Launch(Task<> task) {
            std::erase_if(jobs, [](const Job& j) { return j.Done(); });
            jobs.push_back(rt::Launch(Dispatcher::Main(), std::move(task)));
        }
    };

    inline std::shared_ptr<CoroutineScope> RememberCoroutineScope(Key key = {}) {
        auto& c = *Composition::Current();
        auto scope = c.Remember<std::shared_ptr<CoroutineScope>>([] { return std::make_shared<CoroutineScope>(); }, {}, key);
        // Cancel on leaving the composition, not only on destruction: a job may hold an owner of
        // the scope.
        std::weak_ptr<CoroutineScope> weak = scope;
        c.DisposableEffect({}, [weak] {
            return kiln::Function<void()>([weak] {
                if (auto s = weak.lock()) {
                    auto jobs = std::move(s->jobs);
                    s->jobs.clear();
                }
            });
        }, key);
        return scope;
    }

}
