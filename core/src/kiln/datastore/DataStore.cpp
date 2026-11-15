#include "kiln/datastore/DataStore.h"

#include "kiln/ui/AppDirectories.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

namespace kiln::datastore {

    std::string DataStoreFile(const std::string& fileName) { return ui::FilesDir() + "/datastore/" + fileName; }

    namespace detail {
        namespace {
            void MakeParentDirs(const std::string& path) {
                for (std::size_t i = 1; i < path.size(); ++i)
                    if (path[i] == '/') ::mkdir(path.substr(0, i).c_str(), 0700);
            }
            std::mutex g_FilesMutex;
            std::map<std::string, std::weak_ptr<const void>>& Files() {
                static auto* m = new std::map<std::string, std::weak_ptr<const void>>;
                return *m;
            }
        }

        // nullopt when there is no file; any other failure throws (FileStorage's readData: a
        // file that exists but cannot be read is an error, not empty data an edit would then
        // write over).
        std::optional<std::string> ReadFile(const std::string& path) {
            FILE* f = std::fopen(path.c_str(), "rb");
            if (!f) {
                if (errno == ENOENT) return std::nullopt;
                throw std::runtime_error("kiln: cannot read " + path + ": " + std::strerror(errno));
            }
            std::string out;
            char buf[16384];
            std::size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
            const bool failed = std::ferror(f) != 0;
            std::fclose(f);
            if (failed) throw std::runtime_error("kiln: cannot read " + path);
            return out;
        }

        void WriteFileAtomically(const std::string& path, const std::string& bytes) {
            MakeParentDirs(path);
            const std::string tmp = path + ".tmp";
            const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
            if (fd < 0) throw std::runtime_error("kiln: cannot write " + tmp + ": " + std::strerror(errno));
            std::size_t done = 0;
            while (done < bytes.size()) {
                const ssize_t n = ::write(fd, bytes.data() + done, bytes.size() - done);
                if (n < 0) {
                    if (errno == EINTR) continue;
                    ::close(fd);
                    throw std::runtime_error("kiln: cannot write " + tmp + ": " + std::strerror(errno));
                }
                done += std::size_t(n);
            }
            ::fsync(fd);
            ::close(fd);
            if (std::rename(tmp.c_str(), path.c_str()) != 0) throw std::runtime_error("kiln: cannot replace " + path + ": " + std::strerror(errno));
        }

        bool ClaimFile(const std::string& path, std::weak_ptr<const void> owner) {
            std::lock_guard lock(g_FilesMutex);
            auto& slot = Files()[path];
            if (!slot.expired()) return false;
            slot = std::move(owner);
            return true;
        }

        rt::Task<> TurnLock::Lock() {
            if (!m_Locked) {
                m_Locked = true;
                co_return;
            }
            auto turn = std::make_shared<rt::Channel<bool>>(1);
            m_Waiters.push_back(turn);
            co_await turn->Next();
        }

        void TurnLock::Unlock() {
            while (!m_Waiters.empty()) {
                auto w = m_Waiters.front().lock();
                m_Waiters.pop_front();
                if (w) { w->TrySend(true); return; }   // a cancelled waiter's channel is gone
            }
            m_Locked = false;
        }
    }

}
