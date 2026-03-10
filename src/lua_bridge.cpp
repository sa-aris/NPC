#include "npc/scripting/lua_bridge.hpp"
#include "npc/npc.hpp"
#include "npc/world/world.hpp"
#include "npc/social/relationship_system.hpp"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <cstring>
#include <cmath>
#include <algorithm>

// ─── NPC userdata layout ──────────────────────────────────────────────────────

static const char* NPC_MT = "npc.NPC";

struct LuaNPC { npc::NPC* ptr; };

static npc::NPC* checkNPC(lua_State* L, int idx) {
    auto* ud = static_cast<LuaNPC*>(luaL_checkudata(L, idx, NPC_MT));
    luaL_argcheck(L, ud && ud->ptr, idx, "NPC expected");
    return ud->ptr;
}

// ─── Helpers ──────────────────────────────────────────────────────────────────

static npc::EmotionType emotionFromString(const char* s) {
    if (!s) return npc::EmotionType::Neutral;
    if (strcmp(s, "Happy")     == 0) return npc::EmotionType::Happy;
    if (strcmp(s, "Sad")       == 0) return npc::EmotionType::Sad;
    if (strcmp(s, "Angry")     == 0) return npc::EmotionType::Angry;
    if (strcmp(s, "Fearful")   == 0) return npc::EmotionType::Fearful;
    if (strcmp(s, "Disgusted") == 0) return npc::EmotionType::Disgusted;
    if (strcmp(s, "Surprised") == 0) return npc::EmotionType::Surprised;
    return npc::EmotionType::Neutral;
}

static npc::NeedType needFromString(const char* s) {
    if (!s) return npc::NeedType::Hunger;
    if (strcmp(s, "Hunger")  == 0) return npc::NeedType::Hunger;
    if (strcmp(s, "Thirst")  == 0) return npc::NeedType::Thirst;
    if (strcmp(s, "Sleep")   == 0) return npc::NeedType::Sleep;
    if (strcmp(s, "Social")  == 0) return npc::NeedType::Social;
    if (strcmp(s, "Fun")     == 0) return npc::NeedType::Fun;
    if (strcmp(s, "Safety")  == 0) return npc::NeedType::Safety;
    if (strcmp(s, "Comfort") == 0) return npc::NeedType::Comfort;
    return npc::NeedType::Hunger;
}

// ─── NPC method implementations ──────────────────────────────────────────────

static int l_npc_getName(lua_State* L) {
    lua_pushstring(L, checkNPC(L, 1)->name.c_str());
    return 1;
}

static int l_npc_getType(lua_State* L) {
    lua_pushstring(L, npc::npcTypeToString(checkNPC(L, 1)->type).c_str());
    return 1;
}

static int l_npc_getPosition(lua_State* L) {
    auto* n = checkNPC(L, 1);
    lua_pushnumber(L, static_cast<lua_Number>(n->position.x));
    lua_pushnumber(L, static_cast<lua_Number>(n->position.y));
    return 2;
}

static int l_npc_setPosition(lua_State* L) {
    auto* n = checkNPC(L, 1);
    n->position.x = static_cast<float>(luaL_checknumber(L, 2));
    n->position.y = static_cast<float>(luaL_checknumber(L, 3));
    return 0;
}

static int l_npc_getHealth(lua_State* L) {
    lua_pushnumber(L, static_cast<lua_Number>(checkNPC(L, 1)->combat.stats.health));
    return 1;
}

static int l_npc_getMaxHealth(lua_State* L) {
    lua_pushnumber(L, static_cast<lua_Number>(checkNPC(L, 1)->combat.stats.maxHealth));
    return 1;
}

static int l_npc_getHealthPercent(lua_State* L) {
    lua_pushnumber(L, static_cast<lua_Number>(checkNPC(L, 1)->combat.stats.healthPercent()));
    return 1;
}

static int l_npc_dealDamage(lua_State* L) {
    auto* n  = checkNPC(L, 1);
    float dmg = static_cast<float>(luaL_checknumber(L, 2));
    n->combat.stats.health = std::max(0.0f, n->combat.stats.health - dmg);
    return 0;
}

static int l_npc_heal(lua_State* L) {
    auto* n   = checkNPC(L, 1);
    float amt = static_cast<float>(luaL_checknumber(L, 2));
    n->combat.stats.health = std::min(n->combat.stats.maxHealth,
                                      n->combat.stats.health + amt);
    return 0;
}

static int l_npc_getEmotion(lua_State* L) {
    auto* n   = checkNPC(L, 1);
    auto  dom = n->emotions.getDominantEmotion();
    lua_pushstring(L, npc::emotionToString(dom).c_str());
    return 1;
}

static int l_npc_getEmotionIntensity(lua_State* L) {
    auto* n   = checkNPC(L, 1);
    auto  dom = n->emotions.getDominantEmotion();
    // Walk emotion list to find intensity of dominant type
    float intensity = 0.0f;
    // Access via public getDominantEmotion; intensity found from mood proxy
    // We call getMood and expose a 0-1 scaled value for the dominant emotion
    // Since EmotionSystem doesn't expose per-emotion intensity publicly,
    // we use the stored dominant type and check the mood level as a proxy.
    // (EmotionState is private — use getMood-based approximation.)
    float mood = n->emotions.getMood();
    intensity  = std::clamp(std::abs(mood) * 1.5f, 0.0f, 1.0f);
    (void)dom;
    lua_pushnumber(L, static_cast<lua_Number>(intensity));
    return 1;
}

static int l_npc_addEmotion(lua_State* L) {
    auto* n         = checkNPC(L, 1);
    const char* s   = luaL_checkstring(L, 2);
    float intensity = static_cast<float>(luaL_optnumber(L, 3, 0.7));
    float duration  = static_cast<float>(luaL_optnumber(L, 4, 2.0));
    n->emotions.addEmotion(emotionFromString(s), intensity, duration);
    return 0;
}

static int l_npc_getMood(lua_State* L) {
    lua_pushnumber(L, static_cast<lua_Number>(checkNPC(L, 1)->emotions.getMood()));
    return 1;
}

static int l_npc_getMoodString(lua_State* L) {
    lua_pushstring(L, checkNPC(L, 1)->emotions.getMoodString().c_str());
    return 1;
}

static int l_npc_getNeed(lua_State* L) {
    auto* n       = checkNPC(L, 1);
    const char* s = luaL_checkstring(L, 2);
    float val     = n->emotions.getNeed(needFromString(s)).value;
    lua_pushnumber(L, static_cast<lua_Number>(val));
    return 1;
}

static int l_npc_satisfyNeed(lua_State* L) {
    auto* n       = checkNPC(L, 1);
    const char* s = luaL_checkstring(L, 2);
    float amt     = static_cast<float>(luaL_checknumber(L, 3));
    n->emotions.satisfyNeed(needFromString(s), amt);
    return 0;
}

static int l_npc_depletNeed(lua_State* L) {
    auto* n       = checkNPC(L, 1);
    const char* s = luaL_checkstring(L, 2);
    float amt     = static_cast<float>(luaL_checknumber(L, 3));
    n->emotions.depletNeed(needFromString(s), amt);
    return 0;
}

static int l_npc_setState(lua_State* L) {
    auto* n       = checkNPC(L, 1);
    const char* s = luaL_checkstring(L, 2);
    n->fsm.forceTransition(s);
    return 0;
}

static int l_npc_getState(lua_State* L) {
    lua_pushstring(L, checkNPC(L, 1)->fsm.currentState().c_str());
    return 1;
}

static int l_npc_getTimeInState(lua_State* L) {
    lua_pushnumber(L, static_cast<lua_Number>(checkNPC(L, 1)->fsm.timeInCurrentState()));
    return 1;
}

static int l_npc_getBB(lua_State* L) {
    auto* n       = checkNPC(L, 1);
    const char* k = luaL_checkstring(L, 2);
    auto& bb      = n->fsm.blackboard();

    // Try float first, then bool, then string
    if (auto v = bb.get<float>(k)) {
        lua_pushnumber(L, static_cast<lua_Number>(*v));
        return 1;
    }
    if (auto v = bb.get<bool>(k)) {
        lua_pushboolean(L, *v ? 1 : 0);
        return 1;
    }
    if (auto v = bb.get<std::string>(k)) {
        lua_pushstring(L, v->c_str());
        return 1;
    }
    if (auto v = bb.get<int>(k)) {
        lua_pushinteger(L, static_cast<lua_Integer>(*v));
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

static int l_npc_setBB(lua_State* L) {
    auto* n       = checkNPC(L, 1);
    const char* k = luaL_checkstring(L, 2);
    auto& bb      = n->fsm.blackboard();

    int t = lua_type(L, 3);
    if (t == LUA_TNUMBER) {
        if (lua_isinteger(L, 3))
            bb.set<int>(k, static_cast<int>(lua_tointeger(L, 3)));
        else
            bb.set<float>(k, static_cast<float>(lua_tonumber(L, 3)));
    } else if (t == LUA_TBOOLEAN) {
        bb.set<bool>(k, lua_toboolean(L, 3) != 0);
    } else if (t == LUA_TSTRING) {
        bb.set<std::string>(k, lua_tostring(L, 3));
    }
    return 0;
}

static int l_npc_rememberEvent(lua_State* L) {
    auto* n       = checkNPC(L, 1);
    const char* d = luaL_checkstring(L, 2);
    float emo     = static_cast<float>(luaL_optnumber(L, 3, 0.0));
    n->memory.addMemory(npc::MemoryType::Interaction, d, emo);
    return 0;
}

static int l_npc_moveTo(lua_State* L) {
    auto* n = checkNPC(L, 1);
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    n->moveTo({x, y});
    return 0;
}

static int l_npc_log(lua_State* L) {
    auto* n       = checkNPC(L, 1);
    const char* m = luaL_checkstring(L, 2);
    if (n->verbose)
        printf("[Lua] %s: %s\n", n->name.c_str(), m);
    return 0;
}

// ─── NPC metatable ────────────────────────────────────────────────────────────

static const luaL_Reg NPC_METHODS[] = {
    {"getName",             l_npc_getName},
    {"getType",             l_npc_getType},
    {"getPosition",         l_npc_getPosition},
    {"setPosition",         l_npc_setPosition},
    {"getHealth",           l_npc_getHealth},
    {"getMaxHealth",        l_npc_getMaxHealth},
    {"getHealthPercent",    l_npc_getHealthPercent},
    {"dealDamage",          l_npc_dealDamage},
    {"heal",                l_npc_heal},
    {"getEmotion",          l_npc_getEmotion},
    {"getEmotionIntensity", l_npc_getEmotionIntensity},
    {"addEmotion",          l_npc_addEmotion},
    {"getMood",             l_npc_getMood},
    {"getMoodString",       l_npc_getMoodString},
    {"getNeed",             l_npc_getNeed},
    {"satisfyNeed",         l_npc_satisfyNeed},
    {"depletNeed",          l_npc_depletNeed},
    {"setState",            l_npc_setState},
    {"getState",            l_npc_getState},
    {"getTimeInState",      l_npc_getTimeInState},
    {"getBB",               l_npc_getBB},
    {"setBB",               l_npc_setBB},
    {"rememberEvent",       l_npc_rememberEvent},
    {"moveTo",              l_npc_moveTo},
    {"log",                 l_npc_log},
    {nullptr,               nullptr}
};

// ─── World global functions ───────────────────────────────────────────────────

// world_time() is stored via a lightuserdata upvalue pointing to the GameWorld*
static int l_world_time(lua_State* L) {
    auto* world = static_cast<npc::GameWorld*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (!world) { lua_pushnumber(L, 0.0); return 1; }
    lua_pushnumber(L, static_cast<lua_Number>(world->time().totalHours()));
    return 1;
}

static int l_world_hour(lua_State* L) {
    auto* world = static_cast<npc::GameWorld*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (!world) { lua_pushnumber(L, 0.0); return 1; }
    lua_pushnumber(L, static_cast<lua_Number>(world->time().currentHour()));
    return 1;
}

// ─── Math extensions ──────────────────────────────────────────────────────────

static int l_math_distance(lua_State* L) {
    double x1 = luaL_checknumber(L, 1), y1 = luaL_checknumber(L, 2);
    double x2 = luaL_checknumber(L, 3), y2 = luaL_checknumber(L, 4);
    lua_pushnumber(L, std::sqrt((x2-x1)*(x2-x1) + (y2-y1)*(y2-y1)));
    return 1;
}

static int l_math_clamp(lua_State* L) {
    double v  = luaL_checknumber(L, 1);
    double lo = luaL_checknumber(L, 2);
    double hi = luaL_checknumber(L, 3);
    lua_pushnumber(L, std::max(lo, std::min(hi, v)));
    return 1;
}

static int l_math_lerp(lua_State* L) {
    double a = luaL_checknumber(L, 1);
    double b = luaL_checknumber(L, 2);
    double t = luaL_checknumber(L, 3);
    lua_pushnumber(L, a + (b - a) * t);
    return 1;
}

// ─── LuaBridge implementation ─────────────────────────────────────────────────

namespace npc {

LuaBridge::LuaBridge() {
    L_ = luaL_newstate();
    registerStdLibs();
    registerNPCMetatable();
    registerMathExtensions();
}

LuaBridge::~LuaBridge() {
    if (L_) { lua_close(L_); L_ = nullptr; }
}

bool LuaBridge::loadFile(const std::string& path, std::string* errOut) {
    int r = luaL_dofile(L_, path.c_str());
    if (r != LUA_OK) {
        if (errOut) *errOut = lua_tostring(L_, -1);
        lua_pop(L_, 1);
        return false;
    }
    return true;
}

bool LuaBridge::loadString(const std::string& code, std::string* errOut) {
    int r = luaL_dostring(L_, code.c_str());
    if (r != LUA_OK) {
        if (errOut) *errOut = lua_tostring(L_, -1);
        lua_pop(L_, 1);
        return false;
    }
    return true;
}

void LuaBridge::bindWorld(GameWorld* world) {
    world_ = world;
    registerWorldGlobals();
}

void LuaBridge::bindRelationships(RelationshipSystem* rs) {
    rs_ = rs;
}

void LuaBridge::addLuaState(FSM& fsm,
                             const std::string& stateId,
                             NPC* npc,
                             const std::string& updateFn,
                             const std::string& enterFn,
                             const std::string& exitFn)
{
    fsm.addState(stateId,
        // onUpdate
        [this, npc, updateFn](Blackboard& /*bb*/, float dt) {
            if (!updateFn.empty()) callUpdate(updateFn, *npc, dt);
        },
        // onEnter
        [this, npc, enterFn](Blackboard& /*bb*/) {
            if (!enterFn.empty()) callEnter(enterFn, *npc);
        },
        // onExit
        [this, npc, exitFn](Blackboard& /*bb*/) {
            if (!exitFn.empty()) callExit(exitFn, *npc);
        }
    );
}

bool LuaBridge::callUpdate(const std::string& fn, NPC& npc, float dt) {
    if (!hasFunction(fn)) return false;
    lua_getglobal(L_, fn.c_str());
    pushNPC(&npc);
    lua_pushnumber(L_, static_cast<lua_Number>(dt));
    if (lua_pcall(L_, 2, 0, 0) != LUA_OK) {
        fprintf(stderr, "[LuaBridge] %s: %s\n", fn.c_str(), lua_tostring(L_, -1));
        lua_pop(L_, 1);
        return false;
    }
    return true;
}

bool LuaBridge::callEnter(const std::string& fn, NPC& npc) {
    if (!hasFunction(fn)) return false;
    lua_getglobal(L_, fn.c_str());
    pushNPC(&npc);
    if (lua_pcall(L_, 1, 0, 0) != LUA_OK) {
        fprintf(stderr, "[LuaBridge] %s: %s\n", fn.c_str(), lua_tostring(L_, -1));
        lua_pop(L_, 1);
        return false;
    }
    return true;
}

bool LuaBridge::callExit(const std::string& fn, NPC& npc) {
    return callEnter(fn, npc); // same signature
}

bool LuaBridge::hasFunction(const std::string& fn) const {
    lua_getglobal(L_, fn.c_str());
    bool ok = lua_isfunction(L_, -1);
    lua_pop(L_, 1);
    return ok;
}

// ─── Private helpers ──────────────────────────────────────────────────────────

void LuaBridge::registerStdLibs() {
    // Open safe standard libraries (no os/io file access)
    luaL_requiref(L_, "_G",       luaopen_base,   1); lua_pop(L_, 1);
    luaL_requiref(L_, "math",     luaopen_math,   1); lua_pop(L_, 1);
    luaL_requiref(L_, "string",   luaopen_string, 1); lua_pop(L_, 1);
    luaL_requiref(L_, "table",    luaopen_table,  1); lua_pop(L_, 1);
}

void LuaBridge::registerNPCMetatable() {
    luaL_newmetatable(L_, NPC_MT);

    // __index = method table
    lua_newtable(L_);
    luaL_setfuncs(L_, NPC_METHODS, 0);
    lua_setfield(L_, -2, "__index");

    // __tostring
    lua_pushcfunction(L_, [](lua_State* L) -> int {
        auto* ud = static_cast<LuaNPC*>(lua_touserdata(L, 1));
        if (ud && ud->ptr)
            lua_pushfstring(L, "NPC(%s)", ud->ptr->name.c_str());
        else
            lua_pushstring(L, "NPC(null)");
        return 1;
    });
    lua_setfield(L_, -2, "__tostring");

    lua_pop(L_, 1); // pop metatable
}

void LuaBridge::registerWorldGlobals() {
    if (!world_) return;

    // world_time() and world_hour() as C closures with world_ as upvalue
    lua_pushlightuserdata(L_, world_);
    lua_pushcclosure(L_, l_world_time, 1);
    lua_setglobal(L_, "world_time");

    lua_pushlightuserdata(L_, world_);
    lua_pushcclosure(L_, l_world_hour, 1);
    lua_setglobal(L_, "world_hour");
}

void LuaBridge::registerMathExtensions() {
    lua_getglobal(L_, "math");

    lua_pushcfunction(L_, l_math_distance);
    lua_setfield(L_, -2, "distance");

    lua_pushcfunction(L_, l_math_clamp);
    lua_setfield(L_, -2, "clamp");

    lua_pushcfunction(L_, l_math_lerp);
    lua_setfield(L_, -2, "lerp");

    lua_pop(L_, 1); // pop math table
}

void LuaBridge::pushNPC(NPC* npc) {
    auto* ud = static_cast<LuaNPC*>(lua_newuserdata(L_, sizeof(LuaNPC)));
    ud->ptr  = npc;
    luaL_setmetatable(L_, NPC_MT);
}

} // namespace npc
