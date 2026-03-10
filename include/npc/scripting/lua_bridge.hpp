#pragma once

// Forward-declare Lua state so callers don't need lua.h
extern "C" { typedef struct lua_State lua_State; }

#include "../ai/fsm.hpp"
#include <memory>
#include <string>
#include <vector>

namespace npc {

class NPC;
class GameWorld;
class RelationshipSystem;

// ─── LuaBridge ────────────────────────────────────────────────────────────────
//
// Manages a Lua 5.4 interpreter and exposes NPC systems to Lua scripts.
//
// Usage:
//   LuaBridge bridge;
//   bridge.bindWorld(&world);
//   bridge.loadFile("scripts/guard.lua");
//
//   auto guard = std::make_shared<NPC>(1, "Aldric", NPCType::Guard);
//   bridge.addLuaState(guard->fsm, "patrol", guard.get(),
//                      "guard_update_patrol",   // Lua fn (npc, dt) → void
//                      "guard_enter_patrol",    // optional enter hook
//                      "guard_exit_patrol");    // optional exit hook
//   guard->fsm.setInitialState("patrol");
//
// Lua script API exposed per NPC userdata:
//   npc:getName()                   → string
//   npc:getType()                   → string
//   npc:getPosition()               → x, y  (two numbers)
//   npc:setPosition(x, y)
//   npc:getHealth()                 → float
//   npc:getMaxHealth()              → float
//   npc:getHealthPercent()          → float  (0..1)
//   npc:dealDamage(amount)
//   npc:heal(amount)
//   npc:getEmotion()                → string  (dominant emotion name)
//   npc:getEmotionIntensity()       → float   (0..1, dominant)
//   npc:addEmotion(type, intensity, duration)
//     type: "Happy"|"Sad"|"Angry"|"Fearful"|"Disgusted"|"Surprised"|"Neutral"
//   npc:getMood()                   → float  (-1..1)
//   npc:getMoodString()             → string
//   npc:getNeed(name)               → float  (0..100)
//     name: "Hunger"|"Thirst"|"Sleep"|"Social"|"Fun"|"Safety"|"Comfort"
//   npc:satisfyNeed(name, amount)
//   npc:depletNeed(name, amount)
//   npc:setState(name)              forces FSM transition
//   npc:getState()                  → string
//   npc:getTimeInState()            → float  (seconds in current state)
//   npc:getBB(key)                  → string | number | bool | nil
//   npc:setBB(key, value)
//   npc:rememberEvent(desc, emotionalValue)
//   npc:moveTo(x, y)
//   npc:log(msg)
//
// World globals (available without npc reference):
//   world_time()                    → float  (sim-hours since start)
//   world_hour()                    → float  (0..24, current hour of day)
//   math_distance(x1, y1, x2, y2)  → float
//   math_clamp(v, lo, hi)           → float
//   math_lerp(a, b, t)              → float

class LuaBridge {
public:
    LuaBridge();
    ~LuaBridge();

    LuaBridge(const LuaBridge&) = delete;
    LuaBridge& operator=(const LuaBridge&) = delete;

    // ── Script loading ───────────────────────────────────────────────────────

    // Load and execute a Lua file. Returns false on error; errOut receives message.
    bool loadFile(const std::string& path, std::string* errOut = nullptr);

    // Load and execute a Lua string. Useful for inline behaviour definitions.
    bool loadString(const std::string& code, std::string* errOut = nullptr);

    // ── World binding ────────────────────────────────────────────────────────

    void bindWorld(GameWorld* world);
    void bindRelationships(RelationshipSystem* rs);

    // ── FSM wiring ───────────────────────────────────────────────────────────
    //
    // Adds a state to `fsm` whose callbacks invoke Lua functions.
    // The Lua functions receive (npc_userdata, dt) for update,
    // and (npc_userdata) for enter/exit.
    // Pass an empty string for hooks you don't need.

    void addLuaState(FSM& fsm,
                     const std::string& stateId,
                     NPC* npc,
                     const std::string& updateFn,
                     const std::string& enterFn = "",
                     const std::string& exitFn  = "");

    // ── Direct Lua calls ─────────────────────────────────────────────────────

    // Call a global Lua function with signature (npc_userdata, dt).
    bool callUpdate(const std::string& fn, NPC& npc, float dt);

    // Call a global Lua function with signature (npc_userdata).
    bool callEnter(const std::string& fn, NPC& npc);
    bool callExit (const std::string& fn, NPC& npc);

    // Check whether a global Lua function exists.
    bool hasFunction(const std::string& fn) const;

    // ── Raw state access ─────────────────────────────────────────────────────
    lua_State* state() const { return L_; }

private:
    void registerStdLibs();
    void registerNPCMetatable();
    void registerWorldGlobals();
    void registerMathExtensions();
    void pushNPC(NPC* npc);

    lua_State*    L_     = nullptr;
    GameWorld*    world_ = nullptr;
    RelationshipSystem* rs_ = nullptr;

    // Keep shared_ptr refs alive for the bridge lifetime
    std::vector<std::shared_ptr<NPC>> npcRefs_;
};

} // namespace npc
