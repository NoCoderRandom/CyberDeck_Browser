#pragma once

#include "browser/BrowserTypes.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cyberdeck::deck {

enum class BookmarkNodeShapeType {
    Hex,
    Cube,
    Panel,
};

enum class BookmarkNodeColorTheme {
    Green,
    Yellow,
    Red,
    Mixed,
};

struct BookmarkNodePosition {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct BookmarkVault {
    std::wstring id;
    std::wstring name;
    BookmarkNodeColorTheme color_theme = BookmarkNodeColorTheme::Green;
    std::string created_utc;
    std::string updated_utc;
};

struct BookmarkNode {
    std::wstring id;
    std::wstring title;
    std::wstring url;
    std::optional<std::wstring> vault_id;
    std::optional<std::wstring> favicon_path;
    BookmarkNodeShapeType shape_type = BookmarkNodeShapeType::Hex;
    BookmarkNodeColorTheme color_theme = BookmarkNodeColorTheme::Green;
    std::string created_utc;
    std::string updated_utc;
    std::optional<std::string> last_visited_utc;
    int visit_count = 0;
    std::optional<BookmarkNodePosition> deck_position;
    std::vector<std::wstring> tags;
};

struct BookmarkNodeValidationResult {
    bool valid = false;
    std::wstring message;
};

struct BookmarkNodeCreateResult {
    bool success = false;
    BookmarkNode node;
    std::wstring message;
};

const char* ToJsonString(BookmarkNodeShapeType shape_type);
const char* ToJsonString(BookmarkNodeColorTheme color_theme);
std::optional<BookmarkNodeShapeType> BookmarkNodeShapeTypeFromString(std::string_view value);
std::optional<BookmarkNodeColorTheme> BookmarkNodeColorThemeFromString(std::string_view value);

std::string CurrentBookmarkNodeUtcTimestamp();
std::wstring GenerateBookmarkNodeId(std::string_view created_utc, int sequence);
std::optional<std::wstring> NormalizeBookmarkNodeUrl(std::wstring_view url);
BookmarkNodeValidationResult ValidateBookmarkNode(const BookmarkNode& node);

BookmarkNodeCreateResult CreateBookmarkNodeFromActiveTab(
    const browser::BrowserTabState& active_tab,
    std::wstring_view node_id,
    std::string_view created_utc);
BookmarkNodeCreateResult CreateBookmarkNodeFromActiveTab(const browser::BrowserTabState& active_tab, int id_sequence);

std::string BookmarkNodeToJson(const BookmarkNode& node);
std::optional<BookmarkNode> BookmarkNodeFromJson(std::string_view json_text, std::wstring* error_message = nullptr);

}  // namespace cyberdeck::deck
