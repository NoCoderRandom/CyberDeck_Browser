#pragma once

#include "common/Logger.h"
#include "deck/BookmarkNode.h"
#include "render/DeckGeometry.h"
#include "render/DeckLayout.h"

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <windows.h>

namespace cyberdeck::render {

struct OpenGLDiagnostics {
    std::string vendor = "unknown";
    std::string renderer = "unknown";
    std::string version = "unknown";
};

class OpenGLDeckView {
public:
    OpenGLDeckView() = default;
    ~OpenGLDeckView();

    OpenGLDeckView(const OpenGLDeckView&) = delete;
    OpenGLDeckView& operator=(const OpenGLDeckView&) = delete;

    bool Initialize(HWND parent, const RECT& bounds, common::Logger& logger);
    void Show(const RECT& bounds);
    void Hide();
    void Resize(const RECT& bounds);
    void Shutdown();
    void SetExitRequestedCallback(std::function<void()> callback);
    void SetOpenNodeCallback(std::function<void(deck::BookmarkNode)> callback);
    void SetEditNodeCallback(std::function<void(deck::BookmarkNode)> callback);
    void SetDeleteNodeCallback(std::function<void(deck::BookmarkNode)> callback);
    void SetEditVaultCallback(std::function<void(deck::BookmarkVault)> callback);
    void SetDeleteVaultCallback(std::function<void(deck::BookmarkVault)> callback);
    void SetLayoutChangedCallback(std::function<void(DeckLayoutMode)> callback);
    void SetLayoutMode(DeckLayoutMode mode);
    void SetBookmarkNodes(std::vector<deck::BookmarkNode> nodes);
    void SetBookmarkData(std::vector<deck::BookmarkNode> nodes, std::vector<deck::BookmarkVault> vaults);
    OpenGLDiagnostics Diagnostics() const;
    bool IsInitialized() const;
    std::wstring LastError() const;
    std::optional<std::wstring> ActiveVaultId() const;

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);

    LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param);
    bool RegisterWindowClass();
    bool InitializeContext();
    void ReleaseGraphicsResources();
    void CreateOverlayFont();
    void DestroyOverlayFont();
    bool InitializeNeonShader();
    void DestroyNeonShader();
    void BeginNeonShader();
    void EndNeonShader();
    void RebuildSceneObjects();
    void BuildNodeScene(const std::vector<std::size_t>& node_indices);
    void BuildVaultScene();
    void BuildDebugScene();
    void Render();
    void RenderDeckObject(const DeckSceneObject& object);
    void RenderMesh(const DeckMesh& mesh, NeonMaterialId material, bool hovered, bool selected, float glow_pulse);
    void DrawNodeLabels();
    void DrawHelpOverlay();
    std::optional<std::size_t> PickNodeAt(POINT point) const;
    std::optional<POINT> ProjectNodeCenter(std::size_t index) const;
    void SetSelectedNodeIndex(std::size_t index);
    void SetHoveredNodeIndex(std::optional<std::size_t> index);
    void MoveSelection(int direction);
    void EnterSelectedVault();
    void ExitActiveVault();
    void CycleLayoutMode();
    void OpenSelectedNode();
    void EditSelectedNode();
    void DeleteSelectedNode();
    void StartAnimation();
    void StopAnimation();
    void SetLastErrorText(std::wstring message);
    void SmoothCamera(float delta_seconds);
    void ResetCamera();
    void AdjustZoom(float delta_distance);
    float DefaultCameraDistance() const;

    HWND hwnd_ = nullptr;
    HWND parent_ = nullptr;
    HDC dc_ = nullptr;
    HGLRC context_ = nullptr;
    common::Logger* logger_ = nullptr;
    std::function<void()> exit_requested_callback_;
    std::function<void(deck::BookmarkNode)> open_node_callback_;
    std::function<void(deck::BookmarkNode)> edit_node_callback_;
    std::function<void(deck::BookmarkNode)> delete_node_callback_;
    std::function<void(deck::BookmarkVault)> edit_vault_callback_;
    std::function<void(deck::BookmarkVault)> delete_vault_callback_;
    std::function<void(DeckLayoutMode)> layout_changed_callback_;
    std::wstring last_error_;
    int width_ = 1;
    int height_ = 1;
    float target_yaw_degrees_ = 0.0f;
    float target_pitch_degrees_ = 19.0f;
    float target_distance_ = 8.8f;
    float target_pan_x_ = 0.0f;
    float target_pan_z_ = 0.0f;
    float current_yaw_degrees_ = 0.0f;
    float current_pitch_degrees_ = 19.0f;
    float current_distance_ = 8.8f;
    float current_pan_x_ = 0.0f;
    float current_pan_z_ = 0.0f;
    bool dragging_ = false;
    bool drag_moved_ = false;
    POINT last_mouse_{};
    HFONT overlay_font_ = nullptr;
    unsigned int font_base_ = 0;
    unsigned int neon_program_ = 0;
    OpenGLDiagnostics diagnostics_;
    DeckMesh cube_mesh_;
    DeckMesh hex_prism_mesh_;
    DeckMesh beveled_tile_mesh_;
    std::vector<DeckSceneObject> debug_objects_;
    std::vector<deck::BookmarkNode> bookmark_nodes_;
    std::vector<deck::BookmarkVault> bookmark_vaults_;
    std::vector<std::size_t> displayed_node_indices_;
    std::vector<std::string> bookmark_labels_;
    std::vector<std::string> bookmark_urls_;
    std::vector<std::string> bookmark_favicon_labels_;
    bool bookmark_nodes_loaded_ = false;
    std::optional<std::size_t> hovered_node_index_;
    std::size_t selected_node_index_ = 0;
    std::size_t selected_vault_index_ = 0;
    std::optional<std::wstring> active_vault_id_;
    DeckLayoutMode layout_mode_ = DeckLayoutMode::HexRing;
    std::chrono::steady_clock::time_point last_animation_time_{};
    bool neon_shader_ready_ = false;
    bool initialized_ = false;
    bool animation_timer_active_ = false;
};

}  // namespace cyberdeck::render
