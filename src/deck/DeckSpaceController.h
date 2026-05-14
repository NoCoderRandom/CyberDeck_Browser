#pragma once

#include "common/Logger.h"
#include "deck/BookmarkNode.h"
#include "render/OpenGLDeckView.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <windows.h>

namespace cyberdeck::deck {

class DeckSpaceController {
public:
    DeckSpaceController() = default;
    ~DeckSpaceController();

    DeckSpaceController(const DeckSpaceController&) = delete;
    DeckSpaceController& operator=(const DeckSpaceController&) = delete;

    bool Initialize(HWND parent, const RECT& bounds, common::Logger& logger);
    bool Enter(const RECT& bounds);
    void Exit();
    void Resize(const RECT& bounds);
    void Shutdown();
    void SetExitRequestedCallback(std::function<void()> callback);
    void SetOpenNodeCallback(std::function<void(BookmarkNode)> callback);
    void SetEditNodeCallback(std::function<void(BookmarkNode)> callback);
    void SetDeleteNodeCallback(std::function<void(BookmarkNode)> callback);
    void SetLayoutChangedCallback(std::function<void(render::DeckLayoutMode)> callback);
    void SetLayoutMode(render::DeckLayoutMode mode);
    void SetBookmarkNodes(std::vector<BookmarkNode> nodes);
    void SetBookmarkData(std::vector<BookmarkNode> nodes, std::vector<BookmarkVault> vaults);
    void SetEditVaultCallback(std::function<void(BookmarkVault)> callback);
    void SetDeleteVaultCallback(std::function<void(BookmarkVault)> callback);
    render::OpenGLDiagnostics Diagnostics() const;
    bool active() const;
    bool available() const;
    std::wstring LastError() const;
    std::optional<std::wstring> ActiveVaultId() const;

private:
    render::OpenGLDeckView view_;
    common::Logger* logger_ = nullptr;
    bool active_ = false;
    bool available_ = false;
};

}  // namespace cyberdeck::deck
