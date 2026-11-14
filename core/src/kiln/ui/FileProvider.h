#pragma once

// Sharing the app's own files (androidx.core FileProvider): a content:// URI for a file in the
// app's folders that another app can open once granted (ui::Share grants it), without making the
// file public.
//
//   const std::string path = ui::CacheDir() + "/exports/notes.pdf";
//   ui::Share({ .streams = { ui::GetUriForFile(path) }, .mimeType = "application/pdf" });
//
// dev.kiln.KilnFileProvider (in kiln's manifests, authority <package>.kiln.fileprovider) serves
// these roots: files (FilesDir), cache (CacheDir), no_backup (NoBackupFilesDir), external_files
// and external_cache (the app's folders on external storage).

#include <string>
#include <utility>
#include <vector>

namespace kiln::ui {

    // FileProvider.getUriForFile: content://<authority>/<root>/<path below it>; displayName: the
    // name other apps see (else the file's). Throws std::invalid_argument for a file outside the
    // roots. Without a platform provider (the desktop): a file:// URI.
    std::string GetUriForFile(const std::string& path, const std::string& displayName = {});

    // SimplePathStrategy.getFileForUri: the file a URI names, canonical; throws
    // std::invalid_argument for an unknown root and std::runtime_error for a path that leaves its
    // root ("..").
    std::string GetFileForUri(const std::string& uri);

    // The provider's authority and roots (name, folder); the platform sets them.
    void SetFileProvider(std::string authority, std::vector<std::pair<std::string, std::string>> roots);

}
