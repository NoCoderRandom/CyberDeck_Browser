#include "deck/BookmarkStore.h"

#include "common/Platform.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <climits>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
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

std::string WideToUtf8(const std::wstring& value) {
    return common::WideToUtf8(value);
}

std::wstring Utf8ToWide(const std::string& value) {
    return common::Utf8ToWide(value);
}

std::string NarrowForLog(const std::filesystem::path& path) {
    return WideToUtf8(path.wstring());
}

std::string CurrentFileTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);

    std::tm local_time{};
#if defined(_MSC_VER)
    localtime_s(&local_time, &time);
#elif defined(__MINGW32__)
    localtime_s(&local_time, &time);
#else
    localtime_r(&time, &local_time);
#endif

    std::ostringstream output;
    output << std::put_time(&local_time, "%Y%m%d-%H%M%S");
    return output.str();
}

std::string EscapeJsonString(std::string_view value) {
    std::string output;
    for (unsigned char ch : value) {
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

const JsonValue::Object* AsObject(const JsonValue& value) {
    return std::get_if<JsonValue::Object>(&value.value);
}

const JsonValue::Array* AsArray(const JsonValue& value) {
    return std::get_if<JsonValue::Array>(&value.value);
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

std::optional<bool> BoolField(const JsonValue::Object& object, std::string_view key) {
    const auto found = object.find(std::string(key));
    if (found == object.end()) {
        return std::nullopt;
    }

    const bool* value = std::get_if<bool>(&found->second.value);
    if (value == nullptr) {
        return std::nullopt;
    }
    return *value;
}

const std::string* StringField(const JsonValue::Object& object, std::string_view key) {
    const auto found = object.find(std::string(key));
    if (found == object.end()) {
        return nullptr;
    }
    return std::get_if<std::string>(&found->second.value);
}

std::string EscapeJsonWideString(const std::wstring& value) {
    return EscapeJsonString(WideToUtf8(value));
}

void WriteJsonValue(std::ostream& output, const JsonValue& value) {
    if (std::holds_alternative<std::nullptr_t>(value.value)) {
        output << "null";
    } else if (const bool* bool_value = std::get_if<bool>(&value.value)) {
        output << (*bool_value ? "true" : "false");
    } else if (const double* number = std::get_if<double>(&value.value)) {
        output << std::setprecision(15) << *number;
    } else if (const std::string* text = std::get_if<std::string>(&value.value)) {
        output << '"' << EscapeJsonString(*text) << '"';
    } else if (const JsonValue::Array* array = std::get_if<JsonValue::Array>(&value.value)) {
        output << '[';
        for (std::size_t index = 0; index < array->size(); ++index) {
            output << (index == 0 ? "" : ",");
            WriteJsonValue(output, (*array)[index]);
        }
        output << ']';
    } else if (const JsonValue::Object* object = std::get_if<JsonValue::Object>(&value.value)) {
        output << '{';
        std::size_t index = 0;
        for (const auto& [key, item] : *object) {
            output << (index++ == 0 ? "" : ",") << '"' << EscapeJsonString(key) << "\":";
            WriteJsonValue(output, item);
        }
        output << '}';
    }
}

std::string JsonValueToString(const JsonValue& value) {
    std::ostringstream output;
    WriteJsonValue(output, value);
    return output.str();
}

std::string IndentJson(std::string_view json, std::string_view indent) {
    std::string output;
    bool at_line_start = true;
    for (char ch : json) {
        if (at_line_start && ch != '\n') {
            output.append(indent);
            at_line_start = false;
        }
        output.push_back(ch);
        if (ch == '\n') {
            at_line_start = true;
        }
    }
    return output;
}

bool HasDuplicateIds(const std::vector<BookmarkNode>& nodes) {
    std::set<std::wstring> ids;
    for (const BookmarkNode& node : nodes) {
        if (!ids.insert(node.id).second) {
            return true;
        }
    }
    return false;
}

bool HasDuplicateVaultIds(const std::vector<BookmarkVault>& vaults) {
    std::set<std::wstring> ids;
    for (const BookmarkVault& vault : vaults) {
        if (!ids.insert(vault.id).second) {
            return true;
        }
    }
    return false;
}

bool AreValidNodes(const std::vector<BookmarkNode>& nodes) {
    if (HasDuplicateIds(nodes)) {
        return false;
    }

    return std::all_of(nodes.begin(), nodes.end(), [](const BookmarkNode& node) {
        return ValidateBookmarkNode(node).valid;
    });
}

bool IsValidVault(const BookmarkVault& vault) {
    return !vault.id.empty() && !vault.name.empty() && !vault.created_utc.empty() && !vault.updated_utc.empty();
}

bool AreValidVaults(const std::vector<BookmarkVault>& vaults) {
    if (HasDuplicateVaultIds(vaults)) {
        return false;
    }

    return std::all_of(vaults.begin(), vaults.end(), IsValidVault);
}

std::set<std::wstring> VaultIdSet(const std::vector<BookmarkVault>& vaults) {
    std::set<std::wstring> ids;
    for (const BookmarkVault& vault : vaults) {
        ids.insert(vault.id);
    }
    return ids;
}

bool VaultIdExists(const std::vector<BookmarkVault>& vaults, const std::wstring& id) {
    return std::any_of(vaults.begin(), vaults.end(), [&id](const BookmarkVault& vault) {
        return vault.id == id;
    });
}

bool NodesReferenceKnownVaults(const std::vector<BookmarkNode>& nodes, const std::vector<BookmarkVault>& vaults) {
    const std::set<std::wstring> known_vaults = VaultIdSet(vaults);
    return std::all_of(nodes.begin(), nodes.end(), [&known_vaults](const BookmarkNode& node) {
        return !node.vault_id || known_vaults.find(*node.vault_id) != known_vaults.end();
    });
}

void ClearUnknownVaultReferences(std::vector<BookmarkNode>& nodes, const std::vector<BookmarkVault>& vaults) {
    const std::set<std::wstring> known_vaults = VaultIdSet(vaults);
    for (BookmarkNode& node : nodes) {
        if (node.vault_id && known_vaults.find(*node.vault_id) == known_vaults.end()) {
            node.vault_id.reset();
        }
    }
}

BookmarkNode MakeDefaultBookmark(
    std::wstring id,
    std::wstring title,
    std::wstring url,
    std::optional<std::wstring> vault_id,
    BookmarkNodeShapeType shape_type,
    BookmarkNodeColorTheme color_theme,
    BookmarkNodePosition deck_position,
    std::vector<std::wstring> tags,
    const std::string& timestamp) {
    BookmarkNode node;
    node.id = std::move(id);
    node.title = std::move(title);
    node.url = std::move(url);
    node.vault_id = std::move(vault_id);
    node.shape_type = shape_type;
    node.color_theme = color_theme;
    node.created_utc = timestamp;
    node.updated_utc = timestamp;
    node.visit_count = 0;
    node.deck_position = deck_position;
    node.tags = std::move(tags);
    return node;
}

BookmarkVault MakeDefaultVault(
    std::wstring id,
    std::wstring name,
    BookmarkNodeColorTheme color_theme,
    const std::string& timestamp) {
    BookmarkVault vault;
    vault.id = std::move(id);
    vault.name = std::move(name);
    vault.color_theme = color_theme;
    vault.created_utc = timestamp;
    vault.updated_utc = timestamp;
    return vault;
}

std::vector<BookmarkVault> CreateDefaultVaults() {
    const std::string timestamp = CurrentBookmarkNodeUtcTimestamp();
    return {
        MakeDefaultVault(L"vault-search", L"Search Array", BookmarkNodeColorTheme::Green, timestamp),
        MakeDefaultVault(L"vault-ai", L"AI Core", BookmarkNodeColorTheme::Mixed, timestamp),
        MakeDefaultVault(L"vault-news", L"News Wire", BookmarkNodeColorTheme::Red, timestamp),
        MakeDefaultVault(L"vault-code", L"Code Forge", BookmarkNodeColorTheme::Yellow, timestamp),
        MakeDefaultVault(L"vault-media", L"Media Bay", BookmarkNodeColorTheme::Green, timestamp),
    };
}

std::vector<BookmarkNode> CreateDefaultBookmarks() {
    const std::string timestamp = CurrentBookmarkNodeUtcTimestamp();
    return {
        MakeDefaultBookmark(
            L"default-google",
            L"Google",
            L"https://www.google.com",
            L"vault-search",
            BookmarkNodeShapeType::Hex,
            BookmarkNodeColorTheme::Green,
            BookmarkNodePosition{.x = -1.8f, .y = 0.05f, .z = 0.15f},
            {L"default", L"search"},
            timestamp),
        MakeDefaultBookmark(
            L"default-duckduckgo",
            L"DuckDuckGo",
            L"https://duckduckgo.com",
            L"vault-search",
            BookmarkNodeShapeType::Cube,
            BookmarkNodeColorTheme::Green,
            BookmarkNodePosition{.x = -0.9f, .y = 0.02f, .z = -0.2f},
            {L"default", L"search", L"privacy"},
            timestamp),
        MakeDefaultBookmark(
            L"default-wikipedia",
            L"Wikipedia",
            L"https://www.wikipedia.org",
            L"vault-search",
            BookmarkNodeShapeType::Panel,
            BookmarkNodeColorTheme::Yellow,
            BookmarkNodePosition{.x = 0.8f, .y = 0.0f, .z = -0.15f},
            {L"default", L"knowledge"},
            timestamp),
        MakeDefaultBookmark(
            L"default-perplexity",
            L"Perplexity",
            L"https://www.perplexity.ai",
            L"vault-search",
            BookmarkNodeShapeType::Hex,
            BookmarkNodeColorTheme::Mixed,
            BookmarkNodePosition{.x = 1.65f, .y = 0.05f, .z = 0.25f},
            {L"default", L"search", L"ai"},
            timestamp),
        MakeDefaultBookmark(
            L"default-reddit",
            L"Reddit",
            L"https://www.reddit.com",
            L"vault-news",
            BookmarkNodeShapeType::Panel,
            BookmarkNodeColorTheme::Red,
            BookmarkNodePosition{.x = -0.6f, .y = 0.0f, .z = -0.35f},
            {L"default", L"community"},
            timestamp),
        MakeDefaultBookmark(
            L"default-hacker-news",
            L"Hacker News",
            L"https://news.ycombinator.com",
            L"vault-news",
            BookmarkNodeShapeType::Hex,
            BookmarkNodeColorTheme::Yellow,
            BookmarkNodePosition{.x = -1.3f, .y = 0.02f, .z = 0.15f},
            {L"default", L"news", L"tech"},
            timestamp),
        MakeDefaultBookmark(
            L"default-bbc",
            L"BBC News",
            L"https://www.bbc.com/news",
            L"vault-news",
            BookmarkNodeShapeType::Panel,
            BookmarkNodeColorTheme::Green,
            BookmarkNodePosition{.x = 0.45f, .y = 0.0f, .z = -0.1f},
            {L"default", L"news"},
            timestamp),
        MakeDefaultBookmark(
            L"default-the-verge",
            L"The Verge",
            L"https://www.theverge.com",
            L"vault-news",
            BookmarkNodeShapeType::Cube,
            BookmarkNodeColorTheme::Mixed,
            BookmarkNodePosition{.x = 1.35f, .y = 0.04f, .z = 0.2f},
            {L"default", L"news", L"tech"},
            timestamp),
        MakeDefaultBookmark(
            L"default-github",
            L"GitHub",
            L"https://github.com",
            L"vault-code",
            BookmarkNodeShapeType::Cube,
            BookmarkNodeColorTheme::Yellow,
            BookmarkNodePosition{.x = 0.6f, .y = 0.0f, .z = -0.35f},
            {L"default", L"code"},
            timestamp),
        MakeDefaultBookmark(
            L"default-stack-overflow",
            L"Stack Overflow",
            L"https://stackoverflow.com",
            L"vault-code",
            BookmarkNodeShapeType::Panel,
            BookmarkNodeColorTheme::Yellow,
            BookmarkNodePosition{.x = -1.1f, .y = 0.02f, .z = 0.1f},
            {L"default", L"code", L"help"},
            timestamp),
        MakeDefaultBookmark(
            L"default-mdn",
            L"MDN Web Docs",
            L"https://developer.mozilla.org",
            L"vault-code",
            BookmarkNodeShapeType::Hex,
            BookmarkNodeColorTheme::Green,
            BookmarkNodePosition{.x = 0.0f, .y = 0.0f, .z = -0.18f},
            {L"default", L"docs", L"web"},
            timestamp),
        MakeDefaultBookmark(
            L"default-cppreference",
            L"cppreference",
            L"https://en.cppreference.com",
            L"vault-code",
            BookmarkNodeShapeType::Cube,
            BookmarkNodeColorTheme::Mixed,
            BookmarkNodePosition{.x = 1.15f, .y = 0.04f, .z = 0.12f},
            {L"default", L"docs", L"cpp"},
            timestamp),
        MakeDefaultBookmark(
            L"default-chatgpt",
            L"ChatGPT",
            L"https://chatgpt.com",
            L"vault-ai",
            BookmarkNodeShapeType::Hex,
            BookmarkNodeColorTheme::Mixed,
            BookmarkNodePosition{.x = 1.8f, .y = 0.05f, .z = 0.15f},
            {L"default", L"ai"},
            timestamp),
        MakeDefaultBookmark(
            L"default-claude",
            L"Claude",
            L"https://claude.ai",
            L"vault-ai",
            BookmarkNodeShapeType::Cube,
            BookmarkNodeColorTheme::Yellow,
            BookmarkNodePosition{.x = -1.2f, .y = 0.02f, .z = 0.12f},
            {L"default", L"ai"},
            timestamp),
        MakeDefaultBookmark(
            L"default-gemini",
            L"Gemini",
            L"https://gemini.google.com",
            L"vault-ai",
            BookmarkNodeShapeType::Panel,
            BookmarkNodeColorTheme::Green,
            BookmarkNodePosition{.x = 0.0f, .y = 0.0f, .z = -0.2f},
            {L"default", L"ai"},
            timestamp),
        MakeDefaultBookmark(
            L"default-copilot",
            L"Microsoft Copilot",
            L"https://copilot.microsoft.com",
            L"vault-ai",
            BookmarkNodeShapeType::Hex,
            BookmarkNodeColorTheme::Mixed,
            BookmarkNodePosition{.x = 1.25f, .y = 0.04f, .z = 0.18f},
            {L"default", L"ai"},
            timestamp),
        MakeDefaultBookmark(
            L"default-youtube",
            L"YouTube",
            L"https://www.youtube.com",
            L"vault-media",
            BookmarkNodeShapeType::Hex,
            BookmarkNodeColorTheme::Red,
            BookmarkNodePosition{.x = -1.35f, .y = 0.02f, .z = 0.16f},
            {L"default", L"media", L"video"},
            timestamp),
        MakeDefaultBookmark(
            L"default-twitch",
            L"Twitch",
            L"https://www.twitch.tv",
            L"vault-media",
            BookmarkNodeShapeType::Cube,
            BookmarkNodeColorTheme::Mixed,
            BookmarkNodePosition{.x = 0.0f, .y = 0.0f, .z = -0.2f},
            {L"default", L"media", L"streaming"},
            timestamp),
        MakeDefaultBookmark(
            L"default-spotify",
            L"Spotify",
            L"https://open.spotify.com",
            L"vault-media",
            BookmarkNodeShapeType::Panel,
            BookmarkNodeColorTheme::Green,
            BookmarkNodePosition{.x = 1.25f, .y = 0.04f, .z = 0.18f},
            {L"default", L"media", L"music"},
            timestamp),
    };
}

std::string BookmarkVaultToJson(const BookmarkVault& vault) {
    std::ostringstream output;
    output << "{\n";
    output << "  \"version\": 1,\n";
    output << "  \"id\": \"" << EscapeJsonWideString(vault.id) << "\",\n";
    output << "  \"name\": \"" << EscapeJsonWideString(vault.name) << "\",\n";
    output << "  \"colorTheme\": \"" << ToJsonString(vault.color_theme) << "\",\n";
    output << "  \"createdUtc\": \"" << EscapeJsonString(vault.created_utc) << "\",\n";
    output << "  \"updatedUtc\": \"" << EscapeJsonString(vault.updated_utc) << "\"\n";
    output << "}\n";
    return output.str();
}

std::optional<BookmarkVault> BookmarkVaultFromJson(const JsonValue& value) {
    const JsonValue::Object* object = AsObject(value);
    if (object == nullptr) {
        return std::nullopt;
    }

    const std::optional<int> version = IntField(*object, "version");
    const std::string* id = StringField(*object, "id");
    const std::string* name = StringField(*object, "name");
    const std::string* color = StringField(*object, "colorTheme");
    const std::string* created = StringField(*object, "createdUtc");
    const std::string* updated = StringField(*object, "updatedUtc");
    if (!version || *version != 1 || id == nullptr || name == nullptr || color == nullptr || created == nullptr ||
        updated == nullptr) {
        return std::nullopt;
    }

    const std::optional<BookmarkNodeColorTheme> color_theme = BookmarkNodeColorThemeFromString(*color);
    if (!color_theme) {
        return std::nullopt;
    }

    BookmarkVault vault;
    vault.id = Utf8ToWide(*id);
    vault.name = Utf8ToWide(*name);
    vault.color_theme = *color_theme;
    vault.created_utc = *created;
    vault.updated_utc = *updated;
    return IsValidVault(vault) ? std::optional<BookmarkVault>{std::move(vault)} : std::nullopt;
}

}  // namespace

std::filesystem::path BookmarkStore::DefaultBookmarksPath() {
    return common::AppDataDirectory() / "bookmarks.json";
}

bool BookmarkStore::Initialize(std::filesystem::path bookmarks_path, common::Logger& logger) {
    std::lock_guard<std::mutex> lock(mutex_);
    logger_ = &logger;
    bookmarks_path_ = std::move(bookmarks_path);
    nodes_.clear();
    vaults_.clear();
    defaults_seeded_ = false;
    vaults_seeded_ = false;

    if (!LoadLocked()) {
        if (!RenameCorruptedFileLocked()) {
            logger_->Error("Unable to recover corrupted bookmark file.");
            return false;
        }
        nodes_.clear();
        vaults_.clear();
        defaults_seeded_ = true;
        vaults_seeded_ = true;
        if (!WriteLocked()) {
            logger_->Error("Unable to write clean bookmark file after corruption recovery.");
            return false;
        }
    }

    logger_->Info("Bookmark store ready: " + NarrowForLog(bookmarks_path_));
    return true;
}

std::vector<BookmarkNode> BookmarkStore::LoadBookmarks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return nodes_;
}

std::vector<BookmarkVault> BookmarkStore::LoadVaults() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return vaults_;
}

bool BookmarkStore::SaveBookmarks(std::vector<BookmarkNode> nodes) {
    if (!AreValidNodes(nodes)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (bookmarks_path_.empty() || logger_ == nullptr) {
        return false;
    }
    if (!NodesReferenceKnownVaults(nodes, vaults_)) {
        return false;
    }

    nodes_ = std::move(nodes);
    defaults_seeded_ = true;
    const bool written = WriteLocked();
    if (!written) {
        logger_->Error("Failed to write bookmarks.");
    }
    return written;
}

bool BookmarkStore::SaveVaults(std::vector<BookmarkVault> vaults) {
    if (!AreValidVaults(vaults)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (bookmarks_path_.empty() || logger_ == nullptr) {
        return false;
    }

    std::vector<BookmarkNode> previous_nodes = nodes_;
    std::vector<BookmarkVault> previous_vaults = vaults_;
    ClearUnknownVaultReferences(nodes_, vaults);
    vaults_ = std::move(vaults);
    vaults_seeded_ = true;
    const bool written = WriteLocked();
    if (!written) {
        nodes_ = std::move(previous_nodes);
        vaults_ = std::move(previous_vaults);
        logger_->Error("Failed to write Vaults.");
    }
    return written;
}

bool BookmarkStore::AddBookmark(BookmarkNode node) {
    if (!ValidateBookmarkNode(node).valid) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (bookmarks_path_.empty() || logger_ == nullptr) {
        return false;
    }
    if (node.vault_id && !VaultIdExists(vaults_, *node.vault_id)) {
        return false;
    }

    const auto duplicate = std::find_if(nodes_.begin(), nodes_.end(), [&node](const BookmarkNode& existing) {
        return existing.id == node.id;
    });
    if (duplicate != nodes_.end()) {
        return false;
    }

    nodes_.push_back(std::move(node));
    defaults_seeded_ = true;
    const bool written = WriteLocked();
    if (!written) {
        nodes_.pop_back();
        logger_->Error("Failed to write added bookmark.");
    }
    return written;
}

bool BookmarkStore::UpdateBookmark(BookmarkNode node) {
    if (!ValidateBookmarkNode(node).valid) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (bookmarks_path_.empty() || logger_ == nullptr) {
        return false;
    }
    if (node.vault_id && !VaultIdExists(vaults_, *node.vault_id)) {
        return false;
    }

    auto found = std::find_if(nodes_.begin(), nodes_.end(), [&node](const BookmarkNode& existing) {
        return existing.id == node.id;
    });
    if (found == nodes_.end()) {
        return false;
    }

    BookmarkNode previous = *found;
    *found = std::move(node);
    defaults_seeded_ = true;
    const bool written = WriteLocked();
    if (!written) {
        *found = std::move(previous);
        logger_->Error("Failed to write updated bookmark.");
    }
    return written;
}

bool BookmarkStore::DeleteBookmark(std::wstring_view id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (bookmarks_path_.empty() || logger_ == nullptr || id.empty()) {
        return false;
    }

    auto found = std::find_if(nodes_.begin(), nodes_.end(), [id](const BookmarkNode& node) {
        return node.id == id;
    });
    if (found == nodes_.end()) {
        return false;
    }

    BookmarkNode removed = std::move(*found);
    const auto index = static_cast<std::size_t>(std::distance(nodes_.begin(), found));
    nodes_.erase(found);
    defaults_seeded_ = true;
    const bool written = WriteLocked();
    if (!written) {
        nodes_.insert(nodes_.begin() + static_cast<std::ptrdiff_t>(index), std::move(removed));
        logger_->Error("Failed to write deleted bookmark.");
    }
    return written;
}

bool BookmarkStore::UpdateVault(BookmarkVault vault) {
    if (!IsValidVault(vault)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (bookmarks_path_.empty() || logger_ == nullptr) {
        return false;
    }

    auto found = std::find_if(vaults_.begin(), vaults_.end(), [&vault](const BookmarkVault& existing) {
        return existing.id == vault.id;
    });
    if (found == vaults_.end()) {
        return false;
    }

    BookmarkVault previous = *found;
    *found = std::move(vault);
    vaults_seeded_ = true;
    const bool written = WriteLocked();
    if (!written) {
        *found = std::move(previous);
        logger_->Error("Failed to write updated Vault.");
    }
    return written;
}

bool BookmarkStore::DeleteVault(std::wstring_view id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (bookmarks_path_.empty() || logger_ == nullptr || id.empty()) {
        return false;
    }

    auto found = std::find_if(vaults_.begin(), vaults_.end(), [id](const BookmarkVault& vault) {
        return vault.id == id;
    });
    if (found == vaults_.end()) {
        return false;
    }

    std::vector<BookmarkNode> previous_nodes = nodes_;
    BookmarkVault removed = std::move(*found);
    const auto index = static_cast<std::size_t>(std::distance(vaults_.begin(), found));
    vaults_.erase(found);
    const std::optional<std::wstring> fallback_vault_id = vaults_.empty() ? std::nullopt : std::optional<std::wstring>{vaults_.front().id};
    for (BookmarkNode& node : nodes_) {
        if (node.vault_id && *node.vault_id == id) {
            node.vault_id = fallback_vault_id;
        }
    }
    vaults_seeded_ = true;
    const bool written = WriteLocked();
    if (!written) {
        vaults_.insert(vaults_.begin() + static_cast<std::ptrdiff_t>(index), std::move(removed));
        nodes_ = std::move(previous_nodes);
        logger_->Error("Failed to write deleted Vault.");
    }
    return written;
}

std::optional<BookmarkNode> BookmarkStore::FindBookmarkById(std::wstring_view id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = std::find_if(nodes_.begin(), nodes_.end(), [id](const BookmarkNode& node) {
        return node.id == id;
    });
    if (found == nodes_.end()) {
        return std::nullopt;
    }
    return *found;
}

std::filesystem::path BookmarkStore::path() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bookmarks_path_;
}

bool BookmarkStore::LoadLocked() {
    auto fail = [this](std::string_view reason) {
        if (logger_ != nullptr) {
            logger_->Error(std::string("Bookmark file invalid: ") + std::string(reason));
        }
        return false;
    };

    if (bookmarks_path_.empty()) {
        return fail("empty path");
    }

    std::error_code error;
    if (!std::filesystem::exists(bookmarks_path_, error)) {
        vaults_ = CreateDefaultVaults();
        nodes_ = CreateDefaultBookmarks();
        defaults_seeded_ = true;
        vaults_seeded_ = true;
        return WriteLocked();
    }

    std::string json_text;
    {
        std::ifstream input(bookmarks_path_, std::ios::binary);
        if (!input.is_open()) {
            return fail("could not open file");
        }

        std::ostringstream buffer;
        buffer << input.rdbuf();
        json_text = buffer.str();
    }

    JsonParser parser(json_text);
    const auto root_value = parser.Parse();
    if (!root_value) {
        if (logger_ != nullptr) {
            logger_->Error("Bookmark parse stopped near byte " + std::to_string(parser.position()) + ".");
        }
        return fail("JSON parse failed");
    }

    const JsonValue::Object* root = AsObject(*root_value);
    if (root == nullptr) {
        return fail("root is not an object");
    }

    const std::optional<int> version = IntField(*root, "version");
    if (!version || *version != 1) {
        return fail("version field is missing or unsupported");
    }
    const std::optional<bool> defaults_seeded = BoolField(*root, "defaultsSeeded");
    const std::optional<bool> vaults_seeded = BoolField(*root, "vaultsSeeded");

    const auto nodes_found = root->find("nodes");
    if (nodes_found == root->end()) {
        return fail("nodes field is missing");
    }

    const JsonValue::Array* nodes = AsArray(nodes_found->second);
    if (nodes == nullptr) {
        return fail("nodes field is not an array");
    }

    std::vector<BookmarkNode> loaded;
    loaded.reserve(nodes->size());
    for (const JsonValue& item : *nodes) {
        if (AsObject(item) == nullptr) {
            return fail("node entry is not an object");
        }

        std::wstring node_error;
        auto node = BookmarkNodeFromJson(JsonValueToString(item), &node_error);
        if (!node) {
            return fail("node entry is invalid");
        }
        loaded.push_back(std::move(*node));
    }

    if (HasDuplicateIds(loaded)) {
        return fail("node ids are not unique");
    }

    std::vector<BookmarkVault> loaded_vaults;
    const auto vaults_found = root->find("vaults");
    if (vaults_found != root->end()) {
        const JsonValue::Array* vaults = AsArray(vaults_found->second);
        if (vaults == nullptr) {
            return fail("vaults field is not an array");
        }

        loaded_vaults.reserve(vaults->size());
        for (const JsonValue& item : *vaults) {
            auto vault = BookmarkVaultFromJson(item);
            if (!vault) {
                return fail("Vault entry is invalid");
            }
            loaded_vaults.push_back(std::move(*vault));
        }
        if (!AreValidVaults(loaded_vaults)) {
            return fail("Vault ids are not unique or valid");
        }
    }

    bool should_write = false;
    vaults_seeded_ = vaults_seeded.value_or(false);
    if (loaded_vaults.empty() && !vaults_seeded_) {
        loaded_vaults = CreateDefaultVaults();
        vaults_seeded_ = true;
        should_write = true;
    }

    nodes_ = std::move(loaded);
    vaults_ = std::move(loaded_vaults);
    defaults_seeded_ = defaults_seeded.value_or(false);
    if (nodes_.empty() && !defaults_seeded_) {
        nodes_ = CreateDefaultBookmarks();
        defaults_seeded_ = true;
        should_write = true;
    }
    const std::vector<BookmarkNode> before_vault_cleanup = nodes_;
    ClearUnknownVaultReferences(nodes_, vaults_);
    for (std::size_t index = 0; index < nodes_.size(); ++index) {
        if (nodes_[index].vault_id != before_vault_cleanup[index].vault_id) {
            should_write = true;
            break;
        }
    }
    if (!nodes_.empty()) {
        defaults_seeded_ = true;
    }
    if (!vaults_.empty()) {
        vaults_seeded_ = true;
    }
    return should_write ? WriteLocked() : true;
}

bool BookmarkStore::WriteLocked() {
    if (!AreValidNodes(nodes_) || !AreValidVaults(vaults_) || !NodesReferenceKnownVaults(nodes_, vaults_)) {
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(bookmarks_path_.parent_path(), error);
    if (error) {
        return false;
    }

    const std::filesystem::path temp_path = bookmarks_path_.string() + ".tmp";
    {
        std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            return false;
        }

        output << "{\n";
        output << "  \"version\": 1,\n";
        output << "  \"defaultsSeeded\": " << (defaults_seeded_ ? "true" : "false") << ",\n";
        output << "  \"vaultsSeeded\": " << (vaults_seeded_ ? "true" : "false") << ",\n";
        output << "  \"vaults\": [\n";
        for (std::size_t index = 0; index < vaults_.size(); ++index) {
            output << IndentJson(BookmarkVaultToJson(vaults_[index]), "    ");
            output << (index + 1 == vaults_.size() ? "" : ",") << "\n";
        }
        output << "  ],\n";
        output << "  \"nodes\": [\n";
        for (std::size_t index = 0; index < nodes_.size(); ++index) {
            output << IndentJson(BookmarkNodeToJson(nodes_[index]), "    ");
            output << (index + 1 == nodes_.size() ? "" : ",") << "\n";
        }
        output << "  ]\n";
        output << "}\n";
        output.flush();
        if (!output.good()) {
            return false;
        }
    }

    if (!common::ReplaceFile(temp_path, bookmarks_path_)) {
        std::filesystem::remove(temp_path, error);
        return false;
    }

    return true;
}

bool BookmarkStore::RenameCorruptedFileLocked() {
    std::error_code error;
    if (!std::filesystem::exists(bookmarks_path_, error)) {
        return true;
    }

    const std::filesystem::path corrupted_path =
        bookmarks_path_.parent_path() /
        (bookmarks_path_.filename().wstring() + L".corrupt." + Utf8ToWide(CurrentFileTimestamp()) + L".bak");

    if (!common::ReplaceFile(bookmarks_path_, corrupted_path)) {
        return false;
    }

    if (logger_ != nullptr) {
        logger_->Error("Warning: corrupted bookmark file renamed to: " + NarrowForLog(corrupted_path));
    }
    return true;
}

}  // namespace cyberdeck::deck
