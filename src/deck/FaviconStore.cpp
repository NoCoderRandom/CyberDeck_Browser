#include "deck/FaviconStore.h"

#include "common/Platform.h"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>

namespace cyberdeck::deck {
namespace {

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

std::string WideToAscii(std::wstring_view value) {
    std::string output;
    output.reserve(value.size());
    for (wchar_t ch : value) {
        output.push_back(ch >= 32 && ch <= 126 ? static_cast<char>(ch) : '?');
    }
    return output;
}

std::string ExtractHost(std::wstring_view url) {
    std::wstring trimmed = Trim(url);
    if (StartsWith(trimmed, L"https://")) {
        trimmed = trimmed.substr(8);
    } else if (StartsWith(trimmed, L"http://")) {
        trimmed = trimmed.substr(7);
    }

    const std::size_t end = trimmed.find_first_of(L"/?#:");
    std::wstring host = trimmed.substr(0, end == std::wstring::npos ? trimmed.size() : end);
    std::transform(host.begin(), host.end(), host.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return WideToAscii(host.empty() ? L"node" : host);
}

std::string SafeFileStem(std::string host) {
    std::string stem;
    stem.reserve(host.size());
    for (char ch : host) {
        const unsigned char value = static_cast<unsigned char>(ch);
        if (std::isalnum(value)) {
            stem.push_back(static_cast<char>(std::tolower(value)));
        } else if (ch == '.' || ch == '-') {
            stem.push_back('-');
        }
    }
    while (!stem.empty() && stem.front() == '-') {
        stem.erase(stem.begin());
    }
    while (!stem.empty() && stem.back() == '-') {
        stem.pop_back();
    }
    return stem.empty() ? "node" : stem;
}

char InitialFor(std::wstring_view title, std::string_view host) {
    const std::wstring clean_title = Trim(title);
    if (!clean_title.empty() && clean_title.front() <= 0x7F && std::isalnum(static_cast<unsigned char>(clean_title.front()))) {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(clean_title.front())));
    }
    for (char ch : host) {
        if (std::isalnum(static_cast<unsigned char>(ch))) {
            return static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
    }
    return 'N';
}

std::string EscapeXml(std::string_view value) {
    std::string output;
    for (char ch : value) {
        switch (ch) {
            case '&':
                output += "&amp;";
                break;
            case '<':
                output += "&lt;";
                break;
            case '>':
                output += "&gt;";
                break;
            case '"':
                output += "&quot;";
                break;
            default:
                output.push_back(ch);
                break;
        }
    }
    return output;
}

std::string PlaceholderSvg(std::string_view host, char initial) {
    std::ostringstream output;
    output << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 64 64\">\n";
    output << "<rect width=\"64\" height=\"64\" fill=\"#000000\"/>\n";
    output << "<rect x=\"4\" y=\"4\" width=\"56\" height=\"56\" rx=\"8\" fill=\"#031008\" stroke=\"#00ff00\" stroke-width=\"3\"/>\n";
    output << "<circle cx=\"32\" cy=\"32\" r=\"22\" fill=\"#001b0c\" stroke=\"#ffff00\" stroke-width=\"2\" opacity=\"0.85\"/>\n";
    output << "<text x=\"32\" y=\"39\" text-anchor=\"middle\" font-family=\"Consolas, monospace\" font-size=\"26\" font-weight=\"700\" fill=\"#00ff00\">";
    output << initial << "</text>\n";
    output << "<text x=\"32\" y=\"56\" text-anchor=\"middle\" font-family=\"Consolas, monospace\" font-size=\"6\" fill=\"#ffff00\">";
    output << EscapeXml(host.substr(0, 16)) << "</text>\n";
    output << "</svg>\n";
    return output.str();
}

}  // namespace

std::filesystem::path FaviconStore::DefaultFaviconsDirectory() {
    return common::AppDataDirectory() / "favicons";
}

std::optional<std::wstring> FaviconStore::EnsurePlaceholderFavicon(
    const std::filesystem::path& favicons_directory,
    std::wstring_view url,
    std::wstring_view title) {
    const std::string host = ExtractHost(url);
    const std::string stem = SafeFileStem(host);
    const std::filesystem::path path = favicons_directory / (std::wstring(stem.begin(), stem.end()) + L".svg");

    std::error_code error;
    std::filesystem::create_directories(favicons_directory, error);
    if (error) {
        return std::nullopt;
    }

    if (!std::filesystem::exists(path, error)) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            return std::nullopt;
        }
        output << PlaceholderSvg(host, InitialFor(title, host));
        output.flush();
        if (!output.good()) {
            return std::nullopt;
        }
    }

    return path.wstring();
}

}  // namespace cyberdeck::deck
