#pragma once
// influence_chain.hpp — Social influence propagation tracking
// Tracks messages/beliefs as they hop through an NPC social network.
// Propagation logic lives in the caller (e.g. village_sim.cpp);
// this header is pure data — no NPC or world dependencies.

#include "../core/types.hpp"
#include <string>
#include <vector>
#include <algorithm>

namespace npc {

// ─── InfluenceMessage ─────────────────────────────────────────────────────────
// A belief, rumour, or propaganda item spreading through the social graph.

struct InfluenceMessage {
    std::string id;             // unique slug, e.g. "wolves_attack"
    std::string topic;          // human-readable label
    EntityId    originatorId   = INVALID_ENTITY;
    std::string originatorName;

    // Emotional charge: -1.0 (alarming/fearful) → +1.0 (inspiring/exciting)
    // Mutates slightly at each hop based on receiver personality.
    float charge      = 0.0f;

    // Reliability: degrades each hop. Receivers with high intelligence
    // are more skeptical of low-reliability messages.
    float reliability = 1.0f;

    int   hopCount    = 0;

    // Per-hop trail (parallel arrays, same order as propagation occurred)
    std::vector<EntityId>    reachedIds;   // includes originator at index 0
    std::vector<std::string> reachedNames;

    float createdAt = 0.0f;
    float expiresAt = 999.0f;

    bool hasReached(EntityId id) const {
        return std::find(reachedIds.begin(), reachedIds.end(), id)
               != reachedIds.end();
    }

    // Arrow-joined chain string, e.g. "Alaric ⟶ Brina ⟶ Elmund"
    std::string chainString() const {
        std::string s;
        for (size_t i = 0; i < reachedNames.size(); ++i) {
            if (i > 0) s += " \u27f6 "; // ⟶
            s += reachedNames[i];
        }
        return s;
    }
};

// ─── InfluenceHop ─────────────────────────────────────────────────────────────
// Describes what happened when a message was passed from one NPC to another.

struct InfluenceHop {
    std::string messageId;
    std::string topic;
    std::string senderName;
    std::string receiverName;
    int         hopNumber   = 0;
    float       reliability = 0.0f;  // reliability as delivered
    float       chargeIn    = 0.0f;  // charge that arrived
    float       chargeOut   = 0.0f;  // charge after receiver's filter
    float       relValue    = 0.0f;  // sender→receiver relationship at time of hop
    bool        accepted    = false;
    std::string reactionLine;        // "Brina seems uneasy."
    std::string fullChain;           // chain string up to and including this hop
};

// ─── InfluenceChainSystem ─────────────────────────────────────────────────────
// Stores active messages and records propagation history.
// Propagation logic (NPC personality math, memory injection, emotion triggers)
// belongs in the simulation layer, not here.

class InfluenceChainSystem {
public:
    // Seed a new message. Replaces any existing message with the same id.
    void seed(InfluenceMessage msg) {
        auto it = std::find_if(messages_.begin(), messages_.end(),
            [&](const InfluenceMessage& m){ return m.id == msg.id; });
        if (it != messages_.end())
            *it = std::move(msg);
        else
            messages_.push_back(std::move(msg));
    }

    // Mark that a receiver has accepted a message and record updated state.
    void recordHop(const std::string& msgId,
                   EntityId           receiverId,
                   const std::string& receiverName,
                   float              newReliability,
                   float              newCharge) {
        auto* m = find(msgId);
        if (!m) return;
        m->reachedIds.push_back(receiverId);
        m->reachedNames.push_back(receiverName);
        m->reliability = newReliability;
        m->charge      = newCharge;
        m->hopCount++;
    }

    InfluenceMessage* find(const std::string& id) {
        auto it = std::find_if(messages_.begin(), messages_.end(),
            [&](const InfluenceMessage& m){ return m.id == id; });
        return it != messages_.end() ? &*it : nullptr;
    }

    const InfluenceMessage* find(const std::string& id) const {
        auto it = std::find_if(messages_.begin(), messages_.end(),
            [&](const InfluenceMessage& m){ return m.id == id; });
        return it != messages_.end() ? &*it : nullptr;
    }

    const std::vector<InfluenceMessage>& messages() const { return messages_; }

    // Messages that have spread to at least minHops recipients beyond originator
    std::vector<const InfluenceMessage*> activeChains(int minHops = 1) const {
        std::vector<const InfluenceMessage*> out;
        for (const auto& m : messages_)
            if (m.hopCount >= minHops) out.push_back(&m);
        return out;
    }

private:
    std::vector<InfluenceMessage> messages_;
};

} // namespace npc
