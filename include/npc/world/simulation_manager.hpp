#pragma once
// SimulationManager — orchestrates all NPC systems, update order, LOD, and lifecycle.
//
// Responsibilities:
//   • Correct system update order every tick
//   • LOD (Level of Detail): 3-tier AI budget by player distance
//   • NPC spawn / despawn with automatic event subscription cleanup
//   • SpatialIndex kept in sync after every tick
//   • WeatherSystem integration
//   • Autosave via NpcSerializer
//   • Per-frame performance stats
//
// Usage:
//   GameWorld world(128, 128);
//   SimulationManager sim(world);
//   sim.spawnNPC(make_shared<NPC>(...));
//   sim.setPlayerPosition({64, 64});
//   while (running) sim.update(dt);

#include "world.hpp"
#include "spatial_index.hpp"
#include "weather_system.hpp"
#include "../event/event_system.hpp"
#include "../serialization/npc_serializer.hpp"
#include "../npc.hpp"

#include <vector>
#include <unordered_map>
#include <functional>
#include <string>
#include <chrono>
#include <algorithm>
#include <numeric>

namespace npc {

// ═══════════════════════════════════════════════════════════════════════
// LOD Configuration
// ═══════════════════════════════════════════════════════════════════════

struct LODConfig {
    // Distance thresholds from player position
    float activeRadius     = 60.f;   // Full AI every frame
    float backgroundRadius = 200.f;  // Reduced AI every N frames

    // Tick intervals for non-Active tiers (in simulation frames)
    int backgroundInterval = 5;   // tick every 5 frames
    int dormantInterval    = 30;  // tick every 30 frames
};

// ─── LOD Tier ─────────────────────────────────────────────────────────

enum class LODTier { Active, Background, Dormant };

inline const char* lodTierName(LODTier t) {
    switch (t) {
        case LODTier::Active:     return "Active";
        case LODTier::Background: return "Background";
        case LODTier::Dormant:    return "Dormant";
    }
    return "Unknown";
}

// ═══════════════════════════════════════════════════════════════════════
// Autosave Configuration
// ═══════════════════════════════════════════════════════════════════════

struct AutosaveConfig {
    bool        enabled       = false;
    std::string directory     = "save/";
    float       intervalSecs  = 300.f;  // real-world seconds between autosaves
    int         keepSlots     = 3;      // rolling slot count
    std::string worldFilename = "world.json";
};

// ═══════════════════════════════════════════════════════════════════════
// Frame Stats
// ═══════════════════════════════════════════════════════════════════════

struct SimStats {
    // NPC counts by LOD tier this frame
    int activeCount     = 0;
    int backgroundCount = 0;
    int dormantCount    = 0;
    int totalNPCs       = 0;

    // Tick durations (microseconds)
    int64_t timeMicros          = 0;
    int64_t weatherMicros       = 0;
    int64_t aiMicros            = 0;
    int64_t spatialUpdateMicros = 0;
    int64_t totalMicros         = 0;

    uint64_t frameNumber = 0;
    float    simTimeHours = 0.f; // current simulation hours
};

// ═══════════════════════════════════════════════════════════════════════
// SimulationManager
// ═══════════════════════════════════════════════════════════════════════

class SimulationManager {
public:
    // ── Constructor ──────────────────────────────────────────────────

    explicit SimulationManager(GameWorld& world,
                                LODConfig  lodCfg   = {},
                                float      spatialCellSize = 16.f)
        : world_(world)
        , lodCfg_(lodCfg)
        , spatial_(spatialCellSize)
    {
        weather_.subscribeToEvents(world_.events(), &world_.events());
    }

    // ═══════════════════════════════════════════════════════════════
    // Main Update
    // ═══════════════════════════════════════════════════════════════

    void update(float dt) {
        auto frameStart = now_us();
        ++frameCount_;

        // ── 1. Advance event bus delayed queue ──────────────────────
        float simTimeSecs = world_.time().currentHour() * 3600.f;
        world_.events().update(simTimeSecs);

        // ── 2. Advance simulation clock ──────────────────────────────
        auto t0 = now_us();
        world_.time().update(dt, &world_.events());
        stats_.timeMicros = now_us() - t0;

        // ── 3. Advance weather ───────────────────────────────────────
        auto tw = now_us();
        weather_.update(dt, rng_, &world_.events());
        stats_.weatherMicros = now_us() - tw;

        // ── 4. World event manager (hour-triggered events) ───────────
        world_.eventManager().update(world_.time().currentHour(), world_);

        // ── 5. Classify NPCs by LOD tier ─────────────────────────────
        classify();

        // ── 6. AI tick ───────────────────────────────────────────────
        auto tai = now_us();
        tickActive(dt);
        tickBackground(dt);
        tickDormant(dt);
        stats_.aiMicros = now_us() - tai;

        // ── 7. Refresh spatial index ─────────────────────────────────
        auto ts = now_us();
        for (auto& npc : world_.npcs())
            if (npc) spatial_.update(npc->id, npc->position);
        stats_.spatialUpdateMicros = now_us() - ts;

        // ── 8. Autosave ──────────────────────────────────────────────
        tickAutosave(dt);

        // ── 9. Finalise stats ────────────────────────────────────────
        stats_.totalMicros   = now_us() - frameStart;
        stats_.frameNumber   = frameCount_;
        stats_.simTimeHours  = world_.time().currentHour();
        stats_.totalNPCs     = static_cast<int>(world_.npcs().size());
    }

    // ═══════════════════════════════════════════════════════════════
    // NPC Lifecycle
    // ═══════════════════════════════════════════════════════════════

    void spawnNPC(std::shared_ptr<NPC> npc) {
        if (!npc) return;

        EntityId id = npc->id;

        // Wire event subscriptions; store as ScopedSubscription group
        // so they auto-cleanup on despawn.
        auto& group = subscriptions_[id];
        world_.events().subscribeInto<CombatEvent>(group,
            [n = npc.get()](const CombatEvent& e) { n->onCombatEvent(e); });
        world_.events().subscribeInto<WorldEvent>(group,
            [n = npc.get()](const WorldEvent& e) { n->onWorldEvent(e); });
        world_.events().subscribeInto<QuestCompletedEvent>(group,
            [n = npc.get()](const QuestCompletedEvent& e) { n->onQuestCompleted(e); });
        world_.events().subscribeInto<QuestFailedEvent>(group,
            [n = npc.get()](const QuestFailedEvent& e) { n->onQuestFailed(e); });
        world_.events().subscribeInto<SkillLevelUpEvent>(group,
            [n = npc.get()](const SkillLevelUpEvent& e) { n->onSkillLevelUp(e); });

        // Wire skill system's own event subscriptions
        npc->skills.subscribeToEvents(world_.events());

        // Initial spatial registration
        spatial_.update(id, npc->position);

        // Add to world
        world_.addNPC(npc);

        // Fire spawn callback
        if (onSpawn_) onSpawn_(*world_.findNPC(id));
    }

    void despawnNPC(EntityId id) {
        // Remove event subscriptions (RAII — group destructor fires)
        subscriptions_.erase(id);

        // Remove from spatial index
        spatial_.remove(id);

        // Remove from world NPC list
        auto& npcs = world_.npcs();
        npcs.erase(std::remove_if(npcs.begin(), npcs.end(),
            [id](const std::shared_ptr<NPC>& n) {
                return n && n->id == id;
            }), npcs.end());

        // Clear LOD data
        lodTiers_.erase(id);
        dormantAccum_.erase(id);
        backgroundAccum_.erase(id);

        if (onDespawn_) onDespawn_(id);
    }

    // Despawn all NPCs (e.g., scene transition)
    void despawnAll() {
        subscriptions_.clear();
        spatial_.clear();
        world_.npcs().clear();
        lodTiers_.clear();
        dormantAccum_.clear();
        backgroundAccum_.clear();
    }

    // ═══════════════════════════════════════════════════════════════
    // Player position (drives LOD classification)
    // ═══════════════════════════════════════════════════════════════

    void setPlayerPosition(Vec2 pos) { playerPos_ = pos; }
    Vec2 playerPosition()      const { return playerPos_; }

    // ═══════════════════════════════════════════════════════════════
    // Spatial queries  (convenience wrappers)
    // ═══════════════════════════════════════════════════════════════

    std::vector<EntityId> npcsNearby(Vec2 pos, float radius) const {
        return spatial_.nearby(pos, radius);
    }
    std::vector<EntityId> npcsNearbyExcept(Vec2 pos, float radius, EntityId ex) const {
        return spatial_.nearbyExcept(pos, radius, ex);
    }
    std::optional<SpatialHit> closestNPC(Vec2 pos, float maxDist = 1e9f) const {
        return spatial_.closest(pos, maxDist);
    }
    std::vector<SpatialHit> npcsNearbyWithDist(Vec2 pos, float radius) const {
        return spatial_.nearbyWithDist(pos, radius);
    }

    // ═══════════════════════════════════════════════════════════════
    // LOD queries
    // ═══════════════════════════════════════════════════════════════

    LODTier lodTier(EntityId id) const {
        auto it = lodTiers_.find(id);
        return it != lodTiers_.end() ? it->second : LODTier::Dormant;
    }

    std::vector<EntityId> npcsInTier(LODTier tier) const {
        std::vector<EntityId> out;
        for (auto& [id, t] : lodTiers_)
            if (t == tier) out.push_back(id);
        return out;
    }

    // Force a specific NPC into a tier (e.g., always-active boss)
    void pinLOD(EntityId id, LODTier tier) { pinnedLOD_[id] = tier; }
    void unpinLOD(EntityId id)             { pinnedLOD_.erase(id); }

    // ═══════════════════════════════════════════════════════════════
    // Autosave
    // ═══════════════════════════════════════════════════════════════

    void setAutosave(AutosaveConfig cfg) { autosaveCfg_ = std::move(cfg); }

    bool saveNow(const std::string& filenameOverride = "") const {
        std::string path = autosaveCfg_.directory +
            (filenameOverride.empty() ? autosaveCfg_.worldFilename : filenameOverride);
        return NpcSerializer::saveWorld(world_.npcs(), path);
    }

    bool loadWorld(const std::string& path) {
        auto snaps = NpcSerializer::loadWorld(path);
        for (auto& snap : snaps) {
            // Find existing NPC or skip (caller must pre-create shells)
            NPC* npc = world_.findNPC(snap.id);
            if (npc) NpcSerializer::applySnapshot(*npc, snap);
        }
        return !snaps.empty();
    }

    // ═══════════════════════════════════════════════════════════════
    // Custom tick hooks  (override default LOD behaviour)
    // ═══════════════════════════════════════════════════════════════

    using TickFn = std::function<void(NPC&, float dt, GameWorld&)>;

    // Replace what happens for background-tier NPCs
    void setBackgroundTick(TickFn fn)  { backgroundTickFn_ = std::move(fn); }
    // Replace what happens for dormant-tier NPCs
    void setDormantTick(TickFn fn)     { dormantTickFn_    = std::move(fn); }

    // ═══════════════════════════════════════════════════════════════
    // Lifecycle callbacks
    // ═══════════════════════════════════════════════════════════════

    void onSpawn  (std::function<void(NPC&)>      cb) { onSpawn_   = std::move(cb); }
    void onDespawn(std::function<void(EntityId)>  cb) { onDespawn_ = std::move(cb); }
    void onLODChange(std::function<void(EntityId, LODTier, LODTier)> cb) {
        onLODChange_ = std::move(cb);
    }

    // ═══════════════════════════════════════════════════════════════
    // Accessors
    // ═══════════════════════════════════════════════════════════════

    GameWorld&          world()        { return world_; }
    const GameWorld&    world()  const { return world_; }
    WeatherSystem&      weather()      { return weather_; }
    SpatialIndex&       spatial()      { return spatial_; }
    const SpatialIndex& spatial() const{ return spatial_; }
    const SimStats&     stats()  const { return stats_; }
    LODConfig&          lodConfig()    { return lodCfg_; }
    uint64_t            frameCount()const{ return frameCount_; }

    // ═══════════════════════════════════════════════════════════════
    // Debug / diagnostics
    // ═══════════════════════════════════════════════════════════════

    std::string debugString() const {
        char buf[512];
        std::snprintf(buf, sizeof(buf),
            "Frame %llu | SimTime %.2fh | NPCs: %d total "
            "(%d active / %d bg / %d dormant)\n"
            "  Time %.1fus  Weather %.1fus  AI %.1fus  Spatial %.1fus  Total %.1fus",
            (unsigned long long)stats_.frameNumber,
            stats_.simTimeHours,
            stats_.totalNPCs,
            stats_.activeCount, stats_.backgroundCount, stats_.dormantCount,
            (double)stats_.timeMicros,
            (double)stats_.weatherMicros,
            (double)stats_.aiMicros,
            (double)stats_.spatialUpdateMicros,
            (double)stats_.totalMicros);
        return std::string(buf);
    }

private:
    // ═══════════════════════════════════════════════════════════════
    // LOD Classification
    // ═══════════════════════════════════════════════════════════════

    void classify() {
        stats_.activeCount = stats_.backgroundCount = stats_.dormantCount = 0;

        for (auto& npc : world_.npcs()) {
            if (!npc) continue;
            EntityId id = npc->id;

            // Check pin override
            if (auto it = pinnedLOD_.find(id); it != pinnedLOD_.end()) {
                setTier(id, it->second);
                continue;
            }

            float dist = npc->position.distanceTo(playerPos_);
            LODTier tier;
            if      (dist <= lodCfg_.activeRadius)     tier = LODTier::Active;
            else if (dist <= lodCfg_.backgroundRadius) tier = LODTier::Background;
            else                                        tier = LODTier::Dormant;

            setTier(id, tier);
        }
    }

    void setTier(EntityId id, LODTier tier) {
        auto prev = lodTiers_[id];
        if (prev != tier && onLODChange_) onLODChange_(id, prev, tier);
        lodTiers_[id] = tier;

        switch (tier) {
            case LODTier::Active:     ++stats_.activeCount;     break;
            case LODTier::Background: ++stats_.backgroundCount; break;
            case LODTier::Dormant:    ++stats_.dormantCount;    break;
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // LOD Tick Implementations
    // ═══════════════════════════════════════════════════════════════

    void tickActive(float dt) {
        for (auto& npc : world_.npcs()) {
            if (!npc || lodTiers_[npc->id] != LODTier::Active) continue;
            npc->update(dt, world_);
        }
    }

    void tickBackground(float dt) {
        if (frameCount_ % static_cast<uint64_t>(lodCfg_.backgroundInterval) != 0)
            return;

        float scaledDt = dt * static_cast<float>(lodCfg_.backgroundInterval);

        for (auto& npc : world_.npcs()) {
            if (!npc || lodTiers_[npc->id] != LODTier::Background) continue;

            if (backgroundTickFn_) {
                backgroundTickFn_(*npc, scaledDt, world_);
            } else {
                defaultBackgroundTick(*npc, scaledDt);
            }
        }
    }

    void tickDormant(float dt) {
        // Accumulate dt; fire when threshold crossed
        for (auto& npc : world_.npcs()) {
            if (!npc || lodTiers_[npc->id] != LODTier::Dormant) continue;
            dormantAccum_[npc->id] += dt;
        }

        if (frameCount_ % static_cast<uint64_t>(lodCfg_.dormantInterval) != 0)
            return;

        for (auto& npc : world_.npcs()) {
            if (!npc || lodTiers_[npc->id] != LODTier::Dormant) continue;
            EntityId id  = npc->id;
            float    acc = dormantAccum_[id];
            dormantAccum_[id] = 0.f;

            if (dormantTickFn_) {
                dormantTickFn_(*npc, acc, world_);
            } else {
                defaultDormantTick(*npc, acc);
            }
        }
    }

    // Default background tick: movement + emotions + schedule fatigue
    static void defaultBackgroundTick(NPC& npc, float dt) {
        npc.emotions.update(dt);
        npc.updateMovement(dt);
        npc.schedule.updateFatigue(dt);
    }

    // Default dormant tick: only needs/emotion decay (no movement, no AI)
    static void defaultDormantTick(NPC& npc, float dt) {
        npc.emotions.update(dt);
    }

    // ═══════════════════════════════════════════════════════════════
    // Autosave helper
    // ═══════════════════════════════════════════════════════════════

    void tickAutosave(float dt) {
        if (!autosaveCfg_.enabled) return;
        autosaveAccum_ += dt;
        if (autosaveAccum_ < autosaveCfg_.intervalSecs) return;
        autosaveAccum_ = 0.f;

        // Rolling slot: rename previous saves
        std::string base = autosaveCfg_.directory + autosaveCfg_.worldFilename;
        for (int slot = autosaveCfg_.keepSlots - 1; slot > 0; --slot) {
            std::string older = base + "." + std::to_string(slot);
            std::string newer = slot == 1 ? base : base + "." + std::to_string(slot - 1);
            std::rename(newer.c_str(), older.c_str());
        }
        NpcSerializer::saveWorld(world_.npcs(), base);
    }

    // ═══════════════════════════════════════════════════════════════
    // Utilities
    // ═══════════════════════════════════════════════════════════════

    static int64_t now_us() {
        using namespace std::chrono;
        return duration_cast<microseconds>(
            steady_clock::now().time_since_epoch()).count();
    }

    // ═══════════════════════════════════════════════════════════════
    // Members
    // ═══════════════════════════════════════════════════════════════

    GameWorld&    world_;
    LODConfig     lodCfg_;
    SpatialIndex  spatial_;
    WeatherSystem weather_;
    RandomGenerator rng_;

    Vec2     playerPos_{0.f, 0.f};
    uint64_t frameCount_ = 0;

    // Per-NPC LOD state
    std::unordered_map<EntityId, LODTier> lodTiers_;
    std::unordered_map<EntityId, LODTier> pinnedLOD_;
    std::unordered_map<EntityId, float>   dormantAccum_;
    std::unordered_map<EntityId, float>   backgroundAccum_;

    // Per-NPC event subscription groups (auto-cleanup on despawn)
    std::unordered_map<EntityId, SubscriptionGroup> subscriptions_;

    // Custom tick overrides
    TickFn backgroundTickFn_;
    TickFn dormantTickFn_;

    // Callbacks
    std::function<void(NPC&)>                        onSpawn_;
    std::function<void(EntityId)>                    onDespawn_;
    std::function<void(EntityId, LODTier, LODTier)>  onLODChange_;

    // Autosave
    AutosaveConfig autosaveCfg_;
    float          autosaveAccum_ = 0.f;

    // Stats
    SimStats stats_;
};

} // namespace npc
