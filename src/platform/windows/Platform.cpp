#include "common/Platform.h"

#include <windows.h>

#ifdef ReplaceFile
#undef ReplaceFile
#endif

#include <climits>
#include <optional>

namespace cyberdeck::common {
namespace {

std::optional<std::wstring> EnvironmentVariable(const wchar_t* name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0) {
        return std::nullopt;
    }

    std::wstring value(required, L'\0');
    const DWORD copied = GetEnvironmentVariableW(name, value.data(), required);
    if (copied == 0) {
        return std::nullopt;
    }

    value.resize(copied);
    return value;
}

}  // namespace

std::filesystem::path AppDataDirectory() {
    if (const auto override_path = EnvironmentVariable(L"CYBERDECK_APPDATA_DIR")) {
        if (!override_path->empty()) {
            return *override_path;
        }
    }

    std::filesystem::path root;
    if (const auto app_data = EnvironmentVariable(L"APPDATA")) {
        root = *app_data;
    }

    if (root.empty()) {
        root = std::filesystem::current_path() / "dev" / "appdata";
    }

    return root / "CyberDeckBrowser";
}

bool ReplaceFile(const std::filesystem::path& source, const std::filesystem::path& destination) {
    return MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

std::uint32_t CurrentProcessId() {
    return static_cast<std::uint32_t>(GetCurrentProcessId());
}

std::string WideToUtf8(std::wstring_view value) {
    if (value.empty() || value.size() > static_cast<std::size_t>(INT_MAX)) {
        return {};
    }

    const int required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 0) {
        return {};
    }

    std::string output(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        output.data(),
        required,
        nullptr,
        nullptr);
    return output;
}

std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty() || value.size() > static_cast<std::size_t>(INT_MAX)) {
        return {};
    }

    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (required <= 0) {
        return {};
    }

    std::wstring output(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        output.data(),
        required);
    return output;
}

}  // namespace cyberdeck::common
