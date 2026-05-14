#include "browser/BrowserHost.h"

#include "common/Platform.h"
#include "browser/UrlNavigation.h"
#include "ui/Theme.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <cwchar>
#include <string>
#include <utility>

#if defined(CYBERDECK_HAS_CEF)
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "include/cef_download_handler.h"
#include "include/cef_permission_handler.h"
#include "include/cef_request_handler.h"
#include "include/cef_version.h"
#include <shellapi.h>
#include <shlobj_core.h>
#endif

namespace cyberdeck::browser {
namespace {

constexpr wchar_t kDefaultHomepage[] = L"https://www.example.com";
#if defined(CYBERDECK_HAS_CEF)
constexpr std::wstring_view kErrorPageUrl = L"about:blank#cyberdeck-error";
#endif

[[maybe_unused]] int RectWidth(const RECT& bounds) {
    return std::max(0L, bounds.right - bounds.left);
}

[[maybe_unused]] int RectHeight(const RECT& bounds) {
    return std::max(0L, bounds.bottom - bounds.top);
}

std::string NarrowForLog(std::wstring_view value) {
    std::string result;
    result.reserve(value.size());
    for (wchar_t character : value) {
        result.push_back(character >= 32 && character <= 126 ? static_cast<char>(character) : '?');
    }
    return result;
}

[[maybe_unused]] std::wstring WideFromAscii(std::string_view value) {
    std::wstring output;
    output.reserve(value.size());
    for (char ch : value) {
        output.push_back(static_cast<wchar_t>(ch));
    }
    return output;
}

[[maybe_unused]] bool StartsWith(std::wstring_view value, std::wstring_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

[[maybe_unused]] bool Contains(std::wstring_view value, std::wstring_view needle) {
    return value.find(needle) != std::wstring_view::npos;
}

[[maybe_unused]] std::wstring ToAsciiLower(std::wstring_view value) {
    std::wstring lowered(value);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](wchar_t ch) {
        return ch >= L'A' && ch <= L'Z' ? static_cast<wchar_t>(ch + (L'a' - L'A')) : ch;
    });
    return lowered;
}

#if defined(CYBERDECK_HAS_CEF)
constexpr wchar_t kProtocolDialogClassName[] = L"CyberDeckProtocolWarningDialog";
constexpr wchar_t kRemoteDebuggingHostEnv[] = L"CYBERDECK_CEF_REMOTE_DEBUGGING_HOST";
constexpr wchar_t kRemoteDebuggingPortEnv[] = L"CYBERDECK_CEF_REMOTE_DEBUGGING_PORT";
constexpr wchar_t kRemoteDebuggingAllowAllHostsEnv[] = L"CYBERDECK_CEF_ALLOW_NONLOCAL_REMOTE_DEBUGGING";
constexpr std::string kDefaultRemoteDebugAddress = "127.0.0.1";
constexpr std::wstring_view kDefaultRemoteDebugAddressWide = L"127.0.0.1";

std::string g_remote_debugging_address = kDefaultRemoteDebugAddress;
bool g_remote_debugging_enabled = false;

bool IsMalformedAdClickPopup(std::wstring_view url) {
    const std::wstring lower = ToAsciiLower(url);
    const bool known_ad_click =
        StartsWith(lower, L"https://www.googleadservices.com/pagead/aclk") ||
        StartsWith(lower, L"https://googleads.g.doubleclick.net/pagead/aclk");
    if (!known_ad_click) {
        return false;
    }

    return Contains(lower, L"%5b") || Contains(lower, L"[") ||
           Contains(lower, L"label=video_click_to_advertiser_site");
}

bool ShouldSkipTerminalModeForUrl(std::wstring_view url) {
    const std::wstring lower = ToAsciiLower(url);
    return Contains(lower, L"://www.youtube.com/") ||
           Contains(lower, L"://youtube.com/") ||
           Contains(lower, L"://youtu.be/") ||
           Contains(lower, L"://www.twitch.tv/") ||
           Contains(lower, L"://twitch.tv/") ||
           Contains(lower, L"://vimeo.com/");
}

struct ProtocolDialogData {
    std::wstring url;
    std::wstring scheme;
    std::wstring reason;
    bool allowed = false;
    HFONT font = nullptr;
    HBRUSH background = nullptr;
};

void DrawProtocolButton(const DRAWITEMSTRUCT& draw_item) {
    const bool allow_button = draw_item.CtlID == IDYES;
    const bool selected = (draw_item.itemState & ODS_SELECTED) != 0;
    const COLORREF accent = allow_button ? cyberdeck::ui::Theme::yellow : cyberdeck::ui::Theme::red;
    const COLORREF fill = selected ? cyberdeck::ui::Theme::dark_panel : cyberdeck::ui::Theme::black;

    HBRUSH brush = CreateSolidBrush(fill);
    FillRect(draw_item.hDC, &draw_item.rcItem, brush);
    DeleteObject(brush);

    HPEN pen = CreatePen(PS_SOLID, 1, accent);
    HGDIOBJ previous_pen = SelectObject(draw_item.hDC, pen);
    HGDIOBJ previous_brush = SelectObject(draw_item.hDC, GetStockObject(NULL_BRUSH));
    Rectangle(
        draw_item.hDC,
        draw_item.rcItem.left,
        draw_item.rcItem.top,
        draw_item.rcItem.right,
        draw_item.rcItem.bottom);
    SelectObject(draw_item.hDC, previous_brush);
    SelectObject(draw_item.hDC, previous_pen);
    DeleteObject(pen);

    SetBkMode(draw_item.hDC, TRANSPARENT);
    SetTextColor(draw_item.hDC, accent);
    const wchar_t* text = allow_button ? L"LAUNCH" : L"CANCEL";
    RECT text_rect = draw_item.rcItem;
    DrawTextW(draw_item.hDC, text, -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

LRESULT CALLBACK ProtocolDialogProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* data = reinterpret_cast<ProtocolDialogData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (message) {
        case WM_NCCREATE: {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
            data = static_cast<ProtocolDialogData*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));
            return TRUE;
        }
        case WM_CREATE: {
            if (data == nullptr) {
                return -1;
            }
            data->font = cyberdeck::ui::CreateMonospaceFont(17);
            data->background = CreateSolidBrush(cyberdeck::ui::Theme::black);

            auto create_static = [hwnd, data](int id, const std::wstring& text, int x, int y, int width, int height) {
                HWND control = CreateWindowExW(
                    0,
                    L"STATIC",
                    text.c_str(),
                    WS_CHILD | WS_VISIBLE | SS_LEFT,
                    x,
                    y,
                    width,
                    height,
                    hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                    nullptr,
                    nullptr);
                if (control != nullptr && data->font != nullptr) {
                    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(data->font), TRUE);
                }
            };

            create_static(10, L"EXTERNAL PROTOCOL REQUEST", 18, 16, 444, 24);
            create_static(11, data->reason, 18, 52, 444, 48);
            create_static(12, L"Scheme: " + data->scheme + L"\nURL: " + data->url, 18, 106, 444, 82);

            HWND cancel = CreateWindowExW(
                0,
                L"BUTTON",
                L"CANCEL",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                246,
                204,
                100,
                32,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDNO)),
                nullptr,
                nullptr);
            HWND launch = CreateWindowExW(
                0,
                L"BUTTON",
                L"LAUNCH",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                362,
                204,
                100,
                32,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDYES)),
                nullptr,
                nullptr);
            if (data->font != nullptr) {
                SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(data->font), TRUE);
                SendMessageW(launch, WM_SETFONT, reinterpret_cast<WPARAM>(data->font), TRUE);
            }
            SetFocus(cancel);
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            const int control_id = GetDlgCtrlID(reinterpret_cast<HWND>(l_param));
            HDC dc = reinterpret_cast<HDC>(w_param);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, control_id == 10 ? cyberdeck::ui::Theme::green : cyberdeck::ui::Theme::yellow);
            return reinterpret_cast<LRESULT>(data != nullptr ? data->background : GetStockObject(BLACK_BRUSH));
        }
        case WM_DRAWITEM:
            DrawProtocolButton(*reinterpret_cast<DRAWITEMSTRUCT*>(l_param));
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(w_param) == IDYES || LOWORD(w_param) == IDNO) {
                if (data != nullptr) {
                    data->allowed = LOWORD(w_param) == IDYES;
                }
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            if (data != nullptr) {
                data->allowed = false;
            }
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (data != nullptr) {
                if (data->font != nullptr) {
                    DeleteObject(data->font);
                    data->font = nullptr;
                }
                if (data->background != nullptr) {
                    DeleteObject(data->background);
                    data->background = nullptr;
                }
            }
            return 0;
        default:
            break;
    }

    return DefWindowProcW(hwnd, message, w_param, l_param);
}

bool ConfirmExternalProtocolLaunch(HWND owner, const std::wstring& url, const ProtocolDecision& decision) {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = ProtocolDialogProc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = kProtocolDialogClassName;
    RegisterClassExW(&window_class);

    ProtocolDialogData data{
        .url = url,
        .scheme = decision.scheme,
        .reason = decision.reason,
    };

    RECT owner_rect{0, 0, 0, 0};
    if (owner != nullptr) {
        GetWindowRect(owner, &owner_rect);
    }

    constexpr int width = 500;
    constexpr int height = 280;
    const int x = owner != nullptr ? owner_rect.left + ((owner_rect.right - owner_rect.left) - width) / 2 : CW_USEDEFAULT;
    const int y = owner != nullptr ? owner_rect.top + ((owner_rect.bottom - owner_rect.top) - height) / 2 : CW_USEDEFAULT;

    HWND dialog = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kProtocolDialogClassName,
        L"CyberDeck Protocol Warning",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x,
        y,
        width,
        height,
        owner,
        nullptr,
        instance,
        &data);

    if (dialog == nullptr) {
        return MessageBoxW(
                   owner,
                   (decision.reason + L"\n\n" + url).c_str(),
                   L"CyberDeck Protocol Warning",
                   MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) == IDYES;
    }

    if (owner != nullptr) {
        EnableWindow(owner, FALSE);
    }
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);

    MSG message{};
    while (IsWindow(dialog) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    if (owner != nullptr) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    return data.allowed;
}

std::wstring DefaultDownloadsDirectory() {
    PWSTR known_path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, KF_FLAG_DEFAULT, nullptr, &known_path)) &&
        known_path != nullptr) {
        std::wstring result = known_path;
        CoTaskMemFree(known_path);
        return result;
    }

    DWORD required = GetEnvironmentVariableW(L"USERPROFILE", nullptr, 0);
    if (required > 0) {
        std::wstring user_profile(required, L'\0');
        const DWORD copied = GetEnvironmentVariableW(L"USERPROFILE", user_profile.data(), required);
        if (copied > 0) {
            user_profile.resize(copied);
            return (std::filesystem::path(user_profile) / L"Downloads").wstring();
        }
    }

    return (std::filesystem::current_path() / L"downloads").wstring();
}

std::optional<int> EnvironmentPort(const wchar_t* name) {
    wchar_t buffer[32]{};
    constexpr DWORD buffer_count = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
    const DWORD copied = GetEnvironmentVariableW(name, buffer, buffer_count);
    if (copied == 0 || copied >= buffer_count) {
        return std::nullopt;
    }

    wchar_t* end = nullptr;
    const long value = std::wcstol(buffer, &end, 10);
    if (end == buffer || *end != L'\0' || value <= 0 || value > 65535) {
        return std::nullopt;
    }

    return static_cast<int>(value);
}

std::optional<std::wstring> EnvironmentValue(const wchar_t* name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0) {
        return std::nullopt;
    }

    std::wstring buffer(required, L'\0');
    const DWORD copied = GetEnvironmentVariableW(name, buffer.data(), required);
    if (copied == 0 || copied >= buffer.size()) {
        return std::nullopt;
    }
    buffer.resize(copied);
    return buffer;
}

std::wstring_view TrimAsciiWhitespace(std::wstring_view value) {
    size_t start = 0;
    while (start < value.size() && (value[start] <= L' ')) {
        ++start;
    }

    size_t end = value.size();
    while (end > start && (value[end - 1] <= L' ')) {
        --end;
    }

    return value.substr(start, end - start);
}

bool EnvironmentBoolean(const wchar_t* name) {
    const auto raw = EnvironmentValue(name);
    if (!raw) {
        return false;
    }

    const std::wstring value = ToAsciiLower(TrimAsciiWhitespace(*raw));
    return value == L"1" || value == L"true" || value == L"yes" || value == L"on" || value == L"enable" ||
           value == L"enabled";
}

bool IsLoopbackHost(std::wstring_view host) {
    const std::wstring normalized = ToAsciiLower(host);
    return normalized == L"127.0.0.1" || normalized == L"localhost" || normalized == L"::1";
}
#endif

[[maybe_unused]] std::wstring SafeDownloadFileName(std::wstring suggested_name) {
    suggested_name = std::filesystem::path(suggested_name).filename().wstring();
    if (suggested_name.empty() || suggested_name == L"." || suggested_name == L"..") {
        return L"download.bin";
    }

    constexpr std::wstring_view invalid = L"<>:\"/\\|?*";
    for (wchar_t& ch : suggested_name) {
        if (ch < 32 || invalid.find(ch) != std::wstring_view::npos) {
            ch = L'_';
        }
    }

    return suggested_name.empty() ? L"download.bin" : suggested_name;
}

std::wstring DownloadMessage(const DownloadStatus& status) {
    if (status.canceled) {
        return L"DOWNLOAD CANCELED: " + status.file_name;
    }
    if (status.complete) {
        return L"DOWNLOAD COMPLETE: " + status.file_name;
    }
    if (status.percent_complete >= 0) {
        return L"DOWNLOADING " + status.file_name + L" [" + std::to_wstring(status.percent_complete) + L"%]";
    }
    return L"DOWNLOADING " + status.file_name;
}

std::wstring PermissionMessage(const PermissionStatus& status) {
    return std::wstring(status.allowed ? L"PERMISSION ALLOWED: " : L"PERMISSION DENIED: ") +
           status.permission + L" [" + status.origin + L"]";
}

#if defined(CYBERDECK_HAS_CEF)
std::wstring HtmlEscape(std::wstring_view value) {
    std::wstring escaped;
    escaped.reserve(value.size());
    for (wchar_t ch : value) {
        switch (ch) {
            case L'&':
                escaped += L"&amp;";
                break;
            case L'<':
                escaped += L"&lt;";
                break;
            case L'>':
                escaped += L"&gt;";
                break;
            case L'"':
                escaped += L"&quot;";
                break;
            case L'\'':
                escaped += L"&#39;";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }
    return escaped;
}

std::wstring JavaScriptStringEscape(std::wstring_view value) {
    std::wstring escaped;
    escaped.reserve(value.size() + 8);
    for (wchar_t ch : value) {
        switch (ch) {
            case L'\\':
                escaped += L"\\\\";
                break;
            case L'\'':
                escaped += L"\\'";
                break;
            case L'"':
                escaped += L"\\\"";
                break;
            case L'\n':
                escaped += L"\\n";
                break;
            case L'\r':
                escaped += L"\\r";
                break;
            case L'<':
                escaped += L"\\x3C";
                break;
            case L'>':
                escaped += L"\\x3E";
                break;
            case L'&':
                escaped += L"\\x26";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }
    return escaped;
}

std::wstring ErrorCodeName(int error_code) {
    switch (error_code) {
        case -2:
            return L"ERR_FAILED";
        case -3:
            return L"ERR_ABORTED";
        case -6:
            return L"ERR_FILE_NOT_FOUND";
        case -7:
            return L"ERR_TIMED_OUT";
        case -10:
            return L"ERR_ACCESS_DENIED";
        case -21:
            return L"ERR_NETWORK_CHANGED";
        case -101:
            return L"ERR_CONNECTION_RESET";
        case -102:
            return L"ERR_CONNECTION_REFUSED";
        case -105:
            return L"ERR_NAME_NOT_RESOLVED";
        case -106:
            return L"ERR_INTERNET_DISCONNECTED";
        case -107:
            return L"ERR_SSL_PROTOCOL_ERROR";
        case -108:
            return L"ERR_ADDRESS_INVALID";
        case -109:
            return L"ERR_ADDRESS_UNREACHABLE";
        case -118:
            return L"ERR_CONNECTION_TIMED_OUT";
        case -137:
            return L"ERR_NAME_RESOLUTION_FAILED";
        case -200:
            return L"ERR_CERT_COMMON_NAME_INVALID";
        case -201:
            return L"ERR_CERT_DATE_INVALID";
        case -202:
            return L"ERR_CERT_AUTHORITY_INVALID";
        case -310:
            return L"ERR_TOO_MANY_REDIRECTS";
        case -324:
            return L"ERR_EMPTY_RESPONSE";
        default:
            return L"CEF_ERROR_" + std::to_wstring(error_code);
    }
}

std::wstring FriendlyErrorExplanation(int error_code) {
    switch (error_code) {
        case -105:
        case -137:
            return L"CyberDeck could not resolve that host name. Check the address or try again later.";
        case -106:
            return L"The network link appears to be offline.";
        case -102:
        case -118:
            return L"The remote server did not answer the connection request.";
        case -108:
            return L"The page tried to open an invalid network address. CyberDeck will keep popup navigation inside the current browser tab.";
        case -200:
        case -201:
        case -202:
            return L"The site certificate could not be trusted.";
        case -10:
            return L"Access was denied before the page could load.";
        default:
            return L"The navigation failed before a usable page was received.";
    }
}

struct ErrorPageState {
    std::wstring failed_url;
    int error_code = 0;
    std::wstring error_name;
    std::wstring explanation;
};

std::wstring BuildErrorPageHtml(const ErrorPageState& error) {
    const std::wstring failed_url_html = HtmlEscape(error.failed_url);
    const std::wstring error_name_html = HtmlEscape(error.error_name);
    const std::wstring explanation_html = HtmlEscape(error.explanation);
    const std::wstring retry_url_js = JavaScriptStringEscape(error.failed_url);

    return LR"HTML(<!doctype html>
<html>
<head>
<meta charset="utf-8">
<title>CyberDeck Load Failure</title>
<style>
:root { color-scheme: dark; }
html, body { margin: 0; min-height: 100%; background: #000000; color: #00ff00; font-family: "Cascadia Mono", Consolas, "Courier New", monospace; }
body { display: grid; place-items: center; }
main { width: min(860px, calc(100vw - 48px)); border: 1px solid #00ff00; box-shadow: 0 0 24px rgba(0,255,0,.25); padding: 28px; background: #030a06; }
h1 { margin: 0 0 16px; color: #00ff00; font-size: 24px; letter-spacing: 0; }
.boot { white-space: pre-wrap; color: #00ff00; line-height: 1.25; margin-bottom: 18px; }
.code { color: #ff0000; font-weight: 700; }
.url { color: #ffff00; overflow-wrap: anywhere; }
.explain { color: #ffff00; margin: 16px 0 24px; }
.controls { display: flex; flex-wrap: wrap; gap: 10px; }
button { background: #000000; color: #00ff00; border: 1px solid #00ff00; font: inherit; padding: 9px 14px; cursor: pointer; }
button:hover, button:focus { color: #ffff00; border-color: #ffff00; outline: none; }
button.danger { color: #ff0000; border-color: #ff0000; }
</style>
</head>
<body>
<main>
<h1>CYBERDECK LINK FAILURE</h1>
<div class="boot">[BOOT] ROUTE TRACE FAILED
[NET]  SIGNAL LOST
[USER] MANUAL RECOVERY AVAILABLE</div>
<p>Failed URL: <span class="url">)HTML" + failed_url_html + LR"HTML(</span></p>
<p>Error: <span class="code">)HTML" + error_name_html + L" (" + std::to_wstring(error.error_code) + LR"HTML()</span></p>
<p class="explain">)HTML" + explanation_html + LR"HTML(</p>
<div class="controls">
<button onclick="window.location.href=')HTML" + retry_url_js + LR"HTML('">Retry</button>
<button onclick="history.back()">Back</button>
<button onclick="copyFailedUrl()">Copy URL</button>
</div>
</main>
<script>
function copyFailedUrl() {
  const value = ')HTML" + retry_url_js + LR"HTML(';
  if (navigator.clipboard && navigator.clipboard.writeText) {
    navigator.clipboard.writeText(value);
    return;
  }
  const text = document.createElement('textarea');
  text.value = value;
  document.body.appendChild(text);
  text.select();
  try { document.execCommand('copy'); } finally { text.remove(); }
}
</script>
</body>
</html>)HTML";
}

std::wstring BuildErrorPageInjectionScript(const ErrorPageState& error) {
    const std::wstring html = JavaScriptStringEscape(BuildErrorPageHtml(error));
    return L"document.open();document.write('" + html + L"');document.close();";
}

std::wstring TerminalModeInjectionScript() {
    return LR"JS((function(){
  try {
    var id = 'cyberdeck-terminal-mode-style';
    var existing = document.getElementById(id);
    if (existing) { existing.remove(); }
    var style = document.createElement('style');
    style.id = id;
    style.textContent = `
      html, body {
        background: #000000 !important;
        color: #00ff00 !important;
        font-family: "Cascadia Mono", Consolas, "Courier New", monospace !important;
        text-shadow: 0 0 4px rgba(0, 255, 0, 0.45) !important;
      }
      body * {
        color: #00ff00 !important;
        border-color: #008000 !important;
        font-family: "Cascadia Mono", Consolas, "Courier New", monospace !important;
      }
      a, a *, [role="link"], [role="link"] * {
        color: #ffff00 !important;
        text-shadow: 0 0 4px rgba(255, 255, 0, 0.4) !important;
      }
      strong, b, em, mark, h1, h2, h3, h4, h5, h6 {
        color: #ff0000 !important;
        text-shadow: 0 0 5px rgba(255, 0, 0, 0.45) !important;
      }
      input, textarea, select, button {
        background: #000000 !important;
        color: #00ff00 !important;
        border: 1px solid #008000 !important;
        box-shadow: 0 0 6px rgba(0, 255, 0, 0.2) !important;
      }
      table, section, article, aside, nav, header, footer, main, div {
        border-color: #008000 !important;
      }
      img, video, canvas, svg {
        filter: brightness(0.78) contrast(1.1) saturate(0.75) !important;
      }
      ::selection {
        background: #ffff00 !important;
        color: #000000 !important;
      }
    `;
    (document.head || document.documentElement).appendChild(style);
    document.documentElement.dataset.cyberdeckTerminalMode = 'enabled';
  } catch (error) {
    console.warn('CyberDeck Terminal Mode injection failed', error);
  }
})();)JS";
}

std::wstring TerminalModeRemovalScript() {
    return LR"JS((function(){
  try {
    var existing = document.getElementById('cyberdeck-terminal-mode-style');
    if (existing) { existing.remove(); }
    if (document.documentElement && document.documentElement.dataset) {
      delete document.documentElement.dataset.cyberdeckTerminalMode;
    }
  } catch (error) {
    console.warn('CyberDeck Terminal Mode removal failed', error);
  }
})();)JS";
}

void AppendPermissionName(std::wstring& output, std::wstring_view name) {
    if (!output.empty()) {
        output += L", ";
    }
    output += name;
}

std::wstring MediaPermissionNames(uint32_t requested_permissions) {
    std::wstring names;
    if ((requested_permissions & CEF_MEDIA_PERMISSION_DEVICE_AUDIO_CAPTURE) != 0) {
        AppendPermissionName(names, L"microphone");
    }
    if ((requested_permissions & CEF_MEDIA_PERMISSION_DEVICE_VIDEO_CAPTURE) != 0) {
        AppendPermissionName(names, L"camera");
    }
    if ((requested_permissions & CEF_MEDIA_PERMISSION_DESKTOP_AUDIO_CAPTURE) != 0) {
        AppendPermissionName(names, L"desktop audio capture");
    }
    if ((requested_permissions & CEF_MEDIA_PERMISSION_DESKTOP_VIDEO_CAPTURE) != 0) {
        AppendPermissionName(names, L"desktop video capture");
    }
    return names.empty() ? L"media" : names;
}

std::wstring PromptPermissionNames(uint32_t requested_permissions) {
    std::wstring names;
    if ((requested_permissions & CEF_PERMISSION_TYPE_CAMERA_PAN_TILT_ZOOM) != 0) {
        AppendPermissionName(names, L"camera pan/tilt/zoom");
    }
    if ((requested_permissions & CEF_PERMISSION_TYPE_CAMERA_STREAM) != 0) {
        AppendPermissionName(names, L"camera");
    }
    if ((requested_permissions & CEF_PERMISSION_TYPE_GEOLOCATION) != 0) {
        AppendPermissionName(names, L"geolocation");
    }
    if ((requested_permissions & CEF_PERMISSION_TYPE_MIC_STREAM) != 0) {
        AppendPermissionName(names, L"microphone");
    }
    if ((requested_permissions & CEF_PERMISSION_TYPE_MIDI_SYSEX) != 0) {
        AppendPermissionName(names, L"MIDI SysEx");
    }
    if ((requested_permissions & CEF_PERMISSION_TYPE_NOTIFICATIONS) != 0) {
        AppendPermissionName(names, L"notifications");
    }
    if ((requested_permissions & CEF_PERMISSION_TYPE_CLIPBOARD) != 0) {
        AppendPermissionName(names, L"clipboard");
    }
    if ((requested_permissions & CEF_PERMISSION_TYPE_FILE_SYSTEM_ACCESS) != 0) {
        AppendPermissionName(names, L"file system access");
    }
    if ((requested_permissions & CEF_PERMISSION_TYPE_LOCAL_FONTS) != 0) {
        AppendPermissionName(names, L"local fonts");
    }
#if CEF_API_ADDED(14700)
    if ((requested_permissions & CEF_PERMISSION_TYPE_SENSORS) != 0) {
        AppendPermissionName(names, L"sensors");
    }
#endif
    if ((requested_permissions & CEF_PERMISSION_TYPE_WINDOW_MANAGEMENT) != 0) {
        AppendPermissionName(names, L"window management");
    }
#if CEF_API_ADDED(14500)
    if ((requested_permissions & CEF_PERMISSION_TYPE_LOCAL_NETWORK) != 0) {
        AppendPermissionName(names, L"local network");
    }
    if ((requested_permissions & CEF_PERMISSION_TYPE_LOOPBACK_NETWORK) != 0) {
        AppendPermissionName(names, L"loopback network");
    }
#elif CEF_API_ADDED(13600)
    if ((requested_permissions & CEF_PERMISSION_TYPE_LOCAL_NETWORK_ACCESS) != 0) {
        AppendPermissionName(names, L"local network");
    }
#endif
    if (names.empty()) {
        names = L"special permission";
    }
    return names;
}
#endif

}  // namespace

struct BrowserHost::Impl {
    struct Tab {
        BrowserTabState state;
#if defined(CYBERDECK_HAS_CEF)
        CefRefPtr<CefBrowser> browser;
        CefRefPtr<CefClient> client;
        std::optional<ErrorPageState> pending_error_page;
#endif
    };

    common::Logger* logger = nullptr;
    bool initialized = false;
    HWND parent = nullptr;
    RECT bounds{};
    int next_tab_id = 1;
    int active_tab_id = 0;
    bool terminal_mode_enabled = false;
    std::vector<Tab> tabs;
    std::function<void(const std::vector<BrowserTabState>&, int)> tabs_changed_callback;
    std::function<void(const BrowserTabState&)> successful_navigation_callback;
    std::function<void(const DownloadStatus&)> download_status_callback;
    std::function<void(const PermissionStatus&)> permission_status_callback;

    Tab* FindTab(int tab_id) {
        const auto found = std::find_if(tabs.begin(), tabs.end(), [tab_id](const Tab& tab) {
            return tab.state.id == tab_id;
        });
        return found == tabs.end() ? nullptr : &*found;
    }

    const Tab* FindTab(int tab_id) const {
        const auto found = std::find_if(tabs.begin(), tabs.end(), [tab_id](const Tab& tab) {
            return tab.state.id == tab_id;
        });
        return found == tabs.end() ? nullptr : &*found;
    }

    Tab* ActiveTab() {
        return FindTab(active_tab_id);
    }

    std::vector<BrowserTabState> TabStates() const {
        std::vector<BrowserTabState> states;
        states.reserve(tabs.size());
        for (const Tab& tab : tabs) {
            states.push_back(tab.state);
        }
        return states;
    }

    void NotifyTabsChanged(int committed_tab_id = 0) const {
        if (tabs_changed_callback) {
            std::vector<BrowserTabState> states = TabStates();
            for (BrowserTabState& state : states) {
                state.url_committed = state.id == committed_tab_id;
            }
            tabs_changed_callback(states, active_tab_id);
        }
    }

    void RefreshNavigationState(Tab& tab) {
#if defined(CYBERDECK_HAS_CEF)
        if (tab.browser) {
            tab.state.can_go_back = tab.browser->CanGoBack();
            tab.state.can_go_forward = tab.browser->CanGoForward();
        }
#else
        (void)tab;
#endif
    }

    void ShowOnlyActiveTab() {
#if defined(CYBERDECK_HAS_CEF)
        for (Tab& tab : tabs) {
            if (!tab.browser) {
                continue;
            }

            HWND browser_window = tab.browser->GetHost()->GetWindowHandle();
            if (browser_window == nullptr) {
                continue;
            }

            const bool active = tab.state.id == active_tab_id;
            ShowWindow(browser_window, active ? SW_SHOW : SW_HIDE);
            if (active) {
                SetWindowPos(
                    browser_window,
                    nullptr,
                    bounds.left,
                    bounds.top,
                    RectWidth(bounds),
                    RectHeight(bounds),
                    SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
#endif
    }

    bool LoadIntoActive(std::wstring_view raw_input) {
        Tab* tab = ActiveTab();
        if (tab == nullptr || raw_input.empty()) {
            return false;
        }

        const NormalizedNavigation normalized = NormalizeAddressBarInput(raw_input);
        if (normalized.decision == NavigationDecision::kEmpty) {
            logger->Error("Navigation ignored: empty address bar input.");
            return false;
        }

        if (normalized.decision == NavigationDecision::kBlocked) {
            logger->Error("Navigation blocked: " + NarrowForLog(normalized.reason));
            return false;
        }

        const bool had_previous_url = !tab->state.url.empty();
        tab->state.url = normalized.target_url;
        tab->state.loading = true;
        tab->state.can_go_back = tab->state.can_go_back || had_previous_url;
        tab->state.can_go_forward = false;
        if (tab->state.title.empty() || tab->state.title == L"New Tab") {
            tab->state.title = normalized.target_url;
        }

#if defined(CYBERDECK_HAS_CEF)
        if (!tab->browser) {
            logger->Error("Navigate requested before active CEF browser exists.");
            NotifyTabsChanged();
            return false;
        }

        logger->Info("Navigating active tab to normalized URL: " + NarrowForLog(normalized.target_url));
        tab->browser->GetMainFrame()->LoadURL(CefString(normalized.target_url));
#else
        logger->Info("Navigate skipped because CEF is not configured.");
#endif

        NotifyTabsChanged();
        return true;
    }

    void NotifySuccessfulNavigation(const Tab& tab) const {
        if (successful_navigation_callback) {
            successful_navigation_callback(tab.state);
        }
    }

    void NotifyDownloadStatus(DownloadStatus status) const {
        status.message = DownloadMessage(status);
        if (download_status_callback) {
            download_status_callback(status);
        }
    }

    void NotifyPermissionStatus(PermissionStatus status) const {
        status.message = PermissionMessage(status);
        if (permission_status_callback) {
            permission_status_callback(status);
        }
    }

    bool ShouldAllowPermission(const std::wstring&, const std::wstring&) const {
        // Future hook: consult persisted per-site permission settings here.
        return false;
    }

    void ExecuteTerminalModeScript(Tab& tab, bool enabled) const {
#if defined(CYBERDECK_HAS_CEF)
        if (!tab.browser) {
            return;
        }

        CefRefPtr<CefFrame> frame = tab.browser->GetMainFrame();
        if (!frame) {
            return;
        }

        const bool should_enable = enabled && !ShouldSkipTerminalModeForUrl(tab.state.url);
        frame->ExecuteJavaScript(
            CefString(should_enable ? TerminalModeInjectionScript() : TerminalModeRemovalScript()),
            CefString(std::wstring(L"cyberdeck://terminal-mode")),
            0);
        if (enabled && !should_enable) {
            logger->Info(
                "Skipped Terminal Mode CSS for media-heavy URL in tab " + std::to_string(tab.state.id) + ".");
        } else {
            logger->Info(
                std::string(should_enable ? "Applied" : "Removed") +
                " Terminal Mode CSS for tab " + std::to_string(tab.state.id) + ".");
        }
#else
        (void)tab;
        (void)enabled;
#endif
    }

    void ApplyTerminalModeToLoadedTabs() {
        for (Tab& tab : tabs) {
            ExecuteTerminalModeScript(tab, terminal_mode_enabled);
        }
    }

    bool HandleProtocolNavigation(std::wstring_view url, std::string_view source, bool user_gesture) const {
        const ProtocolDecision decision = ClassifyNavigationProtocol(url);
        if (decision.action == ProtocolAction::kAllow) {
            return false;
        }

        if (decision.action == ProtocolAction::kBlock) {
            logger->Error(
                "Blocked protocol navigation from " + std::string(source) +
                ": scheme=" + NarrowForLog(decision.scheme) +
                " url=" + NarrowForLog(url) +
                " reason=" + NarrowForLog(decision.reason) + ".");
            return true;
        }

        if (!user_gesture) {
            logger->Error(
                "Blocked external protocol without user gesture from " + std::string(source) +
                ": scheme=" + NarrowForLog(decision.scheme) +
                " url=" + NarrowForLog(url) + ".");
            return true;
        }

#if defined(CYBERDECK_HAS_CEF)
        const std::wstring url_text(url);
        const bool confirmed = ConfirmExternalProtocolLaunch(parent, url_text, decision);
        if (!confirmed) {
            logger->Info(
                "User canceled external protocol launch from " + std::string(source) +
                ": scheme=" + NarrowForLog(decision.scheme) +
                " url=" + NarrowForLog(url) + ".");
            return true;
        }

        HINSTANCE result = ShellExecuteW(parent, L"open", url_text.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32) {
            logger->Error(
                "Confirmed external protocol launch failed from " + std::string(source) +
                ": scheme=" + NarrowForLog(decision.scheme) +
                " url=" + NarrowForLog(url) +
                " shell_error=" + std::to_string(reinterpret_cast<INT_PTR>(result)) + ".");
        } else {
            logger->Info(
                "Confirmed external protocol launch from " + std::string(source) +
                ": scheme=" + NarrowForLog(decision.scheme) +
                " url=" + NarrowForLog(url) + ".");
        }
#else
        logger->Error(
            "Blocked external protocol because CEF/external launch support is not configured from " +
            std::string(source) +
            ": scheme=" + NarrowForLog(decision.scheme) +
            " url=" + NarrowForLog(url) + ".");
#endif
        return true;
    }

#if defined(CYBERDECK_HAS_CEF)
    class App final : public CefApp {
    public:
        App() = default;

        void OnBeforeCommandLineProcessing(const CefString&, CefRefPtr<CefCommandLine> command_line) override {
            if (!command_line) {
                return;
            }

            command_line->AppendSwitch("disable-extensions");
            command_line->AppendSwitch("disable-component-extensions-with-background-pages");
            command_line->AppendSwitch("disable-features=AutofillActorMode,GlicActorUi,LensOverlay");
            if (g_remote_debugging_enabled && !g_remote_debugging_address.empty()) {
                command_line->AppendSwitchWithValue("remote-debugging-address", g_remote_debugging_address);
            }
        }

    private:
        IMPLEMENT_REFCOUNTING(App);
    };

    class Client final
        : public CefClient,
          public CefLifeSpanHandler,
          public CefLoadHandler,
          public CefDisplayHandler,
          public CefDownloadHandler,
          public CefPermissionHandler,
          public CefRequestHandler {
    public:
        Client(Impl& owner, int tab_id) : owner_(owner), tab_id_(tab_id) {}

        CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {
            return this;
        }

        CefRefPtr<CefLoadHandler> GetLoadHandler() override {
            return this;
        }

        CefRefPtr<CefDisplayHandler> GetDisplayHandler() override {
            return this;
        }

        CefRefPtr<CefDownloadHandler> GetDownloadHandler() override {
            return this;
        }

        CefRefPtr<CefPermissionHandler> GetPermissionHandler() override {
            return this;
        }

        CefRefPtr<CefRequestHandler> GetRequestHandler() override {
            return this;
        }

        void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
            if (Tab* tab = owner_.FindTab(tab_id_)) {
                tab->browser = browser;
                owner_.RefreshNavigationState(*tab);
                owner_.logger->Info("CEF browser view created for tab " + std::to_string(tab_id_) + ".");
            }

            owner_.ShowOnlyActiveTab();
            owner_.NotifyTabsChanged();
        }

        void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
            if (Tab* tab = owner_.FindTab(tab_id_); tab != nullptr && tab->browser && tab->browser->IsSame(browser)) {
                tab->browser = nullptr;
            }
            owner_.logger->Info("CEF browser view closed for tab " + std::to_string(tab_id_) + ".");
        }

        bool OnBeforePopup(
            CefRefPtr<CefBrowser>,
            CefRefPtr<CefFrame>,
            int,
            const CefString& target_url,
            const CefString&,
            CefLifeSpanHandler::WindowOpenDisposition,
            bool user_gesture,
            const CefPopupFeatures&,
            CefWindowInfo&,
            CefRefPtr<CefClient>&,
            CefBrowserSettings&,
            CefRefPtr<CefDictionaryValue>&,
            bool* no_javascript_access) override {
            if (no_javascript_access != nullptr) {
                *no_javascript_access = true;
            }

            const std::wstring url = target_url.ToWString();
            if (url.empty() || StartsWith(url, L"about:blank")) {
                owner_.logger->Info("Blocked empty popup for tab " + std::to_string(tab_id_) + ".");
                return true;
            }

            if (owner_.HandleProtocolNavigation(url, "OnBeforePopup", user_gesture)) {
                return true;
            }

            if (IsMalformedAdClickPopup(url)) {
                owner_.logger->Info(
                    "Blocked malformed ad click popup for tab " + std::to_string(tab_id_) +
                    " to " + NarrowForLog(url) + ".");
                return true;
            }

            if (!user_gesture) {
                owner_.logger->Info(
                    "Blocked script-created popup for tab " + std::to_string(tab_id_) +
                    " to " + NarrowForLog(url) + ".");
                return true;
            }

            owner_.logger->Info(
                "Opening popup navigation in active CyberDeck tab " + std::to_string(tab_id_) +
                " to " + NarrowForLog(url) + ".");
            owner_.LoadIntoActive(url);
            return true;
        }

        void OnAddressChange(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, const CefString& url) override {
            if (frame && frame->IsMain()) {
                if (Tab* tab = owner_.FindTab(tab_id_)) {
                    tab->state.url = url.ToWString();
                    owner_.RefreshNavigationState(*tab);
                }
                owner_.logger->Info(
                    "CEF navigation committed for tab " + std::to_string(tab_id_) +
                    " to " + NarrowForLog(url.ToWString()) + ".");
                owner_.NotifyTabsChanged(tab_id_);
            }
        }

        void OnTitleChange(CefRefPtr<CefBrowser>, const CefString& title) override {
            if (Tab* tab = owner_.FindTab(tab_id_)) {
                const std::wstring title_text = title.ToWString();
                tab->state.title = title_text.empty() ? L"New Tab" : title_text;
                owner_.logger->Info(
                    "CEF title changed for tab " + std::to_string(tab_id_) +
                    " to " + NarrowForLog(tab->state.title) + ".");
            }
            owner_.NotifyTabsChanged();
        }

        void OnLoadStart(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, TransitionType) override {
            if (frame && frame->IsMain()) {
                if (Tab* tab = owner_.FindTab(tab_id_)) {
                    const std::wstring started_url = frame->GetURL().ToWString();
                    if (!started_url.empty()) {
                        tab->state.url = started_url;
                    }
                    if (!StartsWith(started_url, kErrorPageUrl)) {
                        tab->pending_error_page.reset();
                    }
                    tab->state.loading = true;
                    owner_.RefreshNavigationState(*tab);
                }
                owner_.logger->Info("CEF page load started for tab " + std::to_string(tab_id_) + ".");
                owner_.NotifyTabsChanged();
            }
        }

        void OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, int http_status_code) override {
            if (frame && frame->IsMain()) {
                Tab* tab = owner_.FindTab(tab_id_);
                if (tab != nullptr) {
                    const std::wstring completed_url = frame->GetURL().ToWString();
                    if (!completed_url.empty()) {
                        tab->state.url = completed_url;
                    }
                    tab->state.loading = false;
                    owner_.RefreshNavigationState(*tab);
                    if (tab->pending_error_page && StartsWith(completed_url, kErrorPageUrl)) {
                        tab->state.title = L"CyberDeck Load Failure";
                        const std::wstring error_page_url(kErrorPageUrl);
                        frame->ExecuteJavaScript(
                            CefString(BuildErrorPageInjectionScript(*tab->pending_error_page)),
                            CefString(error_page_url),
                            0);
                        owner_.logger->Info(
                            "Rendered CyberDeck error page for tab " + std::to_string(tab_id_) +
                            " URL " + NarrowForLog(tab->pending_error_page->failed_url) + ".");
                        owner_.NotifyTabsChanged(tab_id_);
                        return;
                    }
                }
                owner_.logger->Info(
                    "CEF page load ended for tab " + std::to_string(tab_id_) +
                    " with HTTP status " + std::to_string(http_status_code) + ".");
                if (tab != nullptr && http_status_code >= 200 && http_status_code < 400) {
                    if (owner_.terminal_mode_enabled) {
                        owner_.ExecuteTerminalModeScript(*tab, true);
                    }
                    owner_.NotifySuccessfulNavigation(*tab);
                }
                owner_.NotifyTabsChanged();
            }
        }

        void OnLoadingStateChange(
            CefRefPtr<CefBrowser>,
            bool is_loading,
            bool can_go_back,
            bool can_go_forward) override {
            if (Tab* tab = owner_.FindTab(tab_id_)) {
                tab->state.loading = is_loading;
                tab->state.can_go_back = can_go_back;
                tab->state.can_go_forward = can_go_forward;
            }
            owner_.NotifyTabsChanged();
        }

        void OnLoadError(
            CefRefPtr<CefBrowser>,
            CefRefPtr<CefFrame> frame,
            ErrorCode error_code,
            const CefString&,
            const CefString& failed_url) override {
            if (frame && frame->IsMain()) {
                if (Tab* tab = owner_.FindTab(tab_id_)) {
                    tab->state.loading = false;
                    tab->state.url = failed_url.ToWString();
                    owner_.RefreshNavigationState(*tab);
                }
                if (static_cast<int>(error_code) == -3) {
                    owner_.logger->Info(
                        "CEF page load aborted for tab " + std::to_string(tab_id_) +
                        " for URL " + NarrowForLog(failed_url.ToWString()) + ".");
                    owner_.NotifyTabsChanged();
                    return;
                }
                owner_.logger->Error(
                    "CEF page load failed for tab " + std::to_string(tab_id_) +
                    " with error code " + std::to_string(error_code) +
                    " for URL " + NarrowForLog(failed_url.ToWString()) + ".");
                if (Tab* tab = owner_.FindTab(tab_id_); tab != nullptr && frame) {
                    const int error_code_int = static_cast<int>(error_code);
                    tab->pending_error_page = ErrorPageState{
                        .failed_url = failed_url.ToWString(),
                        .error_code = error_code_int,
                        .error_name = ErrorCodeName(error_code_int),
                        .explanation = FriendlyErrorExplanation(error_code_int),
                    };
                    tab->state.title = L"CyberDeck Load Failure";
                    frame->LoadURL(CefString(std::wstring(kErrorPageUrl)));
                }
                owner_.NotifyTabsChanged();
            }
        }

        bool OnBeforeDownload(
            CefRefPtr<CefBrowser>,
            CefRefPtr<CefDownloadItem> download_item,
            const CefString& suggested_name,
            CefRefPtr<CefBeforeDownloadCallback> callback) override {
            const int download_id = download_item ? download_item->GetId() : 0;
            const std::wstring file_name = SafeDownloadFileName(suggested_name.ToWString());
            const std::filesystem::path target_path = std::filesystem::path(DefaultDownloadsDirectory()) / file_name;

            std::error_code error;
            std::filesystem::create_directories(target_path.parent_path(), error);
            if (error) {
                owner_.logger->Error("Download canceled; unable to create download directory.");
                owner_.NotifyDownloadStatus({
                    .id = download_id,
                    .file_name = file_name,
                    .target_path = target_path.wstring(),
                    .canceled = true,
                });
                return true;
            }

            if (std::filesystem::exists(target_path, error)) {
                const std::wstring prompt =
                    L"Overwrite existing file?\n\n" + target_path.wstring();
                const int response = MessageBoxW(
                    owner_.parent,
                    prompt.c_str(),
                    L"CyberDeck Browser Download",
                    MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
                if (response != IDYES) {
                    owner_.logger->Info("Download canceled by user before overwrite: " + NarrowForLog(target_path.wstring()));
                    owner_.NotifyDownloadStatus({
                        .id = download_id,
                        .file_name = file_name,
                        .target_path = target_path.wstring(),
                        .canceled = true,
                    });
                    return true;
                }
            }

            owner_.logger->Info("Download started: " + NarrowForLog(target_path.wstring()));
            owner_.NotifyDownloadStatus({
                .id = download_id,
                .file_name = file_name,
                .target_path = target_path.wstring(),
                .percent_complete = 0,
            });

            if (callback) {
                callback->Continue(CefString(target_path.wstring()), false);
            }
            return true;
        }

        void OnDownloadUpdated(
            CefRefPtr<CefBrowser>,
            CefRefPtr<CefDownloadItem> download_item,
            CefRefPtr<CefDownloadItemCallback>) override {
            if (!download_item) {
                return;
            }

            DownloadStatus status;
            status.id = download_item->GetId();
            status.file_name = SafeDownloadFileName(download_item->GetSuggestedFileName().ToWString());
            status.target_path = download_item->GetFullPath().ToWString();
            status.percent_complete = download_item->GetPercentComplete();
            status.complete = download_item->IsComplete();
            status.canceled = download_item->IsCanceled();

            if (status.file_name.empty()) {
                status.file_name = SafeDownloadFileName(std::filesystem::path(status.target_path).filename().wstring());
            }

            if (status.complete) {
                owner_.logger->Info("Download completed: " + NarrowForLog(status.target_path));
            } else if (status.canceled) {
                owner_.logger->Error("Download failed or canceled: " + NarrowForLog(status.target_path));
            }

            owner_.NotifyDownloadStatus(std::move(status));
        }

        bool OnRequestMediaAccessPermission(
            CefRefPtr<CefBrowser>,
            CefRefPtr<CefFrame>,
            const CefString& requesting_origin,
            uint32_t requested_permissions,
            CefRefPtr<CefMediaAccessCallback> callback) override {
            const std::wstring origin = requesting_origin.ToWString();
            const std::wstring permissions = MediaPermissionNames(requested_permissions);
            const bool allowed = owner_.ShouldAllowPermission(origin, permissions);

            owner_.logger->Info(
                std::string("Media permission request ") +
                (allowed ? "allowed" : "denied") +
                " for tab " + std::to_string(tab_id_) +
                " from " + NarrowForLog(origin) +
                ": " + NarrowForLog(permissions) + ".");

            owner_.NotifyPermissionStatus({
                .tab_id = tab_id_,
                .origin = origin,
                .permission = permissions,
                .allowed = allowed,
            });

            if (callback) {
                callback->Continue(allowed ? requested_permissions : CEF_MEDIA_PERMISSION_NONE);
            }
            return true;
        }

        bool OnShowPermissionPrompt(
            CefRefPtr<CefBrowser>,
            uint64_t prompt_id,
            const CefString& requesting_origin,
            uint32_t requested_permissions,
            CefRefPtr<CefPermissionPromptCallback> callback) override {
            const std::wstring origin = requesting_origin.ToWString();
            const std::wstring permissions = PromptPermissionNames(requested_permissions);
            const bool allowed = owner_.ShouldAllowPermission(origin, permissions);

            owner_.logger->Info(
                std::string("Permission prompt ") +
                (allowed ? "allowed" : "denied") +
                " for tab " + std::to_string(tab_id_) +
                " prompt " + std::to_string(prompt_id) +
                " from " + NarrowForLog(origin) +
                ": " + NarrowForLog(permissions) + ".");

            owner_.NotifyPermissionStatus({
                .tab_id = tab_id_,
                .origin = origin,
                .permission = permissions,
                .allowed = allowed,
            });

            if (callback) {
                callback->Continue(allowed ? CEF_PERMISSION_RESULT_ACCEPT : CEF_PERMISSION_RESULT_DENY);
            }
            return true;
        }

        void OnDismissPermissionPrompt(
            CefRefPtr<CefBrowser>,
            uint64_t prompt_id,
            cef_permission_request_result_t result) override {
            owner_.logger->Info(
                "Permission prompt dismissed for tab " + std::to_string(tab_id_) +
                " prompt " + std::to_string(prompt_id) +
                " with result " + std::to_string(result) + ".");
        }

        void OnMediaAccessChange(CefRefPtr<CefBrowser>, bool has_video_access, bool has_audio_access) override {
            if (has_video_access || has_audio_access) {
                std::wstring permissions;
                if (has_video_access) {
                    AppendPermissionName(permissions, L"camera");
                }
                if (has_audio_access) {
                    AppendPermissionName(permissions, L"microphone");
                }
                owner_.logger->Error(
                    "Unexpected active media access for tab " + std::to_string(tab_id_) +
                    ": " + NarrowForLog(permissions) + ".");
                owner_.NotifyPermissionStatus({
                    .tab_id = tab_id_,
                    .origin = L"active page",
                    .permission = permissions,
                    .allowed = true,
                });
            } else {
                owner_.logger->Info("Media access inactive for tab " + std::to_string(tab_id_) + ".");
            }
        }

        bool OnBeforeBrowse(
            CefRefPtr<CefBrowser>,
            CefRefPtr<CefFrame>,
            CefRefPtr<CefRequest> request,
            bool user_gesture,
            bool) override {
            if (!request) {
                return false;
            }
            return owner_.HandleProtocolNavigation(request->GetURL().ToWString(), "OnBeforeBrowse", user_gesture);
        }

        bool OnOpenURLFromTab(
            CefRefPtr<CefBrowser>,
            CefRefPtr<CefFrame>,
            const CefString& target_url,
            cef_window_open_disposition_t,
            bool user_gesture) override {
            return owner_.HandleProtocolNavigation(target_url.ToWString(), "OnOpenURLFromTab", user_gesture);
        }

    private:
        Impl& owner_;
        int tab_id_ = 0;

        IMPLEMENT_REFCOUNTING(Client);
    };

    CefRefPtr<App> app;
#endif
};

BrowserHost::BrowserHost() = default;
BrowserHost::~BrowserHost() = default;

BrowserHost::InitializeResult BrowserHost::Initialize(HINSTANCE instance, common::Logger& logger) {
    impl_ = std::make_unique<Impl>();
    impl_->logger = &logger;

#if defined(CYBERDECK_HAS_CEF)
    impl_->app = new Impl::App();

    CefMainArgs main_args(instance);
    const int process_exit_code = CefExecuteProcess(main_args, impl_->app, nullptr);
    if (process_exit_code >= 0) {
        return {.success = true, .should_exit = true, .exit_code = process_exit_code};
    }

    CefSettings settings;
    settings.no_sandbox = true;
    settings.multi_threaded_message_loop = true;
    settings.persist_session_cookies = true;
    logger.Info(
        "CEF sandbox remains disabled for the current Windows executable packaging. "
        "Do not flip this until the Windows CEF bootstrap/sandbox-info launch path is wired.");

    if (const auto remote_debugging_port = EnvironmentPort(kRemoteDebuggingPortEnv)) {
        settings.remote_debugging_port = *remote_debugging_port;
        std::wstring debug_host(kDefaultRemoteDebugAddressWide);
        const bool allow_nonlocal = EnvironmentBoolean(kRemoteDebuggingAllowAllHostsEnv);
        if (const auto raw_host = EnvironmentValue(kRemoteDebuggingHostEnv)) {
            const std::wstring_view host = TrimAsciiWhitespace(*raw_host);
            if (!host.empty() && (IsLoopbackHost(host) || allow_nonlocal)) {
                debug_host = ToAsciiLower(host);
            } else if (!host.empty()) {
                logger.Info(
                    "CYBERDECK_CEF_REMOTE_DEBUGGING_HOST is non-loopback and was ignored. "
                    "Set CYBERDECK_CEF_ALLOW_NONLOCAL_REMOTE_DEBUGGING=1 to allow binding for a non-local interface.");
            }
        }

        g_remote_debugging_enabled = true;
        g_remote_debugging_address = NarrowForLog(debug_host);
        logger.Info(
            "CEF remote debugging enabled on " + g_remote_debugging_address + ":" + std::to_string(*remote_debugging_port) + ".");
    }

    const std::filesystem::path cef_data_dir = common::AppDataDirectory() / "cef";
    const std::filesystem::path cef_cache_dir = cef_data_dir / "cache";
    const std::filesystem::path cef_log_path = common::AppDataDirectory() / "logs" / "cef.log";
    std::error_code cef_path_error;
    std::filesystem::create_directories(cef_cache_dir, cef_path_error);
    std::filesystem::create_directories(cef_log_path.parent_path(), cef_path_error);
    CefString(&settings.root_cache_path).FromWString(cef_data_dir.wstring());
    CefString(&settings.cache_path).FromWString(cef_cache_dir.wstring());
    CefString(&settings.log_file).FromWString(cef_log_path.wstring());

    const bool initialized = CefInitialize(main_args, settings, impl_->app, nullptr);
    if (!initialized) {
        logger.Error("CEF initialization failed.");
        return {.success = false, .should_exit = false, .exit_code = 1};
    }

    impl_->initialized = true;
    logger.Info("CEF initialized.");
    return {.success = true, .should_exit = false, .exit_code = 0};
#else
    (void)instance;
    logger.Info("CEF_ROOT not configured; running with placeholder browser area.");
    impl_->initialized = true;
    return {.success = true, .should_exit = false, .exit_code = 0};
#endif
}

void BrowserHost::SetTabsChangedCallback(std::function<void(const std::vector<BrowserTabState>&, int)> callback) {
    if (impl_) {
        impl_->tabs_changed_callback = std::move(callback);
    }
}

void BrowserHost::SetSuccessfulNavigationCallback(std::function<void(const BrowserTabState&)> callback) {
    if (impl_) {
        impl_->successful_navigation_callback = std::move(callback);
    }
}

void BrowserHost::SetDownloadStatusCallback(std::function<void(const DownloadStatus&)> callback) {
    if (impl_) {
        impl_->download_status_callback = std::move(callback);
    }
}

void BrowserHost::SetPermissionStatusCallback(std::function<void(const PermissionStatus&)> callback) {
    if (impl_) {
        impl_->permission_status_callback = std::move(callback);
    }
}

bool BrowserHost::CreateInitialTab(HWND parent, const RECT& bounds, std::wstring_view initial_url) {
    if (!impl_ || !impl_->initialized) {
        return false;
    }

    impl_->parent = parent;
    impl_->bounds = bounds;
    return CreateTab(initial_url.empty() ? std::wstring_view(kDefaultHomepage) : initial_url);
}

bool BrowserHost::CreateTab(std::wstring_view initial_url) {
    if (!impl_ || !impl_->initialized || impl_->parent == nullptr) {
        return false;
    }

    const int tab_id = impl_->next_tab_id++;
    Impl::Tab tab;
    tab.state.id = tab_id;
    tab.state.title = L"New Tab";
    tab.state.url = initial_url.empty() ? std::wstring(kDefaultHomepage) : std::wstring(initial_url);
    tab.state.url_committed = false;
    impl_->tabs.push_back(std::move(tab));
    impl_->active_tab_id = tab_id;

#if defined(CYBERDECK_HAS_CEF)
    Impl::Tab* created_tab = impl_->FindTab(tab_id);
    if (created_tab == nullptr) {
        return false;
    }

    CefRefPtr<Impl::Client> client = new Impl::Client(*impl_, tab_id);
    created_tab->client = client;

    CefWindowInfo window_info;
    window_info.SetAsChild(
        impl_->parent,
        CefRect(impl_->bounds.left, impl_->bounds.top, RectWidth(impl_->bounds), RectHeight(impl_->bounds)));

    CefBrowserSettings browser_settings;
    const bool created = CefBrowserHost::CreateBrowser(
        window_info,
        client,
        CefString(created_tab->state.url),
        browser_settings,
        nullptr,
        nullptr);

    if (!created) {
        impl_->logger->Error("CEF browser creation failed for tab " + std::to_string(tab_id) + ".");
        impl_->tabs.erase(
            std::remove_if(impl_->tabs.begin(), impl_->tabs.end(), [tab_id](const Impl::Tab& existing) {
                return existing.state.id == tab_id;
            }),
            impl_->tabs.end());
        impl_->active_tab_id = impl_->tabs.empty() ? 0 : impl_->tabs.back().state.id;
        impl_->NotifyTabsChanged();
        return false;
    }
#else
    impl_->logger->Info("CEF tab browser creation skipped because CEF is not configured.");
#endif

    impl_->logger->Info("Created tab " + std::to_string(tab_id) + ".");
    impl_->ShowOnlyActiveTab();
    impl_->NotifyTabsChanged();
    return true;
}

bool BrowserHost::ActivateTab(int tab_id) {
    if (!impl_ || impl_->FindTab(tab_id) == nullptr) {
        return false;
    }

    impl_->active_tab_id = tab_id;
    if (Impl::Tab* tab = impl_->ActiveTab()) {
        impl_->RefreshNavigationState(*tab);
    }
    impl_->ShowOnlyActiveTab();
    impl_->logger->Info("Activated tab " + std::to_string(tab_id) + ".");
    impl_->NotifyTabsChanged();
    return true;
}

bool BrowserHost::CloseTab(int tab_id) {
    if (!impl_ || impl_->tabs.size() <= 1) {
        if (impl_ && impl_->logger) {
            impl_->logger->Info("Close tab ignored because at least one tab must remain open.");
        }
        return false;
    }

    const auto found = std::find_if(impl_->tabs.begin(), impl_->tabs.end(), [tab_id](const Impl::Tab& tab) {
        return tab.state.id == tab_id;
    });
    if (found == impl_->tabs.end()) {
        return false;
    }

#if defined(CYBERDECK_HAS_CEF)
    if (found->browser) {
        HWND browser_window = found->browser->GetHost()->GetWindowHandle();
        if (browser_window != nullptr) {
            ShowWindow(browser_window, SW_HIDE);
        }
        found->browser->GetHost()->CloseBrowser(true);
    }
    found->client = nullptr;
#endif

    const bool was_active = impl_->active_tab_id == tab_id;
    const std::size_t removed_index = static_cast<std::size_t>(std::distance(impl_->tabs.begin(), found));
    impl_->tabs.erase(found);
    if (was_active) {
        const std::size_t next_index = std::min(removed_index, impl_->tabs.size() - 1);
        impl_->active_tab_id = impl_->tabs[next_index].state.id;
    }

    impl_->ShowOnlyActiveTab();
    impl_->logger->Info("Closed tab " + std::to_string(tab_id) + ".");
    impl_->NotifyTabsChanged();
    return true;
}

void BrowserHost::Resize(const RECT& bounds) {
    if (!impl_) {
        return;
    }

    impl_->bounds = bounds;
    impl_->ShowOnlyActiveTab();
}

void BrowserHost::Navigate(std::wstring_view url) {
    if (!impl_) {
        return;
    }

    impl_->LoadIntoActive(url);
}

void BrowserHost::GoBack() {
    if (!impl_) {
        return;
    }

    Impl::Tab* tab = impl_->ActiveTab();
    if (tab == nullptr) {
        return;
    }

#if defined(CYBERDECK_HAS_CEF)
    if (tab->browser && tab->browser->CanGoBack()) {
        tab->browser->GoBack();
        tab->state.can_go_forward = true;
        impl_->NotifyTabsChanged();
    }
#else
    impl_->logger->Info("Back skipped because CEF is not configured.");
#endif
}

void BrowserHost::GoForward() {
    if (!impl_) {
        return;
    }

    Impl::Tab* tab = impl_->ActiveTab();
    if (tab == nullptr) {
        return;
    }

#if defined(CYBERDECK_HAS_CEF)
    if (tab->browser && tab->browser->CanGoForward()) {
        tab->browser->GoForward();
        tab->state.can_go_back = true;
        impl_->NotifyTabsChanged();
    }
#else
    impl_->logger->Info("Forward skipped because CEF is not configured.");
#endif
}

void BrowserHost::Reload() {
    if (!impl_) {
        return;
    }

    Impl::Tab* tab = impl_->ActiveTab();
    if (tab == nullptr) {
        return;
    }

#if defined(CYBERDECK_HAS_CEF)
    if (tab->browser) {
        tab->browser->Reload();
    }
#else
    impl_->logger->Info("Reload skipped because CEF is not configured.");
#endif
}

void BrowserHost::Stop() {
    if (!impl_) {
        return;
    }

    Impl::Tab* tab = impl_->ActiveTab();
    if (tab == nullptr) {
        return;
    }

#if defined(CYBERDECK_HAS_CEF)
    if (tab->browser) {
        tab->browser->StopLoad();
    }
#else
    impl_->logger->Info("Stop skipped because CEF is not configured.");
#endif
}

void BrowserHost::SetTerminalModeEnabled(bool enabled) {
    if (!impl_) {
        return;
    }

    if (impl_->terminal_mode_enabled == enabled) {
        impl_->ApplyTerminalModeToLoadedTabs();
        return;
    }

    impl_->terminal_mode_enabled = enabled;
    if (impl_->logger) {
        impl_->logger->Info(std::string("Terminal Mode ") + (enabled ? "enabled." : "disabled."));
    }
    impl_->ApplyTerminalModeToLoadedTabs();
}

bool BrowserHost::TerminalModeEnabled() const {
    return impl_ ? impl_->terminal_mode_enabled : false;
}

void BrowserHost::Shutdown() {
    if (!impl_) {
        return;
    }

#if defined(CYBERDECK_HAS_CEF)
    for (Impl::Tab& tab : impl_->tabs) {
        if (tab.browser) {
            tab.browser->GetHost()->CloseBrowser(true);
            tab.browser = nullptr;
        }
        tab.client = nullptr;
    }

    if (impl_->initialized) {
        CefShutdown();
        impl_->initialized = false;
        impl_->logger->Info("CEF shut down.");
    }

    impl_->app = nullptr;
#else
    impl_->initialized = false;
#endif
}

bool BrowserHost::IsCefEnabled() const {
#if defined(CYBERDECK_HAS_CEF)
    return true;
#else
    return false;
#endif
}

std::wstring BrowserHost::CefVersionText() const {
#if defined(CYBERDECK_HAS_CEF)
#if defined(CEF_VERSION)
    return L"CEF " + WideFromAscii(CEF_VERSION);
#else
    return L"CEF enabled (version macro unavailable)";
#endif
#else
    return L"CEF not configured";
#endif
}

int BrowserHost::ActiveTabId() const {
    return impl_ ? impl_->active_tab_id : 0;
}

std::vector<BrowserTabState> BrowserHost::Tabs() const {
    return impl_ ? impl_->TabStates() : std::vector<BrowserTabState>{};
}

}  // namespace cyberdeck::browser
