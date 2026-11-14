// ui::ContentResolver's file-system side (what the desktop uses, and Android for file URIs):
// descriptors, metadata, whole reads and writes, and the C API over it.
#include "Check.h"

#include "kiln/kiln.h"
#include "kiln/ui/ContentResolver.h"

#include <cstdio>
#include <string>
#include <unistd.h>

using namespace kiln;

TEST(ContentResolverReadsAndWritesFiles) {
    char tmpl[] = "/tmp/kiln-content-XXXXXX";
    const int dir = mkdtemp(tmpl) ? 0 : -1;
    CHECK(dir == 0);
    const std::string path = std::string(tmpl) + "/notes file.txt";
    const std::string uri = "file://" + std::string(tmpl) + "/notes%20file.txt";
    const std::string text = "first line\n";
    CHECK(ui::WriteContent(path, std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()), text.size())));
    const auto info = ui::GetContentResolver().Query(uri);
    CHECK(info && info->displayName == std::optional<std::string>("notes file.txt"));
    CHECK(info && info->size == std::optional<std::int64_t>(std::int64_t(text.size())));
    CHECK(info && info->mimeType == std::optional<std::string>("text/plain"));
    const std::string more = "second\n";
    CHECK(ui::WriteContent(uri, std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(more.data()), more.size()), "wa"));
    const auto bytes = ui::ReadContent(uri);
    CHECK(bytes && std::string(bytes->begin(), bytes->end()) == "first line\nsecond\n");
    // A descriptor reads as any file does.
    const int fd = ui::GetContentResolver().OpenFileDescriptor(path, "r");
    CHECK(fd >= 0);
    char buf[5]{};
    CHECK(::read(fd, buf, 5) == 5 && std::string(buf, 5) == "first");
    ::close(fd);
    // Content URIs need the platform; a missing file gives nothing.
    CHECK(ui::GetContentResolver().OpenFileDescriptor("content://media/external/images/1", "r") == -1);
    CHECK(!ui::ReadContent(std::string(tmpl) + "/missing"));
    // The C API.
    size_t n = 0;
    uint8_t* c = kiln_content_read(uri.c_str(), &n);
    CHECK(c && n == bytes->size());
    kiln_content_free(c);
    CHECK(std::string(kiln_content_display_name(uri.c_str())) == "notes file.txt");
    CHECK(kiln_content_size(path.c_str()) == std::int64_t(bytes->size()));
    CHECK(kiln_content_write(path.c_str(), reinterpret_cast<const uint8_t*>("x"), 1, "wt") && kiln_content_size(path.c_str()) == 1);
    std::remove(path.c_str());
    ::rmdir(tmpl);
}
