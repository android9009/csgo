#pragma once
// Studio header / hitbox / bone structures + DrawModelExecute payloads.
// Layouts follow the legacy sdk/game/structs.hpp (Airflow) definitions,
// adapted to the math/ types used by this codebase.
#include "../math/vector.hpp"
#include "../math/matrix.hpp"
#include <cstdint>

struct quaternion_t {
    float x = 0.f, y = 0.f, z = 0.f, w = 0.f;
};

struct model_t {
    void* handle = nullptr;
    char name[260]{};
    int load_flags = 0;
    int server_count = 0;
    int type = 0;
    int flags = 0;
    Vector mins{}, maxs{};
    float radius = 0.f;
    void* key_values = nullptr;
    union {
        void* brush = nullptr;
        unsigned short studio;
        void* sprite;
    };
};

struct mstudiobbox_t {
    int bone = 0;
    int group = 0;
    Vector min{}, max{};
    int name_id = 0;
    Vector rotation{};
    float radius = 0.f;
    char pad[0x10]{};
};

struct mstudiohitboxset_t {
    int name_index = 0;
    int num_hitboxes = 0;
    int hitbox_index = 0;
    mstudiobbox_t* hitbox(int i) const {
        return reinterpret_cast<mstudiobbox_t*>(reinterpret_cast<std::uintptr_t>(this) + hitbox_index) + i;
    }
};

struct mstudiobone_t {
    int name_index = 0;
    char* get_name() const { return reinterpret_cast<char*>(const_cast<mstudiobone_t*>(this)) + name_index; }
    int parent = 0;
    int bone_controller[6]{};
    Vector pos{};
    quaternion_t quat{};
    Vector rot{};
    Vector pos_scale{};
    Vector rot_scale{};
    matrix3x4_t pose_to_bone{};
    quaternion_t alignment{};
    int flags = 0;
    int proc_type = 0;
    int proc_index = 0;
    mutable int physics_bone = 0;
    int surface_prop_index = 0;
    int contents = 0;
    int surface_prop_lookup = 0;
    int unused[7]{};
};

struct mstudioseqdesc_t {
    int baseptr = 0;
    int szlabelindex = 0;
    int szactivitynameindex = 0;
    int flags = 0;
    int activity = 0;
    int actweight = 0;
    int numevents = 0;
    int eventindex = 0;
    Vector bbmin{}, bbmax{};
    int numblends = 0;
    int animindexindex = 0;
    int movementindex = 0;
    int groupsize[2]{};
    int paramindex[2]{};
    float paramstart[2]{};
    float paramend[2]{};
    int paramparent = 0;
    float fadeintime = 0.f;
    float fadeouttime = 0.f;
    int localentrynode = 0;
    int localexitnode = 0;
    int nodeflags = 0;
    float entryphase = 0.f;
    float exitphase = 0.f;
    float lastframe = 0.f;
    int nextseq = 0;
    int pose = 0;
    int numikrules = 0;
    int numautolayers = 0;
    int autolayerindex = 0;
    int weightlistindex = 0;
    int posekeyindex = 0;
    int numiklocks = 0;
    int iklockindex = 0;
    int keyvalueindex = 0;
    int keyvaluesize = 0;
    int cycleposeindex = 0;
    int activitymodifierindex = 0;
    int numactivitymodifiers = 0;
    int animtag_index = 0;
    int num_animtags = 0;
    int unused[3]{};
};

struct studiohdr_t {
    int id = 0;
    int version = 0;
    int checksum = 0;
    char name[64]{};
    int length = 0;
    Vector eye_pos{};
    Vector ilum_pos{};
    Vector hull_min{};
    Vector hull_max{};
    Vector bb_min{};
    Vector bb_max{};
    int flags = 0;
    int bones = 0;
    int bone_index = 0;
    int bone_controllers = 0;
    int bone_controller_index = 0;
    int hitbox_sets = 0;
    int hitbox_set_index = 0;
    int local_anim = 0;
    int local_anim_index = 0;
    int local_seq = 0;
    int local_seq_index = 0;
    int activity_list_version = 0;
    int events_indexed = 0;
    int textures = 0;
    int texture_index = 0;
    int numcdtextures = 0;
    int cdtextureindex = 0;
    int numskinref = 0;
    int numskinfamilies = 0;
    int skinindex = 0;
    int numbodyparts = 0;
    int bodypartindex = 0;
    int numlocalattachments = 0;
    int localattachmentindex = 0;
    int numlocalnodes = 0;
    int localnodeindex = 0;
    int localnodenameindex = 0;
    int numflexdesc = 0;
    int flexdescindex = 0;
    int numflexcontrollers = 0;
    int flexcontrollerindex = 0;
    int numflexrules = 0;
    int flexruleindex = 0;
    int numikchains = 0;
    int ikchainindex = 0;
    int nummouths = 0;
    int mouthindex = 0;
    int numlocalposeparameters = 0;
    int localposeparamindex = 0;

    mstudiohitboxset_t* hitbox_set(int i) {
        if (i < 0 || i >= hitbox_sets) return nullptr;
        return reinterpret_cast<mstudiohitboxset_t*>(reinterpret_cast<std::uint8_t*>(this) + hitbox_set_index) + i;
    }
    const mstudiohitboxset_t* hitbox_set(int i) const {
        if (i < 0 || i >= hitbox_sets) return nullptr;
        return reinterpret_cast<const mstudiohitboxset_t*>(reinterpret_cast<const std::uint8_t*>(this) + hitbox_set_index) + i;
    }
    mstudiobone_t* bone(int i) {
        if (i < 0 || i >= bones) return nullptr;
        return reinterpret_cast<mstudiobone_t*>(reinterpret_cast<std::uint8_t*>(this) + bone_index) + i;
    }
    const mstudiobone_t* bone(int i) const {
        if (i < 0 || i >= bones) return nullptr;
        return reinterpret_cast<const mstudiobone_t*>(reinterpret_cast<const std::uint8_t*>(this) + bone_index) + i;
    }
    mstudioseqdesc_t* seq_desc(int i) {
        if (i < 0 || i >= local_seq) return nullptr;
        return reinterpret_cast<mstudioseqdesc_t*>(reinterpret_cast<std::uint8_t*>(this) + local_seq_index) + i;
    }
};

struct studiohwdata_t {
    int root_lod = 0;
    int num_lods = 0;
    void* lods = nullptr;
    int num_stidio_meshes = 0;
};

// DrawModelExecute payloads (Source 1 x86 layouts, 28 / 72 bytes).
struct DrawModelState_t {
    studiohdr_t* studio_hdr = nullptr;
    studiohwdata_t* studio_hdr_data = nullptr;
    void* renderable = nullptr;
    const matrix3x4_t* model_to_world = nullptr;
    void* decals = nullptr;
    int draw_flags = 0;
    int lod = 0;
};
#if defined(_M_IX86) || defined(__i386__)
static_assert(sizeof(DrawModelState_t) == 28, "Source 1 x86 DrawModelState_t layout");
#endif

struct ModelRenderInfo_t {
    Vector origin{};
    Vector angles{};
    char pad[4]{};
    void* renderable = nullptr;
    const model_t* model = nullptr;
    const matrix3x4_t* model_to_world = nullptr;
    const matrix3x4_t* lightning_offset = nullptr;
    const Vector* lightning_origin = nullptr;
    int flags = 0;
    int entity_index = 0;
    int skin = 0;
    int body = 0;
    int hitboxset = 0;
    unsigned short instance = 0;
    unsigned short pad2 = 0;
    ModelRenderInfo_t() = default;
};
#if defined(_M_IX86) || defined(__i386__)
static_assert(sizeof(ModelRenderInfo_t) == 72, "Source 1 x86 ModelRenderInfo_t layout");
#endif
