#include "render/OpenGLDeckView.h"

#include "render/DeckAnimation.h"
#include "render/DeckLayout.h"
#include "render/NeonMaterial.h"

#include <gl/GL.h>
#include <windowsx.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

namespace cyberdeck::render {
namespace {

constexpr wchar_t kDeckViewClassName[] = L"CyberDeckOpenGLDeckView";
constexpr UINT_PTR kAnimationTimerId = 1;
constexpr UINT kAnimationTimerMs = 16;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTau = kPi * 2.0f;
constexpr float kMinimumZoom = 3.0f;
constexpr float kMaximumZoom = 18.0f;
constexpr float kDeckCameraDistance = 7.6f;
constexpr float kVaultAtlasCameraDistance = 11.0f;
constexpr float kMinimumPitch = -70.0f;
constexpr float kMaximumPitch = 78.0f;
constexpr float kMaximumAnimationDeltaSeconds = 0.1f;

#ifndef GL_VERTEX_SHADER
constexpr GLenum GL_VERTEX_SHADER = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER = 0x8B30;
constexpr GLenum GL_COMPILE_STATUS = 0x8B81;
constexpr GLenum GL_LINK_STATUS = 0x8B82;
constexpr GLenum GL_INFO_LOG_LENGTH = 0x8B84;
#endif

using CreateShaderProc = GLuint(APIENTRY*)(GLenum);
using ShaderSourceProc = void(APIENTRY*)(GLuint, GLsizei, const char**, const GLint*);
using CompileShaderProc = void(APIENTRY*)(GLuint);
using GetShaderivProc = void(APIENTRY*)(GLuint, GLenum, GLint*);
using GetShaderInfoLogProc = void(APIENTRY*)(GLuint, GLsizei, GLsizei*, char*);
using DeleteShaderProc = void(APIENTRY*)(GLuint);
using CreateProgramProc = GLuint(APIENTRY*)();
using AttachShaderProc = void(APIENTRY*)(GLuint, GLuint);
using LinkProgramProc = void(APIENTRY*)(GLuint);
using GetProgramivProc = void(APIENTRY*)(GLuint, GLenum, GLint*);
using GetProgramInfoLogProc = void(APIENTRY*)(GLuint, GLsizei, GLsizei*, char*);
using UseProgramProc = void(APIENTRY*)(GLuint);
using DeleteProgramProc = void(APIENTRY*)(GLuint);

struct ShaderFunctions {
    CreateShaderProc create_shader = nullptr;
    ShaderSourceProc shader_source = nullptr;
    CompileShaderProc compile_shader = nullptr;
    GetShaderivProc get_shader_iv = nullptr;
    GetShaderInfoLogProc get_shader_info_log = nullptr;
    DeleteShaderProc delete_shader = nullptr;
    CreateProgramProc create_program = nullptr;
    AttachShaderProc attach_shader = nullptr;
    LinkProgramProc link_program = nullptr;
    GetProgramivProc get_program_iv = nullptr;
    GetProgramInfoLogProc get_program_info_log = nullptr;
    UseProgramProc use_program = nullptr;
    DeleteProgramProc delete_program = nullptr;
};

ShaderFunctions g_shader;
bool g_shader_functions_loaded = false;
bool g_shader_functions_available = false;

int RectWidth(const RECT& rect) {
    return std::max(1L, rect.right - rect.left);
}

int RectHeight(const RECT& rect) {
    return std::max(1L, rect.bottom - rect.top);
}

std::string GlString(GLenum name) {
    const GLubyte* value = glGetString(name);
    if (value == nullptr) {
        return "unknown";
    }
    return reinterpret_cast<const char*>(value);
}

void* LoadGlFunction(const char* name) {
    PROC proc = wglGetProcAddress(name);
    if (proc != nullptr && proc != reinterpret_cast<PROC>(1) && proc != reinterpret_cast<PROC>(2) &&
        proc != reinterpret_cast<PROC>(3) && proc != reinterpret_cast<PROC>(-1)) {
        return reinterpret_cast<void*>(proc);
    }

    HMODULE opengl = GetModuleHandleW(L"opengl32.dll");
    if (opengl == nullptr) {
        opengl = LoadLibraryW(L"opengl32.dll");
    }
    return opengl == nullptr ? nullptr : reinterpret_cast<void*>(GetProcAddress(opengl, name));
}

template <typename Proc>
Proc LoadGlFunctionAs(const char* name) {
    return reinterpret_cast<Proc>(LoadGlFunction(name));
}

bool LoadShaderFunctions() {
    if (g_shader_functions_loaded) {
        return g_shader_functions_available;
    }

    g_shader_functions_loaded = true;
    g_shader.create_shader = LoadGlFunctionAs<CreateShaderProc>("glCreateShader");
    g_shader.shader_source = LoadGlFunctionAs<ShaderSourceProc>("glShaderSource");
    g_shader.compile_shader = LoadGlFunctionAs<CompileShaderProc>("glCompileShader");
    g_shader.get_shader_iv = LoadGlFunctionAs<GetShaderivProc>("glGetShaderiv");
    g_shader.get_shader_info_log = LoadGlFunctionAs<GetShaderInfoLogProc>("glGetShaderInfoLog");
    g_shader.delete_shader = LoadGlFunctionAs<DeleteShaderProc>("glDeleteShader");
    g_shader.create_program = LoadGlFunctionAs<CreateProgramProc>("glCreateProgram");
    g_shader.attach_shader = LoadGlFunctionAs<AttachShaderProc>("glAttachShader");
    g_shader.link_program = LoadGlFunctionAs<LinkProgramProc>("glLinkProgram");
    g_shader.get_program_iv = LoadGlFunctionAs<GetProgramivProc>("glGetProgramiv");
    g_shader.get_program_info_log = LoadGlFunctionAs<GetProgramInfoLogProc>("glGetProgramInfoLog");
    g_shader.use_program = LoadGlFunctionAs<UseProgramProc>("glUseProgram");
    g_shader.delete_program = LoadGlFunctionAs<DeleteProgramProc>("glDeleteProgram");

    g_shader_functions_available = g_shader.create_shader != nullptr && g_shader.shader_source != nullptr &&
                                   g_shader.compile_shader != nullptr && g_shader.get_shader_iv != nullptr &&
                                   g_shader.get_shader_info_log != nullptr && g_shader.delete_shader != nullptr &&
                                   g_shader.create_program != nullptr && g_shader.attach_shader != nullptr &&
                                   g_shader.link_program != nullptr && g_shader.get_program_iv != nullptr &&
                                   g_shader.get_program_info_log != nullptr && g_shader.use_program != nullptr &&
                                   g_shader.delete_program != nullptr;
    return g_shader_functions_available;
}

std::string NarrowForLog(std::wstring_view value) {
    std::string output;
    output.reserve(value.size());
    for (wchar_t ch : value) {
        output.push_back(ch <= 0x7F ? static_cast<char>(ch) : '?');
    }
    return output;
}

std::string CompactLabel(std::wstring_view value) {
    std::string label = NarrowForLog(value);
    if (label.empty()) {
        label = "Untitled Node";
    }
    constexpr std::size_t kMaximumLabelLength = 34;
    if (label.size() > kMaximumLabelLength) {
        label.resize(kMaximumLabelLength - 3);
        label += "...";
    }
    return label;
}

DeckShape ShapeFromNode(deck::BookmarkNodeShapeType shape_type) {
    switch (shape_type) {
        case deck::BookmarkNodeShapeType::Hex:
            return DeckShape::HexPrism;
        case deck::BookmarkNodeShapeType::Cube:
            return DeckShape::Cube;
        case deck::BookmarkNodeShapeType::Panel:
            return DeckShape::BeveledTile;
    }
    return DeckShape::HexPrism;
}

NeonMaterialId MaterialFromNode(deck::BookmarkNodeColorTheme color_theme, std::size_t index) {
    switch (color_theme) {
        case deck::BookmarkNodeColorTheme::Green:
            return NeonMaterialId::NeonGreen;
        case deck::BookmarkNodeColorTheme::Yellow:
            return NeonMaterialId::YellowHighlight;
        case deck::BookmarkNodeColorTheme::Red:
            return NeonMaterialId::RedDanger;
        case deck::BookmarkNodeColorTheme::Mixed:
            return index % 2 == 0 ? NeonMaterialId::NeonGreen : NeonMaterialId::YellowHighlight;
    }
    return NeonMaterialId::NeonGreen;
}

DeckShape ShapeFromVault(deck::BookmarkNodeColorTheme color_theme) {
    switch (color_theme) {
        case deck::BookmarkNodeColorTheme::Green:
            return DeckShape::HexPrism;
        case deck::BookmarkNodeColorTheme::Yellow:
            return DeckShape::Cube;
        case deck::BookmarkNodeColorTheme::Red:
            return DeckShape::BeveledTile;
        case deck::BookmarkNodeColorTheme::Mixed:
            return DeckShape::Cube;
    }
    return DeckShape::HexPrism;
}

bool NodeInVault(const deck::BookmarkNode& node, const std::wstring& vault_id) {
    return node.vault_id && *node.vault_id == vault_id;
}

Vec3 RotateX(Vec3 value, float degrees) {
    const float radians = degrees * kPi / 180.0f;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return {value.x, value.y * cosine - value.z * sine, value.y * sine + value.z * cosine};
}

Vec3 RotateY(Vec3 value, float degrees) {
    const float radians = degrees * kPi / 180.0f;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return {value.x * cosine + value.z * sine, value.y, -value.x * sine + value.z * cosine};
}

float Approach(float current, float target, float amount) {
    return current + (target - current) * amount;
}

float ResponseBlend(float response, float delta_seconds) {
    if (response <= 0.0f) {
        return 1.0f;
    }
    return std::clamp(1.0f - std::exp(-response * delta_seconds), 0.0f, 1.0f);
}

void DrawOverlayText(unsigned int font_base, int x, int y, std::string_view text, float red, float green, float blue) {
    if (font_base == 0 || text.empty()) {
        return;
    }

    glColor3f(red, green, blue);
    glRasterPos2i(x, y);
    glPushAttrib(GL_LIST_BIT);
    glListBase(static_cast<GLuint>(font_base) - 32);
    glCallLists(static_cast<GLsizei>(text.size()), GL_UNSIGNED_BYTE, text.data());
    glPopAttrib();
}

void DrawGrid() {
    glLineWidth(1.0f);
    glBegin(GL_LINES);
    for (int line = -12; line <= 12; ++line) {
        const float distance = static_cast<float>(std::abs(line)) / 12.0f;
        const float intensity = line == 0 ? 1.0f : 0.66f - distance * 0.38f;
        const float alpha = line == 0 ? 0.92f : 0.44f - distance * 0.24f;
        ApplyMaterial(line == 0 ? NeonMaterialId::NeonGreen : NeonMaterialId::DimInactiveGreen, intensity, alpha);
        glVertex3f(static_cast<float>(line), -1.45f, -12.0f);
        glVertex3f(static_cast<float>(line), -1.45f, 12.0f);
        glVertex3f(-12.0f, -1.45f, static_cast<float>(line));
        glVertex3f(12.0f, -1.45f, static_cast<float>(line));
    }
    glEnd();
}

float VaultOrbitRadius(std::size_t vault_count) {
    return vault_count <= 1 ? 0.0f : std::clamp(5.7f + static_cast<float>(vault_count) * 0.34f, 6.8f, 9.0f);
}

float VaultOrbitAngle(std::size_t index, std::size_t vault_count) {
    if (vault_count == 0) {
        return kPi * 0.5f;
    }
    return kPi * 0.5f + (static_cast<float>(index) / static_cast<float>(vault_count)) * kTau;
}

Vec3 VaultOrbitPosition(std::size_t index, std::size_t vault_count, float radius) {
    const float angle = VaultOrbitAngle(index, vault_count);
    return {
        std::cos(angle) * radius,
        -0.08f + static_cast<float>(index % 3) * 0.11f,
        std::sin(angle) * radius - 0.15f,
    };
}

void DrawVaultOrbitGuide(float radius, std::size_t selected_index, std::size_t vault_count) {
    if (radius <= 0.0f || vault_count <= 1) {
        return;
    }

    glLineWidth(1.5f);
    ApplyMaterial(NeonMaterialId::DimInactiveGreen, 0.72f, 0.42f);
    glBegin(GL_LINE_LOOP);
    for (int segment = 0; segment < 112; ++segment) {
        const float angle = static_cast<float>(segment) / 112.0f * kTau;
        glVertex3f(std::cos(angle) * radius, -0.22f, std::sin(angle) * radius - 0.15f);
    }
    glEnd();

    const Vec3 slot = VaultOrbitPosition(selected_index, vault_count, radius);
    ApplyMaterial(NeonMaterialId::YellowHighlight, 0.86f, 0.52f);
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    glVertex3f(0.0f, 0.16f, 1.05f);
    glVertex3f(slot.x, slot.y + 0.1f, slot.z);
    glEnd();
}

}  // namespace

OpenGLDeckView::~OpenGLDeckView() {
    Shutdown();
}

bool OpenGLDeckView::Initialize(HWND parent, const RECT& bounds, common::Logger& logger) {
    parent_ = parent;
    logger_ = &logger;
    last_error_.clear();

    if (parent_ == nullptr) {
        SetLastErrorText(L"Deck Space parent window is missing.");
        return false;
    }
    if (!RegisterWindowClass()) {
        SetLastErrorText(L"Deck Space window class registration failed.");
        return false;
    }

    hwnd_ = CreateWindowExW(
        0,
        kDeckViewClassName,
        L"",
        WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        bounds.left,
        bounds.top,
        RectWidth(bounds),
        RectHeight(bounds),
        parent_,
        nullptr,
        GetModuleHandleW(nullptr),
        this);
    if (hwnd_ == nullptr) {
        SetLastErrorText(
            L"Deck Space OpenGL child window creation failed. Win32 error " + std::to_wstring(GetLastError()) + L".");
        return false;
    }

    width_ = RectWidth(bounds);
    height_ = RectHeight(bounds);
    if (!InitializeContext()) {
        Shutdown();
        return false;
    }
    RebuildSceneObjects();

    ShowWindow(hwnd_, SW_HIDE);
    initialized_ = true;
    return true;
}

void OpenGLDeckView::Show(const RECT& bounds) {
    if (!initialized_ || hwnd_ == nullptr) {
        return;
    }

    Resize(bounds);
    ShowWindow(hwnd_, SW_SHOW);
    SetWindowPos(hwnd_, HWND_TOP, bounds.left, bounds.top, RectWidth(bounds), RectHeight(bounds), SWP_NOACTIVATE);
    SetFocus(hwnd_);
    StartAnimation();
    Render();
}

void OpenGLDeckView::Hide() {
    StopAnimation();
    if (dragging_) {
        ReleaseCapture();
        dragging_ = false;
    }
    if (hwnd_ != nullptr) {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

void OpenGLDeckView::Resize(const RECT& bounds) {
    width_ = RectWidth(bounds);
    height_ = RectHeight(bounds);
    if (hwnd_ != nullptr) {
        SetWindowPos(hwnd_, HWND_TOP, bounds.left, bounds.top, width_, height_, SWP_NOACTIVATE);
    }
}

void OpenGLDeckView::Shutdown() {
    ReleaseGraphicsResources();

    if (hwnd_ != nullptr) {
        HWND window = hwnd_;
        if (IsWindow(window)) {
            DestroyWindow(window);
        } else {
            hwnd_ = nullptr;
        }
    }

    parent_ = nullptr;
    initialized_ = false;
}

bool OpenGLDeckView::IsInitialized() const {
    return initialized_;
}

void OpenGLDeckView::SetExitRequestedCallback(std::function<void()> callback) {
    exit_requested_callback_ = std::move(callback);
}

void OpenGLDeckView::SetOpenNodeCallback(std::function<void(deck::BookmarkNode)> callback) {
    open_node_callback_ = std::move(callback);
}

void OpenGLDeckView::SetEditNodeCallback(std::function<void(deck::BookmarkNode)> callback) {
    edit_node_callback_ = std::move(callback);
}

void OpenGLDeckView::SetDeleteNodeCallback(std::function<void(deck::BookmarkNode)> callback) {
    delete_node_callback_ = std::move(callback);
}

void OpenGLDeckView::SetEditVaultCallback(std::function<void(deck::BookmarkVault)> callback) {
    edit_vault_callback_ = std::move(callback);
}

void OpenGLDeckView::SetDeleteVaultCallback(std::function<void(deck::BookmarkVault)> callback) {
    delete_vault_callback_ = std::move(callback);
}

void OpenGLDeckView::SetLayoutChangedCallback(std::function<void(DeckLayoutMode)> callback) {
    layout_changed_callback_ = std::move(callback);
}

void OpenGLDeckView::SetLayoutMode(DeckLayoutMode mode) {
    if (layout_mode_ == mode) {
        return;
    }

    layout_mode_ = mode;
    if (initialized_) {
        RebuildSceneObjects();
        StartAnimation();
        Render();
    }
}

void OpenGLDeckView::SetBookmarkNodes(std::vector<deck::BookmarkNode> nodes) {
    SetBookmarkData(std::move(nodes), {});
}

void OpenGLDeckView::SetBookmarkData(std::vector<deck::BookmarkNode> nodes, std::vector<deck::BookmarkVault> vaults) {
    const bool first_bookmark_load = !bookmark_nodes_loaded_;
    bookmark_nodes_ = std::move(nodes);
    bookmark_vaults_ = std::move(vaults);
    bookmark_nodes_loaded_ = true;
    selected_node_index_ = 0;
    selected_vault_index_ = 0;
    hovered_node_index_.reset();
    if (active_vault_id_) {
        const auto active = std::find_if(bookmark_vaults_.begin(), bookmark_vaults_.end(), [this](const deck::BookmarkVault& vault) {
            return vault.id == *active_vault_id_;
        });
        if (active == bookmark_vaults_.end()) {
            active_vault_id_.reset();
        }
    }
    if (first_bookmark_load) {
        target_distance_ = DefaultCameraDistance();
        current_distance_ = target_distance_;
    }
    if (initialized_) {
        RebuildSceneObjects();
        Render();
    }
}

std::wstring OpenGLDeckView::LastError() const {
    return last_error_;
}

std::optional<std::wstring> OpenGLDeckView::ActiveVaultId() const {
    return active_vault_id_;
}

OpenGLDiagnostics OpenGLDeckView::Diagnostics() const {
    return diagnostics_;
}

LRESULT CALLBACK OpenGLDeckView::WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
    OpenGLDeckView* view = reinterpret_cast<OpenGLDeckView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
        view = reinterpret_cast<OpenGLDeckView*>(create->lpCreateParams);
        if (view != nullptr) {
            view->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(view));
        }
    }

    if (view != nullptr) {
        return view->HandleMessage(message, w_param, l_param);
    }

    return DefWindowProcW(hwnd, message, w_param, l_param);
}

LRESULT OpenGLDeckView::HandleMessage(UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            width_ = LOWORD(l_param);
            height_ = HIWORD(l_param);
            Render();
            return 0;
        case WM_LBUTTONDBLCLK: {
            const POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
            if (const auto picked = PickNodeAt(point)) {
                SetSelectedNodeIndex(*picked);
                OpenSelectedNode();
            }
            return 0;
        }
        case WM_LBUTTONDOWN:
            SetFocus(hwnd_);
            SetCapture(hwnd_);
            dragging_ = true;
            drag_moved_ = false;
            last_mouse_ = POINT{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
            return 0;
        case WM_MOUSEMOVE:
            if (dragging_ && (w_param & MK_LBUTTON) != 0) {
                const POINT current{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
                const int dx = current.x - last_mouse_.x;
                const int dy = current.y - last_mouse_.y;
                if (std::abs(dx) > 0 || std::abs(dy) > 0) {
                    drag_moved_ = drag_moved_ || std::abs(dx) > 2 || std::abs(dy) > 2;
                    target_yaw_degrees_ += static_cast<float>(dx) * 0.35f;
                    target_pitch_degrees_ =
                        std::clamp(target_pitch_degrees_ + static_cast<float>(dy) * 0.25f, kMinimumPitch, kMaximumPitch);
                    last_mouse_ = current;
                    StartAnimation();
                }
                return 0;
            }
            SetHoveredNodeIndex(PickNodeAt(POINT{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)}));
            break;
        case WM_LBUTTONUP:
            if (dragging_) {
                ReleaseCapture();
                dragging_ = false;
                if (!drag_moved_) {
                    const POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
                    if (const auto picked = PickNodeAt(point)) {
                        SetSelectedNodeIndex(*picked);
                        if (logger_ != nullptr && *picked < bookmark_labels_.size()) {
                            logger_->Info("Deck Node selected: " + bookmark_labels_[*picked] + ".");
                        }
                    }
                }
                return 0;
            }
            break;
        case WM_MOUSEWHEEL: {
            const int wheel_delta = GET_WHEEL_DELTA_WPARAM(w_param);
            if ((!bookmark_vaults_.empty() && !active_vault_id_) || !displayed_node_indices_.empty()) {
                MoveSelection(wheel_delta > 0 ? -1 : 1);
            } else {
                target_distance_ =
                    std::clamp(target_distance_ - static_cast<float>(wheel_delta) / WHEEL_DELTA * 0.8f, kMinimumZoom, kMaximumZoom);
            }
            StartAnimation();
            return 0;
        }
        case WM_RBUTTONUP:
            if (active_vault_id_) {
                ExitActiveVault();
                return 0;
            }
            break;
        case WM_KEYDOWN:
            switch (w_param) {
                case VK_ESCAPE:
                    if (active_vault_id_) {
                        ExitActiveVault();
                        return 0;
                    }
                    if (exit_requested_callback_) {
                        exit_requested_callback_();
                    }
                    return 0;
                case VK_BACK:
                    if (active_vault_id_) {
                        ExitActiveVault();
                    }
                    return 0;
                case VK_LEFT:
                    if (!bookmark_nodes_.empty() || !bookmark_vaults_.empty()) {
                        MoveSelection(-1);
                        return 0;
                    }
                    target_yaw_degrees_ -= 8.0f;
                    break;
                case VK_RIGHT:
                    if (!bookmark_nodes_.empty() || !bookmark_vaults_.empty()) {
                        MoveSelection(1);
                        return 0;
                    }
                    target_yaw_degrees_ += 8.0f;
                    break;
                case VK_UP:
                    target_pitch_degrees_ = std::clamp(target_pitch_degrees_ - 5.0f, kMinimumPitch, kMaximumPitch);
                    break;
                case VK_DOWN:
                    target_pitch_degrees_ = std::clamp(target_pitch_degrees_ + 5.0f, kMinimumPitch, kMaximumPitch);
                    break;
                case VK_ADD:
                case VK_OEM_PLUS:
                    AdjustZoom(-0.75f);
                    return 0;
                case VK_SUBTRACT:
                case VK_OEM_MINUS:
                    AdjustZoom(0.75f);
                    return 0;
                case 'W':
                    target_pan_z_ -= 0.35f;
                    break;
                case 'S':
                    target_pan_z_ += 0.35f;
                    break;
                case 'A':
                    target_pan_x_ -= 0.35f;
                    break;
                case 'D':
                    target_pan_x_ += 0.35f;
                    break;
                case 'R':
                    ResetCamera();
                    break;
                case 'L':
                    CycleLayoutMode();
                    return 0;
                case VK_RETURN:
                    OpenSelectedNode();
                    return 0;
                case 'E':
                    EditSelectedNode();
                    return 0;
                case VK_DELETE:
                    DeleteSelectedNode();
                    return 0;
                default:
                    return 0;
            }
            StartAnimation();
            return 0;
        case WM_TIMER:
            if (w_param == kAnimationTimerId) {
                const auto now = std::chrono::steady_clock::now();
                float delta_seconds = 1.0f / 60.0f;
                if (last_animation_time_.time_since_epoch().count() != 0) {
                    delta_seconds = std::chrono::duration<float>(now - last_animation_time_).count();
                }
                last_animation_time_ = now;
                delta_seconds = std::clamp(delta_seconds, 0.0f, kMaximumAnimationDeltaSeconds);

                for (DeckSceneObject& object : debug_objects_) {
                    AdvanceDeckAnimation(object, delta_seconds);
                }
                SmoothCamera(delta_seconds);
                Render();
                return 0;
            }
            break;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            BeginPaint(hwnd_, &paint);
            Render();
            EndPaint(hwnd_, &paint);
            return 0;
        }
        case WM_DESTROY:
            ReleaseGraphicsResources();
            return 0;
        case WM_NCDESTROY:
            if (hwnd_ != nullptr) {
                SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
            }
            hwnd_ = nullptr;
            parent_ = nullptr;
            initialized_ = false;
            return 0;
        default:
            break;
    }

    return DefWindowProcW(hwnd_, message, w_param, l_param);
}

bool OpenGLDeckView::RegisterWindowClass() {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = OpenGLDeckView::WindowProc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = kDeckViewClassName;
    window_class.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;

    return RegisterClassExW(&window_class) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool OpenGLDeckView::InitializeContext() {
    dc_ = GetDC(hwnd_);
    if (dc_ == nullptr) {
        SetLastErrorText(L"Deck Space could not get a device context.");
        return false;
    }

    PIXELFORMATDESCRIPTOR descriptor{};
    descriptor.nSize = sizeof(descriptor);
    descriptor.nVersion = 1;
    descriptor.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    descriptor.iPixelType = PFD_TYPE_RGBA;
    descriptor.cColorBits = 32;
    descriptor.cDepthBits = 24;
    descriptor.cStencilBits = 8;
    descriptor.iLayerType = PFD_MAIN_PLANE;

    const int pixel_format = ChoosePixelFormat(dc_, &descriptor);
    if (pixel_format == 0 || !SetPixelFormat(dc_, pixel_format, &descriptor)) {
        SetLastErrorText(L"Deck Space could not select an OpenGL pixel format.");
        return false;
    }

    context_ = wglCreateContext(dc_);
    if (context_ == nullptr || !wglMakeCurrent(dc_, context_)) {
        SetLastErrorText(L"Deck Space could not create an OpenGL context.");
        return false;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glShadeModel(GL_SMOOTH);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    CreateOverlayFont();
    neon_shader_ready_ = InitializeNeonShader();
    diagnostics_ = {
        .vendor = GlString(GL_VENDOR),
        .renderer = GlString(GL_RENDERER),
        .version = GlString(GL_VERSION),
    };

    if (logger_ != nullptr) {
        logger_->Info("OpenGL vendor: " + diagnostics_.vendor);
        logger_->Info("OpenGL renderer: " + diagnostics_.renderer);
        logger_->Info("OpenGL version: " + diagnostics_.version);
        logger_->Info(std::string("Deck neon shader: ") + (neon_shader_ready_ ? "enabled." : "fixed-function fallback."));
    }
    return true;
}

void OpenGLDeckView::ReleaseGraphicsResources() {
    StopAnimation();

    if (dragging_ && GetCapture() == hwnd_) {
        ReleaseCapture();
    }
    dragging_ = false;
    drag_moved_ = false;

    DestroyNeonShader();
    DestroyOverlayFont();

    if (context_ != nullptr) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(context_);
        context_ = nullptr;
    }

    if (dc_ != nullptr && hwnd_ != nullptr) {
        ReleaseDC(hwnd_, dc_);
        dc_ = nullptr;
    }

    initialized_ = false;
}

void OpenGLDeckView::CreateOverlayFont() {
    if (dc_ == nullptr || font_base_ != 0) {
        return;
    }

    overlay_font_ = CreateFontW(
        -21,
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
    if (overlay_font_ == nullptr) {
        overlay_font_ = CreateFontW(
            -21,
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
    if (overlay_font_ == nullptr) {
        return;
    }

    HGDIOBJ previous_font = SelectObject(dc_, overlay_font_);
    font_base_ = glGenLists(96);
    if (font_base_ != 0 && !wglUseFontBitmapsW(dc_, 32, 96, font_base_)) {
        glDeleteLists(font_base_, 96);
        font_base_ = 0;
    }
    SelectObject(dc_, previous_font);
}

void OpenGLDeckView::DestroyOverlayFont() {
    if (context_ != nullptr && dc_ != nullptr && font_base_ != 0) {
        wglMakeCurrent(dc_, context_);
        glDeleteLists(font_base_, 96);
        font_base_ = 0;
    }
    if (overlay_font_ != nullptr) {
        DeleteObject(overlay_font_);
        overlay_font_ = nullptr;
    }
}

bool OpenGLDeckView::InitializeNeonShader() {
    if (!LoadShaderFunctions()) {
        return false;
    }

    constexpr const char* kVertexShader = R"GLSL(
#version 120
varying vec4 cyberdeckColor;
void main() {
    cyberdeckColor = gl_Color;
    gl_Position = ftransform();
}
)GLSL";

    constexpr const char* kFragmentShader = R"GLSL(
#version 120
varying vec4 cyberdeckColor;
void main() {
    vec3 glow = vec3(0.0, 0.055, 0.018);
    vec3 color = min(cyberdeckColor.rgb * 1.35 + glow, vec3(1.0));
    gl_FragColor = vec4(color, cyberdeckColor.a);
}
)GLSL";

    auto compile_shader = [this](GLenum type, const char* source) -> GLuint {
        GLuint shader = g_shader.create_shader(type);
        if (shader == 0) {
            return 0;
        }

        const char* sources[] = {source};
        g_shader.shader_source(shader, 1, sources, nullptr);
        g_shader.compile_shader(shader);

        GLint status = 0;
        g_shader.get_shader_iv(shader, GL_COMPILE_STATUS, &status);
        if (status != 0) {
            return shader;
        }

        GLint length = 0;
        g_shader.get_shader_iv(shader, GL_INFO_LOG_LENGTH, &length);
        if (logger_ != nullptr && length > 1) {
            std::string log(static_cast<std::size_t>(length), '\0');
            GLsizei written = 0;
            g_shader.get_shader_info_log(shader, length, &written, log.data());
            logger_->Error("Deck neon shader compile failed: " + log);
        }
        g_shader.delete_shader(shader);
        return 0;
    };

    const GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, kVertexShader);
    if (vertex_shader == 0) {
        return false;
    }
    const GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, kFragmentShader);
    if (fragment_shader == 0) {
        g_shader.delete_shader(vertex_shader);
        return false;
    }

    const GLuint program = g_shader.create_program();
    if (program == 0) {
        g_shader.delete_shader(vertex_shader);
        g_shader.delete_shader(fragment_shader);
        return false;
    }

    g_shader.attach_shader(program, vertex_shader);
    g_shader.attach_shader(program, fragment_shader);
    g_shader.link_program(program);

    g_shader.delete_shader(vertex_shader);
    g_shader.delete_shader(fragment_shader);

    GLint status = 0;
    g_shader.get_program_iv(program, GL_LINK_STATUS, &status);
    if (status == 0) {
        GLint length = 0;
        g_shader.get_program_iv(program, GL_INFO_LOG_LENGTH, &length);
        if (logger_ != nullptr && length > 1) {
            std::string log(static_cast<std::size_t>(length), '\0');
            GLsizei written = 0;
            g_shader.get_program_info_log(program, length, &written, log.data());
            logger_->Error("Deck neon shader link failed: " + log);
        }
        g_shader.delete_program(program);
        return false;
    }

    neon_program_ = program;
    return true;
}

void OpenGLDeckView::DestroyNeonShader() {
    if (context_ != nullptr && dc_ != nullptr && neon_program_ != 0 && g_shader.delete_program != nullptr) {
        wglMakeCurrent(dc_, context_);
        g_shader.delete_program(neon_program_);
    }
    neon_program_ = 0;
    neon_shader_ready_ = false;
}

void OpenGLDeckView::BeginNeonShader() {
    if (neon_shader_ready_ && g_shader.use_program != nullptr) {
        g_shader.use_program(neon_program_);
    }
}

void OpenGLDeckView::EndNeonShader() {
    if (neon_shader_ready_ && g_shader.use_program != nullptr) {
        g_shader.use_program(0);
    }
}

void OpenGLDeckView::RebuildSceneObjects() {
    if (!bookmark_nodes_loaded_) {
        BuildDebugScene();
        return;
    }

    cube_mesh_ = GenerateCubeMesh(1.0f);
    hex_prism_mesh_ = GenerateHexPrismMesh(0.82f, 0.48f);
    beveled_tile_mesh_ = GenerateBeveledTileMesh();

    debug_objects_.clear();
    bookmark_labels_.clear();
    bookmark_urls_.clear();
    bookmark_favicon_labels_.clear();

    if (!bookmark_vaults_.empty() && !active_vault_id_) {
        BuildVaultScene();
        return;
    }

    std::vector<std::size_t> node_indices;
    if (active_vault_id_) {
        for (std::size_t index = 0; index < bookmark_nodes_.size(); ++index) {
            if (NodeInVault(bookmark_nodes_[index], *active_vault_id_)) {
                node_indices.push_back(index);
            }
        }
    } else {
        node_indices.reserve(bookmark_nodes_.size());
        for (std::size_t index = 0; index < bookmark_nodes_.size(); ++index) {
            node_indices.push_back(index);
        }
    }

    BuildNodeScene(node_indices);
}

void OpenGLDeckView::BuildNodeScene(const std::vector<std::size_t>& node_indices) {
    displayed_node_indices_ = node_indices;
    if (displayed_node_indices_.empty()) {
        if (logger_ != nullptr) {
            logger_->Info(active_vault_id_ ? "Deck Vault scene ready: no saved Nodes." : "Deck bookmark scene ready: no saved Nodes.");
        }
        return;
    }

    if (std::find(displayed_node_indices_.begin(), displayed_node_indices_.end(), selected_node_index_) ==
        displayed_node_indices_.end()) {
        selected_node_index_ = displayed_node_indices_.front();
    }

    const auto selected_iterator = std::find(displayed_node_indices_.begin(), displayed_node_indices_.end(), selected_node_index_);
    const std::size_t selected_display_index =
        selected_iterator == displayed_node_indices_.end()
            ? 0
            : static_cast<std::size_t>(std::distance(displayed_node_indices_.begin(), selected_iterator));
    const std::vector<DeckLayoutItem> layout = BuildDeckLayout(layout_mode_, displayed_node_indices_.size());
    for (std::size_t display_index = 0; display_index < displayed_node_indices_.size(); ++display_index) {
        const std::size_t node_index = displayed_node_indices_[display_index];
        const deck::BookmarkNode& node = bookmark_nodes_[node_index];
        const std::size_t layout_index =
            (display_index + displayed_node_indices_.size() - selected_display_index) % displayed_node_indices_.size();
        const DeckLayoutItem& item = layout[layout_index];
        const bool selected = node_index == selected_node_index_;
        const bool hovered = hovered_node_index_ && *hovered_node_index_ == display_index;
        Vec3 position = item.position;
        if (node.deck_position) {
            position = Vec3{node.deck_position->x, node.deck_position->y, node.deck_position->z};
        }
        DeckShape shape = ShapeFromNode(node.shape_type);
        if (layout_mode_ == DeckLayoutMode::GridDeck) {
            shape = DeckShape::BeveledTile;
        } else if (layout_mode_ == DeckLayoutMode::CubeOrbit && node.shape_type == deck::BookmarkNodeShapeType::Hex) {
            shape = DeckShape::Cube;
        }

        debug_objects_.push_back({
            .shape = shape,
            .transform = {
                .position = position,
                .rotation_degrees = item.rotation_degrees,
                .scale = selected ? item.scale : Vec3{item.scale.x * 0.88f, item.scale.y * 0.88f, item.scale.z * 0.88f},
            },
            .material = MaterialFromNode(node.color_theme, node_index),
            .hovered = hovered,
            .selected = selected,
            .animation = {
                .enabled = bookmark_nodes_.size() <= 100,
                .speed_scale = 1.0f,
                .idle_rotation_degrees_per_second = selected ? 7.0f : (display_index % 2 == 0 ? 3.0f : -2.5f),
                .orbit_radius = selected ? 0.06f : 0.018f,
                .orbit_degrees_per_second = display_index % 2 == 0 ? 2.0f : -1.6f,
                .orbit_phase_degrees = static_cast<float>((display_index * 47) % 360),
                .transition_response = 6.5f,
                .hover_pulse_hz = 0.65f,
                .hover_pulse_scale = 0.02f,
                .selected_pulse_hz = 0.48f,
                .selected_pulse_scale = 0.045f,
            },
            .animation_state = {},
        });
        bookmark_labels_.push_back(CompactLabel(node.title));
        bookmark_urls_.push_back(CompactLabel(node.url));
        bookmark_favicon_labels_.push_back(node.favicon_path && !node.favicon_path->empty() ? "[FAV]" : "[NODE]");
    }

    for (DeckSceneObject& object : debug_objects_) {
        InitializeDeckAnimation(object);
    }

    if (logger_ != nullptr) {
        logger_->Info("Deck bookmark scene ready: nodes=" + std::to_string(displayed_node_indices_.size()) + ".");
    }
}

void OpenGLDeckView::BuildVaultScene() {
    displayed_node_indices_.clear();
    if (selected_vault_index_ >= bookmark_vaults_.size()) {
        selected_vault_index_ = 0;
    }

    const std::size_t vault_count = bookmark_vaults_.size();
    const float vault_radius = VaultOrbitRadius(vault_count);
    for (std::size_t index = 0; index < bookmark_vaults_.size(); ++index) {
        const deck::BookmarkVault& vault = bookmark_vaults_[index];
        const bool selected = index == selected_vault_index_;
        const bool hovered = hovered_node_index_ && *hovered_node_index_ == index;
        Vec3 position{0.0f, 0.44f, 1.05f};
        Vec3 rotation{0.0f, 0.0f, 0.0f};
        Vec3 scale{1.42f, 1.42f, 1.42f};
        if (!selected && vault_count > 1) {
            const float angle = VaultOrbitAngle(index, vault_count);
            const float front_factor = (std::sin(angle) + 1.0f) * 0.5f;
            const float orbit_scale = 0.72f + front_factor * 0.22f;
            position = VaultOrbitPosition(index, vault_count, vault_radius);
            rotation = {0.0f, -angle * 180.0f / kPi + 90.0f, 0.0f};
            scale = {orbit_scale, orbit_scale, orbit_scale};
        }
        std::size_t child_count = 0;
        for (const deck::BookmarkNode& node : bookmark_nodes_) {
            if (NodeInVault(node, vault.id)) {
                ++child_count;
            }
        }

        debug_objects_.push_back({
            .shape = ShapeFromVault(vault.color_theme),
            .transform = {
                .position = position,
                .rotation_degrees = rotation,
                .scale = scale,
            },
            .material = MaterialFromNode(vault.color_theme, index),
            .hovered = hovered,
            .selected = selected,
            .animation = {
                .enabled = true,
                .speed_scale = 1.0f,
                .idle_rotation_degrees_per_second = selected ? 12.0f : (index % 2 == 0 ? 5.0f : -4.0f),
                .orbit_radius = selected ? 0.08f : 0.026f,
                .orbit_degrees_per_second = index % 2 == 0 ? 3.0f : -2.4f,
                .orbit_phase_degrees = static_cast<float>((index * 59) % 360),
                .transition_response = 6.8f,
                .hover_pulse_hz = 0.72f,
                .hover_pulse_scale = 0.03f,
                .selected_pulse_hz = 0.52f,
                .selected_pulse_scale = 0.065f,
            },
            .animation_state = {},
        });
        bookmark_labels_.push_back(CompactLabel(vault.name));
        bookmark_urls_.push_back(std::to_string(child_count) + " Nodes");
        bookmark_favicon_labels_.push_back("[VAULT]");
    }

    if (vault_count > 1 && selected_vault_index_ < bookmark_vaults_.size()) {
        const deck::BookmarkVault& selected_vault = bookmark_vaults_[selected_vault_index_];
        const float angle = VaultOrbitAngle(selected_vault_index_, vault_count);
        const float front_factor = (std::sin(angle) + 1.0f) * 0.5f;
        const float marker_scale = 0.32f + front_factor * 0.08f;
        debug_objects_.push_back({
            .shape = ShapeFromVault(selected_vault.color_theme),
            .transform = {
                .position = VaultOrbitPosition(selected_vault_index_, vault_count, vault_radius),
                .rotation_degrees = {0.0f, -angle * 180.0f / kPi + 90.0f, 0.0f},
                .scale = {marker_scale, marker_scale, marker_scale},
            },
            .material = MaterialFromNode(selected_vault.color_theme, selected_vault_index_),
            .hovered = false,
            .selected = true,
            .animation = {
                .enabled = true,
                .speed_scale = 1.0f,
                .idle_rotation_degrees_per_second = 16.0f,
                .orbit_radius = 0.035f,
                .orbit_degrees_per_second = -4.2f,
                .orbit_phase_degrees = static_cast<float>((selected_vault_index_ * 59 + 31) % 360),
                .transition_response = 6.8f,
                .hover_pulse_hz = 0.72f,
                .hover_pulse_scale = 0.02f,
                .selected_pulse_hz = 0.72f,
                .selected_pulse_scale = 0.09f,
            },
            .animation_state = {},
        });
    }

    for (DeckSceneObject& object : debug_objects_) {
        InitializeDeckAnimation(object);
    }

    if (logger_ != nullptr) {
        logger_->Info("Deck Vault overview ready: vaults=" + std::to_string(bookmark_vaults_.size()) + ".");
    }
}

void OpenGLDeckView::BuildDebugScene() {
    cube_mesh_ = GenerateCubeMesh(1.0f);
    hex_prism_mesh_ = GenerateHexPrismMesh(0.82f, 0.48f);
    beveled_tile_mesh_ = GenerateBeveledTileMesh();

    debug_objects_.clear();
    bookmark_labels_.clear();
    bookmark_urls_.clear();
    bookmark_favicon_labels_.clear();
    debug_objects_.push_back({
        .shape = DeckShape::HexPrism,
        .transform = {.position = {-1.65f, 0.05f, 0.15f}, .rotation_degrees = {0.0f, -18.0f, 0.0f}, .scale = {1.05f, 1.05f, 1.05f}},
        .material = NeonMaterialId::NeonGreen,
        .hovered = false,
        .selected = true,
        .animation = {
            .enabled = true,
            .speed_scale = 1.0f,
            .idle_rotation_degrees_per_second = 18.0f,
            .orbit_radius = 0.12f,
            .orbit_degrees_per_second = 9.0f,
            .orbit_phase_degrees = 215.0f,
            .transition_response = 6.5f,
            .hover_pulse_hz = 0.65f,
            .hover_pulse_scale = 0.025f,
            .selected_pulse_hz = 0.48f,
            .selected_pulse_scale = 0.045f,
        },
        .animation_state = {},
    });
    debug_objects_.push_back({
        .shape = DeckShape::HexPrism,
        .transform = {.position = {0.0f, 0.02f, -0.45f}, .rotation_degrees = {0.0f, 18.0f, 0.0f}, .scale = {0.92f, 0.92f, 0.92f}},
        .material = NeonMaterialId::YellowHighlight,
        .hovered = true,
        .selected = false,
        .animation = {
            .enabled = true,
            .speed_scale = 1.0f,
            .idle_rotation_degrees_per_second = -14.0f,
            .orbit_radius = 0.18f,
            .orbit_degrees_per_second = -7.0f,
            .orbit_phase_degrees = 25.0f,
            .transition_response = 7.5f,
            .hover_pulse_hz = 0.78f,
            .hover_pulse_scale = 0.038f,
            .selected_pulse_hz = 0.55f,
            .selected_pulse_scale = 0.04f,
        },
        .animation_state = {},
    });
    debug_objects_.push_back({
        .shape = DeckShape::Cube,
        .transform = {.position = {1.6f, 0.1f, 0.1f}, .rotation_degrees = {10.0f, 35.0f, 0.0f}, .scale = {0.78f, 0.78f, 0.78f}},
        .material = NeonMaterialId::RedDanger,
        .hovered = false,
        .selected = false,
        .animation = {
            .enabled = true,
            .speed_scale = 1.0f,
            .idle_rotation_degrees_per_second = 24.0f,
            .orbit_radius = 0.08f,
            .orbit_degrees_per_second = 5.0f,
            .orbit_phase_degrees = 125.0f,
            .transition_response = 6.0f,
            .hover_pulse_hz = 0.7f,
            .hover_pulse_scale = 0.02f,
            .selected_pulse_hz = 0.45f,
            .selected_pulse_scale = 0.03f,
        },
        .animation_state = {},
    });
    debug_objects_.push_back({
        .shape = DeckShape::BeveledTile,
        .transform = {.position = {0.0f, -0.95f, 0.45f}, .rotation_degrees = {-68.0f, 0.0f, 0.0f}, .scale = {1.3f, 1.3f, 1.3f}},
        .material = NeonMaterialId::DimInactiveGreen,
        .hovered = false,
        .selected = false,
        .animation = {
            .enabled = true,
            .speed_scale = 1.0f,
            .idle_rotation_degrees_per_second = 0.0f,
            .orbit_radius = 0.0f,
            .orbit_degrees_per_second = 0.0f,
            .orbit_phase_degrees = 0.0f,
            .transition_response = 5.0f,
            .hover_pulse_hz = 0.5f,
            .hover_pulse_scale = 0.015f,
            .selected_pulse_hz = 0.4f,
            .selected_pulse_scale = 0.02f,
        },
        .animation_state = {},
    });
    for (DeckSceneObject& object : debug_objects_) {
        InitializeDeckAnimation(object);
    }

    if (logger_ != nullptr) {
        logger_->Info(
            "Deck debug geometry scene ready: hex=" + std::to_string(hex_prism_mesh_.vertices.size()) +
            " cube=" + std::to_string(cube_mesh_.vertices.size()) +
            " tile=" + std::to_string(beveled_tile_mesh_.vertices.size()) +
            " objects=" + std::to_string(debug_objects_.size()) + ".");
        logger_->Info("Deck animation system ready: delta-time idle rotation, pulse, and orbit enabled.");
    }
}

void OpenGLDeckView::RenderDeckObject(const DeckSceneObject& object) {
    const DeckMesh* mesh = nullptr;
    switch (object.shape) {
        case DeckShape::HexPrism:
            mesh = &hex_prism_mesh_;
            break;
        case DeckShape::Cube:
            mesh = &cube_mesh_;
            break;
        case DeckShape::BeveledTile:
            mesh = &beveled_tile_mesh_;
            break;
    }
    if (mesh == nullptr || mesh->vertices.empty()) {
        return;
    }

    glPushMatrix();
    const Vec3& position =
        object.animation_state.initialized ? object.animation_state.rendered_position : object.transform.position;
    const Vec3& scale = object.animation_state.initialized ? object.animation_state.rendered_scale : object.transform.scale;

    glTranslatef(position.x, position.y, position.z);
    glRotatef(object.transform.rotation_degrees.x, 1.0f, 0.0f, 0.0f);
    glRotatef(object.transform.rotation_degrees.y, 0.0f, 1.0f, 0.0f);
    glRotatef(object.transform.rotation_degrees.z, 0.0f, 0.0f, 1.0f);
    glScalef(scale.x, scale.y, scale.z);
    RenderMesh(*mesh, object.material, object.hovered, object.selected, object.animation_state.glow_pulse);
    glPopMatrix();
}

void OpenGLDeckView::RenderMesh(
    const DeckMesh& mesh,
    NeonMaterialId material,
    bool hovered,
    bool selected,
    float glow_pulse) {
    const float pulse = std::clamp(glow_pulse, 0.0f, 0.18f);
    const float face_intensity = std::min(1.0f, (selected ? 0.68f : (hovered ? 0.58f : 0.42f)) + pulse * 1.8f);
    const float face_alpha = std::min(0.9f, (selected ? 0.72f : (hovered ? 0.62f : 0.46f)) + pulse * 1.3f);
    ApplyMaterial(material, face_intensity, face_alpha);
    for (const MeshFace& face : mesh.faces) {
        glBegin(GL_POLYGON);
        for (int index : face.indices) {
            if (index >= 0 && index < static_cast<int>(mesh.vertices.size())) {
                const Vec3& vertex = mesh.vertices[static_cast<std::size_t>(index)];
                glVertex3f(vertex.x, vertex.y, vertex.z);
            }
        }
        glEnd();
    }

    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glLineWidth((selected ? 8.0f : (hovered ? 6.0f : 4.0f)) + pulse * 18.0f);
    ApplyHaloMaterial(material, std::min(1.0f, (selected ? 0.9f : (hovered ? 0.7f : 0.45f)) + pulse * 2.0f));
    const float halo_scale = 1.035f + pulse * 0.18f;
    for (const MeshFace& face : mesh.faces) {
        glBegin(GL_POLYGON);
        for (int index : face.indices) {
            if (index >= 0 && index < static_cast<int>(mesh.vertices.size())) {
                const Vec3& vertex = mesh.vertices[static_cast<std::size_t>(index)];
                glVertex3f(vertex.x * halo_scale, vertex.y * halo_scale, vertex.z * halo_scale);
            }
        }
        glEnd();
    }

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    ApplyLineMaterial(material, std::min(1.35f, (selected ? 1.2f : 1.0f) + pulse * 1.5f), 1.0f);
    for (const MeshFace& face : mesh.faces) {
        glBegin(GL_POLYGON);
        for (int index : face.indices) {
            if (index >= 0 && index < static_cast<int>(mesh.vertices.size())) {
                const Vec3& vertex = mesh.vertices[static_cast<std::size_t>(index)];
                glVertex3f(vertex.x, vertex.y, vertex.z);
            }
        }
        glEnd();
    }
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void OpenGLDeckView::Render() {
    if (hwnd_ == nullptr || dc_ == nullptr || context_ == nullptr) {
        return;
    }
    if (!wglMakeCurrent(dc_, context_)) {
        return;
    }

    const int width = std::max(1, width_);
    const int height = std::max(1, height_);
    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const double aspect = static_cast<double>(width) / static_cast<double>(height);
    const double near_plane = 0.1;
    const double far_plane = 100.0;
    const double top = std::tan(60.0 * 3.14159265358979323846 / 360.0) * near_plane;
    const double right = top * aspect;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-right, right, -top, top, near_plane, far_plane);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -current_distance_);
    glRotatef(current_pitch_degrees_, 1.0f, 0.0f, 0.0f);
    glRotatef(current_yaw_degrees_, 0.0f, 1.0f, 0.0f);
    glTranslatef(-current_pan_x_, -0.15f, -current_pan_z_);

    BeginNeonShader();
    glLineWidth(1.0f);
    DrawGrid();
    if (!bookmark_vaults_.empty() && !active_vault_id_) {
        DrawVaultOrbitGuide(VaultOrbitRadius(bookmark_vaults_.size()), selected_vault_index_, bookmark_vaults_.size());
    }

    for (const DeckSceneObject& object : debug_objects_) {
        RenderDeckObject(object);
    }
    EndNeonShader();

    DrawNodeLabels();
    DrawHelpOverlay();
    SwapBuffers(dc_);
}

void OpenGLDeckView::DrawNodeLabels() {
    if (font_base_ == 0 || bookmark_labels_.empty()) {
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glPushAttrib(GL_LIST_BIT);
    glListBase(static_cast<GLuint>(font_base_) - 32);

    const std::size_t label_count = std::min(bookmark_labels_.size(), debug_objects_.size());
    for (std::size_t index = 0; index < label_count; ++index) {
        const DeckSceneObject& object = debug_objects_[index];
        const Vec3& position =
            object.animation_state.initialized ? object.animation_state.rendered_position : object.transform.position;
        const Vec3& scale = object.animation_state.initialized ? object.animation_state.rendered_scale : object.transform.scale;
        const std::string& label = bookmark_labels_[index];
        if (label.empty()) {
            continue;
        }

        if (object.selected) {
            glColor3f(1.0f, 1.0f, 0.0f);
        } else if (position.z > 0.0f) {
            glColor3f(0.0f, 0.9f, 0.25f);
        } else {
            glColor3f(0.0f, 0.42f, 0.18f);
        }

        if (index < bookmark_favicon_labels_.size()) {
            glRasterPos3f(position.x - 0.48f * scale.x, position.y + 1.05f * scale.y, position.z);
            const std::string& favicon = bookmark_favicon_labels_[index];
            glCallLists(static_cast<GLsizei>(favicon.size()), GL_UNSIGNED_BYTE, favicon.data());
        }
        glRasterPos3f(position.x - 0.42f * scale.x, position.y + 0.82f * scale.y, position.z);
        glCallLists(static_cast<GLsizei>(label.size()), GL_UNSIGNED_BYTE, label.data());
    }

    glPopAttrib();
    glEnable(GL_DEPTH_TEST);
}

std::optional<POINT> OpenGLDeckView::ProjectNodeCenter(std::size_t index) const {
    if (index >= debug_objects_.size() || width_ <= 0 || height_ <= 0) {
        return std::nullopt;
    }

    const DeckSceneObject& object = debug_objects_[index];
    Vec3 point = object.animation_state.initialized ? object.animation_state.rendered_position : object.transform.position;
    point.x -= current_pan_x_;
    point.y -= 0.15f;
    point.z -= current_pan_z_;
    point = RotateY(point, current_yaw_degrees_);
    point = RotateX(point, current_pitch_degrees_);
    point.z -= current_distance_;
    if (point.z >= -0.2f) {
        return std::nullopt;
    }

    constexpr float fov_y_degrees = 60.0f;
    const float tan_half_fov = std::tan(fov_y_degrees * 3.14159265358979323846f / 360.0f);
    const float aspect = static_cast<float>(std::max(1, width_)) / static_cast<float>(std::max(1, height_));
    const float ndc_x = point.x / (-point.z * tan_half_fov * aspect);
    const float ndc_y = point.y / (-point.z * tan_half_fov);
    if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y) || ndc_x < -1.35f || ndc_x > 1.35f || ndc_y < -1.35f ||
        ndc_y > 1.35f) {
        return std::nullopt;
    }

    return POINT{
        static_cast<LONG>((ndc_x * 0.5f + 0.5f) * static_cast<float>(width_)),
        static_cast<LONG>((0.5f - ndc_y * 0.5f) * static_cast<float>(height_)),
    };
}

std::optional<std::size_t> OpenGLDeckView::PickNodeAt(POINT point) const {
    if (bookmark_labels_.empty()) {
        return std::nullopt;
    }

    float best_distance_squared = 0.0f;
    std::optional<std::size_t> best_index;
    const std::size_t count = std::min(bookmark_labels_.size(), debug_objects_.size());
    for (std::size_t index = 0; index < count; ++index) {
        const auto projected = ProjectNodeCenter(index);
        if (!projected) {
            continue;
        }

        const DeckSceneObject& object = debug_objects_[index];
        const Vec3& scale = object.animation_state.initialized ? object.animation_state.rendered_scale : object.transform.scale;
        const float pick_radius = std::clamp(34.0f * std::max({scale.x, scale.y, scale.z}), 26.0f, 82.0f);
        const float dx = static_cast<float>(point.x - projected->x);
        const float dy = static_cast<float>(point.y - projected->y);
        const float distance_squared = dx * dx + dy * dy;
        if (distance_squared <= pick_radius * pick_radius && (!best_index || distance_squared < best_distance_squared)) {
            best_index = index;
            best_distance_squared = distance_squared;
        }
    }

    return best_index;
}

void OpenGLDeckView::SetSelectedNodeIndex(std::size_t index) {
    if (!bookmark_vaults_.empty() && !active_vault_id_) {
        if (index >= bookmark_vaults_.size()) {
            return;
        }
        selected_vault_index_ = index;
        RebuildSceneObjects();
        StartAnimation();
        Render();
        return;
    }

    if (index >= displayed_node_indices_.size()) {
        return;
    }

    selected_node_index_ = displayed_node_indices_[index];
    RebuildSceneObjects();
    StartAnimation();
    Render();
}

void OpenGLDeckView::SetHoveredNodeIndex(std::optional<std::size_t> index) {
    if (index == hovered_node_index_) {
        return;
    }
    if (index && *index >= debug_objects_.size()) {
        index.reset();
    }

    hovered_node_index_ = index;
    RebuildSceneObjects();
    StartAnimation();
    Render();
}

void OpenGLDeckView::MoveSelection(int direction) {
    if (!bookmark_vaults_.empty() && !active_vault_id_) {
        const int count = static_cast<int>(bookmark_vaults_.size());
        if (count <= 0) {
            return;
        }
        const int current = static_cast<int>(selected_vault_index_);
        selected_vault_index_ = static_cast<std::size_t>((current + direction + count) % count);
        RebuildSceneObjects();
        StartAnimation();
        Render();
        return;
    }

    if (displayed_node_indices_.empty()) {
        return;
    }

    const int count = static_cast<int>(displayed_node_indices_.size());
    const auto found = std::find(displayed_node_indices_.begin(), displayed_node_indices_.end(), selected_node_index_);
    const int current =
        found == displayed_node_indices_.end() ? 0 : static_cast<int>(std::distance(displayed_node_indices_.begin(), found));
    const int next = (current + direction + count) % count;
    selected_node_index_ = displayed_node_indices_[static_cast<std::size_t>(next)];
    RebuildSceneObjects();
    StartAnimation();
    Render();
}

void OpenGLDeckView::EnterSelectedVault() {
    if (bookmark_vaults_.empty() || selected_vault_index_ >= bookmark_vaults_.size()) {
        return;
    }

    active_vault_id_ = bookmark_vaults_[selected_vault_index_].id;
    hovered_node_index_.reset();
    selected_node_index_ = 0;
    target_yaw_degrees_ += 72.0f;
    target_distance_ = DefaultCameraDistance();
    RebuildSceneObjects();
    if (!displayed_node_indices_.empty()) {
        selected_node_index_ = displayed_node_indices_.front();
        RebuildSceneObjects();
    }
    StartAnimation();
    Render();
    if (logger_ != nullptr) {
        logger_->Info("Entered Deck Vault: " + NarrowForLog(bookmark_vaults_[selected_vault_index_].name) + ".");
    }
}

void OpenGLDeckView::ExitActiveVault() {
    if (!active_vault_id_) {
        return;
    }

    active_vault_id_.reset();
    hovered_node_index_.reset();
    target_yaw_degrees_ -= 72.0f;
    target_distance_ = DefaultCameraDistance();
    RebuildSceneObjects();
    StartAnimation();
    Render();
    if (logger_ != nullptr) {
        logger_->Info("Returned to Deck Vault overview.");
    }
}

void OpenGLDeckView::CycleLayoutMode() {
    layout_mode_ = NextLayoutMode(layout_mode_);
    RebuildSceneObjects();
    StartAnimation();
    Render();
    if (layout_changed_callback_) {
        layout_changed_callback_(layout_mode_);
    }
    if (logger_ != nullptr) {
        logger_->Info(std::string("Deck layout changed: ") + ToLayoutModeString(layout_mode_) + ".");
    }
}

void OpenGLDeckView::OpenSelectedNode() {
    if (!bookmark_vaults_.empty() && !active_vault_id_) {
        EnterSelectedVault();
        return;
    }
    if (selected_node_index_ >= bookmark_nodes_.size() || !open_node_callback_) {
        return;
    }

    open_node_callback_(bookmark_nodes_[selected_node_index_]);
}

void OpenGLDeckView::EditSelectedNode() {
    if (!bookmark_vaults_.empty() && !active_vault_id_) {
        if (selected_vault_index_ < bookmark_vaults_.size() && edit_vault_callback_) {
            edit_vault_callback_(bookmark_vaults_[selected_vault_index_]);
        }
        return;
    }
    if (selected_node_index_ >= bookmark_nodes_.size() || !edit_node_callback_) {
        return;
    }

    edit_node_callback_(bookmark_nodes_[selected_node_index_]);
}

void OpenGLDeckView::DeleteSelectedNode() {
    if (!bookmark_vaults_.empty() && !active_vault_id_) {
        if (selected_vault_index_ < bookmark_vaults_.size() && delete_vault_callback_) {
            delete_vault_callback_(bookmark_vaults_[selected_vault_index_]);
        }
        return;
    }
    if (selected_node_index_ >= bookmark_nodes_.size() || !delete_node_callback_) {
        return;
    }

    delete_node_callback_(bookmark_nodes_[selected_node_index_]);
}

void OpenGLDeckView::DrawHelpOverlay() {
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, static_cast<double>(std::max(1, width_)), static_cast<double>(std::max(1, height_)), 0.0, -1.0, 1.0);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    const bool in_vault_overview = !bookmark_vaults_.empty() && !active_vault_id_;
    DrawOverlayText(font_base_, 16, 28, active_vault_id_ ? "DECK VAULT" : "DECK SPACE", 1.0f, 1.0f, 0.0f);
    DrawOverlayText(font_base_, 16, 56, "Drag: orbit   Wheel/Arrows: rotate selection   +/-: zoom   R: reset", 0.0f, 1.0f, 0.0f);
    DrawOverlayText(
        font_base_,
        16,
        84,
        active_vault_id_ ? "Enter: open Node   Right click/Backspace/Esc: leave Vault"
                         : (in_vault_overview ? "Enter/double click: enter Vault   Esc: browser" : "Enter: open Node   Esc: browser"),
        0.0f,
        0.75f,
        0.0f);
    DrawOverlayText(font_base_, 16, 112, in_vault_overview ? "E: rename Vault   Del: delete Vault   L: switch inner layout" : "E: edit Node   Del: delete Node   L: switch layout", 0.85f, 0.85f, 0.0f);
    if (bookmark_nodes_loaded_ && bookmark_labels_.empty()) {
        DrawOverlayText(font_base_, 16, std::max(144, height_ / 2 - 10), active_vault_id_ ? "VAULT EMPTY" : "NO NODES FOUND", 1.0f, 1.0f, 0.0f);
        DrawOverlayText(
            font_base_,
            16,
            std::max(172, height_ / 2 + 18),
            active_vault_id_ ? "PRESS ADD NODE TO STORE THE ACTIVE SITE HERE" : "VISIT A SITE AND PRESS ADD NODE",
            0.0f,
            1.0f,
            0.0f);
    } else if (bookmark_labels_.empty()) {
        DrawOverlayText(font_base_, 16, std::max(144, height_ - 48), "NODE LABEL PIPELINE: bitmap overlay ready", 1.0f, 1.0f, 0.0f);
        DrawOverlayText(font_base_, 16, std::max(172, height_ - 20), "Test Node: Example Domain", 0.0f, 1.0f, 0.0f);
    } else {
        DrawOverlayText(
            font_base_,
            16,
            std::max(144, height_ - 72),
            in_vault_overview ? std::string("VAULT ATLAS: ") + std::to_string(bookmark_vaults_.size()) + " VAULTS"
                              : std::string("LAYOUT ") + ToLayoutModeString(layout_mode_) + ": " +
                                    std::to_string(displayed_node_indices_.size()) + " NODES",
            1.0f,
            1.0f,
            0.0f);
        const std::size_t label_count = std::min<std::size_t>(bookmark_labels_.size(), 3);
        const int label_y = std::max(172, height_ - 96);
        for (std::size_t index = 0; index < label_count; ++index) {
            DrawOverlayText(
                font_base_,
                16,
                label_y + static_cast<int>(index) * 26,
                in_vault_overview ? "Vault: " + bookmark_labels_[index] : "Node: " + bookmark_labels_[index],
                0.0f,
                1.0f,
                0.0f);
        }
        std::size_t info_index = hovered_node_index_.value_or(in_vault_overview ? selected_vault_index_ : 0);
        if (!in_vault_overview) {
            const auto selected_iterator = std::find(displayed_node_indices_.begin(), displayed_node_indices_.end(), selected_node_index_);
            if (!hovered_node_index_ && selected_iterator != displayed_node_indices_.end()) {
                info_index = static_cast<std::size_t>(std::distance(displayed_node_indices_.begin(), selected_iterator));
            }
        }
        if (info_index < bookmark_labels_.size() && info_index < bookmark_urls_.size()) {
            DrawOverlayText(font_base_, 16, 144, in_vault_overview ? "SELECTED VAULT" : "SELECTED NODE", 1.0f, 1.0f, 0.0f);
            DrawOverlayText(font_base_, 16, 172, bookmark_labels_[info_index], 0.0f, 1.0f, 0.0f);
            DrawOverlayText(font_base_, 16, 200, bookmark_urls_[info_index], 1.0f, 1.0f, 0.0f);
            if (info_index < bookmark_favicon_labels_.size()) {
                DrawOverlayText(font_base_, 16, 228, "IDENTITY " + bookmark_favicon_labels_[info_index], 1.0f, 1.0f, 0.0f);
            }
            DrawOverlayText(font_base_, 16, 256, in_vault_overview ? "ACTIONS: ENTER Enter/double click   RENAME E   DELETE Del" : "ACTIONS: OPEN Enter/double click   EDIT E   DELETE Del", 0.0f, 0.75f, 0.0f);
            DrawOverlayText(font_base_, 16, 284, in_vault_overview ? "DELETING A VAULT KEEPS ITS NODES AS LOOSE NODES" : "RIGHT CLICK OR BACKSPACE RETURNS TO VAULT ATLAS WHEN INSIDE A VAULT", 1.0f, 0.0f, 0.0f);
            DrawOverlayText(font_base_, 16, 312, "LAYOUT L: switch Hex Ring / Cube Orbit / Grid Deck", 0.0f, 0.75f, 0.0f);
        }
    }
    glEnable(GL_DEPTH_TEST);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

void OpenGLDeckView::SmoothCamera(float delta_seconds) {
    const float camera_blend = ResponseBlend(10.5f, delta_seconds);
    const float zoom_blend = ResponseBlend(12.0f, delta_seconds);
    current_yaw_degrees_ = Approach(current_yaw_degrees_, target_yaw_degrees_, camera_blend);
    current_pitch_degrees_ = Approach(current_pitch_degrees_, target_pitch_degrees_, camera_blend);
    current_distance_ = Approach(current_distance_, target_distance_, zoom_blend);
    current_pan_x_ = Approach(current_pan_x_, target_pan_x_, camera_blend);
    current_pan_z_ = Approach(current_pan_z_, target_pan_z_, camera_blend);
}

void OpenGLDeckView::ResetCamera() {
    target_yaw_degrees_ = 0.0f;
    target_pitch_degrees_ = 19.0f;
    target_distance_ = DefaultCameraDistance();
    target_pan_x_ = 0.0f;
    target_pan_z_ = 0.0f;
}

void OpenGLDeckView::AdjustZoom(float delta_distance) {
    target_distance_ = std::clamp(target_distance_ + delta_distance, kMinimumZoom, kMaximumZoom);
    StartAnimation();
    Render();
}

float OpenGLDeckView::DefaultCameraDistance() const {
    return !bookmark_vaults_.empty() && !active_vault_id_ ? kVaultAtlasCameraDistance : kDeckCameraDistance;
}

void OpenGLDeckView::StartAnimation() {
    if (hwnd_ == nullptr || animation_timer_active_) {
        return;
    }

    last_animation_time_ = std::chrono::steady_clock::now();
    SetTimer(hwnd_, kAnimationTimerId, kAnimationTimerMs, nullptr);
    animation_timer_active_ = true;
}

void OpenGLDeckView::StopAnimation() {
    if (hwnd_ != nullptr && animation_timer_active_) {
        KillTimer(hwnd_, kAnimationTimerId);
    }
    animation_timer_active_ = false;
}

void OpenGLDeckView::SetLastErrorText(std::wstring message) {
    last_error_ = std::move(message);
    if (logger_ != nullptr) {
        logger_->Error("Deck Space OpenGL error: " + NarrowForLog(last_error_));
    }
}

}  // namespace cyberdeck::render
