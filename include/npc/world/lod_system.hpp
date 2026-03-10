#pragma once
// LOD (Level of Detail) System — standalone NPC tick budget manager.
//
// Features:
//   • 3-tier classification (Active / Background / Dormant)
//   • Hysteresis — demotion delayed by margin + min dwell time (no flickering)
//   • Importance scoring — quest NPCs / bosses get larger effective radius
//   • Velocity prediction — NPCs approaching the player promoted early
//   • Group elevation — all group members share the highest member's tier
//   • CPU budget tracking — reports ms spent per tier per frame
//   • Tick-debt accumulator — variable-framerate–safe dt for Background/Dormant
//   • LOD-change callbacks
//   • SimulationManager delegates to this class
//
// Usage:
//   LODSystem lod;
//   lod.setPlayerPosition(playerPos);
//   lod.update(npcs, simTime, dt);
//   for (auto id : lod.toTickThisFrame(LODTier::Active)) ...

#include "../core/types.hpp"
#include "../core/vec2.hpp"
#include "../npc.hpp"

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <chrono>

namespace npc {

// ═══════════════════════════════════════════════════════════════════════
// LODTier (canonical definition — SimulationManager re-uses this)
// ═══════════════════════════════════════════════════════════════════════

enum class LODTier : uint8_t { Active = 0, Background = 1, Dormant = 2 };

inline const char* lodTierName(LODTier t) {
    switch (t) {
        case LODTier::Active:     return "Active";
        case LODTier::Background: return "Background";
        case LODTier::Dormant:    return "Dormant";
    }
    return "Unknown";
}

// ═══════════════════════════════════════════════════════════════════════
// LODConfig
// ═══════════════════════════════════════════════════════════════════════

struct LODConfig {
    // ── Distance thresholds (base, before importance scaling) ────────
    float activeRadius     = 60.f;
    float backgroundRadius = 200.f;

    // ── Hysteresis ───────────────────────────────────────────────────
    // Demotion only occurs when NPC is `demotionMargin` units PAST the
    // threshold AND has spent at least `minDwellSecs` in the current tier.
    // Promotion is immediate (no hysteresis — responsiveness preferred).
    float demotionMargin  = 12.f;   // extra units past threshold
    float minDwellSecs    = 1.5f;   // min seconds before demotion allowed

    // ── Importance ───────────────────────────────────────────────────
    // importance ∈ [0,1].  Effective radius = base + importance × bonus.
    float importanceBonus = 50.f;   // added to both thresholds at importance=1

    // ── Velocity prediction ──────────────────────────────────────────
    // If predicted position (pos + vel × horizon) crosses a threshold,
    // the NPC is pre-promoted one tier.
    bool  velocityPrediction  = true;
    float predictionHorizonSecs = 3.f;

    // ── Tick intervals (frames to skip between ticks) ────────────────
    int backgroundSkip = 4;   // tick every 5th frame (skip 4)
    int dormantSkip    = 19;  // tick every 20th frame (skip 19)

    // ── CPU budget ───────────────────────────────────────────────────
    float budgetMsPerFrame   = 8.f;   // target AI budget per frame
    int   maxActivePerFrame  = 64;    // hard cap on Active NPCs ticked

    // ── Group elevation ──────────────────────────────────────────────
    bool groupElevation = true;
    // If any group member is Active → remaining members become Background minimum
    // If any group member is Background → remaining become Background minimum
};

// ═══════════════════════════════════════════════════════════════════════
// LODRecord — per-NPC state
// ═══════════════════════════════════════════════════════════════════════

struct LODRecord {
    EntityId  id             = INVALID_ENTITY;
    LODTier   tier           = LODTier::Dormant;
    LODTier   pendingTier    = LODTier::Dormant; // target after hysteresis
    float     distToPlayer   = 0.f;
    float     importance     = 0.f;   // 0=normal … 1=always prioritised
    float     tierEnteredAt  = 0.f;   // sim time (hours) when tier last changed
    uint64_t  ticksInTier    = 0;     // frames spent in current tier

    // Tick scheduling
    float     accumDt        = 0.f;   // accumulated dt for Background/Dormant
    uint64_t  lastTickFrame  = 0;

    // Group
    uint32_t  groupId        = 0;     // 0 = no group

    // Pin override
    bool      pinned         = false;
    LODTier   pinnedTier     = LODTier::Active;

    // Velocity (estimated each frame from position delta)
    Vec2      lastPos{0, 0};
    Vec2      velocity{0, 0};

    // CPU cost (exponential moving average, ms)
    float avgTickMs = 0.f;
};

// ═══════════════════════════════════════════════════════════════════════
// LODSystem
// ═══════════════════════════════════════════════════════════════════════

class LODSystem {
public:
    using TierChangeCb = std::function<void(EntityId, LODTier /*old*/,
                                             LODTier /*new*/)>;

    explicit LODSystem(LODConfig cfg = {}) : cfg_(cfg) {}

    // ── Configuration ────────────────────────────────────────────────

    void setConfig(LODConfig cfg)        { cfg_ = std::move(cfg); }
    const LODConfig& config() const      { return cfg_; }

    void setPlayerPosition(Vec2 pos)     { playerPos_ = pos; }
    Vec2 playerPosition()          const { return playerPos_; }

    // ── NPC registration ─────────────────────────────────────────────

    void registerNPC(EntityId id, float importance = 0.f,
                     uint32_t groupId = 0) {
        LODRecord r;
        r.id         = id;
        r.importance = std::clamp(importance, 0.f, 1.f);
        r.groupId    = groupId;
        r.tier       = LODTier::Dormant;
        records_[id] = r;
    }

    void unregisterNPC(EntityId id) { records_.erase(id); }

    bool isRegistered(EntityId id) const { return records_.count(id); }

    // ── Importance & pinning ─────────────────────────────────────────

    void setImportance(EntityId id, float importance) {
        if (auto it = records_.find(id); it != records_.end())
            it->second.importance = std::clamp(importance, 0.f, 1.f);
    }

    void setGroup(EntityId id, uint32_t groupId) {
        if (auto it = records_.find(id); it != records_.end())
            it->second.groupId = groupId;
    }

    void pin(EntityId id, LODTier tier) {
        if (auto it = records_.find(id); it != records_.end()) {
            it->second.pinned     = true;
            it->second.pinnedTier = tier;
        }
    }

    void unpin(EntityId id) {
        if (auto it = records_.find(id); it != records_.end())
            it->second.pinned = false;
    }

    // ── Main update ──────────────────────────────────────────────────
    // Call once per frame before ticking NPCs.

    void update(const std::vector<std::shared_ptr<NPC>>& npcs,
                float simTimeHours, float dt)
    {
        ++frameCount_;
        frameStartMs_ = nowMs();

        // 1. Update per-NPC velocity estimates and raw distance
        for (auto& npc : npcs) {
            if (!npc) continue;
            auto it = records_.find(npc->id);
            if (it == records_.end()) continue;
            auto& r = it->second;

            if (r.lastTickFrame > 0) {
                r.velocity = (npc->position - r.lastPos) *
                             (dt > 1e-6f ? 1.f / dt : 0.f);
            }
            r.lastPos       = npc->position;
            r.lastTickFrame = frameCount_;
            r.distToPlayer  = npc->position.distanceTo(playerPos_);
        }

        // 2. Classify each NPC into pendingTier
        for (auto& [id, r] : records_) {
            if (r.pinned) { r.pendingTier = r.pinnedTier; continue; }
            r.pendingTier = classify(r, simTimeHours);
        }

        // 3. Group elevation — raise tier of group members
        if (cfg_.groupElevation) applyGroupElevation();

        // 4. Apply hysteresis — commit tier changes
        for (auto& [id, r] : records_) {
            LODTier desired = r.pendingTier;
            if (desired == r.tier) {
                ++r.ticksInTier;
                continue;
            }

            bool promoting = static_cast<int>(desired) <
                             static_cast<int>(r.tier);

            if (promoting) {
                // Promote immediately
                changeTier(r, desired, simTimeHours);
            } else {
                // Demote only after hysteresis check
                if (canDemote(r, desired, simTimeHours)) {
                    changeTier(r, desired, simTimeHours);
                } else {
                    ++r.ticksInTier; // stay in current tier
                }
            }
        }

        // 5. Build per-tier tick lists for this frame
        buildTickLists(dt);
    }

    // ── Tick schedule queries ────────────────────────────────────────
    // Call after update() to get who should tick this frame.

    const std::vector<EntityId>& toTickThisFrame(LODTier tier) const {
        switch (tier) {
            case LODTier::Active:     return tickActive_;
            case LODTier::Background: return tickBackground_;
            case LODTier::Dormant:    return tickDormant_;
        }
        return tickActive_;
    }

    // Accumulated dt for a Background/Dormant NPC (resets after access)
    float consumeAccumDt(EntityId id) {
        auto it = records_.find(id);
        if (it == records_.end()) return 0.f;
        float dt = it->second.accumDt;
        it->second.accumDt = 0.f;
        return dt;
    }

    // ── Tier queries ─────────────────────────────────────────────────

    LODTier tier(EntityId id) const {
        auto it = records_.find(id);
        return it != records_.end() ? it->second.tier : LODTier::Dormant;
    }

    const LODRecord* record(EntityId id) const {
        auto it = records_.find(id);
        return it != records_.end() ? &it->second : nullptr;
    }

    // ── CPU budget reporting ─────────────────────────────────────────

    void reportTickCost(EntityId id, float ms) {
        budgetUsedMs_ += ms;
        auto it = records_.find(id);
        if (it == records_.end()) return;
        // EMA with α=0.2
        it->second.avgTickMs =
            0.8f * it->second.avgTickMs + 0.2f * ms;
    }

    float budgetUsedMs()      const { return budgetUsedMs_; }
    float budgetRemainingMs() const {
        return std::max(0.f, cfg_.budgetMsPerFrame - budgetUsedMs_);
    }

    // ── Callbacks ────────────────────────────────────────────────────

    void onTierChange(TierChangeCb cb) { tierChangeCb_ = std::move(cb); }

    // ── Stats ─────────────────────────────────────────────────────────

    struct Stats {
        int      active = 0, background = 0, dormant = 0, total = 0;
        int      tickedActive = 0, tickedBg = 0, tickedDormant = 0;
        float    budgetUsedMs = 0.f;
        uint64_t frame = 0;
    };

    Stats stats() const {
        Stats s;
        s.frame       = frameCount_;
        s.budgetUsedMs= budgetUsedMs_;
        s.tickedActive    = static_cast<int>(tickActive_.size());
        s.tickedBg        = static_cast<int>(tickBackground_.size());
        s.tickedDormant   = static_cast<int>(tickDormant_.size());
        for (auto& [id, r] : records_) {
            ++s.total;
            switch (r.tier) {
                case LODTier::Active:     ++s.active;     break;
                case LODTier::Background: ++s.background; break;
                case LODTier::Dormant:    ++s.dormant;    break;
            }
        }
        return s;
    }

    std::string debugString() const {
        auto s = stats();
        std::ostringstream ss;
        ss << "LOD frame=" << s.frame
           << " | Active="     << s.active
           << " Background="   << s.background
           << " Dormant="      << s.dormant
           << " | Ticked: A="  << s.tickedActive
           << " B="            << s.tickedBg
           << " D="            << s.tickedDormant
           << " | Budget="     << s.budgetUsedMs << "ms";
        return ss.str();
    }

    // Per-NPC debug line
    std::string debugNPC(EntityId id) const {
        auto* r = record(id);
        if (!r) return "not registered";
        std::ostringstream ss;
        ss << "id=" << id
           << " tier=" << lodTierName(r->tier)
           << " dist=" << static_cast<int>(r->distToPlayer)
           << " imp=" << r->importance
           << " ticks=" << r->ticksInTier
           << " avgMs=" << r->avgTickMs;
        if (r->pinned) ss << " [PINNED]";
        if (r->groupId) ss << " grp=" << r->groupId;
        return ss.str();
    }

    size_t registeredCount() const { return records_.size(); }

private:
    // ── Classification ───────────────────────────────────────────────

    LODTier classify(const LODRecord& r, float /*simTime*/) const {
        float scale  = 1.f + r.importance * cfg_.importanceBonus /
                       std::max(cfg_.activeRadius, 1.f);
        float actR   = cfg_.activeRadius     * scale;
        float bgR    = cfg_.backgroundRadius * scale;

        float dist = r.distToPlayer;

        // Velocity prediction: check predicted position
        if (cfg_.velocityPrediction) {
            float speed = r.velocity.length();
            if (speed > 0.1f) {
                Vec2 predicted = r.lastPos +
                    r.velocity * cfg_.predictionHorizonSecs;
                float predDist = predicted.distanceTo(playerPos_);
                // Use whichever distance is smaller (conservative promotion)
                dist = std::min(dist, predDist);
            }
        }

        if (dist <= actR) return LODTier::Active;
        if (dist <= bgR)  return LODTier::Background;
        return LODTier::Dormant;
    }

    // ── Hysteresis ───────────────────────────────────────────────────

    bool canDemote(const LODRecord& r, LODTier target,
                   float simTimeHours) const {
        // Must have been in tier long enough (real time approximate)
        float dwellHours = simTimeHours - r.tierEnteredAt;
        float dwellSecs  = dwellHours * 3600.f;
        if (dwellSecs < cfg_.minDwellSecs) return false;

        // Must be past the threshold by demotionMargin
        float scale = 1.f + r.importance * cfg_.importanceBonus /
                      std::max(cfg_.activeRadius, 1.f);

        if (r.tier == LODTier::Active && target >= LODTier::Background) {
            float thresh = cfg_.activeRadius * scale + cfg_.demotionMargin;
            return r.distToPlayer > thresh;
        }
        if (r.tier == LODTier::Background && target == LODTier::Dormant) {
            float thresh = cfg_.backgroundRadius * scale + cfg_.demotionMargin;
            return r.distToPlayer > thresh;
        }
        return true;
    }

    // ── Group elevation ──────────────────────────────────────────────

    void applyGroupElevation() {
        // Find best tier per group
        std::unordered_map<uint32_t, LODTier> groupBest;
        for (auto& [id, r] : records_) {
            if (r.groupId == 0) continue;
            auto it = groupBest.find(r.groupId);
            if (it == groupBest.end() ||
                static_cast<int>(r.pendingTier) <
                static_cast<int>(it->second))
                groupBest[r.groupId] = r.pendingTier;
        }

        // Elevate: Active group member → rest become Background minimum
        for (auto& [id, r] : records_) {
            if (r.groupId == 0 || r.pinned) continue;
            auto it = groupBest.find(r.groupId);
            if (it == groupBest.end()) continue;

            LODTier best = it->second;
            // Elevate by one tier from best
            LODTier elevated;
            if (best == LODTier::Active)
                elevated = LODTier::Background; // group active → others min bg
            else
                elevated = best;

            if (static_cast<int>(r.pendingTier) >
                static_cast<int>(elevated))
                r.pendingTier = elevated;
        }
    }

    // ── Tier change bookkeeping ──────────────────────────────────────

    void changeTier(LODRecord& r, LODTier newTier, float simTimeHours) {
        LODTier old = r.tier;
        r.tier          = newTier;
        r.tierEnteredAt = simTimeHours;
        r.ticksInTier   = 0;
        r.accumDt       = 0.f; // reset on transition
        if (tierChangeCb_) tierChangeCb_(r.id, old, newTier);
    }

    // ── Tick list builder ────────────────────────────────────────────

    void buildTickLists(float dt) {
        tickActive_.clear();
        tickBackground_.clear();
        tickDormant_.clear();
        budgetUsedMs_ = 0.f; // reset budget counter each frame

        int activeCount = 0;

        for (auto& [id, r] : records_) {
            r.accumDt += dt;

            switch (r.tier) {
                case LODTier::Active: {
                    if (activeCount < cfg_.maxActivePerFrame) {
                        tickActive_.push_back(id);
                        r.accumDt = 0.f;
                        ++activeCount;
                    }
                    break;
                }
                case LODTier::Background: {
                    // Tick when accumulated dt exceeds threshold
                    float threshold = dt * (cfg_.backgroundSkip + 1);
                    if (r.accumDt >= threshold) {
                        tickBackground_.push_back(id);
                        // accumDt consumed by SimulationManager via consumeAccumDt()
                    }
                    break;
                }
                case LODTier::Dormant: {
                    float threshold = dt * (cfg_.dormantSkip + 1);
                    if (r.accumDt >= threshold) {
                        tickDormant_.push_back(id);
                    }
                    break;
                }
            }
        }

        // Sort active by distance (closest first — most visible)
        std::sort(tickActive_.begin(), tickActive_.end(),
            [this](EntityId a, EntityId b) {
                auto ra = records_.find(a);
                auto rb = records_.find(b);
                if (ra == records_.end() || rb == records_.end()) return false;
                return ra->second.distToPlayer < rb->second.distToPlayer;
            });
    }

    // ── Timing helper ────────────────────────────────────────────────

    static float nowMs() {
        using namespace std::chrono;
        return static_cast<float>(
            duration_cast<microseconds>(
                steady_clock::now().time_since_epoch()).count()) * 0.001f;
    }

    // ── Members ──────────────────────────────────────────────────────

    LODConfig cfg_;
    Vec2      playerPos_{0.f, 0.f};
    uint64_t  frameCount_   = 0;
    float     frameStartMs_ = 0.f;
    float     budgetUsedMs_ = 0.f;

    std::unordered_map<EntityId, LODRecord> records_;

    // Pre-built tick lists (rebuilt each frame)
    std::vector<EntityId> tickActive_;
    std::vector<EntityId> tickBackground_;
    std::vector<EntityId> tickDormant_;

    TierChangeCb tierChangeCb_;
};

} // namespace npc
