#pragma once
// Experimental Source 1 Win32 ABI declarations for the model-preview port.
// These declarations are not proof that a particular client build is compatible.
#include <cstddef>
#include <cstdint>
#include "../math/vector.hpp"
#include "../math/qangle.hpp"
#include "../math/matrix.hpp"

namespace SourceModelPreview {
struct ViewSetup {
    int x{}, old_x{}, y{}, old_y{}, width{}, old_width{}, height{}, old_height{};
    bool orthographic{};
    float ortho_left{}, ortho_top{}, ortho_right{}, ortho_bottom{};
    bool custom_view_matrix{};
    matrix3x4_t custom_matrix{};
    std::uint8_t reserved[0x48]{};
    float fov{}, viewmodel_fov{};
    Vector origin{};
    QAngle angles{};
    float near_z{}, far_z{}, near_viewmodel_z{}, far_viewmodel_z{}, aspect_ratio{};
    float near_blur_depth{}, near_focus_depth{}, far_focus_depth{}, far_blur_depth{};
    float near_blur_radius{}, far_blur_radius{};
    int dof_quality{}, motion_blur_mode{};
    float shutter_time{};
    Vector shutter_open_position{};
    QAngle shutter_open_angles{};
    Vector shutter_close_position{};
    QAngle shutter_close_angles{};
    float offcenter_top{}, offcenter_bottom{}, offcenter_left{}, offcenter_right{};
    std::uint8_t flags{}, flags2{};
};

static_assert(offsetof(ViewSetup, fov) == 0xb0, "Source 1 view fov offset");
static_assert(offsetof(ViewSetup, origin) == 0xb8, "Source 1 view origin offset");
static_assert(offsetof(ViewSetup, near_z) == 0xd0, "Source 1 view near plane offset");

struct Plane { Vector normal{}; float distance{}; };
using Frustum = Plane[6];

// Decoded offline (NO_NETOFFSETS) snapshot in reversed2603/fatality_source.
// These belong to that snapshot, not necessarily the running client.dll.
constexpr std::uintptr_t reference_model_constructor_rva = 0x009aae50;
constexpr std::uintptr_t reference_attachment_bones_rva = 0x009ab380;
constexpr std::uintptr_t reference_set_merged_model_rva = 0x009ab930;

// C_MergedMDL's size and lifetime are build-specific. Keep it opaque: never
// allocate or destroy it with guessed offsets or the wrong allocator.
struct MergedModel;
struct Texture;
struct RenderContext;
using ConstructModelFn = void (__thiscall*)(MergedModel*);
using SetMergedModelFn = void (__thiscall*)(MergedModel*, const char*, void*, void*, bool);
using SetupAttachmentBonesFn = void (__thiscall*)(MergedModel*);
using Push3DViewFn = void (__thiscall*)(void*, RenderContext*, const ViewSetup&, int, Texture*, Plane*);
using PopViewFn = void (__thiscall*)(void*, RenderContext*, Plane*);
constexpr std::size_t push_3d_view_slot = 43;
constexpr std::size_t pop_view_slot = 45;
constexpr int clear_color_depth_stencil = 0x1 | 0x2 | 0x20;
} // namespace SourceModelPreview
