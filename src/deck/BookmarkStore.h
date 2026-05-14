#pragma once

#include "common/Logger.h"
#include "deck/BookmarkNode.h"

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cyberdeck::deck {

class BookmarkStore {
public:
    BookmarkStore() = default;
    BookmarkStore(const BookmarkStore&) = delete;
    BookmarkStore& operator=(const BookmarkStore&) = delete;

    static std::filesystem::path DefaultBookmarksPath();

    bool Initialize(std::filesystem::path bookmarks_path, common::Logger& logger);
    std::vector<BookmarkNode> LoadBookmarks() const;
    std::vector<BookmarkVault> LoadVaults() const;
    bool SaveBookmarks(std::vector<BookmarkNode> nodes);
    bool SaveVaults(std::vector<BookmarkVault> vaults);
    bool AddBookmark(BookmarkNode node);
    bool UpdateBookmark(BookmarkNode node);
    bool DeleteBookmark(std::wstring_view id);
    bool UpdateVault(BookmarkVault vault);
    bool DeleteVault(std::wstring_view id);
    std::optional<BookmarkNode> FindBookmarkById(std::wstring_view id) const;
    std::filesystem::path path() const;

private:
    bool LoadLocked();
    bool WriteLocked();
    bool RenameCorruptedFileLocked();

    mutable std::mutex mutex_;
    std::filesystem::path bookmarks_path_;
    common::Logger* logger_ = nullptr;
    std::vector<BookmarkNode> nodes_;
    std::vector<BookmarkVault> vaults_;
    bool defaults_seeded_ = false;
    bool vaults_seeded_ = false;
};

}  // namespace cyberdeck::deck
