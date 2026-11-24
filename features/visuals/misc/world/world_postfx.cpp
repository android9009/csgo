#include "world_postfx.hpp"
#include "../../../../context.hpp"
#include "../../../../sdk/convars.hpp"
#include "../../../../sdk/entity/entity.hpp"
#include "../../../../sdk/interfaces/interfaces.hpp"
#include "../../../../sdk/trace_ray.hpp"
#include "../../../../sdk/netvars.hpp"
#include "../../../../utils/log.hpp"
#include "../../../../utils/math/math.hpp"
#include <cstring>
#include <mutex>

namespace WorldPostfx {
namespace {

Settings g_snapshot;
std::mutex g_mutex;

// ---------------------------------------------------------------------------
// The env_tonemap_controller entity (Exposure + Bloom).
// ---------------------------------------------------------------------------
CBaseEntity* g_tonemap{};
int g_tonemap_index{};
int g_scan_cooldown{};

// The tonemap entity is a per-map point entity; the client entity list is the only
// reliable way to reach it. The scan is SEH-guarded so an out-of-range list index on an
// unexpected build aborts the scan instead of crashing the game thread.
CBaseEntity* FindTonemap() {
    if (!Interfaces::EntityList) return nullptr;
    const int max_index = 1024;
    __try {
        for (int i = 1; i <= max_index; ++i) {
            auto* entity = static_cast<CBaseEntity*>(Interfaces::EntityList->GetClientEntity(i));
            if (!entity) continue;
            auto* client_class = entity->GetClientClass();
            if (client_class && client_class->m_pNetworkName &&
                std::strcmp(client_class->m_pNetworkName, "env_tonemap_controller") == 0) {
                return entity;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return nullptr;
}

CBaseEntity* Tonemap() {
    if (g_tonemap) {
        // The cached entity is still the one the list hands back for its index.
        __try {
            if (Interfaces::EntityList &&
                static_cast<CBaseEntity*>(Interfaces::EntityList->GetClientEntity(g_tonemap_index)) ==
                    g_tonemap)
                return g_tonemap;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    if (--g_scan_cooldown <= 0) {
        g_scan_cooldown = 60;
        g_tonemap = FindTonemap();
        if (g_tonemap) {
            g_tonemap_index = g_tonemap->GetIndex();
            static bool reported = false;
            if (!reported) {
                reported = true;
                LOG_INFO("world postfx: env_tonemap_controller found (index %d)", g_tonemap_index);
            }
        }
    }
    return g_tonemap;
}

// The tonemap fields: RecvTable walk first, Hazedumper snapshot as fallback.
struct TonemapFields {
    std::ptrdiff_t use_expo_min{};
    std::ptrdiff_t use_expo_max{};
    std::ptrdiff_t custom_expo_min{};
    std::ptrdiff_t custom_expo_max{};
    std::ptrdiff_t use_bloom{};
    std::ptrdiff_t custom_bloom{};
    bool ready{};

    bool Ensure() {
        if (ready) return true;
        auto& pm = PropManager::Get();
        const auto get = [&pm](const char* prop, std::ptrdiff_t fallback) {
            const auto found = pm.GetOffset("DT_EnvTonemapController", prop);
            return found > 0 ? found : fallback;
        };
        use_expo_min = get("m_bUseCustomAutoExposureMin", offsets::netvars::m_bUseCustomAutoExposureMin);
        use_expo_max = get("m_bUseCustomAutoExposureMax", offsets::netvars::m_bUseCustomAutoExposureMax);
        custom_expo_min = get("m_flCustomAutoExposureMin", offsets::netvars::m_flCustomAutoExposureMin);
        custom_expo_max = get("m_flCustomAutoExposureMax", offsets::netvars::m_flCustomAutoExposureMax);
        use_bloom = get("m_bUseCustomBloomScale", offsets::netvars::m_bUseCustomBloomScale);
        custom_bloom = get("m_flCustomBloomScale", offsets::netvars::m_flCustomBloomScale);
        ready = true;
        return true;
    }
} g_fields;

template <typename T>
inline T* TonemapField(CBaseEntity* entity, std::ptrdiff_t offset) {
    if (!entity || offset <= 0 || offset >= 0x10000) return nullptr;
    return reinterpret_cast<T*>(reinterpret_cast<std::uintptr_t>(entity) + offset);
}

// ---------------------------------------------------------------------------
// The cvar half. Each group captures its cvars once, writes change-driven while
// enabled, and hands them back otherwise.
// ---------------------------------------------------------------------------
struct CvarGroup {
    bool attempted{};
    ConVars::ConVar* enable{};
    float saved_enable{};
    bool captured{};

    bool Capture(const char* name) {
        if (captured) return true;
        if (!Interfaces::ConVar || attempted) return false;
        attempted = true;
        enable = ConVars::Find(name);
        if (!enable) return false;
        saved_enable = enable->GetFloat();
        captured = true;
        return true;
    }

    // Write `on ? on_value : (captured original)` change-driven.
    void Assert(bool on, float on_value) {
        if (!captured) return;
        const float want = on ? on_value : saved_enable;
        if (enable->GetFloat() != want) ConVars::SetFloat(enable, want);
    }

    void Restore() {
        if (captured) {
            ConVars::SetFloat(enable, saved_enable);
            captured = false;
        }
    }
};

struct ScalarCvar {
    ConVars::ConVar* var{};
    float saved{};
    bool captured{};

    bool Capture(const char* name) {
        if (captured) return true;
        if (!Interfaces::ConVar) return false;
        var = ConVars::Find(name);
        if (!var) return false;
        saved = var->GetFloat();
        captured = true;
        return true;
    }

    void Assert(float want) {
        if (captured && var->GetFloat() != want) ConVars::SetFloat(var, want);
    }

    void Restore() {
        if (captured && var) {
            ConVars::SetFloat(var, saved);
            captured = false;
        }
    }
};

CvarGroup g_local_contrast;
CvarGroup g_color_correction;
CvarGroup g_dof;

// Local contrast strength/edge (optional: the build may only have the enable cvar).
ScalarCvar g_contrast_strength;
bool g_contrast_strength_tried{};
ScalarCvar g_contrast_edge;
bool g_contrast_edge_tried{};

// Tone curve + grayscale (optional).
ScalarCvar g_tone_curve;
bool g_tone_curve_tried{};
ScalarCvar g_grayscale;
bool g_grayscale_tried{};

// Depth of field focus/blur pair (optional).
ScalarCvar g_dof_near_focus, g_dof_far_focus, g_dof_near_blur_depth, g_dof_far_blur_depth;
bool g_dof_focus_tried{};

// Weather (rain family + wind).
ScalarCvar g_rain_density, g_rain_alpha, g_rain_speed, g_rain_length, g_rain_width,
          g_rain_particles, g_wind_speed;
bool g_weather_tried{};

void EnsureWeatherCvars() {
    if (g_weather_tried) return;
    g_weather_tried = true;
    g_rain_density.Capture("r_raindensity");
    g_rain_alpha.Capture("r_rainalpha");
    g_rain_speed.Capture("r_rainspeed");
    g_rain_length.Capture("r_rainlength");
    g_rain_width.Capture("r_rainwidth");
    g_rain_particles.Capture("r_rainparticledensity");
    g_wind_speed.Capture("cl_windspeed");
    static bool reported = false;
    if (!reported) {
        reported = true;
        LOG_INFO("world postfx: weather cvars (rain_density=%d rain_alpha=%d wind=%d)",
                 g_rain_density.captured, g_rain_alpha.captured, g_wind_speed.captured);
    }
}

void RestoreWeather() {
    g_rain_density.Restore();
    g_rain_alpha.Restore();
    g_rain_speed.Restore();
    g_rain_length.Restore();
    g_rain_width.Restore();
    g_rain_particles.Restore();
    g_wind_speed.Restore();
}

void ApplyWeather(int mode) {
    EnsureWeatherCvars();
    // mode: 0 none, 1 thunder, 2 rain, 3 snow. "None" hands the cvars back to their
    // captured values so a map that drives its own rain is not erased by the menu.
    if (mode == 0) {
        RestoreWeather();
        return;
    }
    const float density = mode == 1 ? 5.f : 3.f;
    const float alpha = mode == 1 ? 0.35f : 0.25f;
    const float speed = mode == 1 ? 0.8f : 0.5f;
    const float length = mode == 1 ? 0.7f : 0.5f;
    const float width = mode == 1 ? 0.7f : 0.5f;
    const float particles = mode == 1 ? 0.8f : 0.5f;
    const float wind = mode == 1 ? 8.f : 4.f;

    // Snow has no cvar family in this build (it is entity/particle driven); the option
    // degrades to nothing, logged once. The rain cvars are handed back so "Snow" is not
    // silently a lighter rain.
    if (mode == 3) {
        static bool snow_reported = false;
        if (!snow_reported) {
            snow_reported = true;
            LOG_WARN("world postfx: no snow cvars in this build; the Snow option stays inactive");
        }
        RestoreWeather();
        return;
    }
    g_rain_density.Assert(density);
    g_rain_alpha.Assert(alpha);
    g_rain_speed.Assert(speed);
    g_rain_length.Assert(length);
    g_rain_width.Assert(width);
    g_rain_particles.Assert(particles);
    g_wind_speed.Assert(wind);
}

// The crosshair DoF: distance from the local eye to the first world hit, in the same
// units the focus cvars take (a fraction of the clip distance).
float CrosshairDistance() {
    if (!ctx.local || !Interfaces::Engine || !Interfaces::EngineTrace) return -1.f;
    QAngle angles;
    Interfaces::Engine->GetViewAngles(angles);
    const Vector dir = Math::angle_vector(angles);
    const Vector eye = ctx.local->GetEyePosition();
    Ray_t ray;
    ray.Init(eye, eye + dir * 8192.f);
    CTraceFilterSimple filter(ctx.local);
    CGameTrace trace;
    Interfaces::EngineTrace->TraceRay(ray, 0x4600400Bu, &filter, &trace);
    return (trace.endpos - eye).Length();
}

// ---------------------------------------------------------------------------
// Per-frame driver.
// ---------------------------------------------------------------------------
void ApplyTonemap(const Settings& s) {
    const bool want_expo = s.exposure_enabled;
    const bool want_bloom = s.bloom_enabled;
    if (!want_expo && !want_bloom) return;
    CBaseEntity* entity = Tonemap();
    if (!entity) return;
    g_fields.Ensure();
    __try {
        if (want_expo) {
            // Airflow's exposure sliders are 1..10 for both bounds; the menu's single
            // percent slider maps onto that range and holds min = max (a fixed level).
            const float level = 1.f + (s.exposure_amount / 100.f) * 9.f;
            if (auto* use_min = TonemapField<bool>(entity, g_fields.use_expo_min)) *use_min = true;
            if (auto* use_max = TonemapField<bool>(entity, g_fields.use_expo_max)) *use_max = true;
            if (auto* min_v = TonemapField<float>(entity, g_fields.custom_expo_min)) *min_v = level;
            if (auto* max_v = TonemapField<float>(entity, g_fields.custom_expo_max)) *max_v = level;
        } else {
            if (auto* use_min = TonemapField<bool>(entity, g_fields.use_expo_min)) *use_min = false;
            if (auto* use_max = TonemapField<bool>(entity, g_fields.use_expo_max)) *use_max = false;
            if (auto* min_v = TonemapField<float>(entity, g_fields.custom_expo_min)) *min_v = 1.f;
            if (auto* max_v = TonemapField<float>(entity, g_fields.custom_expo_max)) *max_v = 1.f;
        }
        if (want_bloom) {
            // A multiplier on the engine bloom: 0 removes it, 100% doubles it.
            const float scale = (s.bloom_strength / 100.f) * 2.f;
            if (auto* use = TonemapField<bool>(entity, g_fields.use_bloom)) *use = true;
            if (auto* scale_v = TonemapField<float>(entity, g_fields.custom_bloom)) *scale_v = scale;
        } else {
            if (auto* use = TonemapField<bool>(entity, g_fields.use_bloom)) *use = false;
            if (auto* scale_v = TonemapField<float>(entity, g_fields.custom_bloom)) *scale_v = 0.f;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // A stale entity pointer (map change mid-frame) is handled by the cache check;
        // anything else just drops this frame's tonemap write.
        g_tonemap = nullptr;
    }
}

void FrameInternal(const Settings& s) {
    if (!Interfaces::ConVar) return;
    ApplyTonemap(s);

    // Local contrast.
    if (g_local_contrast.Capture("mat_local_contrast_enable")) {
        g_local_contrast.Assert(s.local_contrast_enabled, 1.f);
        if (!g_contrast_strength_tried) {
            g_contrast_strength_tried = true;
            g_contrast_strength.Capture("mat_local_contrast_strength");
            if (!g_contrast_strength.captured)
                g_contrast_strength.Capture("mat_localcontrast_strength");
        }
        if (!g_contrast_edge_tried) {
            g_contrast_edge_tried = true;
            g_contrast_edge.Capture("mat_local_contrast_edge_strength");
            if (!g_contrast_edge.captured)
                g_contrast_edge.Capture("mat_localcontrast_edge_strength");
        }
        if (g_contrast_strength.captured)
            g_contrast_strength.Assert(s.local_contrast_enabled ? (s.contrast_strength / 100.f) * 4.f : 1.f);
        if (g_contrast_edge.captured)
            g_contrast_edge.Assert(s.local_contrast_enabled ? (s.contrast_edge_strength / 100.f) * 4.f : 1.f);
    }

    // Color correction.
    if (g_color_correction.Capture("mat_colorcorrection")) {
        g_color_correction.Assert(s.color_correction_enabled, 1.f);
        if (!g_tone_curve_tried) {
            g_tone_curve_tried = true;
            g_tone_curve.Capture("mat_tonemap_algorithm");
        }
        if (g_tone_curve.captured)
            g_tone_curve.Assert(s.color_correction_enabled ? static_cast<float>(s.tone_curve) : g_tone_curve.saved);
        if (!g_grayscale_tried) {
            g_grayscale_tried = true;
            g_grayscale.Capture("mat_grayscale");
        }
        if (g_grayscale.captured)
            g_grayscale.Assert(s.color_correction_enabled && s.grayscale ? 1.f : g_grayscale.saved);
    }

    // Depth of field.
    if (g_dof.Capture("mat_dof_enabled")) {
        g_dof.Assert(s.dof_enabled, 1.f);
        if (!g_dof_focus_tried) {
            g_dof_focus_tried = true;
            g_dof_near_focus.Capture("mat_dof_near_focus_depth");
            g_dof_far_focus.Capture("mat_dof_far_focus_depth");
            g_dof_near_blur_depth.Capture("mat_dof_near_blur_depth");
            g_dof_far_blur_depth.Capture("mat_dof_far_blur_depth");
        }
        if (s.dof_enabled) {
            float focus = s.dof_mode == 1 ? CrosshairDistance() / 8192.f
                                          : s.focus_distance / 100.f;
            if (focus < 0.f) focus = 0.f;
            if (focus > 1.f) focus = 1.f;
            const float range = (s.focus_range / 100.f) * 0.5f;
            const float blur_near = focus - range < 0.f ? 0.f : focus - range;
            const float blur_far = focus + range > 1.f ? 1.f : focus + range;
            // Each cvar is optional: a build with only part of the family still gets the
            // focus window it has.
            if (g_dof_near_focus.captured) g_dof_near_focus.Assert(focus);
            if (g_dof_far_focus.captured) g_dof_far_focus.Assert(focus);
            if (g_dof_near_blur_depth.captured) g_dof_near_blur_depth.Assert(blur_near);
            if (g_dof_far_blur_depth.captured) g_dof_far_blur_depth.Assert(blur_far);
        }
    }

    // Weather.
    ApplyWeather(s.weather);
}

} // namespace

void Publish(const Settings& s) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_snapshot = s;
}

void FrameSeh(const Settings& s) {
    // SEH leaf: only POD locals, so __try is legal (C2712). The snapshot copy (which
    // takes the mutex) lives outside this function.
    __try {
        FrameInternal(s);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            LOG_WARN("world postfx: a write raised an exception; the pass stays off");
        }
    }
}

void Frame() {
    Settings s;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        s = g_snapshot;
    }
    if (!ctx.in_game) return;
    FrameSeh(s);
}

void ResetState() {
    // The lock only spans the snapshot copy: a SEH exception never runs C++ destructors,
    // so the cvar hand-back below must happen OUTSIDE the lock (it is game-thread state
    // anyway). Otherwise one bad write at map unload would leave g_mutex held and freeze
    // the game thread on the next lock.
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_snapshot = Settings{};
    }
    g_local_contrast.Restore();
    g_local_contrast.captured = false;
    g_color_correction.Restore();
    g_color_correction.captured = false;
    g_dof.Restore();
    g_dof.captured = false;
    g_contrast_strength.Restore();
    g_contrast_edge.Restore();
    g_tone_curve.Restore();
    g_grayscale.Restore();
    g_dof_near_focus.Restore();
    g_dof_far_focus.Restore();
    g_dof_near_blur_depth.Restore();
    g_dof_far_blur_depth.Restore();
    g_rain_density.Restore();
    g_rain_alpha.Restore();
    g_rain_speed.Restore();
    g_rain_length.Restore();
    g_rain_width.Restore();
    g_rain_particles.Restore();
    g_wind_speed.Restore();
    g_tonemap = nullptr;
    g_tonemap_index = 0;
}

void Shutdown() {
    ResetState();
}

} // namespace WorldPostfx
