/**
 * run_benchmarks.cpp — NPC Behavior System Performance Benchmarks
 *
 * Answers the core question: "How many NPCs can this handle at once?"
 *
 * Build:
 *   cmake -B build -DCMAKE_BUILD_TYPE=Release -DNPC_BENCHMARKS=ON
 *   cmake --build build --target run_benchmarks
 *   ./build/run_benchmarks
 *
 * Optional flags:
 *   ./build/run_benchmarks --quick     (fewer iterations, faster run)
 *   ./build/run_benchmarks --csv       (machine-readable CSV output)
 */

#include "npc/npc.hpp"
#include "npc/world/world.hpp"
#include "npc/world/spatial_index.hpp"
#include "npc/social/relationship_system.hpp"
#include "npc/navigation/pathfinding.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

/* ── Timing ─────────────────────────────────────────────────────────────── */

using Clock = std::chrono::steady_clock;
using Ms    = std::chrono::duration<double, std::milli>;

struct BenchResult {
    double   mean_ms   = 0.0;
    double   min_ms    = 0.0;
    double   max_ms    = 0.0;
    double   stddev_ms = 0.0;
    int      iters     = 0;
};

static BenchResult measure(int iters, std::function<void()> fn)
{
    // Warm-up
    fn();

    std::vector<double> samples;
    samples.reserve(iters);

    for (int i = 0; i < iters; ++i) {
        auto t0 = Clock::now();
        fn();
        auto t1 = Clock::now();
        samples.push_back(std::chrono::duration_cast<Ms>(t1 - t0).count());
    }

    double mean = std::accumulate(samples.begin(), samples.end(), 0.0) / iters;
    double minv = *std::min_element(samples.begin(), samples.end());
    double maxv = *std::max_element(samples.begin(), samples.end());
    double var  = 0.0;
    for (auto s : samples) var += (s - mean) * (s - mean);
    var /= iters;

    return { mean, minv, maxv, std::sqrt(var), iters };
}

/* ── Formatting ─────────────────────────────────────────────────────────── */

static bool g_csv   = false;
static bool g_quick = false;

static std::string fmtMs(double ms)
{
    char buf[32];
    if (ms < 0.01)        std::snprintf(buf, sizeof(buf), "%.4f ms", ms);
    else if (ms < 0.1)    std::snprintf(buf, sizeof(buf), "%.3f ms", ms);
    else if (ms < 10.0)   std::snprintf(buf, sizeof(buf), "%.2f ms", ms);
    else                  std::snprintf(buf, sizeof(buf), "%.1f ms", ms);
    return buf;
}

static std::string budgetBar(double ms, double budget_ms, int width = 20)
{
    double ratio = ms / budget_ms;
    if (ratio > 1.0) ratio = 1.0;
    int filled = static_cast<int>(ratio * width);
    std::string bar = "[";
    for (int i = 0; i < width; ++i)
        bar += (i < filled) ? '#' : '.';
    bar += "]";
    return bar;
}

static void printHeader(const std::string& title)
{
    if (g_csv) return;
    std::cout << "\n";
    std::string line(60, '-');
    std::cout << line << "\n  " << title << "\n" << line << "\n";
}

static void printRow(const std::string& label, const BenchResult& r,
                     double budget_ms = 16.667)
{
    double pct_used = (r.mean_ms / budget_ms) * 100.0;
    double pct_free = 100.0 - pct_used;
    if (pct_free < 0.0) pct_free = 0.0;

    if (g_csv) {
        std::cout << std::fixed << std::setprecision(4)
                  << label << "," << r.mean_ms << "," << r.min_ms
                  << "," << r.max_ms << "," << r.stddev_ms << "\n";
        return;
    }

    char buf[160];
    std::snprintf(buf, sizeof(buf),
        "  %-22s  %9s  60Hz budget: %5.1f%% free  %s",
        label.c_str(),
        fmtMs(r.mean_ms).c_str(),
        pct_free,
        budgetBar(r.mean_ms, budget_ms).c_str());
    std::cout << buf << "\n";
}

static void printSingle(const std::string& label, const BenchResult& r)
{
    if (g_csv) {
        std::cout << std::fixed << std::setprecision(4)
                  << label << "," << r.mean_ms << "," << r.min_ms
                  << "," << r.max_ms << "\n";
        return;
    }
    char buf[120];
    std::snprintf(buf, sizeof(buf),
        "  %-30s  %s  (min %s / max %s)",
        label.c_str(),
        fmtMs(r.mean_ms).c_str(),
        fmtMs(r.min_ms).c_str(),
        fmtMs(r.max_ms).c_str());
    std::cout << buf << "\n";
}

/* ── NPC factory ─────────────────────────────────────────────────────────── */

static std::mt19937 rng(42);

static std::shared_ptr<npc::NPC> makeNPC(npc::EntityId id,
                                          npc::GameWorld& world,
                                          float worldW, float worldH)
{
    std::uniform_real_distribution<float> px(1.f, worldW - 1.f);
    std::uniform_real_distribution<float> py(1.f, worldH - 1.f);
    std::uniform_int_distribution<int>    ti(0, 6);

    static const npc::NPCType types[] = {
        npc::NPCType::Guard,   npc::NPCType::Merchant, npc::NPCType::Villager,
        npc::NPCType::Farmer,  npc::NPCType::Innkeeper,npc::NPCType::Blacksmith,
        npc::NPCType::Enemy
    };

    auto n = std::make_shared<npc::NPC>(id, "NPC_" + std::to_string(id),
                                         types[ti(rng)]);
    n->position = { px(rng), py(rng) };
    n->verbose  = false;

    // Give every NPC a minimal FSM so the update path is exercised
    n->fsm.addState("idle",
        [](npc::Blackboard&, float) {},   // update
        nullptr, nullptr);
    n->fsm.setInitialState("idle");

    // Add a couple of emotions to stress the emotion system
    n->emotions.addEmotion(npc::EmotionType::Happy,   0.4f, 999.f);
    n->emotions.addEmotion(npc::EmotionType::Fearful,  0.2f, 999.f);

    n->subscribeToEvents(world.events());
    world.addNPC(n);
    return n;
}

/* ═══════════════════════════════════════════════════════════════════════════
   BENCHMARK 1 — Full world tick (world.update)
   ═══════════════════════════════════════════════════════════════════════════ */

static void bench_world_tick(int outer_iters)
{
    printHeader("Full NPC tick  (world.update — all systems, 1 frame @ dt=1/60)");
    if (g_csv) std::cout << "label,mean_ms,min_ms,max_ms,stddev_ms\n";

    // Note: each NPC loops over all other NPCs for contagion + perception
    // → O(N²) per world.update. The LOD system is the intended mitigation.

    const std::vector<int> counts = g_quick
        ? std::vector<int>{50, 100, 250, 500}
        : std::vector<int>{50, 100, 250, 500, 1000, 2000};

    for (int n : counts) {
        // Build world fresh for each count
        npc::GameWorld world(128, 128);
        for (int i = 1; i <= n; ++i)
            makeNPC(static_cast<npc::EntityId>(i), world, 128.f, 128.f);

        const float dt = 1.0f / 60.0f;

        auto r = measure(outer_iters, [&]{ world.update(dt); });

        char label[32];
        std::snprintf(label, sizeof(label), "%5d NPCs", n);
        printRow(label, r);
    }

    if (!g_csv) {
        std::cout << "\n  Note: contagion + perception loops are O(N\xc2\xb2).\n"
                  << "  Use the LOD system to cap active-tick NPC count in production.\n";
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
   BENCHMARK 2 — Emotion + needs update only (isolated subsystem)
   ═══════════════════════════════════════════════════════════════════════════ */

static void bench_emotion_update(int outer_iters)
{
    printHeader("Emotion & needs update (isolated, no perception/contagion)");

    const std::vector<int> counts = g_quick
        ? std::vector<int>{500, 2000, 5000}
        : std::vector<int>{500, 1000, 2000, 5000, 10000};

    for (int n : counts) {
        std::vector<npc::EmotionSystem> systems(n);
        for (auto& es : systems) {
            es.addEmotion(npc::EmotionType::Angry,   0.5f, 999.f);
            es.addEmotion(npc::EmotionType::Fearful, 0.3f, 999.f);
        }

        const float dt = 1.0f / 60.0f;
        auto r = measure(outer_iters, [&]{
            for (auto& es : systems) es.update(dt);
        });

        char label[32];
        std::snprintf(label, sizeof(label), "%6d emotion systems", n);
        printRow(label, r);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
   BENCHMARK 3 — FSM update (state transitions + blackboard writes)
   ═══════════════════════════════════════════════════════════════════════════ */

static void bench_fsm_update(int outer_iters)
{
    printHeader("FSM update (3 states, guarded transitions, blackboard)");

    const std::vector<int> counts = g_quick
        ? std::vector<int>{1000, 5000, 10000}
        : std::vector<int>{1000, 5000, 10000, 50000};

    for (int n : counts) {
        std::vector<npc::FSM> fsms(n);
        for (auto& fsm : fsms) {
            fsm.addState("idle",    [](npc::Blackboard&, float){}, nullptr, nullptr);
            fsm.addState("alert",   [](npc::Blackboard&, float){}, nullptr, nullptr);
            fsm.addState("combat",  [](npc::Blackboard&, float){}, nullptr, nullptr);

            fsm.addTransition("idle",  "alert",
                [](const npc::Blackboard& bb){ return bb.getOr<bool>("threat", false); });
            fsm.addTransition("alert", "combat",
                [](const npc::Blackboard& bb){ return bb.getOr<float>("hp", 1.f) < 0.5f; });
            fsm.addTransition("combat","idle",
                [](const npc::Blackboard& bb){ return bb.getOr<bool>("safe", false); });

            fsm.setInitialState("idle");
        }

        const float dt = 1.0f / 60.0f;
        std::uniform_real_distribution<float> rf(0.f, 1.f);

        auto r = measure(outer_iters, [&]{
            for (auto& fsm : fsms) {
                // Randomise blackboard so transitions are exercised
                fsm.blackboard().set<bool>("threat",  rf(rng) > 0.7f);
                fsm.blackboard().set<float>("hp",     rf(rng));
                fsm.blackboard().set<bool>("safe",    rf(rng) > 0.8f);
                fsm.update(dt);
            }
        });

        char label[32];
        std::snprintf(label, sizeof(label), "%6d FSMs", n);
        printRow(label, r);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
   BENCHMARK 4 — Pathfinding (A* at various grid sizes)
   ═══════════════════════════════════════════════════════════════════════════ */

static void bench_pathfinding(int outer_iters)
{
    printHeader("Pathfinding — A*  (corner-to-corner, open grid, no cache)");

    struct GridCase { int w, h; };
    const std::vector<GridCase> cases = g_quick
        ? std::vector<GridCase>{{16,16},{64,64}}
        : std::vector<GridCase>{{16,16},{32,32},{64,64},{128,128},{256,256}};

    for (auto [w, h] : cases) {
        auto walkable = [](int, int){ return true; };
        npc::Vec2 start{1.f, 1.f};
        npc::Vec2 goal{ static_cast<float>(w-2), static_cast<float>(h-2) };

        // Construct a fresh Pathfinder per iteration so every call is a
        // cold-cache A* query (construction cost is negligible vs. the search).
        auto r = measure(outer_iters, [&]{
            npc::Pathfinder pf(w, h, walkable);
            pf.findPath(start, goal);
        });

        char label[40];
        std::snprintf(label, sizeof(label), "%4dx%-4d grid", w, h);
        printSingle(label, r);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
   BENCHMARK 5 — Pathfinding cache hit vs miss
   ═══════════════════════════════════════════════════════════════════════════ */

static void bench_pathfinding_cache(int outer_iters)
{
    printHeader("Pathfinding — cache miss vs hit  (64x64 grid)");

    auto walkable = [](int, int){ return true; };
    npc::Vec2 start{1.f, 1.f};
    npc::Vec2 goal{62.f, 62.f};

    // Miss — fresh Pathfinder each call, always cold cache
    {
        auto r = measure(outer_iters, [&]{
            npc::Pathfinder pf(64, 64, walkable);
            pf.findPath(start, goal);
        });
        printSingle("cache miss     (raw A*)", r);
    }

    // Hit — same query every time
    {
        npc::Pathfinder pf(64, 64, walkable);
        pf.setCacheCapacity(256);
        pf.findPath(start, goal);  // prime the cache
        auto r = measure(outer_iters, [&]{ pf.findPath(start, goal); });
        printSingle("cache hit      (LRU)",   r);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
   BENCHMARK 6 — Spatial index
   ═══════════════════════════════════════════════════════════════════════════ */

static void bench_spatial(int outer_iters)
{
    printHeader("Spatial index — SpatialGrid  (insert + radius query)");

    const std::vector<int> counts = g_quick
        ? std::vector<int>{1000, 5000}
        : std::vector<int>{1000, 5000, 10000, 50000};

    for (int n : counts) {
        npc::SpatialIndex idx(10.f);

        std::uniform_real_distribution<float> rf(0.f, 1000.f);
        std::vector<npc::Vec2> positions(n);
        for (int i = 0; i < n; ++i) {
            positions[i] = {rf(rng), rf(rng)};
            idx.update(static_cast<npc::EntityId>(i+1), positions[i]);
        }

        npc::Vec2 queryPos{500.f, 500.f};

        // Insert + query each frame (simulate full sync)
        auto rInsert = measure(outer_iters, [&]{
            for (int i = 0; i < n; ++i)
                idx.update(static_cast<npc::EntityId>(i+1), positions[i]);
        });

        auto rQuery = measure(outer_iters, [&]{
            idx.nearby(queryPos, 50.f);
        });

        char label[40];
        std::snprintf(label, sizeof(label), "%6d entities — update", n);
        printSingle(label, rInsert);
        std::snprintf(label, sizeof(label), "%6d entities — query r=50", n);
        printSingle(label, rQuery);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
   BENCHMARK 7 — Relationship system
   ═══════════════════════════════════════════════════════════════════════════ */

static void bench_relationships(int outer_iters)
{
    printHeader("Relationship system  (record event + getValue + narrative)");

    const std::vector<int> counts = g_quick
        ? std::vector<int>{100, 500}
        : std::vector<int>{100, 500, 1000, 5000};

    for (int n : counts) {
        npc::RelationshipSystem rs;

        // Pre-populate with N directed pairs
        for (int i = 0; i < n; ++i) {
            std::string a = "npc_" + std::to_string(i);
            std::string b = "npc_" + std::to_string((i + 1) % n);
            rs.recordEvent(a, b, npc::RelationshipEventType::Helped, 0.0);
        }

        // Measure: record N events
        auto rRecord = measure(outer_iters, [&]{
            for (int i = 0; i < n; ++i) {
                std::string a = "npc_" + std::to_string(i);
                std::string b = "npc_" + std::to_string((i+1) % n);
                rs.recordEvent(a, b, npc::RelationshipEventType::Traded, 1.0);
            }
        });

        // Measure: N getValue queries
        auto rGet = measure(outer_iters, [&]{
            for (int i = 0; i < n; ++i) {
                std::string a = "npc_" + std::to_string(i);
                std::string b = "npc_" + std::to_string((i+1) % n);
                volatile float v = rs.getValue(a, b);
                (void)v;
            }
        });

        char label[48];
        std::snprintf(label, sizeof(label), "%5d pairs — recordEvent", n);
        printSingle(label, rRecord);
        std::snprintf(label, sizeof(label), "%5d pairs — getValue",   n);
        printSingle(label, rGet);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
   BENCHMARK 8 — Memory system
   ═══════════════════════════════════════════════════════════════════════════ */

static void bench_memory(int outer_iters)
{
    printHeader("Memory system  (addMemory + update/decay)");

    const std::vector<int> counts = g_quick
        ? std::vector<int>{100, 1000}
        : std::vector<int>{100, 500, 1000, 5000, 10000};

    for (int n : counts) {
        std::vector<npc::MemorySystem> systems;
        systems.reserve(n);
        for (int i = 0; i < n; ++i) {
            npc::MemorySystem ms(50);
            for (int j = 0; j < 20; ++j)
                ms.addMemory(npc::MemoryType::Interaction,
                             "event_" + std::to_string(j),
                             (j % 2 == 0) ? 0.5f : -0.3f,
                             std::nullopt, 0.6f);
            systems.push_back(std::move(ms));
        }

        const float dt = 1.0f / 60.0f;
        auto r = measure(outer_iters, [&]{
            for (auto& ms : systems) ms.update(dt);
        });

        char label[40];
        std::snprintf(label, sizeof(label), "%6d memory systems", n);
        printRow(label, r);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
   BENCHMARK 9 — Memory per NPC estimate
   ═══════════════════════════════════════════════════════════════════════════ */

static void bench_memory_footprint()
{
    if (g_csv) return;

    printHeader("Memory footprint estimate");

    // Rough per-NPC size
    std::size_t npc_size   = sizeof(npc::NPC);
    std::size_t world_size = sizeof(npc::GameWorld);

    std::cout << "  sizeof(NPC)            : " << npc_size   << " bytes ("
              << (npc_size / 1024) << " KB)\n";
    std::cout << "  sizeof(GameWorld)      : " << world_size << " bytes\n";

    // Estimate full heap cost including FSM/memory/emotions containers
    // (containers have ~heap overhead; this is a lower bound)
    std::cout << "\n  Estimated heap per NPC : ~"
              << (npc_size / 1024 + 4)
              << "–" << (npc_size / 1024 + 16)
              << " KB  (depends on memory/emotion/path history depth)\n";

    std::size_t npc_1k  = (npc_size + 8192) * 1000  / (1024*1024);
    std::size_t npc_10k = (npc_size + 8192) * 10000 / (1024*1024);
    std::cout << "  1 000 NPCs (estimate)  : ~" << npc_1k  << " MB\n";
    std::cout << "  10 000 NPCs (estimate) : ~" << npc_10k << " MB\n";
}

/* ═══════════════════════════════════════════════════════════════════════════
   main
   ═══════════════════════════════════════════════════════════════════════════ */

int main(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--csv")   == 0) g_csv   = true;
        if (std::strcmp(argv[i], "--quick") == 0) g_quick = true;
    }

    const int ITERS = g_quick ? 5 : 20;

    if (!g_csv) {
        std::cout << "\n";
        std::cout << "================================================================\n";
        std::cout << "  NPC Behavior System — Performance Benchmarks\n";
        std::cout << "  Build: Release | C++17\n";
        std::cout << "  Iterations per measurement: " << ITERS << "\n";
        std::cout << "  Budget reference: 16.67 ms (60 Hz frame)\n";
        std::cout << "================================================================\n";
    } else {
        std::cout << "benchmark,label,mean_ms,min_ms,max_ms,stddev_ms\n";
    }

    bench_world_tick(ITERS);
    bench_emotion_update(ITERS);
    bench_fsm_update(ITERS);
    bench_pathfinding(ITERS);
    bench_pathfinding_cache(ITERS);
    bench_spatial(ITERS);
    bench_relationships(ITERS);
    bench_memory(ITERS);
    bench_memory_footprint();

    if (!g_csv) {
        std::cout << "\n";
        std::cout << "================================================================\n";
        std::cout << "  All benchmarks complete.\n";
        std::cout << "================================================================\n\n";
    }

    return 0;
}
