#pragma once

#include "../core/types.hpp"
#include <vector>
#include <string>
#include <optional>
#include <algorithm>
#include <functional>

namespace npc {

// ─── Source Reliability ───────────────────────────────────────────────────────
enum class MemorySource {
    Observed,   // NPC directly witnessed this event
    Hearsay     // NPC was told this by another NPC
};

inline std::string memorySourceToString(MemorySource s) {
    return s == MemorySource::Observed ? "observed" : "hearsay";
}

// ─── Memory Struct ────────────────────────────────────────────────────────────
struct Memory {
    MemoryType   type;
    MemorySource source        = MemorySource::Observed;

    std::optional<EntityId> entityId;       // subject of the memory
    std::optional<EntityId> sourceEntity;   // who told this (hearsay only)

    std::string description;
    float emotionalImpact  = 0.0f;   // -1 (very negative) to +1 (very positive)
    float timestamp        = 0.0f;   // game time when created / received
    float importance       = 0.5f;   // 0 (trivial) to 1 (unforgettable)
    float decayRate        = 0.01f;  // importance loss per game hour
    float currentStrength  = 1.0f;   // decays over time

    // Gossip-specific fields
    float reliability      = 1.0f;   // 1.0 = directly witnessed, decays per hop
    int   hopCount         = 0;      // how many NPC-to-NPC transfers occurred
};

// ─── Gossip Propagation Constants ────────────────────────────────────────────
namespace gossip {
    constexpr float BASE_HOP_DECAY         = 0.75f;  // reliability multiplied per hop
    constexpr float MIN_TRUST_MULTIPLIER   = 0.50f;  // worst-case trust modifier
    constexpr float MAX_TRUST_MULTIPLIER   = 1.00f;  // best-case trust modifier
    constexpr float DISTORTION_THRESHOLD   = 0.35f;  // below this, info may warp
    constexpr float MIN_ACCEPT_RELIABILITY = 0.10f;  // below this, memory is ignored
}

// ─── MemorySystem ─────────────────────────────────────────────────────────────
class MemorySystem {
public:
    explicit MemorySystem(size_t maxMemories = 100)
        : maxMemories_(maxMemories) {}

    // ── Core add ──────────────────────────────────────────────────────────────
    void addMemory(Memory mem) {
        mem.currentStrength = 1.0f;
        memories_.push_back(std::move(mem));
        if (memories_.size() > maxMemories_)
            forgetWeakest();
    }

    void addMemory(MemoryType type, const std::string& desc,
                   float emotionalImpact = 0.0f,
                   std::optional<EntityId> entity = std::nullopt,
                   float importance = 0.5f, float timestamp = 0.0f) {
        Memory m;
        m.type            = type;
        m.description     = desc;
        m.emotionalImpact = emotionalImpact;
        m.entityId        = entity;
        m.importance      = importance;
        m.timestamp       = timestamp;
        m.source          = MemorySource::Observed;
        m.reliability     = 1.0f;
        m.hopCount        = 0;
        addMemory(std::move(m));
    }

    // ── Gossip: receive hearsay from another NPC ──────────────────────────────
    // tellerTrust: relationship score [-100, 100] with the teller.
    // Returns true if the memory was accepted (reliability above threshold).
    bool receiveGossip(Memory sourceMemory,
                       EntityId tellerId,
                       float tellerTrust,
                       float currentTime = 0.0f) {
        // Map [-100,100] → [0.5, 1.0]
        float trustMod = gossip::MIN_TRUST_MULTIPLIER +
                         (tellerTrust + 100.0f) / 200.0f *
                         (gossip::MAX_TRUST_MULTIPLIER - gossip::MIN_TRUST_MULTIPLIER);

        float newReliability = sourceMemory.reliability
                               * gossip::BASE_HOP_DECAY
                               * trustMod;

        if (newReliability < gossip::MIN_ACCEPT_RELIABILITY)
            return false;  // too unreliable to store

        Memory gossipMem          = sourceMemory;
        gossipMem.source          = MemorySource::Hearsay;
        gossipMem.sourceEntity    = tellerId;
        gossipMem.reliability     = newReliability;
        gossipMem.hopCount        = sourceMemory.hopCount + 1;
        gossipMem.timestamp       = currentTime;
        gossipMem.currentStrength = 1.0f;

        // Low reliability warps the emotional signal and flags the description
        if (newReliability < gossip::DISTORTION_THRESHOLD) {
            gossipMem.emotionalImpact *= (newReliability / gossip::DISTORTION_THRESHOLD);
            gossipMem.description      = "[distorted] " + gossipMem.description;
            gossipMem.importance      *= 0.8f;
        }

        addMemory(std::move(gossipMem));
        return true;
    }

    // ── Produce a shareable copy for passing to another NPC ───────────────────
    // Returns nullopt if the memory is too weak or unreliable to share.
    std::optional<Memory> prepareForGossip(const Memory& mem) const {
        if (mem.reliability     < gossip::MIN_ACCEPT_RELIABILITY) return std::nullopt;
        if (mem.currentStrength < 0.2f)                           return std::nullopt;
        return mem;
    }

    // ── All memories worth gossiping about, ranked by importance × reliability ─
    std::vector<Memory> gossipCandidates(float importanceThreshold = 0.4f) const {
        std::vector<Memory> out;
        for (const auto& m : memories_) {
            if (m.importance      >= importanceThreshold &&
                m.reliability     >= gossip::MIN_ACCEPT_RELIABILITY &&
                m.currentStrength >= 0.2f)
            {
                out.push_back(m);
            }
        }
        std::sort(out.begin(), out.end(), [](const Memory& a, const Memory& b) {
            return (a.importance * a.reliability) > (b.importance * b.reliability);
        });
        return out;
    }

    // ── Time update ───────────────────────────────────────────────────────────
    void update(float dt) {
        for (auto& m : memories_) {
            m.currentStrength -= m.decayRate * dt;
            m.currentStrength  = std::max(0.0f, m.currentStrength);
        }
        memories_.erase(
            std::remove_if(memories_.begin(), memories_.end(),
                [](const Memory& m) {
                    return m.currentStrength <= 0.0f && m.importance < 0.7f;
                }),
            memories_.end());
    }

    // ── Recall helpers ────────────────────────────────────────────────────────
    std::vector<Memory> recall(MemoryType type) const {
        std::vector<Memory> result;
        for (const auto& m : memories_)
            if (m.type == type) result.push_back(m);
        std::sort(result.begin(), result.end(),
            [](const Memory& a, const Memory& b) { return a.timestamp > b.timestamp; });
        return result;
    }

    std::vector<Memory> recallAbout(EntityId entity) const {
        std::vector<Memory> result;
        for (const auto& m : memories_)
            if (m.entityId.has_value() && *m.entityId == entity)
                result.push_back(m);
        return result;
    }

    // Only directly observed memories
    std::vector<Memory> recallObserved(MemoryType type) const {
        std::vector<Memory> result;
        for (const auto& m : memories_)
            if (m.type == type && m.source == MemorySource::Observed)
                result.push_back(m);
        return result;
    }

    // Only hearsay memories
    std::vector<Memory> recallHearsay() const {
        std::vector<Memory> result;
        for (const auto& m : memories_)
            if (m.source == MemorySource::Hearsay)
                result.push_back(m);
        return result;
    }

    // Opinion weighted by reliability: hearsay matters less than observation
    float getOpinionOf(EntityId entity) const {
        float opinion = 0.0f;
        int count = 0;
        for (const auto& m : memories_) {
            if (m.entityId.has_value() && *m.entityId == entity) {
                opinion += m.emotionalImpact * m.currentStrength * m.reliability;
                ++count;
            }
        }
        return count > 0 ? std::clamp(opinion / count, -1.0f, 1.0f) : 0.0f;
    }

    bool hasMemoryOf(MemoryType type,
                     std::optional<EntityId> entity = std::nullopt) const {
        for (const auto& m : memories_)
            if (m.type == type)
                if (!entity.has_value() || m.entityId == entity)
                    return true;
        return false;
    }

    const std::vector<Memory>& allMemories() const { return memories_; }

    Memory* mostRecent() {
        if (memories_.empty()) return nullptr;
        return &*std::max_element(memories_.begin(), memories_.end(),
            [](const Memory& a, const Memory& b) { return a.timestamp < b.timestamp; });
    }

private:
    void forgetWeakest() {
        if (memories_.empty()) return;
        auto weakest = std::min_element(memories_.begin(), memories_.end(),
            [](const Memory& a, const Memory& b) {
                return (a.currentStrength * a.importance * a.reliability) <
                       (b.currentStrength * b.importance * b.reliability);
            });
        memories_.erase(weakest);
    }

    std::vector<Memory> memories_;
    size_t maxMemories_;
};

} // namespace npc
