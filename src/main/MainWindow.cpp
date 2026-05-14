#include "main/MainWindow.h"

#include "common/AppInfo.h"
#include "ui/Theme.h"

#include <windowsx.h>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace cyberdeck::main {
namespace {

constexpr wchar_t kWindowClassName[] = L"CyberDeckBrowserMainWindow";
constexpr int kInitialWidth = 1500;
constexpr int kInitialHeight = 800;
constexpr int kMinimumWidth = 1320;
constexpr int kMinimumHeight = 760;
constexpr int kToolbarHeight = 74;
constexpr int kTabStripHeight = 52;
constexpr int kToolbarMargin = 12;
constexpr int kToolbarGap = 7;
constexpr int kButtonWidth = 76;
constexpr int kTerminalButtonWidth = 90;
constexpr int kEffectButtonWidth = 72;
constexpr int kSettingsButtonWidth = 68;
constexpr int kDeckButtonWidth = 78;
constexpr int kAddNodeButtonWidth = 120;
constexpr int kGoButtonWidth = 68;
constexpr int kControlHeight = 46;
constexpr int kUrlMinimumWidth = 260;
constexpr int kTabWidth = 250;
constexpr int kTabHeight = 40;
constexpr int kTabTop = kToolbarHeight + 6;
constexpr int kTabCloseSize = 24;
constexpr int kNewTabWidth = 48;
constexpr UINT_PTR kLoadingTimerId = 1;
constexpr UINT kLoadingTimerMs = 160;
constexpr UINT kApplyTabsMessage = WM_APP + 1;
constexpr UINT kApplyDownloadStatusMessage = WM_APP + 2;
constexpr COLORREF kBlack = cyberdeck::ui::Theme::black;
constexpr COLORREF kToolbarBlack = cyberdeck::ui::Theme::dark_panel;
constexpr COLORREF kTabBlack = cyberdeck::ui::Theme::darker_panel;
constexpr COLORREF kGreen = cyberdeck::ui::Theme::green;
constexpr COLORREF kGreenDim = cyberdeck::ui::Theme::dim_green;
constexpr COLORREF kGreenFaint = cyberdeck::ui::Theme::faint_green;
constexpr COLORREF kYellow = cyberdeck::ui::Theme::yellow;
constexpr COLORREF kRed = cyberdeck::ui::Theme::red;
constexpr COLORREF kRedDim = cyberdeck::ui::Theme::red_dim;

enum ToolbarCommand : int {
    kCommandBack = 1001,
    kCommandForward = 1002,
    kCommandReload = 1003,
    kCommandStop = 1004,
    kCommandTerminalMode = 1005,
    kCommandScanlines = 1006,
    kCommandGlow = 1007,
    kCommandFlicker = 1008,
    kCommandSettings = 1009,
    kCommandDeckSpace = 1010,
    kCommandUrlEdit = 1011,
    kCommandGo = 1012,
    kCommandNewTab = 1013,
    kCommandAddNode = 1014,
    kCommandTabBase = 20000,
    kCommandCloseTabBase = 21000,
};

class ScopedSelectObject {
public:
    ScopedSelectObject(HDC dc, HGDIOBJ object) : dc_(dc) {
        if (dc_ != nullptr && object != nullptr) {
            HGDIOBJ selected = SelectObject(dc_, object);
            if (selected != nullptr && selected != HGDI_ERROR) {
                previous_ = selected;
            }
        }
    }

    ~ScopedSelectObject() {
        if (dc_ != nullptr && previous_ != nullptr) {
            SelectObject(dc_, previous_);
        }
    }

    ScopedSelectObject(const ScopedSelectObject&) = delete;
    ScopedSelectObject& operator=(const ScopedSelectObject&) = delete;

private:
    HDC dc_ = nullptr;
    HGDIOBJ previous_ = nullptr;
};

std::wstring_view ButtonLabel(int control_id) {
    switch (control_id) {
        case kCommandBack:
            return L"BACK";
        case kCommandForward:
            return L"FWD";
        case kCommandReload:
            return L"RELOAD";
        case kCommandStop:
            return L"STOP";
        case kCommandTerminalMode:
            return L"TERM";
        case kCommandScanlines:
            return L"SCAN";
        case kCommandGlow:
            return L"GLOW";
        case kCommandFlicker:
            return L"FLK";
        case kCommandSettings:
            return L"SET";
        case kCommandDeckSpace:
            return L"DECK";
        case kCommandAddNode:
            return L"ADD NODE";
        case kCommandGo:
            return L"GO";
        default:
            return L"";
    }
}

COLORREF ButtonAccent(int control_id) {
    if (control_id == kCommandStop) {
        return kRed;
    }
    if (control_id == kCommandGo) {
        return kYellow;
    }
    if (control_id == kCommandTerminalMode || control_id == kCommandScanlines || control_id == kCommandGlow ||
        control_id == kCommandFlicker || control_id == kCommandSettings || control_id == kCommandDeckSpace ||
        control_id == kCommandAddNode) {
        return kYellow;
    }
    return kGreen;
}

bool IsCrtEffectCommand(int control_id) {
    return control_id == kCommandScanlines || control_id == kCommandGlow || control_id == kCommandFlicker;
}

const UiTabState* FindTabById(const std::vector<UiTabState>& tabs, int tab_id) {
    const auto found = std::find_if(tabs.begin(), tabs.end(), [tab_id](const UiTabState& tab) {
        return tab.id == tab_id;
    });
    return found == tabs.end() ? nullptr : &*found;
}

const UiTabState* ActiveTab(const std::vector<UiTabState>& tabs, int active_tab_id) {
    return FindTabById(tabs, active_tab_id);
}

std::wstring CompactTabTitle(const UiTabState& tab) {
    std::wstring title = tab.title.empty() ? tab.url : tab.title;
    if (title.empty()) {
        title = L"New Tab";
    }

    constexpr std::size_t kMaximumTitleLength = 22;
    if (title.size() > kMaximumTitleLength) {
        title.resize(kMaximumTitleLength - 3);
        title += L"...";
    }

    return title;
}

bool AnyTabLoading(const std::vector<UiTabState>& tabs) {
    return std::any_of(tabs.begin(), tabs.end(), [](const UiTabState& tab) {
        return tab.loading;
    });
}

std::wstring LoadingPrefix(int frame) {
    constexpr std::wstring_view frames[] = {L"[|] ", L"[/] ", L"[-] ", L"[\\] "};
    return std::wstring(frames[static_cast<std::size_t>(frame) % std::size(frames)]);
}

void DrawHorizontalLine(HDC dc, int y, int width, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    if (pen == nullptr) {
        return;
    }

    HGDIOBJ previous_pen = SelectObject(dc, pen);
    MoveToEx(dc, 0, y, nullptr);
    LineTo(dc, width, y);
    SelectObject(dc, previous_pen);
    DeleteObject(pen);
}

void Fill(HDC dc, const RECT& rect, COLORREF color) {
    cyberdeck::ui::FillRectColor(dc, rect, color);
}

void FrameRectWithPen(HDC dc, const RECT& rect, COLORREF color, int width = 1) {
    cyberdeck::ui::FrameRectColor(dc, rect, color, width);
}

}  // namespace

MainWindow::~MainWindow() {
    if (toolbar_font_ != nullptr) {
        DeleteObject(toolbar_font_);
    }
    if (edit_background_brush_ != nullptr) {
        DeleteObject(edit_background_brush_);
    }
}

bool MainWindow::Create(HINSTANCE instance, int show_command) {
    instance_ = instance;
    ui_thread_id_ = GetCurrentThreadId();

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = MainWindow::WindowProc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = kWindowClassName;

    if (RegisterClassExW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    hwnd_ = CreateWindowExW(
        0,
        kWindowClassName,
        cyberdeck::common::AppName().data(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        kInitialWidth,
        kInitialHeight,
        nullptr,
        nullptr,
        instance,
        this);

    if (hwnd_ == nullptr) {
        return false;
    }
    cyberdeck::ui::ApplyDarkWindowFrame(hwnd_);

    if (!CreateToolbarControls(instance)) {
        DestroyWindow(hwnd_);
        return false;
    }

    LayoutToolbarControls();
    ShowWindow(hwnd_, show_command);
    UpdateWindow(hwnd_);
    return true;
}

RECT MainWindow::ContentBounds() const {
    RECT client{};
    if (hwnd_ == nullptr || !GetClientRect(hwnd_, &client)) {
        return client;
    }

    client.top = std::min<LONG>(client.bottom, kToolbarHeight + kTabStripHeight);
    return client;
}

void MainWindow::SetContentResizedCallback(std::function<void(const RECT&)> callback) {
    content_resized_callback_ = std::move(callback);
}

void MainWindow::SetToolbarCallbacks(ToolbarCallbacks callbacks) {
    toolbar_callbacks_ = std::move(callbacks);
}

void MainWindow::SetTabCallbacks(TabCallbacks callbacks) {
    tab_callbacks_ = std::move(callbacks);
}

void MainWindow::SetTabs(const std::vector<UiTabState>& tabs, int active_tab_id) {
    if (hwnd_ != nullptr && ui_thread_id_ != 0 && GetCurrentThreadId() != ui_thread_id_) {
        {
            std::lock_guard<std::mutex> lock(pending_tabs_mutex_);
            pending_tabs_ = tabs;
            pending_active_tab_id_ = active_tab_id;
        }
        SendMessageW(hwnd_, kApplyTabsMessage, 0, 0);
        return;
    }

    ApplyTabs(tabs, active_tab_id);
}

void MainWindow::ApplyTabs(const std::vector<UiTabState>& tabs, int active_tab_id) {
    const bool active_tab_changed = active_tab_id_ != active_tab_id;
    tabs_ = tabs;
    active_tab_id_ = active_tab_id;
    RebuildTabControls();
    UpdateLoadingTimer();

    const UiTabState* active = ActiveTab(tabs_, active_tab_id_);
    if (active != nullptr) {
        EnableWindow(back_button_, active->can_go_back);
        EnableWindow(forward_button_, active->can_go_forward);
        EnableWindow(reload_button_, !active->loading);
        EnableWindow(stop_button_, active->loading);

        const bool user_is_typing_url = GetFocus() == url_edit_ && url_edit_dirty_;
        const bool can_update_url = active_tab_changed || active->url_committed || !user_is_typing_url;
        if (url_edit_ != nullptr && !active->url.empty() && can_update_url) {
            SetUrlTextInternal(active->url);
        }
    } else {
        EnableWindow(back_button_, FALSE);
        EnableWindow(forward_button_, FALSE);
        EnableWindow(reload_button_, FALSE);
        EnableWindow(stop_button_, FALSE);
    }

    if (hwnd_ != nullptr) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void MainWindow::SetUrlText(const std::wstring& url) {
    SetUrlTextInternal(url);
}

void MainWindow::SetDownloadStatus(const std::wstring& status) {
    if (hwnd_ != nullptr && ui_thread_id_ != 0 && GetCurrentThreadId() != ui_thread_id_) {
        {
            std::lock_guard<std::mutex> lock(pending_download_status_mutex_);
            pending_download_status_ = status;
        }
        SendMessageW(hwnd_, kApplyDownloadStatusMessage, 0, 0);
        return;
    }

    download_status_ = status;
    if (hwnd_ != nullptr) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void MainWindow::SetTerminalModeEnabled(bool enabled) {
    terminal_mode_enabled_ = enabled;
    if (terminal_mode_button_ != nullptr) {
        InvalidateRect(terminal_mode_button_, nullptr, TRUE);
    }
    if (hwnd_ != nullptr) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void MainWindow::SetScanlinesEnabled(bool enabled) {
    scanlines_enabled_ = enabled;
    InvalidateEffectControls();
}

void MainWindow::SetGlowEnabled(bool enabled) {
    glow_enabled_ = enabled;
    InvalidateEffectControls();
}

void MainWindow::SetFlickerIntensity(int intensity) {
    flicker_intensity_ = std::clamp(intensity, 0, 2);
    UpdateLoadingTimer();
    InvalidateEffectControls();
}

void MainWindow::SetDeckSpaceEnabled(bool enabled) {
    deck_space_enabled_ = enabled;
    if (deck_space_button_ != nullptr) {
        InvalidateRect(deck_space_button_, nullptr, TRUE);
    }
    if (hwnd_ != nullptr) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void MainWindow::ToggleScanlines() {
    scanlines_enabled_ = !scanlines_enabled_;
    SetDownloadStatus(scanlines_enabled_ ? L"SCANLINES ENABLED" : L"SCANLINES DISABLED");
    if (toolbar_callbacks_.scanlines) {
        toolbar_callbacks_.scanlines(scanlines_enabled_);
    }
    InvalidateEffectControls();
}

void MainWindow::ToggleGlow() {
    glow_enabled_ = !glow_enabled_;
    SetDownloadStatus(glow_enabled_ ? L"GLOW ENABLED" : L"GLOW DISABLED");
    if (toolbar_callbacks_.glow) {
        toolbar_callbacks_.glow(glow_enabled_);
    }
    InvalidateEffectControls();
}

void MainWindow::CycleFlickerIntensity() {
    flicker_intensity_ = (flicker_intensity_ + 1) % 3;
    if (flicker_intensity_ == 0) {
        SetDownloadStatus(L"FLICKER OFF");
    } else if (flicker_intensity_ == 1) {
        SetDownloadStatus(L"FLICKER SUBTLE");
    } else {
        SetDownloadStatus(L"FLICKER MEDIUM");
    }

    if (toolbar_callbacks_.flicker_intensity) {
        toolbar_callbacks_.flicker_intensity(flicker_intensity_);
    }
    UpdateLoadingTimer();
    InvalidateEffectControls();
}

void MainWindow::InvalidateEffectControls() {
    const std::array<HWND, 3> controls{scanlines_button_, glow_button_, flicker_button_};
    for (HWND control : controls) {
        if (control != nullptr) {
            InvalidateRect(control, nullptr, TRUE);
        }
    }
    if (hwnd_ != nullptr) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void MainWindow::SetUrlTextInternal(const std::wstring& url) {
    if (url_edit_ != nullptr) {
        SetWindowTextW(url_edit_, url.c_str());
        url_edit_dirty_ = false;
    }
}

HWND MainWindow::hwnd() const {
    return hwnd_;
}

LRESULT CALLBACK MainWindow::WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
    MainWindow* window = nullptr;

    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
        window = static_cast<MainWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        window->hwnd_ = hwnd;
    } else {
        window = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (window != nullptr) {
        return window->HandleMessage(message, w_param, l_param);
    }

    return DefWindowProcW(hwnd, message, w_param, l_param);
}

LRESULT CALLBACK MainWindow::UrlEditProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* window = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (window != nullptr) {
        window->MarkUrlEditedByUser(message, w_param);
    }

    if (window != nullptr && message == WM_KEYDOWN && w_param == VK_RETURN) {
        window->NavigateFromEdit();
        return 0;
    }

    if (window != nullptr && window->url_edit_proc_ != nullptr) {
        return CallWindowProcW(window->url_edit_proc_, hwnd, message, w_param, l_param);
    }

    return DefWindowProcW(hwnd, message, w_param, l_param);
}

LRESULT MainWindow::HandleMessage(UINT message, WPARAM w_param, LPARAM l_param) {
    if (message == kApplyTabsMessage) {
        std::vector<UiTabState> tabs;
        int active_tab_id = 0;
        {
            std::lock_guard<std::mutex> lock(pending_tabs_mutex_);
            tabs = pending_tabs_;
            active_tab_id = pending_active_tab_id_;
        }
        ApplyTabs(tabs, active_tab_id);
        return 0;
    }

    if (message == kApplyDownloadStatusMessage) {
        std::wstring status;
        {
            std::lock_guard<std::mutex> lock(pending_download_status_mutex_);
            status = pending_download_status_;
        }
        SetDownloadStatus(status);
        return 0;
    }

    switch (message) {
        case WM_COMMAND: {
            const int command_id = LOWORD(w_param);
            switch (command_id) {
                case kCommandBack:
                    if (toolbar_callbacks_.back) {
                        toolbar_callbacks_.back();
                    }
                    return 0;
                case kCommandForward:
                    if (toolbar_callbacks_.forward) {
                        toolbar_callbacks_.forward();
                    }
                    return 0;
                case kCommandReload:
                    if (toolbar_callbacks_.reload) {
                        toolbar_callbacks_.reload();
                    }
                    return 0;
                case kCommandStop:
                    if (toolbar_callbacks_.stop) {
                        toolbar_callbacks_.stop();
                    }
                    return 0;
                case kCommandTerminalMode:
                    SetTerminalModeEnabled(!terminal_mode_enabled_);
                    if (toolbar_callbacks_.terminal_mode) {
                        toolbar_callbacks_.terminal_mode(terminal_mode_enabled_);
                    }
                    return 0;
                case kCommandScanlines:
                    ToggleScanlines();
                    return 0;
                case kCommandGlow:
                    ToggleGlow();
                    return 0;
                case kCommandFlicker:
                    CycleFlickerIntensity();
                    return 0;
                case kCommandSettings:
                    if (toolbar_callbacks_.settings_panel) {
                        toolbar_callbacks_.settings_panel();
                    }
                    return 0;
                case kCommandDeckSpace:
                    SetDeckSpaceEnabled(!deck_space_enabled_);
                    if (toolbar_callbacks_.deck_space) {
                        toolbar_callbacks_.deck_space(deck_space_enabled_);
                    }
                    return 0;
                case kCommandAddNode:
                    if (toolbar_callbacks_.add_node) {
                        toolbar_callbacks_.add_node();
                    }
                    return 0;
                case kCommandGo:
                    NavigateFromEdit();
                    return 0;
                case kCommandNewTab:
                    if (tab_callbacks_.new_tab) {
                        tab_callbacks_.new_tab();
                    }
                    return 0;
                default:
                    break;
            }

            if (command_id >= kCommandTabBase && command_id < kCommandCloseTabBase) {
                const int tab_id = command_id - kCommandTabBase;
                if (tab_callbacks_.activate_tab) {
                    tab_callbacks_.activate_tab(tab_id);
                }
                return 0;
            }

            if (command_id >= kCommandCloseTabBase && command_id < kCommandCloseTabBase + 10000) {
                const int tab_id = command_id - kCommandCloseTabBase;
                if (tab_callbacks_.close_tab) {
                    tab_callbacks_.close_tab(tab_id);
                }
                return 0;
            }
            break;
        }

        case WM_LBUTTONDOWN: {
            POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
            HandleTabStripClick(point);
            return 0;
        }

        case WM_DRAWITEM:
            DrawToolbarButton(*reinterpret_cast<DRAWITEMSTRUCT*>(l_param));
            return TRUE;

        case WM_TIMER:
            if (w_param == kLoadingTimerId) {
                spinner_frame_ = (spinner_frame_ + 1) % 4;
                flicker_frame_ = (flicker_frame_ + 1) % 120;
                InvalidateRect(hwnd_, nullptr, FALSE);
                for (HWND button : tab_buttons_) {
                    InvalidateRect(button, nullptr, FALSE);
                }
                return 0;
            }
            break;

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            auto edit_dc = reinterpret_cast<HDC>(w_param);
            SetTextColor(edit_dc, kGreen);
            SetBkColor(edit_dc, kBlack);
            return reinterpret_cast<LRESULT>(edit_background_brush_);
        }

        case WM_CLOSE:
            DestroyWindow(hwnd_);
            return 0;

        case WM_DESTROY:
            if (loading_timer_active_) {
                KillTimer(hwnd_, kLoadingTimerId);
                loading_timer_active_ = false;
            }
            hwnd_ = nullptr;
            PostQuitMessage(0);
            return 0;

        case WM_GETMINMAXINFO:
            SetMinimumTrackSize(reinterpret_cast<MINMAXINFO*>(l_param));
            return 0;

        case WM_SIZE:
            InvalidateRect(hwnd_, nullptr, FALSE);
            LayoutToolbarControls();
            LayoutTabControls();
            if (w_param != SIZE_MINIMIZED && content_resized_callback_) {
                content_resized_callback_(ContentBounds());
            }
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            Paint();
            return 0;

        default:
            return DefWindowProcW(hwnd_, message, w_param, l_param);
    }

    return DefWindowProcW(hwnd_, message, w_param, l_param);
}

bool MainWindow::CreateToolbarControls(HINSTANCE instance) {
    toolbar_font_ = cyberdeck::ui::CreateMonospaceFont(24, FW_SEMIBOLD);

    edit_background_brush_ = CreateSolidBrush(kBlack);
    if (toolbar_font_ == nullptr || edit_background_brush_ == nullptr) {
        return false;
    }

    auto create_button = [this, instance](int command_id) -> HWND {
        return CreateWindowExW(
            0,
            L"BUTTON",
            ButtonLabel(command_id).data(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0,
            0,
            0,
            0,
            hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(command_id)),
            instance,
            nullptr);
    };

    back_button_ = create_button(kCommandBack);
    forward_button_ = create_button(kCommandForward);
    reload_button_ = create_button(kCommandReload);
    stop_button_ = create_button(kCommandStop);
    terminal_mode_button_ = create_button(kCommandTerminalMode);
    scanlines_button_ = create_button(kCommandScanlines);
    glow_button_ = create_button(kCommandGlow);
    flicker_button_ = create_button(kCommandFlicker);
    settings_button_ = create_button(kCommandSettings);
    deck_space_button_ = create_button(kCommandDeckSpace);
    add_node_button_ = create_button(kCommandAddNode);
    go_button_ = create_button(kCommandGo);

    url_edit_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"https://www.example.com",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCommandUrlEdit)),
        instance,
        nullptr);

    const std::array<HWND, 13> controls{
        back_button_,
        forward_button_,
        reload_button_,
        stop_button_,
        terminal_mode_button_,
        scanlines_button_,
        glow_button_,
        flicker_button_,
        settings_button_,
        deck_space_button_,
        add_node_button_,
        url_edit_,
        go_button_};
    for (HWND control : controls) {
        if (control == nullptr) {
            return false;
        }
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(toolbar_font_), TRUE);
    }

    SetWindowLongPtrW(url_edit_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    url_edit_proc_ = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(url_edit_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(MainWindow::UrlEditProc)));

    return url_edit_proc_ != nullptr;
}

void MainWindow::RebuildTabControls() {
    for (HWND button : tab_buttons_) {
        DestroyWindow(button);
    }
    for (HWND button : tab_close_buttons_) {
        DestroyWindow(button);
    }
    tab_buttons_.clear();
    tab_close_buttons_.clear();

    if (hwnd_ == nullptr || instance_ == nullptr) {
        return;
    }

    for (const UiTabState& tab : tabs_) {
        const std::wstring title = CompactTabTitle(tab);
        HWND tab_button = CreateWindowExW(
            0,
            L"BUTTON",
            title.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0,
            0,
            0,
            0,
            hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCommandTabBase + tab.id)),
            instance_,
            nullptr);

        HWND close_button = CreateWindowExW(
            0,
            L"BUTTON",
            L"X",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0,
            0,
            0,
            0,
            hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCommandCloseTabBase + tab.id)),
            instance_,
            nullptr);

        if (tab_button != nullptr) {
            SendMessageW(tab_button, WM_SETFONT, reinterpret_cast<WPARAM>(toolbar_font_), TRUE);
            tab_buttons_.push_back(tab_button);
        }
        if (close_button != nullptr) {
            SendMessageW(close_button, WM_SETFONT, reinterpret_cast<WPARAM>(toolbar_font_), TRUE);
            tab_close_buttons_.push_back(close_button);
        }
    }

    if (new_tab_button_ == nullptr) {
        new_tab_button_ = CreateWindowExW(
            0,
            L"BUTTON",
            L"+",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0,
            0,
            0,
            0,
            hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCommandNewTab)),
            instance_,
            nullptr);
        if (new_tab_button_ != nullptr) {
            SendMessageW(new_tab_button_, WM_SETFONT, reinterpret_cast<WPARAM>(toolbar_font_), TRUE);
        }
    }

    LayoutTabControls();
}

void MainWindow::UpdateLoadingTimer() {
    if (hwnd_ == nullptr) {
        return;
    }

    const bool should_run = AnyTabLoading(tabs_) || flicker_intensity_ > 0;
    if (should_run && !loading_timer_active_) {
        SetTimer(hwnd_, kLoadingTimerId, kLoadingTimerMs, nullptr);
        loading_timer_active_ = true;
    } else if (!should_run && loading_timer_active_) {
        KillTimer(hwnd_, kLoadingTimerId);
        loading_timer_active_ = false;
        spinner_frame_ = 0;
        flicker_frame_ = 0;
    }
}

void MainWindow::LayoutTabControls() {
    if (hwnd_ == nullptr) {
        return;
    }

    int x = kToolbarMargin;
    const int tab_button_width = kTabWidth - kTabCloseSize - 8;
    for (std::size_t index = 0; index < tab_buttons_.size(); ++index) {
        SetWindowPos(
            tab_buttons_[index],
            nullptr,
            x,
            kTabTop,
            tab_button_width,
            kTabHeight,
            SWP_NOZORDER | SWP_NOACTIVATE);

        if (index < tab_close_buttons_.size()) {
            SetWindowPos(
                tab_close_buttons_[index],
                nullptr,
                x + tab_button_width,
                kTabTop,
                kTabCloseSize + 8,
                kTabHeight,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }

        x += kTabWidth + kToolbarGap;
    }

    if (new_tab_button_ != nullptr) {
        SetWindowPos(
            new_tab_button_,
            nullptr,
            x,
            kTabTop,
            kNewTabWidth,
            kTabHeight,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void MainWindow::LayoutToolbarControls() {
    if (hwnd_ == nullptr || url_edit_ == nullptr) {
        return;
    }

    RECT client{};
    GetClientRect(hwnd_, &client);

    const int width = client.right - client.left;
    const int y = kToolbarMargin + 1;
    int x = kToolbarMargin;

    const std::array<HWND, 4> navigation_buttons{back_button_, forward_button_, reload_button_, stop_button_};
    for (HWND button : navigation_buttons) {
        SetWindowPos(button, nullptr, x, y, kButtonWidth, kControlHeight, SWP_NOZORDER | SWP_NOACTIVATE);
        x += kButtonWidth + kToolbarGap;
    }
    SetWindowPos(
        terminal_mode_button_,
        nullptr,
        x,
        y,
        kTerminalButtonWidth,
        kControlHeight,
        SWP_NOZORDER | SWP_NOACTIVATE);
    x += kTerminalButtonWidth + kToolbarGap;

    const std::array<HWND, 3> effect_buttons{scanlines_button_, glow_button_, flicker_button_};
    for (HWND button : effect_buttons) {
        SetWindowPos(button, nullptr, x, y, kEffectButtonWidth, kControlHeight, SWP_NOZORDER | SWP_NOACTIVATE);
        x += kEffectButtonWidth + kToolbarGap;
    }
    SetWindowPos(
        settings_button_,
        nullptr,
        x,
        y,
        kSettingsButtonWidth,
        kControlHeight,
        SWP_NOZORDER | SWP_NOACTIVATE);
    x += kSettingsButtonWidth + kToolbarGap;
    SetWindowPos(
        deck_space_button_,
        nullptr,
        x,
        y,
        kDeckButtonWidth,
        kControlHeight,
        SWP_NOZORDER | SWP_NOACTIVATE);
    x += kDeckButtonWidth + kToolbarGap;
    SetWindowPos(
        add_node_button_,
        nullptr,
        x,
        y,
        kAddNodeButtonWidth,
        kControlHeight,
        SWP_NOZORDER | SWP_NOACTIVATE);
    x += kAddNodeButtonWidth + kToolbarGap;

    const int go_x = std::max(x + kUrlMinimumWidth + kToolbarGap, width - kToolbarMargin - kGoButtonWidth);
    const int edit_width = std::max(kUrlMinimumWidth, go_x - x - kToolbarGap);
    SetWindowPos(url_edit_, nullptr, x, y, edit_width, kControlHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(go_button_, nullptr, x + edit_width + kToolbarGap, y, kGoButtonWidth, kControlHeight, SWP_NOZORDER | SWP_NOACTIVATE);
}

void MainWindow::NavigateFromEdit() {
    if (url_edit_ == nullptr || !toolbar_callbacks_.navigate) {
        return;
    }

    const int length = GetWindowTextLengthW(url_edit_);
    if (length <= 0) {
        return;
    }

    std::wstring url(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(url_edit_, url.data(), length + 1);
    url.resize(static_cast<std::size_t>(length));
    toolbar_callbacks_.navigate(url);
}

void MainWindow::MarkUrlEditedByUser(UINT message, WPARAM w_param) {
    switch (message) {
        case WM_CHAR:
        case WM_CUT:
        case WM_PASTE:
        case WM_CLEAR:
        case WM_UNDO:
            url_edit_dirty_ = true;
            break;
        case WM_KEYDOWN:
            if (w_param == VK_BACK || w_param == VK_DELETE) {
                url_edit_dirty_ = true;
            }
            break;
        default:
            break;
    }
}

void MainWindow::HandleTabStripClick(POINT point) {
    if (point.y < kToolbarHeight || point.y >= kToolbarHeight + kTabStripHeight) {
        return;
    }

    int x = kToolbarMargin;
    for (const UiTabState& tab : tabs_) {
        RECT tab_rect{x, kTabTop, x + kTabWidth, kTabTop + kTabHeight};
        RECT close_rect{
            tab_rect.right - kTabCloseSize - 5,
            tab_rect.top + 5,
            tab_rect.right - 5,
            tab_rect.bottom - 5};

        if (PtInRect(&close_rect, point)) {
            if (tab_callbacks_.close_tab) {
                tab_callbacks_.close_tab(tab.id);
            }
            return;
        }

        if (PtInRect(&tab_rect, point)) {
            if (tab_callbacks_.activate_tab) {
                tab_callbacks_.activate_tab(tab.id);
            }
            return;
        }

        x += kTabWidth + kToolbarGap;
    }

    RECT new_tab_rect{x, kTabTop, x + kNewTabWidth, kTabTop + kTabHeight};
    if (PtInRect(&new_tab_rect, point) && tab_callbacks_.new_tab) {
        tab_callbacks_.new_tab();
    }
}

void MainWindow::DrawToolbarButton(const DRAWITEMSTRUCT& draw_item) {
    if (draw_item.CtlType != ODT_BUTTON) {
        return;
    }

    const int control_id = static_cast<int>(draw_item.CtlID);
    const bool pressed = (draw_item.itemState & ODS_SELECTED) != 0;
    const bool focused = (draw_item.itemState & ODS_FOCUS) != 0;
    const bool hot = (draw_item.itemState & ODS_HOTLIGHT) != 0;
    const bool disabled = (draw_item.itemState & ODS_DISABLED) != 0;
    const COLORREF accent = ButtonAccent(control_id);
    const RECT rect = draw_item.rcItem;
    ScopedSelectObject selected_font(draw_item.hDC, toolbar_font_);

    if (control_id == kCommandNewTab) {
        Fill(draw_item.hDC, rect, pressed ? RGB(18, 16, 0) : RGB(0, 14, 8));
        FrameRectWithPen(draw_item.hDC, rect, hot || focused ? kYellow : kGreen, hot ? 2 : 1);
        SetBkMode(draw_item.hDC, TRANSPARENT);
        SetTextColor(draw_item.hDC, hot ? kYellow : kGreen);
        RECT text_rect = rect;
        DrawTextW(draw_item.hDC, L"+", -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    if (control_id == kCommandTerminalMode || IsCrtEffectCommand(control_id)) {
        bool enabled = terminal_mode_enabled_;
        std::wstring label = L"TERM";
        if (control_id == kCommandScanlines) {
            enabled = scanlines_enabled_;
            label = L"SCAN";
        } else if (control_id == kCommandGlow) {
            enabled = glow_enabled_;
            label = L"GLOW";
        } else if (control_id == kCommandFlicker) {
            enabled = flicker_intensity_ > 0;
            label = L"FLK" + std::to_wstring(flicker_intensity_);
        }

        Fill(draw_item.hDC, rect, enabled ? RGB(18, 18, 0) : (pressed ? RGB(10, 12, 6) : kBlack));
        if (enabled) {
            if (glow_enabled_) {
                cyberdeck::ui::DrawGlowFrame(draw_item.hDC, rect, kYellow);
            }
            FrameRectWithPen(draw_item.hDC, rect, kYellow, 2);
        } else {
            FrameRectWithPen(draw_item.hDC, rect, hot || focused ? kYellow : kGreenDim, hot || focused ? 2 : 1);
        }
        if (scanlines_enabled_) {
            cyberdeck::ui::DrawScanlineOverlay(draw_item.hDC, rect, 5);
        }
        SetBkMode(draw_item.hDC, TRANSPARENT);
        SetTextColor(draw_item.hDC, enabled || hot ? kYellow : kGreen);
        RECT text_rect = rect;
        DrawTextW(draw_item.hDC, label.c_str(), -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    if (control_id == kCommandDeckSpace) {
        Fill(draw_item.hDC, rect, deck_space_enabled_ ? RGB(0, 18, 18) : (pressed ? RGB(10, 12, 12) : kBlack));
        if (deck_space_enabled_) {
            if (glow_enabled_) {
                cyberdeck::ui::DrawGlowFrame(draw_item.hDC, rect, kYellow);
            }
            FrameRectWithPen(draw_item.hDC, rect, kYellow, 2);
        } else {
            FrameRectWithPen(draw_item.hDC, rect, hot || focused ? kYellow : kGreenDim, hot || focused ? 2 : 1);
        }
        if (scanlines_enabled_) {
            cyberdeck::ui::DrawScanlineOverlay(draw_item.hDC, rect, 5);
        }
        SetBkMode(draw_item.hDC, TRANSPARENT);
        SetTextColor(draw_item.hDC, deck_space_enabled_ || hot ? kYellow : kGreen);
        RECT text_rect = rect;
        DrawTextW(
            draw_item.hDC,
            deck_space_enabled_ ? L"WEB" : L"DECK",
            -1,
            &text_rect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    if (control_id >= kCommandCloseTabBase && control_id < kCommandCloseTabBase + 10000) {
        Fill(draw_item.hDC, rect, pressed ? RGB(40, 0, 0) : RGB(10, 0, 0));
        FrameRectWithPen(draw_item.hDC, rect, hot || focused ? kRed : kRedDim, hot ? 2 : 1);
        SetBkMode(draw_item.hDC, TRANSPARENT);
        SetTextColor(draw_item.hDC, hot ? kYellow : kRed);
        RECT text_rect = rect;
        DrawTextW(draw_item.hDC, L"X", -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    if (control_id >= kCommandTabBase && control_id < kCommandCloseTabBase) {
        const int tab_id = control_id - kCommandTabBase;
        const UiTabState* tab = FindTabById(tabs_, tab_id);
        if (tab == nullptr) {
            return;
        }

        const bool active = tab->id == active_tab_id_;
        Fill(draw_item.hDC, rect, active ? RGB(0, 22, 10) : RGB(1, 7, 4));

        if (active && glow_enabled_) {
            cyberdeck::ui::DrawGlowFrame(draw_item.hDC, rect, kGreen);
            FrameRectWithPen(draw_item.hDC, rect, kGreen, 2);
        } else {
            FrameRectWithPen(draw_item.hDC, rect, hot ? kYellow : kGreenDim, hot ? 2 : 1);
        }

        SetBkMode(draw_item.hDC, TRANSPARENT);
        SetTextColor(draw_item.hDC, active ? kYellow : (hot ? kGreen : RGB(0, 150, 72)));
        RECT title_rect = rect;
        title_rect.left += 8;
        title_rect.right -= 6;
        std::wstring title = CompactTabTitle(*tab);
        if (tab->loading) {
            title = LoadingPrefix(spinner_frame_) + title;
        }
        DrawTextW(
            draw_item.hDC,
            title.c_str(),
            -1,
            &title_rect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (scanlines_enabled_) {
            cyberdeck::ui::DrawScanlineOverlay(draw_item.hDC, rect, 5);
        }
        return;
    }

    Fill(draw_item.hDC, rect, pressed ? RGB(14, 18, 12) : kBlack);

    const COLORREF button_color = disabled ? RGB(0, 70, 34) : (hot ? kYellow : accent);
    FrameRectWithPen(draw_item.hDC, rect, button_color, focused || hot ? 2 : 1);
    if (scanlines_enabled_) {
        cyberdeck::ui::DrawScanlineOverlay(draw_item.hDC, rect, 5);
    }

    SetBkMode(draw_item.hDC, TRANSPARENT);
    SetTextColor(draw_item.hDC, button_color);
    RECT text_rect = rect;
    DrawTextW(
        draw_item.hDC,
        ButtonLabel(control_id).data(),
        -1,
        &text_rect,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void MainWindow::Paint() {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd_, &paint);
    if (dc == nullptr) {
        return;
    }

    RECT client{};
    GetClientRect(hwnd_, &client);
    ScopedSelectObject selected_font(dc, toolbar_font_);

    const int width = client.right - client.left;
    const int tab_strip_bottom = kToolbarHeight + kTabStripHeight;
    RECT toolbar{0, 0, width, kToolbarHeight};
    RECT tab_strip{0, kToolbarHeight, width, tab_strip_bottom};
    RECT content = ContentBounds();

    Fill(dc, toolbar, kToolbarBlack);
    Fill(dc, tab_strip, kTabBlack);
    Fill(dc, content, kBlack);

    DrawHorizontalLine(dc, kToolbarHeight, width, kGreenDim);
    DrawHorizontalLine(dc, tab_strip_bottom, width, kYellow);
    if (glow_enabled_) {
        RECT shell_frame{0, 0, width, tab_strip_bottom};
        cyberdeck::ui::DrawGlowFrame(dc, shell_frame, kGreenDim);
    }

    int tab_x = kToolbarMargin;
    for (const UiTabState& tab : tabs_) {
        const bool active = tab.id == active_tab_id_;
        RECT tab_rect{tab_x, kTabTop, tab_x + kTabWidth, kTabTop + kTabHeight};
        Fill(dc, tab_rect, active ? RGB(0, 18, 10) : RGB(3, 5, 4));

        HPEN tab_pen = CreatePen(PS_SOLID, active ? 2 : 1, active ? kYellow : kGreenDim);
        HGDIOBJ previous_pen = SelectObject(dc, tab_pen);
        HGDIOBJ previous_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, tab_rect.left, tab_rect.top, tab_rect.right, tab_rect.bottom);
        SelectObject(dc, previous_brush);
        SelectObject(dc, previous_pen);
        DeleteObject(tab_pen);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, active ? kYellow : kGreen);
        RECT title_rect{tab_rect.left + 8, tab_rect.top, tab_rect.right - 30, tab_rect.bottom};
        const std::wstring title = (tab.loading ? L"* " : L"") + CompactTabTitle(tab);
        DrawTextW(dc, title.c_str(), -1, &title_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        RECT close_rect{
            tab_rect.right - kTabCloseSize - 5,
            tab_rect.top + 5,
            tab_rect.right - 5,
            tab_rect.bottom - 5};
        SetTextColor(dc, kRed);
        DrawTextW(dc, L"X", -1, &close_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        tab_x += kTabWidth + kToolbarGap;
    }

    RECT new_tab_rect{tab_x, kTabTop, tab_x + kNewTabWidth, kTabTop + kTabHeight};
    Fill(dc, new_tab_rect, RGB(0, 12, 7));
    HPEN new_pen = CreatePen(PS_SOLID, 1, kGreen);
    HGDIOBJ previous_pen = SelectObject(dc, new_pen);
    HGDIOBJ previous_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, new_tab_rect.left, new_tab_rect.top, new_tab_rect.right, new_tab_rect.bottom);
    SelectObject(dc, previous_brush);
    SelectObject(dc, previous_pen);
    DeleteObject(new_pen);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kGreen);
    DrawTextW(dc, L"+", -1, &new_tab_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    if (!download_status_.empty()) {
        RECT status_rect{std::max(0, width - 470), kTabTop, width - kToolbarMargin, kTabTop + kTabHeight};
        Fill(dc, status_rect, RGB(0, 10, 6));
        FrameRectWithPen(dc, status_rect, kYellow);
        status_rect.left += 8;
        status_rect.right -= 8;
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, kYellow);
        DrawTextW(
            dc,
            download_status_.c_str(),
            -1,
            &status_rect,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    HFONT font = cyberdeck::ui::CreateMonospaceFont(24, FW_SEMIBOLD);

    HGDIOBJ previous_font = nullptr;
    if (font != nullptr) {
        previous_font = SelectObject(dc, font);
    }

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kGreen);

    RECT boot_text = content;
    boot_text.top += 28;
    DrawTextW(
        dc,
        L"CYBERDECK BOOT SEQUENCE READY",
        -1,
        &boot_text,
        DT_CENTER | DT_TOP | DT_SINGLELINE);

    if (previous_font != nullptr) {
        SelectObject(dc, previous_font);
    }
    if (font != nullptr) {
        DeleteObject(font);
    }

    RECT shell_effects{0, 0, width, tab_strip_bottom};
    if (scanlines_enabled_) {
        cyberdeck::ui::DrawScanlineOverlay(dc, shell_effects, 4);
    }
    if (flicker_intensity_ > 0) {
        cyberdeck::ui::DrawFlickerLines(dc, shell_effects, flicker_intensity_, flicker_frame_);
    }

    EndPaint(hwnd_, &paint);
}

void MainWindow::SetMinimumTrackSize(MINMAXINFO* minmax_info) const {
    if (minmax_info == nullptr) {
        return;
    }

    minmax_info->ptMinTrackSize.x = kMinimumWidth;
    minmax_info->ptMinTrackSize.y = kMinimumHeight;
}

}  // namespace cyberdeck::main
