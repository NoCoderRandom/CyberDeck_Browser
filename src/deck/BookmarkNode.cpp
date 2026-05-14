#include "deck/BookmarkNode.h"

#include "browser/UrlNavigation.h"
#include "common/Platform.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace cyberdeck::deck {
namespace {

struct JsonValue {
    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue>;

    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> value;
};

class JsonParser {
public:
    explicit JsonParser(std::string_view input) : input_(input) {}

    std::optional<JsonValue> Parse() {
        auto value = ParseValue();
        if (!value) {
            return std::nullopt;
        }

        SkipWhitespace();
        if (position_ != input_.size()) {
            return std::nullopt;
        }
        return value;
    }

    std::size_t position() const {
        return position_;
    }

private:
    std::optional<JsonValue> ParseValue() {
        SkipWhitespace();
        if (position_ >= input_.size()) {
            return std::nullopt;
        }

        const char current = input_[position_];
        if (current == '"') {
            auto text = ParseString();
            if (!text) {
                return std::nullopt;
            }
            return JsonValue{std::move(*text)};
        }
        if (current == '{') {
            return ParseObject();
        }
        if (current == '[') {
            return ParseArray();
        }
        if (std::isdigit(static_cast<unsigned char>(current)) || current == '-') {
            return ParseNumber();
        }
        if (ConsumeLiteral("true")) {
            return JsonValue{true};
        }
        if (ConsumeLiteral("false")) {
            return JsonValue{false};
        }
        if (ConsumeLiteral("null")) {
            return JsonValue{nullptr};
        }
        return std::nullopt;
    }

    std::optional<JsonValue> ParseObject() {
        if (!Consume('{')) {
            return std::nullopt;
        }

        JsonValue::Object object;
        SkipWhitespace();
        if (Consume('}')) {
            return JsonValue{std::move(object)};
        }

        while (true) {
            SkipWhitespace();
            auto key = ParseString();
            if (!key || !Consume(':')) {
                return std::nullopt;
            }

            auto value = ParseValue();
            if (!value) {
                return std::nullopt;
            }
            object.emplace(std::move(*key), std::move(*value));

            SkipWhitespace();
            if (Consume('}')) {
                break;
            }
            if (!Consume(',')) {
                return std::nullopt;
            }
        }

        return JsonValue{std::move(object)};
    }

    std::optional<JsonValue> ParseArray() {
        if (!Consume('[')) {
            return std::nullopt;
        }

        JsonValue::Array array;
        SkipWhitespace();
        if (Consume(']')) {
            return JsonValue{std::move(array)};
        }

        while (true) {
            auto value = ParseValue();
            if (!value) {
                return std::nullopt;
            }
            array.push_back(std::move(*value));

            SkipWhitespace();
            if (Consume(']')) {
                break;
            }
            if (!Consume(',')) {
                return std::nullopt;
            }
        }

        return JsonValue{std::move(array)};
    }

    std::optional<std::string> ParseString() {
        if (!Consume('"')) {
            return std::nullopt;
        }

        std::string output;
        while (position_ < input_.size()) {
            const char current = input_[position_++];
            if (current == '"') {
                return output;
            }
            if (static_cast<unsigned char>(current) < 0x20) {
                return std::nullopt;
            }
            if (current != '\\') {
                output.push_back(current);
                continue;
            }
            if (position_ >= input_.size()) {
                return std::nullopt;
            }

            const char escaped = input_[position_++];
            switch (escaped) {
                case '"':
                case '\\':
                case '/':
                    output.push_back(escaped);
                    break;
                case 'b':
                    output.push_back('\b');
                    break;
                case 'f':
                    output.push_back('\f');
                    break;
                case 'n':
                    output.push_back('\n');
                    break;
                case 'r':
                    output.push_back('\r');
                    break;
                case 't':
                    output.push_back('\t');
                    break;
                case 'u':
                    if (!AppendUnicodeEscape(output)) {
                        return std::nullopt;
                    }
                    break;
                default:
                    return std::nullopt;
            }
        }

        return std::nullopt;
    }

    std::optional<JsonValue> ParseNumber() {
        const std::size_t start = position_;
        if (input_[position_] == '-') {
            ++position_;
        }
        if (position_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[position_]))) {
            return std::nullopt;
        }
        while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_]))) {
            ++position_;
        }
        if (position_ < input_.size() && input_[position_] == '.') {
            ++position_;
            if (position_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[position_]))) {
                return std::nullopt;
            }
            while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_]))) {
                ++position_;
            }
        }

        try {
            return JsonValue{std::stod(std::string(input_.substr(start, position_ - start)))};
        } catch (...) {
            return std::nullopt;
        }
    }

    bool AppendUnicodeEscape(std::string& output) {
        if (position_ + 4 > input_.size()) {
            return false;
        }

        int codepoint = 0;
        for (int index = 0; index < 4; ++index) {
            const char ch = input_[position_++];
            codepoint <<= 4;
            if (ch >= '0' && ch <= '9') {
                codepoint += ch - '0';
            } else if (ch >= 'a' && ch <= 'f') {
                codepoint += ch - 'a' + 10;
            } else if (ch >= 'A' && ch <= 'F') {
                codepoint += ch - 'A' + 10;
            } else {
                return false;
            }
        }

        if (codepoint <= 0x7F) {
            output.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7FF) {
            output.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else {
            output.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
        return true;
    }

    bool Consume(char expected) {
        SkipWhitespace();
        if (position_ >= input_.size() || input_[position_] != expected) {
            return false;
        }
        ++position_;
        return true;
    }

    bool ConsumeLiteral(std::string_view literal) {
        if (input_.substr(position_, literal.size()) != literal) {
            return false;
        }
        position_ += literal.size();
        return true;
    }

    void SkipWhitespace() {
        while (position_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[position_]))) {
            ++position_;
        }
    }

    std::string_view input_;
    std::size_t position_ = 0;
};

std::wstring Trim(std::wstring_view input) {
    auto begin = input.begin();
    auto end = input.end();
    while (begin != end && std::iswspace(*begin)) {
        ++begin;
    }
    while (begin != end && std::iswspace(*(end - 1))) {
        --end;
    }
    return std::wstring(begin, end);
}

bool StartsWith(std::wstring_view value, std::wstring_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::string WideToUtf8(const std::wstring& value) {
    return common::WideToUtf8(value);
}

std::wstring Utf8ToWide(const std::string& value) {
    return common::Utf8ToWide(value);
}

std::string EscapeJsonString(const std::wstring& value) {
    std::string output;
    for (unsigned char ch : WideToUtf8(value)) {
        switch (ch) {
            case '"':
                output += "\\\"";
                break;
            case '\\':
                output += "\\\\";
                break;
            case '\b':
                output += "\\b";
                break;
            case '\f':
                output += "\\f";
                break;
            case '\n':
                output += "\\n";
                break;
            case '\r':
                output += "\\r";
                break;
            case '\t':
                output += "\\t";
                break;
            default:
                if (ch < 0x20) {
                    output += "\\u00";
                    constexpr char hex[] = "0123456789ABCDEF";
                    output.push_back(hex[(ch >> 4) & 0x0F]);
                    output.push_back(hex[ch & 0x0F]);
                } else {
                    output.push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    return output;
}

std::string EscapeJsonAscii(std::string_view value) {
    std::string output;
    for (unsigned char ch : value) {
        if (ch == '"' || ch == '\\') {
            output.push_back('\\');
        }
        if (ch >= 0x20) {
            output.push_back(static_cast<char>(ch));
        }
    }
    return output;
}

const JsonValue::Object* AsObject(const JsonValue& value) {
    return std::get_if<JsonValue::Object>(&value.value);
}

const JsonValue::Array* AsArray(const JsonValue& value) {
    return std::get_if<JsonValue::Array>(&value.value);
}

const std::string* StringField(const JsonValue::Object& object, std::string_view key) {
    const auto found = object.find(std::string(key));
    if (found == object.end()) {
        return nullptr;
    }
    return std::get_if<std::string>(&found->second.value);
}

std::optional<std::string> OptionalStringField(const JsonValue::Object& object, std::string_view key) {
    const auto found = object.find(std::string(key));
    if (found == object.end() || std::holds_alternative<std::nullptr_t>(found->second.value)) {
        return std::nullopt;
    }

    const std::string* value = std::get_if<std::string>(&found->second.value);
    return value == nullptr ? std::optional<std::string>{} : std::optional<std::string>{*value};
}

bool OptionalStringFieldIsValid(const JsonValue::Object& object, std::string_view key) {
    const auto found = object.find(std::string(key));
    return found == object.end() || std::holds_alternative<std::nullptr_t>(found->second.value) ||
           std::holds_alternative<std::string>(found->second.value);
}

std::optional<int> IntField(const JsonValue::Object& object, std::string_view key) {
    const auto found = object.find(std::string(key));
    if (found == object.end()) {
        return std::nullopt;
    }

    const double* value = std::get_if<double>(&found->second.value);
    if (value == nullptr || *value < 0.0 || *value > static_cast<double>(INT_MAX)) {
        return std::nullopt;
    }

    const int result = static_cast<int>(*value);
    if (static_cast<double>(result) != *value) {
        return std::nullopt;
    }
    return result;
}

std::optional<float> FloatField(const JsonValue::Object& object, std::string_view key) {
    const auto found = object.find(std::string(key));
    if (found == object.end()) {
        return std::nullopt;
    }

    const double* value = std::get_if<double>(&found->second.value);
    if (value == nullptr || !std::isfinite(*value)) {
        return std::nullopt;
    }
    return static_cast<float>(*value);
}

bool IsNullOrMissing(const JsonValue::Object& object, std::string_view key) {
    const auto found = object.find(std::string(key));
    return found == object.end() || std::holds_alternative<std::nullptr_t>(found->second.value);
}

bool IsValidShapeType(BookmarkNodeShapeType shape_type) {
    switch (shape_type) {
        case BookmarkNodeShapeType::Hex:
        case BookmarkNodeShapeType::Cube:
        case BookmarkNodeShapeType::Panel:
            return true;
    }
    return false;
}

bool IsValidColorTheme(BookmarkNodeColorTheme color_theme) {
    switch (color_theme) {
        case BookmarkNodeColorTheme::Green:
        case BookmarkNodeColorTheme::Yellow:
        case BookmarkNodeColorTheme::Red:
        case BookmarkNodeColorTheme::Mixed:
            return true;
    }
    return false;
}

void SetError(std::wstring* error_message, std::wstring message) {
    if (error_message != nullptr) {
        *error_message = std::move(message);
    }
}

}  // namespace

const char* ToJsonString(BookmarkNodeShapeType shape_type) {
    switch (shape_type) {
        case BookmarkNodeShapeType::Hex:
            return "hex";
        case BookmarkNodeShapeType::Cube:
            return "cube";
        case BookmarkNodeShapeType::Panel:
            return "panel";
    }
    return "hex";
}

const char* ToJsonString(BookmarkNodeColorTheme color_theme) {
    switch (color_theme) {
        case BookmarkNodeColorTheme::Green:
            return "green";
        case BookmarkNodeColorTheme::Yellow:
            return "yellow";
        case BookmarkNodeColorTheme::Red:
            return "red";
        case BookmarkNodeColorTheme::Mixed:
            return "mixed";
    }
    return "green";
}

std::optional<BookmarkNodeShapeType> BookmarkNodeShapeTypeFromString(std::string_view value) {
    if (value == "hex") {
        return BookmarkNodeShapeType::Hex;
    }
    if (value == "cube") {
        return BookmarkNodeShapeType::Cube;
    }
    if (value == "panel") {
        return BookmarkNodeShapeType::Panel;
    }
    return std::nullopt;
}

std::optional<BookmarkNodeColorTheme> BookmarkNodeColorThemeFromString(std::string_view value) {
    if (value == "green") {
        return BookmarkNodeColorTheme::Green;
    }
    if (value == "yellow") {
        return BookmarkNodeColorTheme::Yellow;
    }
    if (value == "red") {
        return BookmarkNodeColorTheme::Red;
    }
    if (value == "mixed") {
        return BookmarkNodeColorTheme::Mixed;
    }
    return std::nullopt;
}

std::string CurrentBookmarkNodeUtcTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);

    std::tm utc_time{};
#if defined(_MSC_VER)
    gmtime_s(&utc_time, &time);
#elif defined(__MINGW32__)
    gmtime_s(&utc_time, &time);
#else
    gmtime_r(&time, &utc_time);
#endif

    std::ostringstream output;
    output << std::put_time(&utc_time, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

std::wstring GenerateBookmarkNodeId(std::string_view created_utc, int sequence) {
    std::wstring id = L"node-";
    for (char ch : created_utc) {
        if (std::isalnum(static_cast<unsigned char>(ch))) {
            id.push_back(static_cast<wchar_t>(std::tolower(static_cast<unsigned char>(ch))));
        }
    }

    std::wostringstream suffix;
    suffix << L'-' << std::setw(6) << std::setfill(L'0') << std::max(1, sequence);
    id += suffix.str();
    return id;
}

std::optional<std::wstring> NormalizeBookmarkNodeUrl(std::wstring_view url) {
    const browser::NormalizedNavigation normalized = browser::NormalizeAddressBarInput(url);
    if (normalized.decision != browser::NavigationDecision::kNavigate || normalized.target_url.empty()) {
        return std::nullopt;
    }

    const browser::ProtocolDecision protocol = browser::ClassifyNavigationProtocol(normalized.target_url);
    if (protocol.action != browser::ProtocolAction::kAllow) {
        return std::nullopt;
    }

    if (!StartsWith(normalized.target_url, L"https://") && !StartsWith(normalized.target_url, L"http://")) {
        return std::nullopt;
    }
    return normalized.target_url;
}

BookmarkNodeValidationResult ValidateBookmarkNode(const BookmarkNode& node) {
    if (Trim(node.id).empty()) {
        return {.valid = false, .message = L"Node id must not be empty."};
    }
    if (Trim(node.title).empty()) {
        return {.valid = false, .message = L"Node title must not be empty."};
    }

    const std::optional<std::wstring> normalized_url = NormalizeBookmarkNodeUrl(node.url);
    if (!normalized_url) {
        return {.valid = false, .message = L"Node URL is not safe for bookmarking."};
    }
    if (*normalized_url != node.url) {
        return {.valid = false, .message = L"Node URL must be normalized before saving."};
    }

    if (!IsValidShapeType(node.shape_type)) {
        return {.valid = false, .message = L"Node shape type is invalid."};
    }
    if (!IsValidColorTheme(node.color_theme)) {
        return {.valid = false, .message = L"Node color theme is invalid."};
    }
    if (node.created_utc.empty() || node.updated_utc.empty()) {
        return {.valid = false, .message = L"Node timestamps must not be empty."};
    }
    if (node.last_visited_utc && node.last_visited_utc->empty()) {
        return {.valid = false, .message = L"Node last visited timestamp must be null or non-empty."};
    }
    if (node.vault_id && Trim(*node.vault_id).empty()) {
        return {.valid = false, .message = L"Node Vault id must be null or non-empty."};
    }
    if (node.visit_count < 0) {
        return {.valid = false, .message = L"Node visit count must not be negative."};
    }
    for (const std::wstring& tag : node.tags) {
        if (Trim(tag).empty()) {
            return {.valid = false, .message = L"Node tags must not be empty."};
        }
    }

    return {.valid = true, .message = {}};
}

BookmarkNodeCreateResult CreateBookmarkNodeFromActiveTab(
    const browser::BrowserTabState& active_tab,
    std::wstring_view node_id,
    std::string_view created_utc) {
    const std::optional<std::wstring> normalized_url = NormalizeBookmarkNodeUrl(active_tab.url);
    if (!normalized_url) {
        return {.success = false, .node = {}, .message = L"Active tab URL is not safe for a Deck Node."};
    }

    BookmarkNode node;
    node.id = Trim(node_id);
    node.url = *normalized_url;
    node.title = Trim(active_tab.title).empty() ? node.url : Trim(active_tab.title);
    node.shape_type = BookmarkNodeShapeType::Hex;
    node.color_theme = BookmarkNodeColorTheme::Green;
    node.created_utc = std::string(created_utc);
    node.updated_utc = std::string(created_utc);
    node.visit_count = 0;

    const BookmarkNodeValidationResult validation = ValidateBookmarkNode(node);
    if (!validation.valid) {
        return {.success = false, .node = {}, .message = validation.message};
    }
    return {.success = true, .node = std::move(node), .message = {}};
}

BookmarkNodeCreateResult CreateBookmarkNodeFromActiveTab(const browser::BrowserTabState& active_tab, int id_sequence) {
    const std::string now = CurrentBookmarkNodeUtcTimestamp();
    return CreateBookmarkNodeFromActiveTab(active_tab, GenerateBookmarkNodeId(now, id_sequence), now);
}

std::string BookmarkNodeToJson(const BookmarkNode& node) {
    std::ostringstream output;
    output << std::setprecision(6);
    output << "{\n";
    output << "  \"version\": 1,\n";
    output << "  \"id\": \"" << EscapeJsonString(node.id) << "\",\n";
    output << "  \"title\": \"" << EscapeJsonString(node.title) << "\",\n";
    output << "  \"url\": \"" << EscapeJsonString(node.url) << "\",\n";
    if (node.vault_id) {
        output << "  \"vaultId\": \"" << EscapeJsonString(*node.vault_id) << "\",\n";
    } else {
        output << "  \"vaultId\": null,\n";
    }
    if (node.favicon_path) {
        output << "  \"faviconPath\": \"" << EscapeJsonString(*node.favicon_path) << "\",\n";
    } else {
        output << "  \"faviconPath\": null,\n";
    }
    output << "  \"shapeType\": \"" << ToJsonString(node.shape_type) << "\",\n";
    output << "  \"colorTheme\": \"" << ToJsonString(node.color_theme) << "\",\n";
    output << "  \"createdUtc\": \"" << EscapeJsonAscii(node.created_utc) << "\",\n";
    output << "  \"updatedUtc\": \"" << EscapeJsonAscii(node.updated_utc) << "\",\n";
    if (node.last_visited_utc) {
        output << "  \"lastVisitedUtc\": \"" << EscapeJsonAscii(*node.last_visited_utc) << "\",\n";
    } else {
        output << "  \"lastVisitedUtc\": null,\n";
    }
    output << "  \"visitCount\": " << node.visit_count << ",\n";
    if (node.deck_position) {
        output << "  \"deckPosition\": {\n";
        output << "    \"x\": " << node.deck_position->x << ",\n";
        output << "    \"y\": " << node.deck_position->y << ",\n";
        output << "    \"z\": " << node.deck_position->z << "\n";
        output << "  },\n";
    } else {
        output << "  \"deckPosition\": null,\n";
    }
    output << "  \"tags\": [";
    for (std::size_t index = 0; index < node.tags.size(); ++index) {
        output << (index == 0 ? "\n    \"" : ",\n    \"") << EscapeJsonString(node.tags[index]) << "\"";
    }
    output << (node.tags.empty() ? "" : "\n  ");
    output << "]\n";
    output << "}\n";
    return output.str();
}

std::optional<BookmarkNode> BookmarkNodeFromJson(std::string_view json_text, std::wstring* error_message) {
    JsonParser parser(json_text);
    const auto root_value = parser.Parse();
    if (!root_value) {
        SetError(error_message, L"Bookmark Node JSON parse failed near byte " + std::to_wstring(parser.position()) + L".");
        return std::nullopt;
    }

    const JsonValue::Object* root = AsObject(*root_value);
    if (root == nullptr) {
        SetError(error_message, L"Bookmark Node JSON root must be an object.");
        return std::nullopt;
    }

    const std::string* id = StringField(*root, "id");
    const std::string* title = StringField(*root, "title");
    const std::string* url = StringField(*root, "url");
    const std::string* shape = StringField(*root, "shapeType");
    const std::string* color = StringField(*root, "colorTheme");
    const std::string* created = StringField(*root, "createdUtc");
    const std::string* updated = StringField(*root, "updatedUtc");
    const std::optional<int> version = IntField(*root, "version");
    const std::optional<int> visit_count = IntField(*root, "visitCount");
    if (!version || *version != 1 || id == nullptr || title == nullptr || url == nullptr || shape == nullptr || color == nullptr ||
        created == nullptr || updated == nullptr || !visit_count) {
        SetError(error_message, L"Bookmark Node JSON has missing or invalid required fields.");
        return std::nullopt;
    }
    if (!OptionalStringFieldIsValid(*root, "vaultId") || !OptionalStringFieldIsValid(*root, "faviconPath") ||
        !OptionalStringFieldIsValid(*root, "lastVisitedUtc")) {
        SetError(error_message, L"Bookmark Node JSON has an invalid optional string field.");
        return std::nullopt;
    }

    const std::optional<BookmarkNodeShapeType> shape_type = BookmarkNodeShapeTypeFromString(*shape);
    const std::optional<BookmarkNodeColorTheme> color_theme = BookmarkNodeColorThemeFromString(*color);
    if (!shape_type || !color_theme) {
        SetError(error_message, L"Bookmark Node JSON has an invalid shape or color theme.");
        return std::nullopt;
    }

    BookmarkNode node;
    node.id = Utf8ToWide(*id);
    node.title = Utf8ToWide(*title);
    node.url = Utf8ToWide(*url);
    if (const std::optional<std::string> vault_id = OptionalStringField(*root, "vaultId")) {
        node.vault_id = Utf8ToWide(*vault_id);
    }
    if (const std::optional<std::string> favicon = OptionalStringField(*root, "faviconPath")) {
        node.favicon_path = Utf8ToWide(*favicon);
    }
    node.shape_type = *shape_type;
    node.color_theme = *color_theme;
    node.created_utc = *created;
    node.updated_utc = *updated;
    node.last_visited_utc = OptionalStringField(*root, "lastVisitedUtc");
    node.visit_count = *visit_count;

    if (!IsNullOrMissing(*root, "deckPosition")) {
        const auto found = root->find("deckPosition");
        const JsonValue::Object* position = found == root->end() ? nullptr : AsObject(found->second);
        if (position == nullptr) {
            SetError(error_message, L"Bookmark Node deckPosition must be null or an object.");
            return std::nullopt;
        }

        const auto x = FloatField(*position, "x");
        const auto y = FloatField(*position, "y");
        const auto z = FloatField(*position, "z");
        if (!x || !y || !z) {
            SetError(error_message, L"Bookmark Node deckPosition has missing coordinates.");
            return std::nullopt;
        }
        node.deck_position = BookmarkNodePosition{.x = *x, .y = *y, .z = *z};
    }

    if (!IsNullOrMissing(*root, "tags")) {
        const auto found = root->find("tags");
        const JsonValue::Array* tags = found == root->end() ? nullptr : AsArray(found->second);
        if (tags == nullptr) {
            SetError(error_message, L"Bookmark Node tags must be an array.");
            return std::nullopt;
        }
        for (const JsonValue& item : *tags) {
            const std::string* tag = std::get_if<std::string>(&item.value);
            if (tag == nullptr) {
                SetError(error_message, L"Bookmark Node tags must contain only strings.");
                return std::nullopt;
            }
            node.tags.push_back(Utf8ToWide(*tag));
        }
    }

    const BookmarkNodeValidationResult validation = ValidateBookmarkNode(node);
    if (!validation.valid) {
        SetError(error_message, validation.message);
        return std::nullopt;
    }

    return node;
}

}  // namespace cyberdeck::deck
