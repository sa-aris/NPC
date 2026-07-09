/**
 * npc_capi.h — Pure C interface for Aithena
 *
 * Drop-in binding layer for Unity (C# P/Invoke), Unreal Engine (native plugin),
 * Godot (GDExtension / GDNative), or any language with a C FFI.
 *
 * Build a shared library:
 *   cmake -B build -DNPC_SHARED=ON
 *   cmake --build build --target npc_shared
 *
 * Unity (C#):
 *   [DllImport("npc_shared")] static extern IntPtr npc_world_create(int w, int h);
 *
 * Unreal (C++):
 *   #include "npc_capi.h"   // plain C — no engine conflicts
 *
 * Godot (GDExtension):
 *   auto world = npc_world_create(64, 64);
 */

#ifndef NPC_CAPI_H
#define NPC_CAPI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ── Export macro ─────────────────────────────────────────────────────────── */

#if defined(_WIN32) || defined(__CYGWIN__)
#  ifdef NPC_BUILDING_DLL
#    define NPC_API __declspec(dllexport)
#  else
#    define NPC_API __declspec(dllimport)
#  endif
#else
#  define NPC_API __attribute__((visibility("default")))
#endif

/* ── Opaque handles ───────────────────────────────────────────────────────── */

typedef struct NpcWorld_s    NpcWorld;     /* GameWorld             */
typedef struct NpcEntity_s*  NpcHandle;    /* NPC*                  */
typedef struct NpcRelSys_s   NpcRelSys;    /* RelationshipSystem    */

/* ── Enumerations (match C++ values exactly) ─────────────────────────────── */

typedef enum {
    NPC_EMOTION_HAPPY     = 0,
    NPC_EMOTION_SAD       = 1,
    NPC_EMOTION_ANGRY     = 2,
    NPC_EMOTION_FEARFUL   = 3,
    NPC_EMOTION_DISGUSTED = 4,
    NPC_EMOTION_SURPRISED = 5,
    NPC_EMOTION_NEUTRAL   = 6
} NpcEmotion;

typedef enum {
    NPC_NEED_HUNGER  = 0,
    NPC_NEED_THIRST  = 1,
    NPC_NEED_SLEEP   = 2,
    NPC_NEED_SOCIAL  = 3,
    NPC_NEED_FUN     = 4,
    NPC_NEED_SAFETY  = 5,
    NPC_NEED_COMFORT = 6
} NpcNeedType;

typedef enum {
    NPC_TYPE_GUARD      = 0,
    NPC_TYPE_MERCHANT   = 1,
    NPC_TYPE_BLACKSMITH = 2,
    NPC_TYPE_VILLAGER   = 3,
    NPC_TYPE_INNKEEPER  = 4,
    NPC_TYPE_FARMER     = 5,
    NPC_TYPE_ENEMY      = 6
} NpcType;

typedef enum {
    NPC_REL_HELPED    = 0,
    NPC_REL_SAVED     = 1,
    NPC_REL_BETRAYED  = 2,
    NPC_REL_ATTACKED  = 3,
    NPC_REL_GIFTED    = 4,
    NPC_REL_TRADED    = 5,
    NPC_REL_LIED      = 6,
    NPC_REL_PROTECTED = 7
} NpcRelEventType;

/* ── Callback types ───────────────────────────────────────────────────────── */

/** FSM state update callback: called every tick while the NPC is in this state */
typedef void (*NpcUpdateFn) (NpcHandle npc, float dt,   void* userdata);
/** FSM enter / exit hook */
typedef void (*NpcHookFn)   (NpcHandle npc,             void* userdata);
/** World / combat event callback — event data delivered as a JSON string */
typedef void (*NpcEventFn)  (const char* event_json,    void* userdata);

/* ── NPC position snapshot (plain struct, safe across the FFI boundary) ──── */

typedef struct {
    float x, y;
} NpcVec2;

/* ══════════════════════════════════════════════════════════════════════════
   World
   ══════════════════════════════════════════════════════════════════════════ */

/** Create a world with grid dimensions width × height. */
NPC_API NpcWorld* npc_world_create (int width, int height);

/** Destroy the world and all NPCs it owns. */
NPC_API void      npc_world_destroy(NpcWorld* world);

/**
 * Advance the simulation by dt game-hours.
 * Typical values: 1.0f/60.0f  (1 game-minute per call at 60 Hz)
 */
NPC_API void      npc_world_update (NpcWorld* world, float dt);

/** Current in-game hour of day (0.0 – 23.99). */
NPC_API float     npc_world_get_hour      (NpcWorld* world);

/** Total game-hours elapsed since world creation. */
NPC_API float     npc_world_get_total_time(NpcWorld* world);

/* ── World event callbacks ─────────────────────────────────────────────── */

/**
 * Register a callback that fires on every WorldEvent (threats, disasters, …).
 * event_json: {"type":"...", "description":"...", "severity":0.8}
 */
NPC_API void npc_world_on_world_event (NpcWorld* world, NpcEventFn cb, void* ud);

/**
 * Register a callback that fires on every CombatEvent.
 * event_json: {"attacker":1, "defender":2, "damage":15.0, "killed":false}
 */
NPC_API void npc_world_on_combat_event(NpcWorld* world, NpcEventFn cb, void* ud);

/* ── Fire a world event manually (e.g. wolf attack from engine code) ─────── */
NPC_API void npc_world_fire_event(NpcWorld*   world,
                                  const char* event_type,
                                  const char* description,
                                  float       severity);

/* ══════════════════════════════════════════════════════════════════════════
   NPC lifecycle
   ══════════════════════════════════════════════════════════════════════════ */

/**
 * Create an NPC, add it to the world, and return a handle.
 * The world owns the NPC — do not free the handle manually.
 * @param id    Unique entity ID (must be non-zero).
 * @param name  Display name (copied internally).
 * @param type  One of NpcType.
 */
NPC_API NpcHandle npc_create (NpcWorld* world, uint32_t id,
                               const char* name, NpcType type);

/**
 * Remove an NPC from the world. The handle is invalid after this call.
 */
NPC_API void      npc_destroy(NpcWorld* world, NpcHandle npc);

/* ══════════════════════════════════════════════════════════════════════════
   Position & movement
   ══════════════════════════════════════════════════════════════════════════ */

NPC_API NpcVec2 npc_get_position(NpcHandle npc);
NPC_API void    npc_set_position(NpcHandle npc, float x, float y);

/** Begin pathfinding toward (x, y). Requires pathfinder to be set up. */
NPC_API void    npc_move_to    (NpcHandle npc, float x, float y);

NPC_API float   npc_get_move_speed(NpcHandle npc);
NPC_API void    npc_set_move_speed(NpcHandle npc, float speed);

/* ══════════════════════════════════════════════════════════════════════════
   Identity
   ══════════════════════════════════════════════════════════════════════════ */

/** Returns a pointer to an internal buffer — valid until the next call on this NPC. */
NPC_API const char* npc_get_name(NpcHandle npc);
NPC_API NpcType     npc_get_type(NpcHandle npc);

/* ══════════════════════════════════════════════════════════════════════════
   Health & combat
   ══════════════════════════════════════════════════════════════════════════ */

NPC_API float npc_get_health        (NpcHandle npc);
NPC_API float npc_get_max_health    (NpcHandle npc);
NPC_API float npc_get_health_percent(NpcHandle npc);   /* 0.0 – 1.0 */
NPC_API int   npc_is_alive          (NpcHandle npc);   /* 1 = alive */

NPC_API void  npc_deal_damage(NpcHandle npc, float amount);
NPC_API void  npc_heal       (NpcHandle npc, float amount);

NPC_API void  npc_set_max_health(NpcHandle npc, float max_health);

/* ══════════════════════════════════════════════════════════════════════════
   Emotions & needs
   ══════════════════════════════════════════════════════════════════════════ */

NPC_API NpcEmotion  npc_get_dominant_emotion    (NpcHandle npc);
NPC_API float       npc_get_mood                (NpcHandle npc); /* -1 .. 1 */
NPC_API const char* npc_get_mood_string         (NpcHandle npc);

/**
 * Add or intensify an emotion.
 * @param duration  Game-hours the emotion lasts (e.g. 2.0 = two in-game hours).
 */
NPC_API void npc_add_emotion(NpcHandle npc, NpcEmotion emotion,
                              float intensity, float duration);

NPC_API float npc_get_need    (NpcHandle npc, NpcNeedType need); /* 0 – 100 */
NPC_API void  npc_satisfy_need(NpcHandle npc, NpcNeedType need, float amount);
NPC_API void  npc_deplete_need(NpcHandle npc, NpcNeedType need, float amount);

/* ══════════════════════════════════════════════════════════════════════════
   FSM (Finite State Machine)
   ══════════════════════════════════════════════════════════════════════════ */

/**
 * Register a state with C callbacks.
 * Pass NULL for hooks you don't need.
 * @param userdata  Forwarded to every callback — use for engine-side context.
 */
NPC_API void npc_fsm_add_state(NpcHandle   npc,
                                const char* state_id,
                                NpcUpdateFn on_update,
                                NpcHookFn   on_enter,
                                NpcHookFn   on_exit,
                                void*       userdata);

NPC_API void        npc_fsm_set_initial(NpcHandle npc, const char* state_id);
NPC_API void        npc_fsm_set_state  (NpcHandle npc, const char* state_id);
NPC_API const char* npc_fsm_get_state  (NpcHandle npc);
NPC_API float       npc_fsm_time_in_state(NpcHandle npc);

/* ══════════════════════════════════════════════════════════════════════════
   Blackboard (key-value per NPC)
   ══════════════════════════════════════════════════════════════════════════ */

NPC_API void  npc_bb_set_float(NpcHandle npc, const char* key, float val);
NPC_API float npc_bb_get_float(NpcHandle npc, const char* key, float fallback);

NPC_API void npc_bb_set_int(NpcHandle npc, const char* key, int val);
NPC_API int  npc_bb_get_int(NpcHandle npc, const char* key, int fallback);

NPC_API void npc_bb_set_bool(NpcHandle npc, const char* key, int val);  /* 0/1 */
NPC_API int  npc_bb_get_bool(NpcHandle npc, const char* key, int fallback);

NPC_API void npc_bb_set_string(NpcHandle npc, const char* key, const char* val);
/** Returns NULL if key absent. Pointer valid until next set/get on same key. */
NPC_API const char* npc_bb_get_string(NpcHandle npc, const char* key);

/* ══════════════════════════════════════════════════════════════════════════
   Memory
   ══════════════════════════════════════════════════════════════════════════ */

/**
 * Store an episodic memory.
 * @param emotional_value  -1.0 (traumatic) … +1.0 (positive)
 * @param importance       0.0 … 1.0 — higher = decays slower
 */
NPC_API void npc_memory_add(NpcHandle   npc,
                             const char* description,
                             float       emotional_value,
                             float       importance);

/* ══════════════════════════════════════════════════════════════════════════
   Relationship system (standalone, not tied to a specific world)
   ══════════════════════════════════════════════════════════════════════════ */

NPC_API NpcRelSys* npc_rel_create (void);
NPC_API void       npc_rel_destroy(NpcRelSys* rs);

NPC_API void  npc_rel_record_event(NpcRelSys*     rs,
                                    const char*    from_id,
                                    const char*    to_id,
                                    NpcRelEventType event_type,
                                    float          sim_time);

/** Returns relationship value in -100 … +100. */
NPC_API float npc_rel_get_value(NpcRelSys* rs,
                                 const char* from_id,
                                 const char* to_id);

NPC_API void npc_rel_modify(NpcRelSys*  rs,
                             const char* from_id,
                             const char* to_id,
                             float       delta);

NPC_API void npc_rel_update(NpcRelSys* rs, float sim_time, float elapsed_hours);

/* ── Narrative recall ──────────────────────────────────────────────────── */

/**
 * Write a human-readable relationship summary into buf (at most buf_len bytes).
 * Returns the number of bytes written (excluding null terminator).
 */
NPC_API int npc_rel_narrative(NpcRelSys*  rs,
                               const char* from_id,
                               const char* to_id,
                               float       sim_time,
                               char*       buf,
                               int         buf_len);

/* ══════════════════════════════════════════════════════════════════════════
   Utility
   ══════════════════════════════════════════════════════════════════════════ */

/** Library version string: "major.minor.patch" */
NPC_API const char* npc_version(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* NPC_CAPI_H */
