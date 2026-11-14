// Sharing the app's files: androidx FileProviderTest's SimplePathStrategy cases.
#include "Check.h"

#include "kiln/Uri.h"
#include "kiln/ui/FileProvider.h"

#include <filesystem>
#include <stdexcept>

using namespace kiln;
using namespace kiln::ui;

namespace {
    // A files folder of the test's own, canonical.
    struct Files {
        std::string dir;
        Files() {
            dir = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path() / "kiln-fileprovider" / "files").string();
            std::filesystem::create_directories(dir + "/tag");
        }
        ~Files() {
            SetFileProvider({}, {});
            std::error_code ec;
            std::filesystem::remove_all(std::filesystem::path(dir).parent_path(), ec);
        }
    };
    template <typename E, typename F>
    bool Throws(F&& f) {
        try { f(); } catch (const E&) { return true; }
        return false;
    }
}

TEST(FileProviderUrisFollowSimplePathStrategy) {
    Files f;
    SetFileProvider("authority", { { "tag", f.dir } });
    CHECK(GetUriForFile(f.dir + "/file.test") == "content://authority/tag/file.test");
    CHECK(GetUriForFile(f.dir + "/subdir/file.test") == "content://authority/tag/subdir/file.test");
    CHECK(Throws<std::invalid_argument>([] { GetUriForFile("/elsewhere/file.test"); }));
    // Jumping out, or the root itself, is not a file below it.
    CHECK(Throws<std::invalid_argument>([&] { GetUriForFile(f.dir + "/../file.test"); }));
    CHECK(Throws<std::invalid_argument>([&] { GetUriForFile(f.dir); }));
    // The most specific root wins, in either order.
    SetFileProvider("authority", { { "tag1", f.dir }, { "tag2", "/" } });
    CHECK(GetUriForFile(f.dir + "/file.test") == "content://authority/tag1/file.test");
    SetFileProvider("authority", { { "tag1", "/" }, { "tag2", f.dir } });
    CHECK(GetUriForFile(f.dir + "/file.test") == "content://authority/tag2/file.test");
    // Separators collapse; a display name rides along.
    SetFileProvider("authority", { { "tag", f.dir } });
    CHECK(GetUriForFile(f.dir + "//foo//bar//") == "content://authority/tag/foo/bar");
    CHECK(GetUriForFile(f.dir + "/a.pdf", "Notes for May.pdf") == "content://authority/tag/a.pdf?displayName=Notes%20for%20May.pdf");
}

TEST(FileProviderFilesFollowSimplePathStrategy) {
    Files f;
    SetFileProvider("authority", { { "tag", f.dir } });
    CHECK(GetFileForUri("content://authority/tag/file.test") == f.dir + "/file.test");
    CHECK(GetFileForUri("content://authority/tag/subdir/file.test") == f.dir + "/subdir/file.test");
    CHECK(GetFileForUri("content://authority/tag/file.txt?extra=foo") == f.dir + "/file.txt");
    CHECK(GetFileForUri("content://authority/tag/foo/bar") == f.dir + "/foo/bar");
    // Out of the root: refused.
    CHECK(Throws<std::runtime_error>([] { GetFileForUri("content://authority/tag/../file.test"); }));
    CHECK(Throws<std::invalid_argument>([] { GetFileForUri("content://authority/tag"); }));
    CHECK(Throws<std::invalid_argument>([] { GetFileForUri("content://authority/other/file"); }));
    SetFileProvider("authority", { { "my_tag", f.dir + "/tag" } });
    CHECK(Throws<std::runtime_error>([] { GetFileForUri("content://authority/my_tag/../tag"); }));
    // Escaping both ways.
    SetFileProvider("authority", { { "t/g", f.dir } });
    const std::string escaped = "content://authority/t%2Fg/lol%22wat%3Ffoo%26bar/wat.txt";
    CHECK(GetUriForFile(f.dir + "/lol\"wat?foo&bar/wat.txt") == escaped);
    CHECK(GetFileForUri(escaped) == f.dir + "/lol\"wat?foo&bar/wat.txt");
    // Without a platform provider: a file URI.
    SetFileProvider({}, {});
    CHECK(GetUriForFile("/tmp/a b.txt") == "file:///tmp/a%20b.txt");
    CHECK(Uri::Encode("a/b c~é", "/") == "a/b%20c~%C3%A9");
}
