#include "app/Application.h"

#include "browser/UrlNavigation.h"
#include "common/AppInfo.h"
#include "render/DeckLayout.h"

#include <algorithm>
#include <cwctype>
#include <optional>
#include <utility>
#include <vector>

namespace cyberdeck::app {
namespace {

std::vector<main::UiTabState> ToUiTabs(const std::vector<browser::BrowserTabState>& tabs) {
    std::vector<main::UiTabState> ui_tabs;
    ui_tabs.reserve(tabs.size());
    for (const browser::BrowserTabState& tab : tabs) {
        ui_tabs.push_back({
            .id = tab.id,
            .title = tab.title,
            .url = tab.url,
            .loading = tab.loading,
            .url_committed = tab.url_committed,
            .can_go_back = tab.can_go_back,
            .can_go_forward = tab.can_go_forward,
        });
    }
    return ui_tabs;
}

std::wstring BoolText(bool value) {
    return value ? L"true" : L"false";
}

std::wstring WideFromAscii(std::string_view value) {
    std::wstring output;
    output.reserve(value.size());
    for (char ch : value) {
        output.push_back(static_cast<wchar_t>(ch));
    }
    return output;
}

std::string NarrowAscii(std::wstring_view value) {
    std::string output;
    output.reserve(value.size());
    for (wchar_t ch : value) {
        output.push_back(ch <= 0x7F ? static_cast<char>(ch) : '?');
    }
    return output;
}

const browser::BrowserTabState* ActiveBrowserTab(
    const std::vector<browser::BrowserTabState>& tabs,
    int active_tab_id) {
    const auto found = std::find_if(tabs.begin(), tabs.end(), [active_tab_id](const browser::BrowserTabState& tab) {
        return tab.id == active_tab_id;
    });
    return found == tabs.end() ? nullptr : &*found;
}

bool StartsWith(std::wstring_view value, std::wstring_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

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

std::wstring GetControlText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) {
        return {};
    }
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    return text;
}

struct NodeEditDialogData {
    deck::BookmarkNode node;
    bool saved = false;
    HFONT font = nullptr;
    HBRUSH background = nullptr;
    HWND title_edit = nullptr;
    HWND url_edit = nullptr;
    HWND shape_combo = nullptr;
    HWND color_combo = nullptr;
};

constexpr wchar_t kNodeEditDialogClassName[] = L"CyberDeckNodeEditDialog";
constexpr int kNodeEditTitle = 1101;
constexpr int kNodeEditUrl = 1102;
constexpr int kNodeEditShape = 1103;
constexpr int kNodeEditColor = 1104;
constexpr int kNodeEditSave = 1105;
constexpr int kNodeEditCancel = 1106;
constexpr wchar_t kVaultEditDialogClassName[] = L"CyberDeckVaultEditDialog";
constexpr int kVaultEditName = 1201;
constexpr int kVaultEditColor = 1202;
constexpr int kVaultEditSave = 1203;
constexpr int kVaultEditCancel = 1204;

struct VaultEditDialogData {
    deck::BookmarkVault vault;
    bool saved = false;
    HFONT font = nullptr;
    HBRUSH background = nullptr;
    HWND name_edit = nullptr;
    HWND color_combo = nullptr;
};

int ShapeIndex(deck::BookmarkNodeShapeType shape) {
    switch (shape) {
        case deck::BookmarkNodeShapeType::Hex:
            return 0;
        case deck::BookmarkNodeShapeType::Cube:
            return 1;
        case deck::BookmarkNodeShapeType::Panel:
            return 2;
    }
    return 0;
}

int ColorIndex(deck::BookmarkNodeColorTheme color) {
    switch (color) {
        case deck::BookmarkNodeColorTheme::Green:
            return 0;
        case deck::BookmarkNodeColorTheme::Yellow:
            return 1;
        case deck::BookmarkNodeColorTheme::Red:
            return 2;
        case deck::BookmarkNodeColorTheme::Mixed:
            return 3;
    }
    return 0;
}

deck::BookmarkNodeShapeType ShapeFromIndex(int index) {
    switch (index) {
        case 1:
            return deck::BookmarkNodeShapeType::Cube;
        case 2:
            return deck::BookmarkNodeShapeType::Panel;
        default:
            return deck::BookmarkNodeShapeType::Hex;
    }
}

deck::BookmarkNodeColorTheme ColorFromIndex(int index) {
    switch (index) {
        case 1:
            return deck::BookmarkNodeColorTheme::Yellow;
        case 2:
            return deck::BookmarkNodeColorTheme::Red;
        case 3:
            return deck::BookmarkNodeColorTheme::Mixed;
        default:
            return deck::BookmarkNodeColorTheme::Green;
    }
}

HWND CreateDialogControl(
    HWND parent,
    const wchar_t* class_name,
    const wchar_t* text,
    DWORD style,
    int id,
    int x,
    int y,
    int width,
    int height,
    HFONT font) {
    HWND control = CreateWindowExW(
        0,
        class_name,
        text,
        WS_CHILD | WS_VISIBLE | style,
        x,
        y,
        width,
        height,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        nullptr,
        nullptr);
    if (control != nullptr && font != nullptr) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    return control;
}

LRESULT CALLBACK NodeEditDialogProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* data = reinterpret_cast<NodeEditDialogData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (message) {
        case WM_NCCREATE: {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
            data = static_cast<NodeEditDialogData*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));
            return TRUE;
        }
        case WM_CREATE: {
            if (data == nullptr) {
                return -1;
            }
            data->font = CreateFontW(
                -16,
                0,
                0,
                0,
                FW_MEDIUM,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                FIXED_PITCH | FF_MODERN,
                L"Cascadia Mono");
            if (data->font == nullptr) {
                data->font = CreateFontW(
                    -16,
                    0,
                    0,
                    0,
                    FW_MEDIUM,
                    FALSE,
                    FALSE,
                    FALSE,
                    DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY,
                    FIXED_PITCH | FF_MODERN,
                    L"Consolas");
            }
            data->background = CreateSolidBrush(RGB(0, 0, 0));

            CreateDialogControl(hwnd, L"STATIC", L"EDIT DECK NODE", SS_LEFT, 0, 18, 16, 430, 24, data->font);
            CreateDialogControl(hwnd, L"STATIC", L"Title", SS_LEFT, 0, 18, 54, 90, 22, data->font);
            data->title_edit = CreateDialogControl(
                hwnd,
                L"EDIT",
                data->node.title.c_str(),
                WS_TABSTOP | ES_AUTOHSCROLL | WS_BORDER,
                kNodeEditTitle,
                108,
                50,
                350,
                26,
                data->font);
            CreateDialogControl(hwnd, L"STATIC", L"URL", SS_LEFT, 0, 18, 92, 90, 22, data->font);
            data->url_edit = CreateDialogControl(
                hwnd,
                L"EDIT",
                data->node.url.c_str(),
                WS_TABSTOP | ES_AUTOHSCROLL | WS_BORDER,
                kNodeEditUrl,
                108,
                88,
                350,
                26,
                data->font);
            CreateDialogControl(hwnd, L"STATIC", L"Shape", SS_LEFT, 0, 18, 132, 90, 22, data->font);
            data->shape_combo = CreateDialogControl(
                hwnd,
                L"COMBOBOX",
                L"",
                WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                kNodeEditShape,
                108,
                128,
                160,
                120,
                data->font);
            CreateDialogControl(hwnd, L"STATIC", L"Color", SS_LEFT, 0, 18, 172, 90, 22, data->font);
            data->color_combo = CreateDialogControl(
                hwnd,
                L"COMBOBOX",
                L"",
                WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                kNodeEditColor,
                108,
                168,
                160,
                130,
                data->font);

            const wchar_t* shapes[] = {L"hex", L"cube", L"panel"};
            for (const wchar_t* shape : shapes) {
                SendMessageW(data->shape_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(shape));
            }
            SendMessageW(data->shape_combo, CB_SETCURSEL, ShapeIndex(data->node.shape_type), 0);

            const wchar_t* colors[] = {L"green", L"yellow", L"red", L"mixed"};
            for (const wchar_t* color : colors) {
                SendMessageW(data->color_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(color));
            }
            SendMessageW(data->color_combo, CB_SETCURSEL, ColorIndex(data->node.color_theme), 0);

            CreateDialogControl(
                hwnd,
                L"BUTTON",
                L"SAVE",
                WS_TABSTOP | BS_PUSHBUTTON,
                kNodeEditSave,
                250,
                218,
                92,
                32,
                data->font);
            CreateDialogControl(
                hwnd,
                L"BUTTON",
                L"CANCEL",
                WS_TABSTOP | BS_PUSHBUTTON,
                kNodeEditCancel,
                360,
                218,
                98,
                32,
                data->font);
            SetFocus(data->title_edit);
            return 0;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(w_param);
            SetBkColor(dc, RGB(0, 0, 0));
            SetTextColor(dc, RGB(0, 255, 0));
            return reinterpret_cast<LRESULT>(data != nullptr ? data->background : GetStockObject(BLACK_BRUSH));
        }
        case WM_COMMAND:
            if (LOWORD(w_param) == kNodeEditSave && data != nullptr) {
                data->node.title = Trim(GetControlText(data->title_edit));
                data->node.url = Trim(GetControlText(data->url_edit));
                data->node.shape_type = ShapeFromIndex(static_cast<int>(SendMessageW(data->shape_combo, CB_GETCURSEL, 0, 0)));
                data->node.color_theme = ColorFromIndex(static_cast<int>(SendMessageW(data->color_combo, CB_GETCURSEL, 0, 0)));
                data->saved = true;
                DestroyWindow(hwnd);
                return 0;
            }
            if (LOWORD(w_param) == kNodeEditCancel) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
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

std::optional<deck::BookmarkNode> ShowNodeEditDialog(HWND owner, deck::BookmarkNode node) {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = NodeEditDialogProc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = kNodeEditDialogClassName;
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassExW(&window_class);

    RECT owner_rect{0, 0, 0, 0};
    if (owner != nullptr) {
        GetWindowRect(owner, &owner_rect);
    }
    constexpr int width = 500;
    constexpr int height = 300;
    const int x = owner != nullptr ? owner_rect.left + ((owner_rect.right - owner_rect.left) - width) / 2 : CW_USEDEFAULT;
    const int y = owner != nullptr ? owner_rect.top + ((owner_rect.bottom - owner_rect.top) - height) / 2 : CW_USEDEFAULT;

    NodeEditDialogData data{.node = std::move(node)};
    HWND dialog = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kNodeEditDialogClassName,
        L"CyberDeck Edit Node",
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
        return std::nullopt;
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
    return data.saved ? std::optional<deck::BookmarkNode>{data.node} : std::nullopt;
}

LRESULT CALLBACK VaultEditDialogProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* data = reinterpret_cast<VaultEditDialogData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (message) {
        case WM_NCCREATE: {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
            data = static_cast<VaultEditDialogData*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));
            return TRUE;
        }
        case WM_CREATE: {
            if (data == nullptr) {
                return -1;
            }
            data->font = CreateFontW(
                -16,
                0,
                0,
                0,
                FW_MEDIUM,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                FIXED_PITCH | FF_MODERN,
                L"Cascadia Mono");
            if (data->font == nullptr) {
                data->font = CreateFontW(
                    -16,
                    0,
                    0,
                    0,
                    FW_MEDIUM,
                    FALSE,
                    FALSE,
                    FALSE,
                    DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY,
                    FIXED_PITCH | FF_MODERN,
                    L"Consolas");
            }
            data->background = CreateSolidBrush(RGB(0, 0, 0));

            CreateDialogControl(hwnd, L"STATIC", L"RENAME DECK VAULT", SS_LEFT, 0, 18, 16, 360, 24, data->font);
            CreateDialogControl(hwnd, L"STATIC", L"Name", SS_LEFT, 0, 18, 58, 80, 22, data->font);
            data->name_edit = CreateDialogControl(
                hwnd,
                L"EDIT",
                data->vault.name.c_str(),
                WS_TABSTOP | ES_AUTOHSCROLL | WS_BORDER,
                kVaultEditName,
                104,
                54,
                310,
                26,
                data->font);
            CreateDialogControl(hwnd, L"STATIC", L"Color", SS_LEFT, 0, 18, 98, 80, 22, data->font);
            data->color_combo = CreateDialogControl(
                hwnd,
                L"COMBOBOX",
                L"",
                WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                kVaultEditColor,
                104,
                94,
                160,
                130,
                data->font);
            const wchar_t* colors[] = {L"green", L"yellow", L"red", L"mixed"};
            for (const wchar_t* color : colors) {
                SendMessageW(data->color_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(color));
            }
            SendMessageW(data->color_combo, CB_SETCURSEL, ColorIndex(data->vault.color_theme), 0);

            CreateDialogControl(
                hwnd,
                L"BUTTON",
                L"SAVE",
                WS_TABSTOP | BS_PUSHBUTTON,
                kVaultEditSave,
                214,
                148,
                92,
                32,
                data->font);
            CreateDialogControl(
                hwnd,
                L"BUTTON",
                L"CANCEL",
                WS_TABSTOP | BS_PUSHBUTTON,
                kVaultEditCancel,
                322,
                148,
                92,
                32,
                data->font);
            SetFocus(data->name_edit);
            return 0;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(w_param);
            SetBkColor(dc, RGB(0, 0, 0));
            SetTextColor(dc, RGB(0, 255, 0));
            return reinterpret_cast<LRESULT>(data != nullptr ? data->background : GetStockObject(BLACK_BRUSH));
        }
        case WM_COMMAND:
            if (LOWORD(w_param) == kVaultEditSave && data != nullptr) {
                data->vault.name = Trim(GetControlText(data->name_edit));
                data->vault.color_theme = ColorFromIndex(static_cast<int>(SendMessageW(data->color_combo, CB_GETCURSEL, 0, 0)));
                data->saved = true;
                DestroyWindow(hwnd);
                return 0;
            }
            if (LOWORD(w_param) == kVaultEditCancel) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
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

std::optional<deck::BookmarkVault> ShowVaultEditDialog(HWND owner, deck::BookmarkVault vault) {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = VaultEditDialogProc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = kVaultEditDialogClassName;
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassExW(&window_class);

    RECT owner_rect{0, 0, 0, 0};
    if (owner != nullptr) {
        GetWindowRect(owner, &owner_rect);
    }
    constexpr int width = 456;
    constexpr int height = 232;
    const int x = owner != nullptr ? owner_rect.left + ((owner_rect.right - owner_rect.left) - width) / 2 : CW_USEDEFAULT;
    const int y = owner != nullptr ? owner_rect.top + ((owner_rect.bottom - owner_rect.top) - height) / 2 : CW_USEDEFAULT;

    VaultEditDialogData data{.vault = std::move(vault)};
    HWND dialog = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kVaultEditDialogClassName,
        L"CyberDeck Vault",
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
        return std::nullopt;
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
    return data.saved ? std::optional<deck::BookmarkVault>{data.vault} : std::nullopt;
}

}  // namespace

Application::Application(HINSTANCE instance, int show_command, std::wstring initial_url)
    : instance_(instance), show_command_(show_command), initial_url_(std::move(initial_url)) {}

Application::~Application() {
    deck_space_.Shutdown();
    browser_host_.Shutdown();
}

void Application::PersistSettings() {
    if (!settings_store_.Save(current_settings_)) {
        logger_.Error("Failed to persist settings.");
    }
}

void Application::ShowSettingsPanel() const {
    std::wstring text;
    const render::OpenGLDiagnostics gl = deck_space_.Diagnostics();
    text += std::wstring(common::AppName()) + L"\n";
    text += L"version: " + std::wstring(common::AppVersion());
    text += L"\nCEF: " + browser_host_.CefVersionText();
    text += L"\nOpenGL vendor: " + WideFromAscii(gl.vendor);
    text += L"\nOpenGL renderer: " + WideFromAscii(gl.renderer);
    text += L"\nOpenGL version: " + WideFromAscii(gl.version);
    text += L"\n\ndata directory:\n";
    text += settings_store_.path().parent_path().wstring();
    text += L"\n\nlog file:\n";
    text += logger_.path().wstring();
    text += L"\n\nsettings.json:\n";
    text += settings_store_.path().wstring();
    text += L"\n\nterminalModeEnabled: " + BoolText(current_settings_.terminal_mode_enabled);
    text += L"\nscanlinesEnabled: " + BoolText(current_settings_.scanlines_enabled);
    text += L"\nglowEnabled: " + BoolText(current_settings_.glow_enabled);
    text += L"\nflickerIntensity: " + std::to_wstring(current_settings_.flicker_intensity);
    text += L"\nkeepDeckOpenAfterNodeOpen: " + BoolText(current_settings_.keep_deck_open_after_node_open);
    text += L"\ndeckLayoutMode: " + current_settings_.deck_layout_mode;
    text += L"\nhomepage: " + current_settings_.homepage;
    text += L"\nsearchEngineUrl: " + current_settings_.search_engine_url;

    MessageBoxW(main_window_.hwnd(), text.c_str(), L"CyberDeck Diagnostics", MB_OK | MB_ICONINFORMATION);
}

std::vector<deck::BookmarkNode> Application::LoadBookmarksWithFavicons() {
    std::vector<deck::BookmarkNode> nodes = bookmark_store_.LoadBookmarks();
    bool changed = false;
    for (deck::BookmarkNode& node : nodes) {
        const bool had_favicon = node.favicon_path && !node.favicon_path->empty();
        EnsureNodeFavicon(node);
        changed = changed || (!had_favicon && node.favicon_path && !node.favicon_path->empty());
    }
    if (changed && !bookmark_store_.SaveBookmarks(nodes)) {
        logger_.Error("Failed to persist generated placeholder favicons for Deck Nodes.");
    }
    return nodes;
}

void Application::RefreshDeckSpaceBookmarks() {
    deck_space_.SetBookmarkData(LoadBookmarksWithFavicons(), bookmark_store_.LoadVaults());
}

void Application::AddNodeFromCurrentTab() {
    const std::vector<browser::BrowserTabState> tabs = browser_host_.Tabs();
    const browser::BrowserTabState* active_tab = ActiveBrowserTab(tabs, browser_host_.ActiveTabId());
    if (active_tab == nullptr || active_tab->url.empty()) {
        MessageBoxW(
            main_window_.hwnd(),
            L"There is no active website to save as a Deck Node yet.",
            L"Add Node",
            MB_OK | MB_ICONWARNING);
        main_window_.SetDownloadStatus(L"ADD NODE NEEDS A WEBSITE");
        return;
    }

    if (StartsWith(active_tab->url, L"about:") || active_tab->title == L"CyberDeck Load Failure") {
        MessageBoxW(
            main_window_.hwnd(),
            L"This page is an internal or error page, so it cannot be saved as a Deck Node.",
            L"Add Node",
            MB_OK | MB_ICONWARNING);
        main_window_.SetDownloadStatus(L"ADD NODE BLOCKED FOR INTERNAL PAGE");
        return;
    }

    const std::vector<deck::BookmarkNode> current_nodes = bookmark_store_.LoadBookmarks();
    const std::string now = deck::CurrentBookmarkNodeUtcTimestamp();
    int sequence = static_cast<int>(current_nodes.size()) + 1;
    std::wstring node_id;
    do {
        node_id = deck::GenerateBookmarkNodeId(now, sequence++);
    } while (std::any_of(current_nodes.begin(), current_nodes.end(), [&node_id](const deck::BookmarkNode& node) {
        return node.id == node_id;
    }));

    auto created = deck::CreateBookmarkNodeFromActiveTab(*active_tab, node_id, now);
    if (!created.success) {
        const std::wstring message = created.message.empty()
                                         ? L"This URL is not safe to save as a Deck Node."
                                         : created.message;
        MessageBoxW(main_window_.hwnd(), message.c_str(), L"Add Node", MB_OK | MB_ICONWARNING);
        main_window_.SetDownloadStatus(L"ADD NODE BLOCKED");
        return;
    }
    created.node.color_theme = deck::BookmarkNodeColorTheme::Mixed;
    if (const auto active_vault_id = deck_space_.ActiveVaultId()) {
        created.node.vault_id = *active_vault_id;
    }
    EnsureNodeFavicon(created.node);

    auto duplicate = std::find_if(current_nodes.begin(), current_nodes.end(), [&created](const deck::BookmarkNode& node) {
        return node.url == created.node.url;
    });
    if (duplicate != current_nodes.end()) {
        const std::wstring prompt =
            L"This URL is already a Deck Node.\n\n"
            L"Yes: update the existing Node title\n"
            L"No: create a duplicate Node anyway\n"
            L"Cancel: leave Deck Space unchanged";
        const int response = MessageBoxW(
            main_window_.hwnd(),
            prompt.c_str(),
            L"Add Node",
            MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON3);
        if (response == IDCANCEL) {
            main_window_.SetDownloadStatus(L"ADD NODE CANCELED");
            return;
        }
        if (response == IDYES) {
            deck::BookmarkNode updated = *duplicate;
            updated.title = created.node.title;
            if (!updated.favicon_path && created.node.favicon_path) {
                updated.favicon_path = created.node.favicon_path;
            }
            updated.updated_utc = now;
            if (bookmark_store_.UpdateBookmark(std::move(updated))) {
                logger_.Info("Updated existing Deck Node title from active tab.");
                RefreshDeckSpaceBookmarks();
                main_window_.SetDownloadStatus(L"NODE UPDATED IN DECK");
            } else {
                logger_.Error("Failed to update existing Deck Node.");
                main_window_.SetDownloadStatus(L"NODE UPDATE FAILED");
                MessageBoxW(
                    main_window_.hwnd(),
                    L"CyberDeck could not update that Node on disk.",
                    L"Add Node",
                    MB_OK | MB_ICONERROR);
            }
            return;
        }
    }

    if (bookmark_store_.AddBookmark(std::move(created.node))) {
        logger_.Info("Added Deck Node from active tab.");
        RefreshDeckSpaceBookmarks();
        main_window_.SetDownloadStatus(L"NODE ADDED TO DECK");
        return;
    }

    logger_.Error("Failed to add Deck Node from active tab.");
    main_window_.SetDownloadStatus(L"NODE ADD FAILED");
    MessageBoxW(
        main_window_.hwnd(),
        L"CyberDeck could not save that Node to bookmarks.json.",
        L"Add Node",
        MB_OK | MB_ICONERROR);
}

void Application::EnsureNodeFavicon(deck::BookmarkNode& node) {
    if (node.favicon_path && !node.favicon_path->empty()) {
        return;
    }

    const auto favicon_path = deck::FaviconStore::EnsurePlaceholderFavicon(
        deck::FaviconStore::DefaultFaviconsDirectory(),
        node.url,
        node.title);
    if (favicon_path) {
        node.favicon_path = *favicon_path;
    } else {
        logger_.Error("Failed to create placeholder favicon for Deck Node.");
    }
}

void Application::OpenDeckNode(deck::BookmarkNode node) {
    if (node.url.empty()) {
        main_window_.SetDownloadStatus(L"NODE OPEN FAILED: EMPTY URL");
        return;
    }

    const std::string now = deck::CurrentBookmarkNodeUtcTimestamp();
    node.last_visited_utc = now;
    node.updated_utc = now;
    ++node.visit_count;
    if (!bookmark_store_.UpdateBookmark(node)) {
        logger_.Error("Failed to update Deck Node visit metadata before opening.");
    }
    RefreshDeckSpaceBookmarks();

    if (!current_settings_.keep_deck_open_after_node_open) {
        deck_space_.Exit();
        main_window_.SetDeckSpaceEnabled(false);
    }

    if (browser_host_.CreateTab(node.url)) {
        main_window_.SetTabs(ToUiTabs(browser_host_.Tabs()), browser_host_.ActiveTabId());
        main_window_.SetUrlText(node.url);
        main_window_.SetDownloadStatus(L"NODE OPENED IN NEW TAB");
        logger_.Info("Opened Deck Node in new tab.");
    } else {
        main_window_.SetDownloadStatus(L"NODE OPEN FAILED");
        logger_.Error("Failed to open Deck Node in new tab.");
    }
}

void Application::EditDeckNode(deck::BookmarkNode node) {
    const auto edited = ShowNodeEditDialog(main_window_.hwnd(), std::move(node));
    if (!edited) {
        main_window_.SetDownloadStatus(L"NODE EDIT CANCELED");
        return;
    }

    deck::BookmarkNode updated = *edited;
    const std::optional<std::wstring> normalized_url = deck::NormalizeBookmarkNodeUrl(updated.url);
    if (!normalized_url) {
        MessageBoxW(
            main_window_.hwnd(),
            L"That URL is not safe for a Deck Node. Use a normal http or https website URL.",
            L"Edit Node",
            MB_OK | MB_ICONWARNING);
        main_window_.SetDownloadStatus(L"NODE EDIT BLOCKED: BAD URL");
        return;
    }

    updated.url = *normalized_url;
    EnsureNodeFavicon(updated);
    updated.updated_utc = deck::CurrentBookmarkNodeUtcTimestamp();
    const deck::BookmarkNodeValidationResult validation = deck::ValidateBookmarkNode(updated);
    if (!validation.valid) {
        const std::wstring message = validation.message.empty() ? L"That Node edit is not valid." : validation.message;
        MessageBoxW(main_window_.hwnd(), message.c_str(), L"Edit Node", MB_OK | MB_ICONWARNING);
        main_window_.SetDownloadStatus(L"NODE EDIT BLOCKED");
        return;
    }

    if (bookmark_store_.UpdateBookmark(std::move(updated))) {
        RefreshDeckSpaceBookmarks();
        main_window_.SetDownloadStatus(L"NODE UPDATED IN DECK");
        logger_.Info("Deck Node edited.");
    } else {
        main_window_.SetDownloadStatus(L"NODE EDIT FAILED");
        logger_.Error("Failed to persist edited Deck Node.");
        MessageBoxW(main_window_.hwnd(), L"CyberDeck could not save the edited Node.", L"Edit Node", MB_OK | MB_ICONERROR);
    }
}

void Application::DeleteDeckNode(deck::BookmarkNode node) {
    const std::wstring prompt =
        L"DANGER: DELETE DECK NODE\n\n" + node.title + L"\n" + node.url +
        L"\n\nThis removes the Node from bookmarks.json immediately.";
    const int response = MessageBoxW(
        main_window_.hwnd(),
        prompt.c_str(),
        L"Delete Node",
        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (response != IDYES) {
        main_window_.SetDownloadStatus(L"NODE DELETE CANCELED");
        return;
    }

    if (bookmark_store_.DeleteBookmark(node.id)) {
        RefreshDeckSpaceBookmarks();
        main_window_.SetDownloadStatus(L"NODE DELETED FROM DECK");
        logger_.Info("Deck Node deleted.");
    } else {
        main_window_.SetDownloadStatus(L"NODE DELETE FAILED");
        logger_.Error("Failed to delete Deck Node.");
        MessageBoxW(main_window_.hwnd(), L"CyberDeck could not delete that Node.", L"Delete Node", MB_OK | MB_ICONERROR);
    }
}

void Application::EditDeckVault(deck::BookmarkVault vault) {
    const auto edited = ShowVaultEditDialog(main_window_.hwnd(), std::move(vault));
    if (!edited) {
        main_window_.SetDownloadStatus(L"VAULT RENAME CANCELED");
        return;
    }

    deck::BookmarkVault updated = *edited;
    if (Trim(updated.name).empty()) {
        MessageBoxW(main_window_.hwnd(), L"Vault name must not be empty.", L"Deck Vault", MB_OK | MB_ICONWARNING);
        main_window_.SetDownloadStatus(L"VAULT RENAME BLOCKED");
        return;
    }

    updated.updated_utc = deck::CurrentBookmarkNodeUtcTimestamp();
    if (bookmark_store_.UpdateVault(std::move(updated))) {
        RefreshDeckSpaceBookmarks();
        main_window_.SetDownloadStatus(L"VAULT UPDATED");
        logger_.Info("Deck Vault renamed.");
    } else {
        main_window_.SetDownloadStatus(L"VAULT UPDATE FAILED");
        logger_.Error("Failed to persist edited Deck Vault.");
        MessageBoxW(main_window_.hwnd(), L"CyberDeck could not save the edited Vault.", L"Deck Vault", MB_OK | MB_ICONERROR);
    }
}

void Application::DeleteDeckVault(deck::BookmarkVault vault) {
    std::size_t child_count = 0;
    for (const deck::BookmarkNode& node : bookmark_store_.LoadBookmarks()) {
        if (node.vault_id && *node.vault_id == vault.id) {
            ++child_count;
        }
    }

    const std::wstring prompt =
        L"DELETE DECK VAULT\n\n" + vault.name + L"\n\nThis removes the Vault shell. " +
        std::to_wstring(child_count) + L" Nodes will move to the next available Vault, or stay as loose Nodes if this is the last Vault.";
    const int response = MessageBoxW(
        main_window_.hwnd(),
        prompt.c_str(),
        L"Delete Vault",
        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (response != IDYES) {
        main_window_.SetDownloadStatus(L"VAULT DELETE CANCELED");
        return;
    }

    if (bookmark_store_.DeleteVault(vault.id)) {
        RefreshDeckSpaceBookmarks();
        main_window_.SetDownloadStatus(L"VAULT DELETED; NODES KEPT");
        logger_.Info("Deck Vault deleted while keeping child Nodes.");
    } else {
        main_window_.SetDownloadStatus(L"VAULT DELETE FAILED");
        logger_.Error("Failed to delete Deck Vault.");
        MessageBoxW(main_window_.hwnd(), L"CyberDeck could not delete that Vault.", L"Delete Vault", MB_OK | MB_ICONERROR);
    }
}

int Application::Run() {
    const bool logging_ready = logger_.Initialize(common::Logger::DefaultLogPath());

    const auto browser_startup = browser_host_.Initialize(instance_, logger_);
    if (browser_startup.should_exit) {
        return browser_startup.exit_code;
    }

    if (!browser_startup.success) {
        logger_.Error("Browser host initialization failed.");
        return 1;
    }

    if (logging_ready) {
        logger_.Info("CyberDeck Browser startup.");
    }

    logger_.Info("Browser host initialized.");

    if (settings_store_.Initialize(settings::SettingsStore::DefaultSettingsPath(), logger_)) {
        current_settings_ = settings_store_.Settings();
    } else {
        current_settings_ = settings::UserSettings{};
        logger_.Error("Settings store initialization failed; defaults will be used for this run.");
    }

    if (!history_store_.Initialize(history::HistoryStore::DefaultHistoryPath(), logger_)) {
        logger_.Error("History store initialization failed; browsing will continue without history persistence.");
    }

    if (!bookmark_store_.Initialize(deck::BookmarkStore::DefaultBookmarksPath(), logger_)) {
        logger_.Error("Bookmark store initialization failed; Deck Nodes will not persist this run.");
    }

    browser_host_.SetTerminalModeEnabled(current_settings_.terminal_mode_enabled);
    main_window_.SetTerminalModeEnabled(current_settings_.terminal_mode_enabled);
    main_window_.SetScanlinesEnabled(current_settings_.scanlines_enabled);
    main_window_.SetGlowEnabled(current_settings_.glow_enabled);
    main_window_.SetFlickerIntensity(current_settings_.flicker_intensity);
    deck_space_.SetLayoutMode(render::DeckLayoutModeFromString(NarrowAscii(current_settings_.deck_layout_mode)));

    browser_host_.SetTabsChangedCallback([this](const std::vector<browser::BrowserTabState>& tabs, int active_tab_id) {
        main_window_.SetTabs(ToUiTabs(tabs), active_tab_id);
    });

    browser_host_.SetSuccessfulNavigationCallback([this](const browser::BrowserTabState& tab) {
        if (history_store_.RecordVisit(tab.title, tab.url)) {
            logger_.Info("Recorded history visit.");
        }
    });

    browser_host_.SetDownloadStatusCallback([this](const browser::DownloadStatus& status) {
        main_window_.SetDownloadStatus(status.message);
    });

    browser_host_.SetPermissionStatusCallback([this](const browser::PermissionStatus& status) {
        main_window_.SetDownloadStatus(status.message);
    });

    main_window_.SetContentResizedCallback([this](const RECT& bounds) {
        browser_host_.Resize(bounds);
        deck_space_.Resize(bounds);
    });

    auto refresh_tabs = [this]() {
        main_window_.SetTabs(ToUiTabs(browser_host_.Tabs()), browser_host_.ActiveTabId());
    };

    auto exit_deck_space = [this]() {
        deck_space_.Exit();
        main_window_.SetDeckSpaceEnabled(false);
        main_window_.SetDownloadStatus(L"BROWSER MODE ACTIVE");
    };

    main_window_.SetToolbarCallbacks({
        .back = [this, refresh_tabs]() {
            browser_host_.GoBack();
            refresh_tabs();
        },
        .forward = [this, refresh_tabs]() {
            browser_host_.GoForward();
            refresh_tabs();
        },
        .reload = [this, refresh_tabs]() {
            browser_host_.Reload();
            refresh_tabs();
        },
        .stop = [this, refresh_tabs]() {
            browser_host_.Stop();
            refresh_tabs();
        },
        .terminal_mode = [this](bool enabled) {
            current_settings_.terminal_mode_enabled = enabled;
            PersistSettings();
            browser_host_.SetTerminalModeEnabled(enabled);
            main_window_.SetTerminalModeEnabled(browser_host_.TerminalModeEnabled());
            main_window_.SetDownloadStatus(
                browser_host_.TerminalModeEnabled() ? L"TERMINAL MODE ENABLED" : L"TERMINAL MODE DISABLED");
        },
        .scanlines = [this](bool enabled) {
            current_settings_.scanlines_enabled = enabled;
            PersistSettings();
        },
        .glow = [this](bool enabled) {
            current_settings_.glow_enabled = enabled;
            PersistSettings();
        },
        .flicker_intensity = [this](int intensity) {
            current_settings_.flicker_intensity = intensity;
            PersistSettings();
        },
        .settings_panel = [this]() {
            ShowSettingsPanel();
        },
        .deck_space = [this, exit_deck_space](bool enabled) {
            if (enabled) {
                deck_space_.SetLayoutMode(render::DeckLayoutModeFromString(NarrowAscii(current_settings_.deck_layout_mode)));
                RefreshDeckSpaceBookmarks();
                if (deck_space_.Enter(main_window_.ContentBounds())) {
                    main_window_.SetDeckSpaceEnabled(true);
                    main_window_.SetDownloadStatus(L"DECK SPACE ACTIVE");
                } else {
                    main_window_.SetDeckSpaceEnabled(false);
                    const std::wstring error =
                        deck_space_.LastError().empty() ? L"DECK SPACE OPENGL INIT FAILED" : deck_space_.LastError();
                    main_window_.SetDownloadStatus(error);
                    MessageBoxW(main_window_.hwnd(), error.c_str(), L"Deck Space", MB_OK | MB_ICONERROR);
                }
            } else {
                exit_deck_space();
            }
        },
        .add_node = [this]() {
            AddNodeFromCurrentTab();
        },
        .navigate = [this, refresh_tabs](const std::wstring& url) {
            logger_.Info("Toolbar navigation requested.");
            const browser::NormalizedNavigation normalized = browser::NormalizeAddressBarInput(url);
            browser_host_.Navigate(url);
            refresh_tabs();
            if (normalized.decision == browser::NavigationDecision::kNavigate) {
                main_window_.SetUrlText(normalized.target_url);
            }
        },
    });

    main_window_.SetTabCallbacks({
        .new_tab = [this, refresh_tabs]() {
            browser_host_.CreateTab();
            refresh_tabs();
        },
        .activate_tab = [this, refresh_tabs](int tab_id) {
            browser_host_.ActivateTab(tab_id);
            refresh_tabs();
        },
        .close_tab = [this, refresh_tabs](int tab_id) {
            browser_host_.CloseTab(tab_id);
            refresh_tabs();
        },
    });

    if (!main_window_.Create(instance_, show_command_)) {
        logger_.Error("Main window creation failed.");
        browser_host_.Shutdown();
        return 1;
    }

    logger_.Info("Main window created.");
    if (!deck_space_.Initialize(main_window_.hwnd(), main_window_.ContentBounds(), logger_)) {
        main_window_.SetDownloadStatus(L"DECK SPACE OPENGL INIT FAILED");
    } else {
        deck_space_.SetExitRequestedCallback(exit_deck_space);
        deck_space_.SetOpenNodeCallback([this](deck::BookmarkNode node) {
            OpenDeckNode(std::move(node));
        });
        deck_space_.SetEditNodeCallback([this](deck::BookmarkNode node) {
            EditDeckNode(std::move(node));
        });
        deck_space_.SetDeleteNodeCallback([this](deck::BookmarkNode node) {
            DeleteDeckNode(std::move(node));
        });
        deck_space_.SetEditVaultCallback([this](deck::BookmarkVault vault) {
            EditDeckVault(std::move(vault));
        });
        deck_space_.SetDeleteVaultCallback([this](deck::BookmarkVault vault) {
            DeleteDeckVault(std::move(vault));
        });
        deck_space_.SetLayoutChangedCallback([this](render::DeckLayoutMode mode) {
            current_settings_.deck_layout_mode = WideFromAscii(render::ToLayoutModeString(mode));
            PersistSettings();
            main_window_.SetDownloadStatus(L"DECK LAYOUT: " + current_settings_.deck_layout_mode);
        });
    }

    const std::wstring initial_url = initial_url_.empty() ? current_settings_.homepage : initial_url_;
    main_window_.SetUrlText(initial_url);
    if (browser_host_.CreateInitialTab(main_window_.hwnd(), main_window_.ContentBounds(), initial_url)) {
        logger_.Info("Initial tab requested.");
        refresh_tabs();
    } else {
        logger_.Error("Initial tab creation failed.");
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    logger_.Info("Message loop ended.");
    deck_space_.Shutdown();
    browser_host_.Shutdown();
    logger_.Info("CyberDeck Browser shutdown.");
    return static_cast<int>(message.wParam);
}

}  // namespace cyberdeck::app
