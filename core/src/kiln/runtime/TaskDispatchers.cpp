#include "kiln/runtime/TaskDispatchers.h"
#include <kiln/Function.h>

#include <algorithm>

namespace kiln::rt {

    BackgroundDispatcher::BackgroundDispatcher(unsigned maxThreads, std::string name) : m_Max(std::max(1u, maxThreads)), m_Name(std::move(name)) {}

    BackgroundDispatcher::~BackgroundDispatcher() {
        {
            std::lock_guard lock(m_Mutex);
            m_Stop = true;
        }
        m_Cv.notify_all();
        for (auto& t : m_Threads) t.join();
    }

    void BackgroundDispatcher::Execute(kiln::Function<void()> work) {
        std::lock_guard lock(m_Mutex);
        m_Queue.push_back(std::move(work));
        // A thread more when none is idle, up to the limit.
        if (m_Idle == 0 && m_Threads.size() < m_Max) m_Threads.emplace_back([this] { Run(); });
        else m_Cv.notify_one();
    }

    void BackgroundDispatcher::Run() {
        std::unique_lock lock(m_Mutex);
        for (;;) {
            ++m_Idle;
            m_Cv.wait(lock, [this] { return m_Stop || !m_Queue.empty(); });
            --m_Idle;
            if (m_Queue.empty()) return;   // stopping, and the queue ran dry
            auto work = std::move(m_Queue.front());
            m_Queue.pop_front();
            lock.unlock();
            work();
            lock.lock();
        }
    }

    const TaskDispatchers& TaskDispatchers::Platform() {
        struct Pools final : TaskDispatchers {
            BackgroundDispatcher def{ std::max(2u, std::thread::hardware_concurrency()), "kiln-default" };
            BackgroundDispatcher io{ std::max(64u, std::thread::hardware_concurrency()), "kiln-io" };
            BackgroundDispatcher& Default() const override { return const_cast<BackgroundDispatcher&>(def); }
            BackgroundDispatcher& IO() const override { return const_cast<BackgroundDispatcher&>(io); }
        };
        static Pools pools;
        return pools;
    }

}
