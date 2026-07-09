/**
 * npc_capi.cpp — C ABI implementation for Aithena
 *
 * Wraps the C++17 npc:: objects behind the pure-C interface declared in
 * include/npc/npc_capi.h.  All allocations are done with new/delete so
 * no special allocator is required on the caller side.
 */

#define NPC_BUILDING_DLL   /* export symbols on Windows */

#include "npc/npc_capi.h"

#include "npc/world/world.hpp"
#include "npc/npc.hpp"
#include "npc/social/relationship_system.hpp"

#include <cstring>
#include <cstdio>
#include <string>
#include <memory>
#include <sstream>

/* ── Opaque-struct definitions ─────────────────────────────────────────────
 * These complete the forward-declared tags from the header so that the
 * C++ side can store real objects inside them.                              */

struct NpcWorld_s {
    npc::GameWorld world;
    explicit NpcWorld_s(int w, int h) : world(w, h) {}
};

/* NpcEntity_s — thin wrapper so that NpcHandle (struct NpcEntity_s*) has a
 * unique type, while still pointing at the real npc::NPC object.           */
struct NpcEntity_s {
    npc::NPC* ptr = nullptr;
    /* NPC lifetime is managed by the owning GameWorld (shared_ptr).
     * This wrapper is stack-allocated on each C API call site using
     * a thread-local pool (see makeHandle / releaseHandle below).           */
};

struct NpcRelSys_s {
    npc::RelationshipSystem rs;
};

/* ── Handle helpers ─────────────────────────────────────────────────────────
 * We need to hand out stable NpcHandle pointers for any NPC that lives in a
 * world.  The world stores shared_ptr<NPC>; we keep a parallel map from
 * NPC* → NpcEntity_s so the same NPC always yields the same handle pointer.
 *
 * For simplicity the registry is embedded in NpcWorld_s.                   */

#include <unordered_map>

/* Extend NpcWorld_s with the handle registry */
static NpcHandle registerHandle(NpcWorld_s* ws, npc::NPC* npcPtr)
{
    /* We store NpcEntity_s objects by pointer inside an unordered_map keyed
     * on the raw npc::NPC*.  The map is owned by the world so handles are
     * valid for as long as the world lives.                                 */
    // (registry is a static map inside this function for simplicity)
    static thread_local std::unordered_map<npc::NPC*, std::unique_ptr<NpcEntity_s>> s_reg;
    auto it = s_reg.find(npcPtr);
    if (it != s_reg.end()) return it->second.get();
    auto h = std::make_unique<NpcEntity_s>();
    h->ptr = npcPtr;
    NpcHandle raw = h.get();
    s_reg[npcPtr] = std::move(h);
    (void)ws; // currently unused — could use per-world registries
    return raw;
}

static inline npc::NPC* npcOf(NpcHandle h)
{
    return h ? h->ptr : nullptr;
}

/* ── Enum conversion helpers ────────────────────────────────────────────── */

static npc::EmotionType toEmotionType(NpcEmotion e)
{
    switch (e) {
        case NPC_EMOTION_HAPPY:     return npc::EmotionType::Happy;
        case NPC_EMOTION_SAD:       return npc::EmotionType::Sad;
        case NPC_EMOTION_ANGRY:     return npc::EmotionType::Angry;
        case NPC_EMOTION_FEARFUL:   return npc::EmotionType::Fearful;
        case NPC_EMOTION_DISGUSTED: return npc::EmotionType::Disgusted;
        case NPC_EMOTION_SURPRISED: return npc::EmotionType::Surprised;
        default:                    return npc::EmotionType::Neutral;
    }
}

static NpcEmotion fromEmotionType(npc::EmotionType e)
{
    switch (e) {
        case npc::EmotionType::Happy:     return NPC_EMOTION_HAPPY;
        case npc::EmotionType::Sad:       return NPC_EMOTION_SAD;
        case npc::EmotionType::Angry:     return NPC_EMOTION_ANGRY;
        case npc::EmotionType::Fearful:   return NPC_EMOTION_FEARFUL;
        case npc::EmotionType::Disgusted: return NPC_EMOTION_DISGUSTED;
        case npc::EmotionType::Surprised: return NPC_EMOTION_SURPRISED;
        default:                          return NPC_EMOTION_NEUTRAL;
    }
}

static npc::NeedType toNeedType(NpcNeedType n)
{
    switch (n) {
        case NPC_NEED_HUNGER:  return npc::NeedType::Hunger;
        case NPC_NEED_THIRST:  return npc::NeedType::Thirst;
        case NPC_NEED_SLEEP:   return npc::NeedType::Sleep;
        case NPC_NEED_SOCIAL:  return npc::NeedType::Social;
        case NPC_NEED_FUN:     return npc::NeedType::Fun;
        case NPC_NEED_SAFETY:  return npc::NeedType::Safety;
        default:               return npc::NeedType::Comfort;
    }
}

static npc::NPCType toNPCType(NpcType t)
{
    switch (t) {
        case NPC_TYPE_GUARD:      return npc::NPCType::Guard;
        case NPC_TYPE_MERCHANT:   return npc::NPCType::Merchant;
        case NPC_TYPE_BLACKSMITH: return npc::NPCType::Blacksmith;
        case NPC_TYPE_VILLAGER:   return npc::NPCType::Villager;
        case NPC_TYPE_INNKEEPER:  return npc::NPCType::Innkeeper;
        case NPC_TYPE_FARMER:     return npc::NPCType::Farmer;
        default:                  return npc::NPCType::Enemy;
    }
}

static NpcType fromNPCType(npc::NPCType t)
{
    switch (t) {
        case npc::NPCType::Guard:      return NPC_TYPE_GUARD;
        case npc::NPCType::Merchant:   return NPC_TYPE_MERCHANT;
        case npc::NPCType::Blacksmith: return NPC_TYPE_BLACKSMITH;
        case npc::NPCType::Villager:   return NPC_TYPE_VILLAGER;
        case npc::NPCType::Innkeeper:  return NPC_TYPE_INNKEEPER;
        case npc::NPCType::Farmer:     return NPC_TYPE_FARMER;
        default:                       return NPC_TYPE_ENEMY;
    }
}

static npc::RelationshipEventType toRelEventType(NpcRelEventType t)
{
    switch (t) {
        case NPC_REL_HELPED:    return npc::RelationshipEventType::Helped;
        case NPC_REL_SAVED:     return npc::RelationshipEventType::Saved;
        case NPC_REL_BETRAYED:  return npc::RelationshipEventType::Betrayed;
        case NPC_REL_ATTACKED:  return npc::RelationshipEventType::Attacked;
        case NPC_REL_GIFTED:    return npc::RelationshipEventType::Gifted;
        case NPC_REL_TRADED:    return npc::RelationshipEventType::Traded;
        case NPC_REL_LIED:      return npc::RelationshipEventType::Lied;
        default:                return npc::RelationshipEventType::Defended;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
   World
   ══════════════════════════════════════════════════════════════════════════ */

NpcWorld* npc_world_create(int width, int height)
{
    return new NpcWorld_s(width, height);
}

void npc_world_destroy(NpcWorld* world)
{
    delete world;
}

void npc_world_update(NpcWorld* world, float dt)
{
    if (world) world->world.update(dt);
}

float npc_world_get_hour(NpcWorld* world)
{
    return world ? world->world.time().currentHour() : 0.0f;
}

float npc_world_get_total_time(NpcWorld* world)
{
    return world ? world->world.time().totalHours() : 0.0f;
}

/* ── World event callbacks ──────────────────────────────────────────────── */

void npc_world_on_world_event(NpcWorld* world, NpcEventFn cb, void* ud)
{
    if (!world || !cb) return;
    world->world.events().subscribe<npc::WorldEvent>(
        [cb, ud](const npc::WorldEvent& e) {
            /* Build a minimal JSON string for the event */
            std::ostringstream ss;
            ss << "{\"type\":\"" << e.eventType
               << "\",\"description\":\"" << e.description
               << "\",\"severity\":" << e.severity << "}";
            std::string json = ss.str();
            cb(json.c_str(), ud);
        });
}

void npc_world_on_combat_event(NpcWorld* world, NpcEventFn cb, void* ud)
{
    if (!world || !cb) return;
    world->world.events().subscribe<npc::CombatEvent>(
        [cb, ud](const npc::CombatEvent& e) {
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                "{\"attacker\":%u,\"defender\":%u,\"damage\":%.1f,\"killed\":%s}",
                e.attacker, e.defender, e.damage, e.killed ? "true" : "false");
            cb(buf, ud);
        });
}

void npc_world_fire_event(NpcWorld*   world,
                          const char* event_type,
                          const char* description,
                          float       severity)
{
    if (!world) return;
    npc::WorldEvent we;
    we.eventType   = event_type   ? event_type   : "";
    we.description = description  ? description  : "";
    we.severity    = severity;
    world->world.events().publish(we);
}

/* ══════════════════════════════════════════════════════════════════════════
   NPC lifecycle
   ══════════════════════════════════════════════════════════════════════════ */

NpcHandle npc_create(NpcWorld* world, uint32_t id,
                     const char* name, NpcType type)
{
    if (!world || !name) return nullptr;
    auto npcPtr = std::make_shared<npc::NPC>(
        static_cast<npc::EntityId>(id),
        std::string(name),
        toNPCType(type));
    npcPtr->subscribeToEvents(world->world.events());
    world->world.addNPC(npcPtr);
    return registerHandle(world, npcPtr.get());
}

void npc_destroy(NpcWorld* world, NpcHandle npc)
{
    if (!world || !npc) return;
    npc::NPC* ptr = npcOf(npc);
    auto& npcs = world->world.npcs();
    npcs.erase(
        std::remove_if(npcs.begin(), npcs.end(),
            [ptr](const std::shared_ptr<npc::NPC>& sp) { return sp.get() == ptr; }),
        npcs.end());
    /* NpcEntity_s handle stays in the registry; the raw pointer is now
     * dangling — the caller must not use it after this call.               */
}

/* ══════════════════════════════════════════════════════════════════════════
   Position & movement
   ══════════════════════════════════════════════════════════════════════════ */

NpcVec2 npc_get_position(NpcHandle npc)
{
    npc::NPC* p = npcOf(npc);
    if (!p) return {0.0f, 0.0f};
    return {p->position.x, p->position.y};
}

void npc_set_position(NpcHandle npc, float x, float y)
{
    npc::NPC* p = npcOf(npc);
    if (p) p->position = {x, y};
}

void npc_move_to(NpcHandle npc, float x, float y)
{
    npc::NPC* p = npcOf(npc);
    if (p) p->moveTo({x, y});
}

float npc_get_move_speed(NpcHandle npc)
{
    npc::NPC* p = npcOf(npc);
    return p ? p->moveSpeed : 0.0f;
}

void npc_set_move_speed(NpcHandle npc, float speed)
{
    npc::NPC* p = npcOf(npc);
    if (p) p->moveSpeed = speed;
}

/* ══════════════════════════════════════════════════════════════════════════
   Identity
   ══════════════════════════════════════════════════════════════════════════ */

const char* npc_get_name(NpcHandle npc)
{
    npc::NPC* p = npcOf(npc);
    return p ? p->name.c_str() : "";
}

NpcType npc_get_type(NpcHandle npc)
{
    npc::NPC* p = npcOf(npc);
    return p ? fromNPCType(p->type) : NPC_TYPE_VILLAGER;
}

/* ══════════════════════════════════════════════════════════════════════════
   Health & combat
   ══════════════════════════════════════════════════════════════════════════ */

float npc_get_health(NpcHandle npc)
{
    npc::NPC* p = npcOf(npc);
    return p ? p->combat.stats.health : 0.0f;
}

float npc_get_max_health(NpcHandle npc)
{
    npc::NPC* p = npcOf(npc);
    return p ? p->combat.stats.maxHealth : 0.0f;
}

float npc_get_health_percent(NpcHandle npc)
{
    npc::NPC* p = npcOf(npc);
    return p ? p->combat.stats.healthPercent() : 0.0f;
}

int npc_is_alive(NpcHandle npc)
{
    npc::NPC* p = npcOf(npc);
    return (p && p->combat.stats.isAlive()) ? 1 : 0;
}

void npc_deal_damage(NpcHandle npc, float amount)
{
    npc::NPC* p = npcOf(npc);
    if (!p) return;
    p->combat.stats.health -= amount;
    if (p->combat.stats.health < 0.0f) p->combat.stats.health = 0.0f;
}

void npc_heal(NpcHandle npc, float amount)
{
    npc::NPC* p = npcOf(npc);
    if (!p) return;
    p->combat.stats.health += amount;
    if (p->combat.stats.health > p->combat.stats.maxHealth)
        p->combat.stats.health = p->combat.stats.maxHealth;
}

void npc_set_max_health(NpcHandle npc, float max_health)
{
    npc::NPC* p = npcOf(npc);
    if (p) p->combat.stats.maxHealth = max_health;
}

/* ══════════════════════════════════════════════════════════════════════════
   Emotions & needs
   ══════════════════════════════════════════════════════════════════════════ */

NpcEmotion npc_get_dominant_emotion(NpcHandle npc)
{
    npc::NPC* p = npcOf(npc);
    return p ? fromEmotionType(p->emotions.getDominantEmotion()) : NPC_EMOTION_NEUTRAL;
}

float npc_get_mood(NpcHandle npc)
{
    npc::NPC* p = npcOf(npc);
    return p ? p->emotions.getMood() : 0.0f;
}

const char* npc_get_mood_string(NpcHandle npc)
{
    npc::NPC* p = npcOf(npc);
    if (!p) return "Neutral";
    /* getMoodString() returns a std::string; store in a thread-local buffer */
    thread_local std::string s_buf;
    s_buf = p->emotions.getMoodString();
    return s_buf.c_str();
}

void npc_add_emotion(NpcHandle npc, NpcEmotion emotion,
                     float intensity, float duration)
{
    npc::NPC* p = npcOf(npc);
    if (p) p->emotions.addEmotion(toEmotionType(emotion), intensity, duration);
}

float npc_get_need(NpcHandle npc, NpcNeedType need)
{
    npc::NPC* p = npcOf(npc);
    if (!p) return 0.0f;
    return p->emotions.getNeed(toNeedType(need)).value;
}

void npc_satisfy_need(NpcHandle npc, NpcNeedType need, float amount)
{
    npc::NPC* p = npcOf(npc);
    if (p) p->emotions.satisfyNeed(toNeedType(need), amount);
}

void npc_deplete_need(NpcHandle npc, NpcNeedType need, float amount)
{
    npc::NPC* p = npcOf(npc);
    if (p) p->emotions.depletNeed(toNeedType(need), amount);
}

/* ══════════════════════════════════════════════════════════════════════════
   FSM
   ══════════════════════════════════════════════════════════════════════════ */

void npc_fsm_add_state(NpcHandle   npc,
                       const char* state_id,
                       NpcUpdateFn on_update,
                       NpcHookFn   on_enter,
                       NpcHookFn   on_exit,
                       void*       userdata)
{
    npc::NPC* p = npcOf(npc);
    if (!p || !state_id) return;

    /* Capture the raw NPC* (not the handle — the handle wrapper may move)
     * and the C function pointers.  The NPC* is stable for the world's life. */
    NpcHandle h = npc;

    npc::LambdaState::Callback updateCb;
    if (on_update)
        updateCb = [h, on_update, userdata](npc::Blackboard&, float dt) {
            on_update(h, dt, userdata);
        };

    npc::LambdaState::HookCallback enterCb;
    if (on_enter)
        enterCb = [h, on_enter, userdata](npc::Blackboard&) {
            on_enter(h, userdata);
        };

    npc::LambdaState::HookCallback exitCb;
    if (on_exit)
        exitCb = [h, on_exit, userdata](npc::Blackboard&) {
            on_exit(h, userdata);
        };

    p->fsm.addState(state_id,
                    std::move(updateCb),
                    std::move(enterCb),
                    std::move(exitCb));
}

void npc_fsm_set_initial(NpcHandle npc, const char* state_id)
{
    npc::NPC* p = npcOf(npc);
    if (p && state_id) p->fsm.setInitialState(state_id);
}

void npc_fsm_set_state(NpcHandle npc, const char* state_id)
{
    npc::NPC* p = npcOf(npc);
    if (p && state_id) p->fsm.forceTransition(state_id);
}

const char* npc_fsm_get_state(NpcHandle npc)
{
    npc::NPC* p = npcOf(npc);
    return p ? p->fsm.currentState().c_str() : "";
}

float npc_fsm_time_in_state(NpcHandle npc)
{
    npc::NPC* p = npcOf(npc);
    return p ? p->fsm.timeInCurrentState() : 0.0f;
}

/* ══════════════════════════════════════════════════════════════════════════
   Blackboard
   ══════════════════════════════════════════════════════════════════════════ */

void npc_bb_set_float(NpcHandle npc, const char* key, float val)
{
    npc::NPC* p = npcOf(npc);
    if (p && key) p->fsm.blackboard().set<float>(key, val);
}

float npc_bb_get_float(NpcHandle npc, const char* key, float fallback)
{
    npc::NPC* p = npcOf(npc);
    if (!p || !key) return fallback;
    return p->fsm.blackboard().getOr<float>(key, fallback);
}

void npc_bb_set_int(NpcHandle npc, const char* key, int val)
{
    npc::NPC* p = npcOf(npc);
    if (p && key) p->fsm.blackboard().set<int>(key, val);
}

int npc_bb_get_int(NpcHandle npc, const char* key, int fallback)
{
    npc::NPC* p = npcOf(npc);
    if (!p || !key) return fallback;
    return p->fsm.blackboard().getOr<int>(key, fallback);
}

void npc_bb_set_bool(NpcHandle npc, const char* key, int val)
{
    npc::NPC* p = npcOf(npc);
    if (p && key) p->fsm.blackboard().set<bool>(key, val != 0);
}

int npc_bb_get_bool(NpcHandle npc, const char* key, int fallback)
{
    npc::NPC* p = npcOf(npc);
    if (!p || !key) return fallback;
    return p->fsm.blackboard().getOr<bool>(key, fallback != 0) ? 1 : 0;
}

void npc_bb_set_string(NpcHandle npc, const char* key, const char* val)
{
    npc::NPC* p = npcOf(npc);
    if (p && key && val)
        p->fsm.blackboard().set<std::string>(key, std::string(val));
}

const char* npc_bb_get_string(NpcHandle npc, const char* key)
{
    npc::NPC* p = npcOf(npc);
    if (!p || !key) return nullptr;
    auto opt = p->fsm.blackboard().get<std::string>(key);
    if (!opt) return nullptr;
    /* Return pointer to the string stored inside the blackboard's std::any.
     * Valid until the next set on the same key.                             */
    const std::any* raw = p->fsm.blackboard().getAny(key);
    if (!raw) return nullptr;
    return std::any_cast<std::string>(raw)->c_str();
}

/* ══════════════════════════════════════════════════════════════════════════
   Memory
   ══════════════════════════════════════════════════════════════════════════ */

void npc_memory_add(NpcHandle   npc,
                    const char* description,
                    float       emotional_value,
                    float       importance)
{
    npc::NPC* p = npcOf(npc);
    if (!p || !description) return;
    p->memory.addMemory(npc::MemoryType::Interaction,
                        std::string(description),
                        emotional_value,
                        std::nullopt,
                        importance);
}

/* ══════════════════════════════════════════════════════════════════════════
   Relationship system
   ══════════════════════════════════════════════════════════════════════════ */

NpcRelSys* npc_rel_create(void)
{
    return new NpcRelSys_s();
}

void npc_rel_destroy(NpcRelSys* rs)
{
    delete rs;
}

void npc_rel_record_event(NpcRelSys*      rs,
                          const char*     from_id,
                          const char*     to_id,
                          NpcRelEventType event_type,
                          float           sim_time)
{
    if (!rs || !from_id || !to_id) return;
    rs->rs.recordEvent(from_id, to_id,
                       toRelEventType(event_type),
                       static_cast<double>(sim_time));
}

float npc_rel_get_value(NpcRelSys*  rs,
                        const char* from_id,
                        const char* to_id)
{
    if (!rs || !from_id || !to_id) return 0.0f;
    return rs->rs.getValue(from_id, to_id);
}

void npc_rel_modify(NpcRelSys*  rs,
                    const char* from_id,
                    const char* to_id,
                    float       delta)
{
    if (!rs || !from_id || !to_id) return;
    rs->rs.modifyValue(from_id, to_id, delta);
}

void npc_rel_update(NpcRelSys* rs, float sim_time, float elapsed_hours)
{
    if (rs) rs->rs.update(static_cast<double>(sim_time), elapsed_hours);
}

int npc_rel_narrative(NpcRelSys*  rs,
                      const char* from_id,
                      const char* to_id,
                      float       sim_time,
                      char*       buf,
                      int         buf_len)
{
    if (!rs || !from_id || !to_id || !buf || buf_len <= 0) return 0;
    std::string text = rs->rs.narrative(from_id, to_id,
                                        static_cast<double>(sim_time));
    int written = static_cast<int>(text.size());
    if (written >= buf_len) written = buf_len - 1;
    std::memcpy(buf, text.c_str(), static_cast<size_t>(written));
    buf[written] = '\0';
    return written;
}

/* ══════════════════════════════════════════════════════════════════════════
   Utility
   ══════════════════════════════════════════════════════════════════════════ */

const char* npc_version(void)
{
    return "1.0.0";
}
