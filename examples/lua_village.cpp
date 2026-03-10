// lua_village.cpp
// Demonstrates Lua scripting bridge: guard and merchant behaviours
// are defined entirely in Lua scripts — no C++ FSM lambdas required.
//
// Build:
//   cmake -B build -DNPC_LUA_BRIDGE=ON
//   cmake --build build --target lua_village

#include "npc/npc.hpp"
#include "npc/world/world.hpp"
#include "npc/scripting/lua_bridge.hpp"

#include <cstdio>
#include <memory>
#include <string>

// ─── helpers ──────────────────────────────────────────────────────────────────

static std::string fmtHour(float simHours) {
    // sim starts at 06:00; simHours is game-hours elapsed
    float h = std::fmod(6.0f + simHours, 24.0f);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d",
                  static_cast<int>(h),
                  static_cast<int>((h - static_cast<int>(h)) * 60.0f));
    return buf;
}

static void printStatus(float simSecs, const npc::NPC& n) {
    std::printf("[%s] %-10s %-12s  HP:%3.0f%%  mood:%-10s  emotion:%s\n",
        fmtHour(simSecs).c_str(),
        n.name.c_str(),
        n.fsm.currentState().c_str(),
        n.combat.stats.healthPercent() * 100.0f,
        n.emotions.getMoodString().c_str(),
        npc::emotionToString(n.emotions.getDominantEmotion()).c_str()
    );
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    // Script directory can be overridden via argv[1]
    std::string scriptDir = (argc > 1) ? argv[1] : "examples/scripts";

    std::puts("╔══════════════════════════════════════════════╗");
    std::puts("║   NPC Lua Scripting Bridge — Village Demo    ║");
    std::puts("╚══════════════════════════════════════════════╝\n");

    // ── World setup ───────────────────────────────────────────────────────────
    npc::GameWorld world(64, 64);

    // ── Lua bridge ────────────────────────────────────────────────────────────
    npc::LuaBridge bridge;
    bridge.bindWorld(&world);

    std::string err;
    if (!bridge.loadFile(scriptDir + "/guard.lua", &err)) {
        std::fprintf(stderr, "Failed to load guard.lua: %s\n", err.c_str());
        return 1;
    }
    if (!bridge.loadFile(scriptDir + "/merchant.lua", &err)) {
        std::fprintf(stderr, "Failed to load merchant.lua: %s\n", err.c_str());
        return 1;
    }
    std::puts("Lua scripts loaded.\n");

    // ── Guard ─────────────────────────────────────────────────────────────────
    auto guard = std::make_shared<npc::NPC>(1, "Aldric", npc::NPCType::Guard);
    guard->position           = {20.0f, 15.0f};
    guard->personality        = npc::PersonalityTraits::guard();
    guard->combat.stats.maxHealth = 120.0f;
    guard->combat.stats.health    = 120.0f;
    guard->verbose            = false;

    bridge.addLuaState(guard->fsm, "patrol",  guard.get(),
                       "guard_update_patrol",  "guard_enter_patrol");
    bridge.addLuaState(guard->fsm, "alert",   guard.get(),
                       "guard_update_alert",   "guard_enter_alert");
    bridge.addLuaState(guard->fsm, "combat",  guard.get(),
                       "guard_update_combat",  "guard_enter_combat",
                       "guard_exit_combat");
    bridge.addLuaState(guard->fsm, "flee",    guard.get(),
                       "guard_update_flee",    "guard_enter_flee");
    bridge.addLuaState(guard->fsm, "recover", guard.get(),
                       "guard_update_recover", "guard_enter_recover");
    guard->fsm.setInitialState("patrol");

    // ── Merchant ──────────────────────────────────────────────────────────────
    auto merchant = std::make_shared<npc::NPC>(2, "Mira", npc::NPCType::Merchant);
    merchant->position        = {30.0f, 20.0f};
    merchant->personality     = npc::PersonalityTraits::merchant();
    merchant->combat.stats.maxHealth = 60.0f;
    merchant->combat.stats.health    = 60.0f;
    merchant->verbose         = false;

    bridge.addLuaState(merchant->fsm, "open_shop", merchant.get(),
                       "merchant_update_open",   "merchant_enter_open");
    bridge.addLuaState(merchant->fsm, "lunch",     merchant.get(),
                       "merchant_update_lunch",   "merchant_enter_lunch");
    bridge.addLuaState(merchant->fsm, "closed",    merchant.get(),
                       "merchant_update_closed",  "merchant_enter_closed");
    bridge.addLuaState(merchant->fsm, "worried",   merchant.get(),
                       "merchant_update_worried", "merchant_enter_worried");
    merchant->fsm.setInitialState("open_shop");

    world.addNPC(guard);
    world.addNPC(merchant);
    guard->subscribeToEvents(world.events());
    merchant->subscribeToEvents(world.events());

    // ── Simulation loop ───────────────────────────────────────────────────────
    // TimeSystem uses hours as its unit: dt = 1/60 means 1 game-minute per tick.
    // 16 sim-hours = 960 ticks. Print status every 0.5 hours (30 min).
    const float DT            = 1.0f / 60.0f;  // 1 game-minute per tick
    const float SIM_HOURS     = 16.0f;
    const float REPORT_EVERY  = 0.5f;           // hours

    float simTime      = 0.0f;
    float nextReport   = 0.0f;
    float wolfTime     = 4.0f;   // wolf attack at ~10:00 (4h into sim)
    bool  wolfFired    = false;

    std::puts("TIME         NPC        STATE         HP     MOOD        EMOTION");
    std::puts("──────────────────────────────────────────────────────────────────");

    while (simTime < SIM_HOURS * 3600.0f) {
        world.update(DT);
        simTime += DT;

        // Trigger a wolf attack mid-simulation to stress the guard
        if (!wolfFired && simTime >= wolfTime) {
            wolfFired = true;
            std::puts("\n!! WOLF ATTACK — guard takes 55 damage !!\n");
            guard->combat.stats.health -= 55.0f;
            guard->emotions.addEmotion(npc::EmotionType::Fearful, 0.9f, 5.0f);
            guard->emotions.depletNeed(npc::NeedType::Safety, 50.0f);
            // Publish a world event so merchant reacts too
            npc::WorldEvent we;
            we.description = "Wolf attack";
            we.severity    = 0.8f;
            we.location    = guard->position;
            world.events().publish(we);
        }

        if (simTime >= nextReport) {
            printStatus(simTime, *guard);
            printStatus(simTime, *merchant);
            std::puts("");
            nextReport += REPORT_EVERY;
        }
    }

    std::puts("\n══ End of simulation ══");
    std::printf("Guard final state   : %s  HP: %.0f/%.0f\n",
        guard->fsm.currentState().c_str(),
        guard->combat.stats.health,
        guard->combat.stats.maxHealth);
    std::printf("Merchant final state: %s\n",
        merchant->fsm.currentState().c_str());
    std::puts("\nAll behaviour logic ran from Lua — zero C++ FSM lambdas.");

    return 0;
}
