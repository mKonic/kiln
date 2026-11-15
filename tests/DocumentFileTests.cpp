// DocumentFile (androidx documentfile): RawDocumentFile on the file system (RawDocumentFileTest),
// and TreeDocumentFile / DocumentsContractApi19's rules over a provider the test plays.
#include "Check.h"

#include "kiln/ui/DocumentFile.h"

#include <filesystem>
#include <fstream>
#include <map>

using namespace kiln;
using DF = ui::DocumentFile;

TEST(DocumentFileOverTheFileSystem) {
    const std::string dir = test::MakeTempDir("kiln-docfile-");
    DF root = DF::FromFile(dir);
    CHECK(root.IsDirectory() && !root.IsFile() && root.Exists() && !root.Type() && root.CanWrite());
    CHECK(root.Uri().rfind("file://", 0) == 0);
    // createFile tacks on the MIME type's extension; a second with the same name fails.
    auto notes = root.CreateFile("text/plain", "notes");
    CHECK(notes && notes->Name() == std::optional<std::string>("notes.txt") && notes->IsFile());
    CHECK(notes && notes->Type() == std::optional<std::string>("text/plain"));
    CHECK(!root.CreateFile("text/plain", "notes"));
    auto raw = root.CreateFile("application/x-unknown", "data.bin");
    CHECK(raw && raw->Name() == std::optional<std::string>("data.bin") && raw->Type() == std::optional<std::string>("application/octet-stream"));
    std::ofstream(dir + "/notes.txt") << "hello";
    CHECK(notes && notes->Length() == 5 && notes->LastModified() > 0);
    auto sub = root.CreateDirectory("pictures");
    CHECK(sub && sub->IsDirectory() && root.CreateDirectory("pictures"));   // an existing directory is fine
    CHECK(sub && sub->ParentFile() && sub->ParentFile()->Uri() == root.Uri());
    CHECK(root.ListFiles().size() == 3 && root.FindFile("pictures") && !root.FindFile("missing"));
    // Rename keeps the object current; delete removes a directory's contents too.
    CHECK(notes && notes->RenameTo("renamed.txt") && notes->Name() == std::optional<std::string>("renamed.txt") && notes->Exists());
    if (sub) sub->CreateFile("image/png", "a");
    CHECK(sub && sub->Delete() && !sub->Exists());
    std::filesystem::remove_all(dir);
}

TEST(DocumentFileTreeRulesAsDocumentsContractApi19) {
    struct Provider final : DF::Host {
        std::map<std::string, DF::DocumentInfo> docs;
        std::map<std::string, std::vector<std::string>> children;
        bool read = true, write = true;
        bool IsDocumentUri(const std::string& u) override { return u.find("/document/") != std::string::npos; }
        std::optional<std::string> TreeDocumentUri(const std::string& t) override {
            if (t.find("/tree/") == std::string::npos) return std::nullopt;
            return t + "/document/root";
        }
        std::optional<DF::DocumentInfo> Query(const std::string& u) override {
            auto it = docs.find(u);
            return it == docs.end() ? std::nullopt : std::optional(it->second);
        }
        bool HasPermission(const std::string&, bool w) override { return w ? write : read; }
        std::optional<std::string> Create(const std::string& parent, const std::string& mime, const std::string& name) override {
            const std::string uri = parent + "/" + name;
            docs[uri] = { name, mime, DF::kFlagSupportsDelete, 1, 0 };
            children[parent].push_back(uri);
            return uri;
        }
        bool Delete(const std::string& u) override { return docs.erase(u) != 0; }
        std::optional<std::string> Rename(const std::string& u, const std::string& name) override {
            auto info = docs[u];
            docs.erase(u);
            info.displayName = name;
            docs["renamed:" + name] = info;
            return "renamed:" + name;
        }
        std::vector<std::string> Children(const std::string& u) override { return children[u]; }
    };
    auto provider = std::make_shared<Provider>();
    const std::string tree = "content://p/tree/root";
    provider->docs[tree + "/document/root"] = { "Root", DF::kMimeTypeDir, DF::kFlagDirSupportsCreate, 0, 0 };
    DF::SetHost(provider);
    CHECK(!DF::FromTreeUri("content://p/document/x"));
    auto root = DF::FromTreeUri(tree);
    CHECK(root && root->IsDirectory() && !root->Type() && root->CanWrite() && root->CanRead());
    auto file = root ? root->CreateFile("text/plain", "a.txt") : std::nullopt;
    CHECK(file && file->IsFile() && file->Type() == std::optional<std::string>("text/plain") && file->CanWrite());   // deletable: writable
    // A document without a MIME type is neither readable nor writable; a directory without
    // FLAG_DIR_SUPPORTS_CREATE is not writable; no grant, no access.
    provider->docs["content://p/x/document/bare"] = { "bare", std::nullopt, DF::kFlagSupportsDelete, 0, 0 };
    DF bare = DF::FromSingleUri("content://p/x/document/bare");
    CHECK(!bare.CanRead() && !bare.CanWrite() && !bare.IsFile() && !bare.IsDirectory());
    provider->docs[tree + "/document/root"].flags = 0;
    CHECK(root && !root->CanWrite());
    provider->read = false;
    CHECK(root && !root->CanRead());
    provider->read = true;
    // Virtual documents (FLAG_VIRTUAL_DOCUMENT) on document URIs only.
    provider->docs["content://p/x/document/virt"] = { "v", "application/pdf", DF::kFlagVirtualDocument, 0, 0 };
    CHECK(DF::FromSingleUri("content://p/x/document/virt").IsVirtual());
    // A single document has no children and cannot be renamed.
    bool threw = false;
    try { bare.ListFiles(); } catch (const std::logic_error&) { threw = true; }
    CHECK(threw);
    CHECK(root && root->FindFile("a.txt") && file && file->RenameTo("b.txt") && file->Uri() == "renamed:b.txt");
    CHECK(file && file->Delete() && !file->Exists());
    DF::SetHost(nullptr);
}
