#pragma once
// quest_generator.hpp — procedural quests derived from live world state
// Part of Aithena (C++17, header-only)
//
// Instead of hand-authoring every quest, the generator inspects what the
// simulation is ALREADY producing — unsolved crimes, market shortages,
// feuds, close friendships, caravans on the road — and turns each situation
// into a ready-to-register Quest (quest_system.hpp). Each source situation
// is fingerprinted so the same feud or shortage never spawns duplicates.

#include "../core/types.hpp"
#include "quest_system.hpp"
#include "../social/relationship_system.hpp"
#include "../social/reputation_system.hpp"
#include "../economy/economy_system.hpp"
#include <string>
#include <vector>
#include <set>
#include <sstream>
#include <algorithm>

namespace npc {

// ─── Generated quest kinds ────────────────────────────────────────────────────

enum class GeneratedQuestKind {
    Bounty,        // bring an unsolved criminal to justice
    SupplyRun,     // fetch scarce goods for a settlement
    Mediation,     // defuse a feud between two NPCs
    GiftDelivery,  // carry a token between close friends
    CaravanEscort  // guard a caravan already on the road
};

inline const char* generatedQuestKindName(GeneratedQuestKind k) {
    switch (k) {
        case GeneratedQuestKind::Bounty:        return "Bounty";
        case GeneratedQuestKind::SupplyRun:     return "SupplyRun";
        case GeneratedQuestKind::Mediation:     return "Mediation";
        case GeneratedQuestKind::GiftDelivery:  return "GiftDelivery";
        case GeneratedQuestKind::CaravanEscort: return "CaravanEscort";
    }
    return "Unknown";
}

// A generated quest plus the context it came from
struct GeneratedQuest {
    GeneratedQuestKind kind;
    Quest              quest;
    std::string        contextKey;  // fingerprint of the source situation
};

// ─── QuestGenerator ───────────────────────────────────────────────────────────

class QuestGenerator {
public:
    struct Config {
        float feudThreshold        = -50.0f; // mutual value at/below → feud
        float friendshipThreshold  =  60.0f; // mutual value at/above → gift quest
        float bountyRewardScale    = 1.0f;   // multiplier on crime bounty gold
        float supplyRewardPerUnit  = 3.0f;   // gold per requested unit
        float mediationReward      = 40.0f;
        float giftReward           = 15.0f;
        float escortReward         = 35.0f;
        int   supplyQtyCap         = 10;     // max units a supply run asks for
    };

    Config config;

    // Wire in whichever systems exist — every source is optional.
    RelationshipSystem* relationships = nullptr;
    ReputationSystem*   reputation    = nullptr;
    EconomySystem*      economy       = nullptr;

    // Maps a string NPC id (used by the social systems) to the EntityId the
    // QuestManager expects for giverId. Optional; defaults to INVALID_ENTITY.
    std::function<EntityId(const std::string&)> resolveEntity;

    // ── Generation ────────────────────────────────────────────────────────────
    // Scan all wired systems and return up to maxQuests new quests.
    // Situations that already produced a quest are skipped (fingerprinted).
    std::vector<GeneratedQuest> generate(double simTime,
                                         std::size_t maxQuests = 8) {
        std::vector<GeneratedQuest> out;
        if (reputation)  generateBounties(simTime, maxQuests, out);
        if (economy)     generateSupplyRuns(simTime, maxQuests, out);
        if (relationships) {
            generateMediations(simTime, maxQuests, out);
            generateGiftDeliveries(simTime, maxQuests, out);
        }
        if (economy)     generateEscorts(simTime, maxQuests, out);
        return out;
    }

    // Convenience: generate and register everything into a QuestManager.
    std::vector<GeneratedQuest> generateInto(QuestManager& manager,
                                             double simTime,
                                             std::size_t maxQuests = 8) {
        auto batch = generate(simTime, maxQuests);
        for (auto& g : batch) {
            Quest q = g.quest;
            q.status = QuestStatus::Available;
            manager.registerQuest(std::move(q));
        }
        return batch;
    }

    // Forget a fingerprint so the same situation may generate again
    // (e.g. after the quest was completed or failed).
    void releaseContext(const std::string& contextKey) {
        usedContexts_.erase(contextKey);
    }

    std::size_t generatedCount() const { return nextId_ - 1; }

private:
    // ── Bounty quests from unsolved, reported crimes ──────────────────────────
    void generateBounties(double /*simTime*/, std::size_t maxQuests,
                          std::vector<GeneratedQuest>& out) {
        for (const auto* crime : reputation->openCrimes()) {
            if (out.size() >= maxQuests) return;
            if (crime->bounty <= 0.0f) continue;

            std::string key = "bounty:" + std::to_string(crime->id);
            if (!claimContext(key)) continue;

            Quest q = makeQuest("Bounty: " + crime->perpetrator,
                                crime->perpetrator + " is wanted for " +
                                std::string(crimeTypeName(crime->type)) +
                                (crime->victim.empty() ? std::string{}
                                    : " against " + crime->victim) +
                                ". Bring them to justice.");
            QuestObjective obj;
            obj.id          = "capture";
            obj.type        = ObjectiveType::Kill; // "defeat/subdue" the target
            obj.description = "Track down " + crime->perpetrator;
            obj.enemyTag    = crime->perpetrator;
            q.objectives.push_back(obj);
            q.reward.gold       = crime->bounty * config.bountyRewardScale;
            q.reward.customNote = "Bounty posted for " +
                                  std::string(crimeTypeName(crime->type));
            q.giverId = resolve(crime->victim);

            out.push_back({GeneratedQuestKind::Bounty, std::move(q), key});
        }
    }

    // ── Supply runs from settlement shortages ─────────────────────────────────
    void generateSupplyRuns(double /*simTime*/, std::size_t maxQuests,
                            std::vector<GeneratedQuest>& out) {
        for (const auto& [name, s] : economy->settlements()) {
            for (ItemId itemId : economy->scarceItems(name)) {
                if (out.size() >= maxQuests) return;

                std::string key = "supply:" + name + ":" + std::to_string(itemId);
                if (!claimContext(key)) continue;

                const auto* item = economy->getItemInfo(itemId);
                std::string itemName = item ? item->name : "goods";

                auto tIt   = s.targetStock.find(itemId);
                int target = tIt != s.targetStock.end() ? tIt->second
                                                        : std::max(1, s.population);
                int needed = std::clamp(target - s.stockOf(itemId),
                                        1, config.supplyQtyCap);

                Quest q = makeQuest("Supply run: " + itemName + " for " + name,
                                    name + " is running out of " + itemName +
                                    ". Deliver " + std::to_string(needed) +
                                    " to restock the settlement.");
                QuestObjective obj;
                obj.id          = "deliver";
                obj.type        = ObjectiveType::Collect;
                obj.description = "Gather " + itemName;
                obj.itemId      = itemId;
                obj.required    = needed;
                q.objectives.push_back(obj);
                q.reward.gold       = needed * config.supplyRewardPerUnit;
                q.reward.customNote = "Prices in " + name + " are soaring.";

                out.push_back({GeneratedQuestKind::SupplyRun, std::move(q), key});
            }
        }
    }

    // ── Mediation quests from feuds ───────────────────────────────────────────
    void generateMediations(double /*simTime*/, std::size_t maxQuests,
                            std::vector<GeneratedQuest>& out) {
        relationships->forEach([&](const std::string& a, const std::string& b,
                                   const RelationshipData& data) {
            if (out.size() >= maxQuests) return;
            if (data.value > config.feudThreshold) return;
            // Only fire on mutual hostility, and only once per unordered pair
            if (relationships->getValue(b, a) > config.feudThreshold) return;
            if (a > b) return; // canonical order de-duplicates the pair

            std::string key = "feud:" + a + ":" + b;
            if (!claimContext(key)) return;

            Quest q = makeQuest("Bad blood: " + a + " and " + b,
                                a + " and " + b + " are at each other's throats. "
                                "Talk to both sides before it turns violent.");
            QuestObjective o1;
            o1.id = "hear_a"; o1.type = ObjectiveType::TalkTo;
            o1.description = "Hear " + a + "'s side";
            o1.targetNpc   = resolve(a);
            QuestObjective o2;
            o2.id = "hear_b"; o2.type = ObjectiveType::TalkTo;
            o2.description = "Hear " + b + "'s side";
            o2.targetNpc   = resolve(b);
            q.objectives.push_back(o1);
            q.objectives.push_back(o2);
            q.reward.gold       = config.mediationReward;
            q.reward.customNote = "The whole village will breathe easier.";

            out.push_back({GeneratedQuestKind::Mediation, std::move(q), key});
        });
    }

    // ── Gift deliveries between close friends ─────────────────────────────────
    void generateGiftDeliveries(double /*simTime*/, std::size_t maxQuests,
                                std::vector<GeneratedQuest>& out) {
        relationships->forEach([&](const std::string& a, const std::string& b,
                                   const RelationshipData& data) {
            if (out.size() >= maxQuests) return;
            if (data.value < config.friendshipThreshold) return;
            if (relationships->getValue(b, a) < config.friendshipThreshold) return;
            if (a > b) return;

            std::string key = "gift:" + a + ":" + b;
            if (!claimContext(key)) return;

            Quest q = makeQuest("A token for " + b,
                                a + " wants to send " + b +
                                " a gift. Deliver it and share the good news.");
            QuestObjective obj;
            obj.id          = "deliver_gift";
            obj.type        = ObjectiveType::TalkTo;
            obj.description = "Bring the gift to " + b;
            obj.targetNpc   = resolve(b);
            q.objectives.push_back(obj);
            q.giverId           = resolve(a);
            q.reward.gold       = config.giftReward;
            q.reward.customNote = a + " will remember this kindness.";

            out.push_back({GeneratedQuestKind::GiftDelivery, std::move(q), key});
        });
    }

    // ── Escorts for caravans currently in transit ─────────────────────────────
    void generateEscorts(double /*simTime*/, std::size_t maxQuests,
                         std::vector<GeneratedQuest>& out) {
        for (const auto* c : economy->caravansInTransit()) {
            if (out.size() >= maxQuests) return;

            std::string key = "escort:" + std::to_string(c->id);
            if (!claimContext(key)) continue;

            const auto* item = economy->getItemInfo(c->item);
            std::string itemName = item ? item->name : "goods";

            Quest q = makeQuest("Escort the " + itemName + " caravan",
                                "A caravan hauling " + itemName + " from " +
                                c->from + " to " + c->to +
                                " needs protection on the road.");
            QuestObjective obj;
            obj.id           = "escort";
            obj.type         = ObjectiveType::Escort;
            obj.description  = "See the caravan safely to " + c->to;
            obj.locationName = c->to;
            q.objectives.push_back(obj);
            q.reward.gold       = config.escortReward;
            q.reward.customNote = "The merchants pool their coin for guards.";

            out.push_back({GeneratedQuestKind::CaravanEscort, std::move(q), key});
        }
    }

    // ── Helpers ───────────────────────────────────────────────────────────────
    Quest makeQuest(std::string title, std::string description) {
        Quest q;
        q.id          = "gen_" + std::to_string(nextId_++);
        q.title       = std::move(title);
        q.description = std::move(description);
        q.status      = QuestStatus::Available;
        return q;
    }

    EntityId resolve(const std::string& npcId) const {
        if (npcId.empty() || !resolveEntity) return INVALID_ENTITY;
        return resolveEntity(npcId);
    }

    bool claimContext(const std::string& key) {
        return usedContexts_.insert(key).second;
    }

    std::set<std::string> usedContexts_;
    uint32_t              nextId_ = 1;
};

} // namespace npc
