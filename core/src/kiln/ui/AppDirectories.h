#pragma once

// The app's private directories (Context.getFilesDir, getCacheDir, getNoBackupFilesDir): set by
// the host on Android; on the desktop $XDG_DATA_HOME/kiln/<app>/files (and no_backup) and
// $XDG_CACHE_HOME/kiln/<app>, created on first use. Paths have no trailing slash.

#include <string>

namespace kiln::ui {

    std::string FilesDir();
    std::string CacheDir();
    std::string NoBackupFilesDir();
    // The host's directories (Android), or the app's name for the desktop ones.
    void SetAppDirectories(std::string files, std::string cache, std::string noBackup);
    void SetAppName(std::string name);

}
