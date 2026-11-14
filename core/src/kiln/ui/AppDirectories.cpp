#include "kiln/ui/AppDirectories.h"

#include <cerrno>
#include <cstdlib>
#include <mutex>
#include <sys/stat.h>

#if defined(__linux__) && !defined(__ANDROID__)
#include <cstring>
extern char* program_invocation_short_name;
#endif

namespace kiln::ui {

    namespace {
        struct Dirs {
            std::mutex mutex;
            std::string files, cache, noBackup, app;
        };
        Dirs& D() {
            static Dirs* d = new Dirs;
            return *d;
        }
        // mkdir -p
        void MakeDirs(const std::string& path) {
            for (std::size_t i = 1; i <= path.size(); ++i)
                if (i == path.size() || path[i] == '/') ::mkdir(path.substr(0, i).c_str(), 0700);
        }
        std::string AppName() {
            if (!D().app.empty()) return D().app;
#if defined(__linux__) && !defined(__ANDROID__)
            if (program_invocation_short_name && *program_invocation_short_name) return program_invocation_short_name;
#endif
            return "app";
        }
        std::string Base(const char* xdg, const char* fallback) {
            if (const char* v = std::getenv(xdg); v && *v) return v;
            const char* home = std::getenv("HOME");
            return std::string(home ? home : "/tmp") + fallback;
        }
        std::string Resolve(std::string& slot, const char* xdg, const char* fallback, const char* leaf) {
            std::lock_guard lock(D().mutex);
            if (slot.empty()) slot = Base(xdg, fallback) + "/kiln/" + AppName() + leaf;
            MakeDirs(slot);
            return slot;
        }
    }

    std::string FilesDir() { return Resolve(D().files, "XDG_DATA_HOME", "/.local/share", "/files"); }
    std::string CacheDir() { return Resolve(D().cache, "XDG_CACHE_HOME", "/.cache", ""); }
    std::string NoBackupFilesDir() { return Resolve(D().noBackup, "XDG_DATA_HOME", "/.local/share", "/no_backup"); }

    std::string DatabasePath(const std::string& name) {
        std::string files = FilesDir();
        const std::size_t slash = files.rfind('/');
        const std::string dir = (slash == std::string::npos ? files : files.substr(0, slash)) + "/databases";
        MakeDirs(dir);
        return dir + "/" + name;
    }

    void SetAppDirectories(std::string files, std::string cache, std::string noBackup) {
        std::lock_guard lock(D().mutex);
        D().files = std::move(files);
        D().cache = std::move(cache);
        D().noBackup = std::move(noBackup);
    }
    void SetAppName(std::string name) {
        std::lock_guard lock(D().mutex);
        D().app = std::move(name);
    }

}
