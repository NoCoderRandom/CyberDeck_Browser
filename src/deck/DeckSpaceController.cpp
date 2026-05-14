#include "deck/DeckSpaceController.h"

#include <utility>

namespace cyberdeck::deck {

DeckSpaceController::~DeckSpaceController() {
    Shutdown();
}

bool DeckSpaceController::Initialize(HWND parent, const RECT& bounds, common::Logger& logger) {
    logger_ = &logger;
    available_ = view_.Initialize(parent, bounds, logger);
    active_ = false;

    if (available_) {
        logger.Info("Deck Space OpenGL view initialized.");
    } else {
        logger.Error("Deck Space OpenGL view failed to initialize.");
    }
    return available_;
}

bool DeckSpaceController::Enter(const RECT& bounds) {
    if (!available_) {
        return false;
    }

    view_.Show(bounds);
    active_ = true;
    if (logger_ != nullptr) {
        logger_->Info("Deck Space entered.");
    }
    return true;
}

void DeckSpaceController::Exit() {
    if (!active_) {
        return;
    }

    view_.Hide();
    active_ = false;
    if (logger_ != nullptr) {
        logger_->Info("Deck Space exited.");
    }
}

void DeckSpaceController::Resize(const RECT& bounds) {
    if (available_) {
        view_.Resize(bounds);
    }
}

void DeckSpaceController::Shutdown() {
    active_ = false;
    available_ = false;
    view_.Shutdown();
}

void DeckSpaceController::SetExitRequestedCallback(std::function<void()> callback) {
    view_.SetExitRequestedCallback(std::move(callback));
}

void DeckSpaceController::SetOpenNodeCallback(std::function<void(BookmarkNode)> callback) {
    view_.SetOpenNodeCallback(std::move(callback));
}

void DeckSpaceController::SetEditNodeCallback(std::function<void(BookmarkNode)> callback) {
    view_.SetEditNodeCallback(std::move(callback));
}

void DeckSpaceController::SetDeleteNodeCallback(std::function<void(BookmarkNode)> callback) {
    view_.SetDeleteNodeCallback(std::move(callback));
}

void DeckSpaceController::SetEditVaultCallback(std::function<void(BookmarkVault)> callback) {
    view_.SetEditVaultCallback(std::move(callback));
}

void DeckSpaceController::SetDeleteVaultCallback(std::function<void(BookmarkVault)> callback) {
    view_.SetDeleteVaultCallback(std::move(callback));
}

void DeckSpaceController::SetLayoutChangedCallback(std::function<void(render::DeckLayoutMode)> callback) {
    view_.SetLayoutChangedCallback(std::move(callback));
}

void DeckSpaceController::SetLayoutMode(render::DeckLayoutMode mode) {
    view_.SetLayoutMode(mode);
}

void DeckSpaceController::SetBookmarkNodes(std::vector<BookmarkNode> nodes) {
    view_.SetBookmarkNodes(std::move(nodes));
}

void DeckSpaceController::SetBookmarkData(std::vector<BookmarkNode> nodes, std::vector<BookmarkVault> vaults) {
    view_.SetBookmarkData(std::move(nodes), std::move(vaults));
}

render::OpenGLDiagnostics DeckSpaceController::Diagnostics() const {
    return view_.Diagnostics();
}

bool DeckSpaceController::active() const {
    return active_;
}

bool DeckSpaceController::available() const {
    return available_;
}

std::wstring DeckSpaceController::LastError() const {
    return view_.LastError();
}

std::optional<std::wstring> DeckSpaceController::ActiveVaultId() const {
    return view_.ActiveVaultId();
}

}  // namespace cyberdeck::deck
