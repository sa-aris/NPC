#pragma once

#include "../core/types.hpp"
#include "../event/event_system.hpp"
#include "../social/relationship_system.hpp"
#include "../trade/trade_system.hpp"
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <algorithm>
#include <functional>
#include <cmath>

namespace npc {

// ─── Quest Status ─────────────────────────────────────────────────────────────
enum class QuestStatus {
    Locked,      // prerequisite chain not yet completed
    Available,   // NPC is ready to offer it
    Active,      // accepted by taker, in progress
    Completed,
    Failed,      // expired or abandoned
    Abandoned
};

inline std::string questStatusToString(QuestStatus s) {
    switch (s) {
        case QuestStatus::Locked:     return "Locked";
        case QuestStatus::Available:  return "Available";
        case QuestStatus::Active:     return "Active";
        case QuestStatus::Completed:  return "Completed";
        case QuestStatus::Failed:     return "Failed";
        case QuestStatus::Abandoned:  return "Abandoned";
    }
    return "Unknown";
}

// ─── Objective Type ───────────────────────────────────────────────────────────
enum class ObjectiveType {
    Collect,        // gather N of itemId
    Kill,           // defeat N enemies (by type tag or specific id)
    Deliver,        // bring itemId to targetNpc
    TalkTo,         // initiate dialog with targetNpc
    ReachLocation,  // arrive within radius of locationName
    Escort,         // keep targetNpc alive until they reach locationName
    Craft,          // craft itemId N times
    Custom          // caller-managed via updateObjective()
};

// ─── Quest Objective ──────────────────────────────────────────────────────────
struct QuestObjective {
    std::string   id;
    ObjectiveType type        = ObjectiveType::Custom;
    std::string   description;
    int           required    = 1;
    int           current     = 0;
    bool          optional    = false;

    // Type-specific payload
    ItemId        itemId      = 0;
    EntityId      targetNpc   = INVALID_ENTITY;
    std::string   locationName;
    std::string   enemyTag;   // e.g. "Wolf", "Bandit" — matches NPCType string

    bool  isComplete() const { return current >= required; }
    float progress()   const {
        return required > 0
            ? std::clamp(static_cast<float>(current) / required, 0.0f, 1.0f)
            : 1.0f;
    }

    void advance(int delta = 1) {
        current = std::min(current + delta, required);
    }

    std::string progressString() const {
        return std::to_string(current) + "/" + std::to_string(required);
    }
};

// ─── Quest Reward ─────────────────────────────────────────────────────────────
struct QuestReward {
    float                             gold           = 0.0f;
    std::vector<std::pair<ItemId,int>> items;
    float                             relationDelta  = 15.0f; // with giver
    std::vector<std::string>          unlocksQuests;          // quest IDs → Available
    FactionId                         factionId      = NO_FACTION;
    float                             factionDelta   = 0.0f;
    std::string                       customNote;             // shown to player
};

// ─── Quest Events ─────────────────────────────────────────────────────────────
struct QuestOfferedEvent {
    EntityId    takerId;
    std::string questId;
    EntityId    giverId;
};
struct QuestAcceptedEvent {
    EntityId    takerId;
    std::string questId;
};
struct QuestProgressEvent {
    EntityId    takerId;
    std::string questId;
    std::string objectiveId;
    int         current;
    int         required;
};
struct QuestCompletedEvent {
    EntityId    takerId;
    std::string questId;
    EntityId    giverId;
};
struct QuestFailedEvent {
    EntityId    takerId;
    std::string questId;
    std::string reason;   // "expired", "abandoned"
};

// ─── Quest ────────────────────────────────────────────────────────────────────
struct Quest {
    std::string                  id;
    std::string                  title;
    std::string                  description;
    EntityId                     giverId           = INVALID_ENTITY;

    std::vector<QuestObjective>  objectives;
    QuestReward                  reward;
    QuestStatus                  status            = QuestStatus::Locked;

    // Time limit (0 = none)
    float                        timeLimitHours    = 0.0f;
    float                        acceptedAt        = 0.0f;
    float                        completedAt       = 0.0f;

    // Chain membership
    std::string                  chainId;
    int                          chainStep         = 0;
    std::string                  prerequisiteId;   // quest ID that must be Completed first

    // Failure
    float                        failRelDelta      = -5.0f; // relationship penalty on fail/abandon
    bool                         repeatable        = false;

    // ── Helpers ──────────────────────────────────────────────────────────────
    bool hasTimeLimit()  const { return timeLimitHours > 0.0f; }

    float timeRemaining(float currentTime) const {
        if (!hasTimeLimit()) return -1.0f;
        return (acceptedAt + timeLimitHours) - currentTime;
    }
    bool isExpired(float currentTime) const {
        return hasTimeLimit() && currentTime > acceptedAt + timeLimitHours;
    }

    bool allObjectivesMet() const {
        for (const auto& obj : objectives)
            if (!obj.optional && !obj.isComplete()) return false;
        return true;
    }

    float overallProgress() const {
        if (objectives.empty()) return 1.0f;
        float sum = 0.0f; int count = 0;
        for (const auto& obj : objectives)
            if (!obj.optional) { sum += obj.progress(); ++count; }
        return count > 0 ? sum / count : 1.0f;
    }

    QuestObjective* findObjective(const std::string& objId) {
        for (auto& o : objectives) if (o.id == objId) return &o;
        return nullptr;
    }
    const QuestObjective* findObjective(const std::string& objId) const {
        for (const auto& o : objectives) if (o.id == objId) return &o;
        return nullptr;
    }
};

// ─── Quest Chain ──────────────────────────────────────────────────────────────
struct QuestChain {
    std::string              id;
    std::string              name;
    std::vector<std::string> questIds;   // ordered; completing [n] unlocks [n+1]
};

// ─── Per-entity Quest Log ─────────────────────────────────────────────────────
struct QuestLog {
    std::vector<std::string> active;
    std::vector<std::string> completed;
    std::vector<std::string> failed;

    bool isActive(const std::string& id) const {
        return std::find(active.begin(), active.end(), id) != active.end();
    }
    bool isCompleted(const std::string& id) const {
        return std::find(completed.begin(), completed.end(), id) != completed.end();
    }
    bool isFailed(const std::string& id) const {
        return std::find(failed.begin(), failed.end(), id) != failed.end();
    }
};

// ─── QuestManager ─────────────────────────────────────────────────────────────
class QuestManager {
public:
    // ── Registration ──────────────────────────────────────────────────────────
    void registerQuest(Quest q) {
        quests_[q.id] = std::move(q);
    }

    void registerChain(QuestChain chain) {
        // Wire prerequisite IDs automatically
        for (int i = 1; i < static_cast<int>(chain.questIds.size()); ++i) {
            auto it = quests_.find(chain.questIds[i]);
            if (it != quests_.end()) {
                it->second.prerequisiteId = chain.questIds[i - 1];
                it->second.chainId        = chain.id;
                it->second.chainStep      = i;
            }
        }
        if (!chain.questIds.empty()) {
            auto it = quests_.find(chain.questIds[0]);
            if (it != quests_.end()) {
                it->second.chainId   = chain.id;
                it->second.chainStep = 0;
                if (it->second.status == QuestStatus::Locked)
                    it->second.status = QuestStatus::Available;
            }
        }
        chains_[chain.id] = std::move(chain);
    }

    // Make a quest available (unlocked by giver NPC talking to player etc.)
    bool unlockQuest(const std::string& questId) {
        auto* q = findQuest(questId);
        if (!q) return false;
        if (q->status == QuestStatus::Locked) q->status = QuestStatus::Available;
        return true;
    }

    // ── Offering ──────────────────────────────────────────────────────────────
    // NPC offers quest to an entity. Returns false if prerequisites unmet.
    bool offerQuest(const std::string& questId, EntityId takerId,
                    float currentTime, EventBus* bus = nullptr) {
        auto* q = findQuest(questId);
        if (!q) return false;
        if (q->status == QuestStatus::Locked) return false;
        if (log_[takerId].isCompleted(questId) && !q->repeatable) return false;
        if (log_[takerId].isActive(questId)) return false;

        // Check prerequisite
        if (!q->prerequisiteId.empty() && !log_[takerId].isCompleted(q->prerequisiteId))
            return false;

        q->status = QuestStatus::Available;
        if (bus) bus->publish(QuestOfferedEvent{takerId, questId, q->giverId});
        return true;
    }

    // ── Accepting ─────────────────────────────────────────────────────────────
    bool acceptQuest(const std::string& questId, EntityId takerId,
                     float currentTime, EventBus* bus = nullptr) {
        auto* q = findQuest(questId);
        if (!q || q->status != QuestStatus::Available) return false;
        if (log_[takerId].isActive(questId)) return false;

        // Reset objectives for repeat quests
        if (q->repeatable)
            for (auto& obj : q->objectives) obj.current = 0;

        q->status     = QuestStatus::Active;
        q->acceptedAt = currentTime;

        log_[takerId].active.push_back(questId);
        if (bus) bus->publish(QuestAcceptedEvent{takerId, questId});
        return true;
    }

    // ── Progress ──────────────────────────────────────────────────────────────
    // Advance an objective by delta. Returns true if objective is now complete.
    bool updateObjective(const std::string& questId, EntityId takerId,
                         const std::string& objectiveId, int delta,
                         EventBus* bus = nullptr) {
        auto* q = findQuest(questId);
        if (!q || q->status != QuestStatus::Active) return false;
        if (!log_[takerId].isActive(questId)) return false;

        auto* obj = q->findObjective(objectiveId);
        if (!obj || obj->isComplete()) return false;

        obj->advance(delta);
        if (bus) bus->publish(QuestProgressEvent{takerId, questId, objectiveId,
                                                  obj->current, obj->required});
        return obj->isComplete();
    }

    // Shortcut: advance first matching objective by type+tag
    bool notifyKill(EntityId takerId, const std::string& enemyTag,
                    float currentTime, EventBus* bus = nullptr) {
        bool any = false;
        for (auto& [id, q] : quests_) {
            if (q.status != QuestStatus::Active) continue;
            if (!log_[takerId].isActive(id)) continue;
            for (auto& obj : q.objectives) {
                if (obj.type == ObjectiveType::Kill &&
                    obj.enemyTag == enemyTag && !obj.isComplete()) {
                    obj.advance(1);
                    if (bus) bus->publish(QuestProgressEvent{takerId, id, obj.id,
                                                              obj.current, obj.required});
                    any = true;
                }
            }
        }
        return any;
    }

    bool notifyItemCollected(EntityId takerId, ItemId itemId, int qty,
                             EventBus* bus = nullptr) {
        bool any = false;
        for (auto& [id, q] : quests_) {
            if (q.status != QuestStatus::Active) continue;
            if (!log_[takerId].isActive(id)) continue;
            for (auto& obj : q.objectives) {
                if ((obj.type == ObjectiveType::Collect ||
                     obj.type == ObjectiveType::Deliver) &&
                    obj.itemId == itemId && !obj.isComplete()) {
                    obj.advance(qty);
                    if (bus) bus->publish(QuestProgressEvent{takerId, id, obj.id,
                                                              obj.current, obj.required});
                    any = true;
                }
            }
        }
        return any;
    }

    bool notifyTalkedTo(EntityId takerId, EntityId npcId, EventBus* bus = nullptr) {
        bool any = false;
        for (auto& [id, q] : quests_) {
            if (q.status != QuestStatus::Active) continue;
            if (!log_[takerId].isActive(id)) continue;
            for (auto& obj : q.objectives) {
                if (obj.type == ObjectiveType::TalkTo &&
                    obj.targetNpc == npcId && !obj.isComplete()) {
                    obj.advance(1);
                    if (bus) bus->publish(QuestProgressEvent{takerId, id, obj.id,
                                                              obj.current, obj.required});
                    any = true;
                }
            }
        }
        return any;
    }

    bool notifyReachedLocation(EntityId takerId, const std::string& locationName,
                                EventBus* bus = nullptr) {
        bool any = false;
        for (auto& [id, q] : quests_) {
            if (q.status != QuestStatus::Active) continue;
            if (!log_[takerId].isActive(id)) continue;
            for (auto& obj : q.objectives) {
                if ((obj.type == ObjectiveType::ReachLocation ||
                     obj.type == ObjectiveType::Escort) &&
                    obj.locationName == locationName && !obj.isComplete()) {
                    obj.advance(1);
                    if (bus) bus->publish(QuestProgressEvent{takerId, id, obj.id,
                                                              obj.current, obj.required});
                    any = true;
                }
            }
        }
        return any;
    }

    // ── Tick: auto-complete + expire ──────────────────────────────────────────
    void update(float currentTime, EventBus* bus,
                RelationshipSystem* rel  = nullptr,
                Inventory*          inv  = nullptr) {
        for (auto& [takerId, log] : log_) {
            for (auto it = log.active.begin(); it != log.active.end(); ) {
                auto* q = findQuest(*it);
                if (!q) { it = log.active.erase(it); continue; }

                // Expired?
                if (q->isExpired(currentTime)) {
                    failQuest(q, takerId, "expired", currentTime, bus, rel);
                    log.failed.push_back(*it);
                    it = log.active.erase(it);
                    continue;
                }

                // Auto-complete when all objectives met
                if (q->allObjectivesMet()) {
                    completeQuest(q, takerId, currentTime, bus, rel, inv);
                    log.completed.push_back(*it);
                    it = log.active.erase(it);
                    continue;
                }

                ++it;
            }
        }
    }

    // ── Manual complete (e.g. player turns in to NPC) ─────────────────────────
    bool turnIn(const std::string& questId, EntityId takerId,
                float currentTime, EventBus* bus,
                RelationshipSystem* rel = nullptr,
                Inventory* inv          = nullptr) {
        auto* q = findQuest(questId);
        if (!q || !log_[takerId].isActive(questId)) return false;
        if (!q->allObjectivesMet()) return false;

        completeQuest(q, takerId, currentTime, bus, rel, inv);
        auto& log = log_[takerId];
        log.active.erase(std::remove(log.active.begin(), log.active.end(), questId),
                         log.active.end());
        log.completed.push_back(questId);
        return true;
    }

    // ── Abandon ───────────────────────────────────────────────────────────────
    bool abandonQuest(const std::string& questId, EntityId takerId,
                      float currentTime, EventBus* bus,
                      RelationshipSystem* rel = nullptr) {
        auto* q = findQuest(questId);
        if (!q || !log_[takerId].isActive(questId)) return false;

        failQuest(q, takerId, "abandoned", currentTime, bus, rel);
        auto& log = log_[takerId];
        log.active.erase(std::remove(log.active.begin(), log.active.end(), questId),
                         log.active.end());
        log.failed.push_back(questId);
        return true;
    }

    // ── Query ─────────────────────────────────────────────────────────────────
    const Quest*        getQuest(const std::string& id) const {
        auto it = quests_.find(id);
        return it != quests_.end() ? &it->second : nullptr;
    }
    const QuestLog&     getLog(EntityId takerId) const {
        static QuestLog empty;
        auto it = log_.find(takerId);
        return it != log_.end() ? it->second : empty;
    }

    std::vector<const Quest*> getActiveQuests(EntityId takerId) const {
        std::vector<const Quest*> out;
        auto it = log_.find(takerId);
        if (it == log_.end()) return out;
        for (const auto& id : it->second.active)
            if (auto* q = getQuest(id)) out.push_back(q);
        return out;
    }

    // All quests this NPC can currently offer (Available + no active duplicate)
    std::vector<const Quest*> getOfferable(EntityId giverId,
                                            EntityId takerId) const {
        std::vector<const Quest*> out;
        const auto& log = getLog(takerId);
        for (const auto& [id, q] : quests_) {
            if (q.giverId != giverId) continue;
            if (q.status != QuestStatus::Available) continue;
            if (log.isActive(id) || (log.isCompleted(id) && !q.repeatable)) continue;
            if (!q.prerequisiteId.empty() && !log.isCompleted(q.prerequisiteId)) continue;
            out.push_back(&q);
        }
        return out;
    }

    bool isActive(const std::string& questId, EntityId takerId) const {
        return getLog(takerId).isActive(questId);
    }
    bool isCompleted(const std::string& questId, EntityId takerId) const {
        return getLog(takerId).isCompleted(questId);
    }

    const std::map<std::string, Quest>&      allQuests()  const { return quests_; }
    const std::map<std::string, QuestChain>& allChains()  const { return chains_; }

private:
    Quest* findQuest(const std::string& id) {
        auto it = quests_.find(id);
        return it != quests_.end() ? &it->second : nullptr;
    }

    void completeQuest(Quest* q, EntityId takerId, float currentTime,
                       EventBus* bus, RelationshipSystem* rel, Inventory* inv) {
        q->status      = QuestStatus::Completed;
        q->completedAt = currentTime;

        // Apply rewards
        if (inv) {
            inv->addGold(q->reward.gold);
            for (const auto& [itemId, qty] : q->reward.items)
                inv->addItem(itemId, qty);
        }
        if (rel && q->giverId != INVALID_ENTITY)
            rel->modifyRelationship(takerId, q->giverId, q->reward.relationDelta);

        // Unlock next quests
        for (const auto& nextId : q->reward.unlocksQuests)
            unlockQuest(nextId);

        // Auto-unlock next in chain
        if (!q->chainId.empty()) {
            auto chainIt = chains_.find(q->chainId);
            if (chainIt != chains_.end()) {
                const auto& ids = chainIt->second.questIds;
                int next = q->chainStep + 1;
                if (next < static_cast<int>(ids.size()))
                    unlockQuest(ids[next]);
            }
        }

        if (bus) bus->publish(QuestCompletedEvent{takerId, q->id, q->giverId});
    }

    void failQuest(Quest* q, EntityId takerId, const std::string& reason,
                   float currentTime, EventBus* bus, RelationshipSystem* rel) {
        q->status = (reason == "abandoned") ? QuestStatus::Abandoned : QuestStatus::Failed;
        if (rel && q->giverId != INVALID_ENTITY && q->failRelDelta != 0.0f)
            rel->modifyRelationship(takerId, q->giverId, q->failRelDelta);
        if (bus) bus->publish(QuestFailedEvent{takerId, q->id, reason});
    }

    std::map<std::string, Quest>      quests_;
    std::map<std::string, QuestChain> chains_;
    std::map<EntityId, QuestLog>      log_;
};

} // namespace npc
