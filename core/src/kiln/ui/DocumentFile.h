#pragma once

// DocumentFile (androidx documentfile): files and folders behind a document URI, as the
// Storage Access Framework hands them over, or a plain file.
//   - FromTreeUri: the folder an OpenDocumentTree result names. List it, find, create, rename
//     and delete inside it (TreeDocumentFile).
//   - FromSingleUri: one document an OpenDocument / CreateDocument result names
//     (SingleDocumentFile: no listing or creating).
//   - FromFile: a path on the file system (RawDocumentFile).
// Read and write a document's bytes with ui::ReadContent / WriteContent on its Uri().
//
//   auto folder = ui::DocumentFile::FromTreeUri(treeUri);
//   if (folder && folder->CanWrite())
//       if (auto f = folder->CreateFile("text/plain", "notes")) ui::WriteContent(f->Uri(), bytes);
//
// Each call asks the document's provider, which may be slow (a cloud drive): call from a
// background dispatcher. Failures are an empty result or false, as upstream.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kiln::ui {

    class DocumentFile {
    public:
        enum class Kind { Tree, Single, Raw };

        // nullopt when the URI is not a tree URI.
        static std::optional<DocumentFile> FromTreeUri(const std::string& treeUri);
        static DocumentFile FromSingleUri(const std::string& uri);
        static DocumentFile FromFile(const std::string& path);
        // DocumentsContract.isDocumentUri.
        static bool IsDocumentUri(const std::string& uri);

        // A new file in this folder: its display name gets an extension for the MIME type where
        // the provider adds one. nullopt on failure. Not on a single document (std::logic_error).
        std::optional<DocumentFile> CreateFile(const std::string& mimeType, const std::string& displayName) const;
        std::optional<DocumentFile> CreateDirectory(const std::string& displayName) const;

        const std::string& Uri() const { return m_Uri; }
        Kind GetKind() const { return m_Kind; }
        std::optional<std::string> Name() const;
        // The MIME type; nullopt for a directory.
        std::optional<std::string> Type() const;
        bool IsDirectory() const;
        bool IsFile() const;
        bool IsVirtual() const;
        std::int64_t LastModified() const;   // ms since the epoch; 0 when unknown
        std::int64_t Length() const;         // bytes; 0 for a directory or unknown
        bool CanRead() const;
        bool CanWrite() const;
        bool Delete() const;
        bool Exists() const;
        // The folder's documents. Not on a single document (std::logic_error).
        std::vector<DocumentFile> ListFiles() const;
        // The first document in this folder whose name is displayName.
        std::optional<DocumentFile> FindFile(const std::string& displayName) const;
        // Renames it (its URI may change). Not on a single document (std::logic_error).
        bool RenameTo(const std::string& displayName);
        // The folder it was reached from: nullopt for one made with From*.
        std::optional<DocumentFile> ParentFile() const;

        // What the platform answers for a document URI (DocumentsContract and the provider's
        // Document columns). Android installs it; without one only FromFile works.
        struct DocumentInfo {
            std::optional<std::string> displayName, mimeType;
            std::int64_t flags = 0, lastModified = 0, size = 0;
        };
        struct Host {
            virtual ~Host() = default;
            virtual bool IsDocumentUri(const std::string& uri) = 0;
            // buildDocumentUriUsingTree(treeUri, documentId of the tree or of the document in it).
            virtual std::optional<std::string> TreeDocumentUri(const std::string& treeUri) = 0;
            virtual std::optional<DocumentInfo> Query(const std::string& uri) = 0;   // nullopt: no such document
            // checkCallingOrSelfUriPermission for read or write.
            virtual bool HasPermission(const std::string& uri, bool write) = 0;
            virtual std::optional<std::string> Create(const std::string& parentUri, const std::string& mimeType, const std::string& displayName) = 0;
            virtual bool Delete(const std::string& uri) = 0;
            virtual std::optional<std::string> Rename(const std::string& uri, const std::string& displayName) = 0;
            virtual std::vector<std::string> Children(const std::string& treeDocumentUri) = 0;
        };
        static void SetHost(std::shared_ptr<Host> host);

        // DocumentsContract.Document flags and the directory MIME type.
        static constexpr std::int64_t kFlagSupportsWrite = 0x2, kFlagSupportsDelete = 0x4, kFlagDirSupportsCreate = 0x8,
                                      kFlagVirtualDocument = 0x200;
        static constexpr const char* kMimeTypeDir = "vnd.android.document/directory";

    private:
        DocumentFile(Kind kind, std::string uri, std::shared_ptr<const DocumentFile> parent)
            : m_Kind(kind), m_Uri(std::move(uri)), m_Parent(std::move(parent)) {}
        std::optional<DocumentInfo> Info() const;
        std::optional<std::string> RawType() const;
        Kind m_Kind;
        std::string m_Uri;   // a path for Raw
        std::shared_ptr<const DocumentFile> m_Parent;
    };

}
