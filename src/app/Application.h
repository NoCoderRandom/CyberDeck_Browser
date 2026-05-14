#pragma once

#include "browser/BrowserHost.h"
#include "common/Logger.h"
#include "deck/DeckSpaceController.h"
#include "deck/FaviconStore.h"
#include "deck/BookmarkStore.h"
#include "history/HistoryStore.h"
#include "main/MainWindow.h"
#include "settings/SettingsStore.h"

#include <string>
#include <vector>
#include <windows.h>

namespace cyberdeck::app {

class Application {
public:
    Application(HINSTANCE instance, int show_command, std::wstring initial_url = {});
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int Run();

private:
    void PersistSettings();
    void ShowSettingsPanel() const;
    std::vector<deck::BookmarkNode> LoadBookmarksWithFavicons();
    void RefreshDeckSpaceBookmarks();
    void AddNodeFromCurrentTab();
    void EnsureNodeFavicon(deck::BookmarkNode& node);
    void OpenDeckNode(deck::BookmarkNode node);
    void EditDeckNode(deck::BookmarkNode node);
    void DeleteDeckNode(deck::BookmarkNode node);
    void EditDeckVault(deck::BookmarkVault vault);
    void DeleteDeckVault(deck::BookmarkVault vault);

    HINSTANCE instance_ = nullptr;
    int show_command_ = SW_SHOWDEFAULT;
    std::wstring initial_url_;
    browser::BrowserHost browser_host_;
    deck::DeckSpaceController deck_space_;
    deck::BookmarkStore bookmark_store_;
    common::Logger logger_;
    history::HistoryStore history_store_;
    settings::SettingsStore settings_store_;
    settings::UserSettings current_settings_;
    main::MainWindow main_window_;
};

}  // namespace cyberdeck::app
