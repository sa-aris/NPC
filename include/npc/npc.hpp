#pragma once

#include "core/types.hpp"
#include "core/vec2.hpp"
#include "core/random.hpp"
#include "event/event_system.hpp"
#include "ai/fsm.hpp"
#include "ai/behavior_tree.hpp"
#include "ai/utility_ai.hpp"
#include "ai/goap.hpp"
#include "ai/blackboard.hpp"
#include "perception/perception_system.hpp"
#include "memory/memory_system.hpp"
#include "emotion/emotion_system.hpp"
#include "combat/combat_system.hpp"
#include "dialog/dialog_system.hpp"
#include "trade/trade_system.hpp"
#include "social/faction_system.hpp"
#include "social/group_behavior.hpp"
#include "schedule/schedule_system.hpp"
#include "navigation/pathfinding.hpp"
#include "personality/personality_traits.hpp"
#include "quest/quest_system.hpp"
#include "skill/skill_system.hpp"

#include <string>
#include <vector>
#include <memory>
#include <iostream>
#include <sstream>

namespace npc {

class GameWorld;

class NPC : public std::enable_shared_from_this<NPC> {
public:
    // ─── Identity ────────────────────────────────────────────────────
    EntityId    id;
    std::string name;
    NPCType     type;
    FactionId   factionId = NO_FACTION;

    // ─── Personality ─────────────────────────────────────────────────
    PersonalityTraits personality;

    // ─── Position ────────────────────────────────────────────────────
    Vec2  position;
    Vec2  facing{1.0f, 0.0f};
    float moveSpeed = 3.0f;

    // ─── Systems ─────────────────────────────────────────────────────
    FSM              fsm;
    UtilityAI        utilityAI;
    BehaviorTree     combatBT;
    BehaviorTree     socializeBT;
    PerceptionSystem perception;
    MemorySystem     memory{50};
    EmotionSystem    emotions;
    CombatSystem     combat;
    DialogSystem     dialog;
    TradeSystem      trade;
    ScheduleSystem   schedule;
    SkillSystem      skills;
    std::shared_ptr<Pathfinder> pathfinder;

    // ─── Quest ───────────────────────────────────────────────────────
    std::vector<std::string> offeredQuestIds;

    // ─── AI Control ──────────────────────────────────────────────────
    bool      useUtilityAI = false;
    bool      useGOAP      = false;
    GOAPAgent goap;

    // ─── Movement state ──────────────────────────────────────────────
    std::vector<Vec2> currentPath;
    size_t            pathIndex = 0;
    Vec2              moveTarget;
    bool              isMoving  = false;

    // ─── Logging ─────────────────────────────────────────────────────
    bool verbose = true;

    NPC(EntityId id, std::string name, NPCType type)
        : id(id), name(std::move(name)), type(type)
        , skills(id) {}

    // ─── Main update ─────────────────────────────────────────────────
    void update(float dt, GameWorld& world);

    // ─── Apply skill bonuses to subsystems ───────────────────────────
    // Call once after level-up or on init.
    void applySkillBonuses() {
        const SkillBonuses& b = skills.bonuses();

        // Combat: scale max health and stamina by bonuses
        combat.stats.maxHealth  = combat.stats.maxHealth  + b.maxHealthBonus;
        combat.stats.stamina.max= combat.stats.stamina.max + b.maxStaminaBonus;

        // Trade: re-apply personality+skill combined modifiers
        trade.applyPersonality(
            personality.buyMarkupMultiplier()   * skills.tradeMarkupMul(),
            personality.sellMarkdownMultiplier()* skills.tradeMarkdownMul(),
            personality.scarcityMultiplier(),
            personality.relationshipDiscountMultiplier());

        // Fatigue modifier: store on schedule conditions
        ScheduleConditions cond = schedule.conditions();
        // (fatigueMul is applied in updateFatigue; no stored field needed here)
        (void)cond;
    }

    // ─── Movement ────────────────────────────────────────────────────
    void moveTo(Vec2 target) {
        if (pathfinder) {
            currentPath = pathfinder->smoothPath(pathfinder->findPath(position, target));
            pathIndex   = 0;
            moveTarget  = target;
            isMoving    = !currentPath.empty();
        } else {
            moveTarget = target;
            isMoving   = true;
            currentPath.clear();
        }
    }

    void updateMovement(float dt) {
        if (!isMoving) return;

        Vec2 target = (!currentPath.empty() && pathIndex < currentPath.size())
                      ? currentPath[pathIndex] : moveTarget;

        Vec2  dir  = target - position;
        float dist = dir.length();

        if (dist < 0.5f) {
            position = target;
            if (!currentPath.empty() && pathIndex < currentPath.size() - 1)
                ++pathIndex;
            else { isMoving = false; currentPath.clear(); }
            return;
        }

        Vec2 move = dir.normalized() * moveSpeed * dt;
        if (move.length() > dist) move = dir;
        position += move;
        facing    = dir.normalized();
    }

    bool isAtLocation(Vec2 loc, float threshold = 2.0f) const {
        return position.distanceTo(loc) < threshold;
    }

    // ─── Event handling ──────────────────────────────────────────────
    void onCombatEvent(const CombatEvent& e) {
        if (e.defender == id) {
            emotions.addEmotion(EmotionType::Angry,
                0.6f * personality.angerIntensityMultiplier(), 2.0f);
            emotions.depletNeed(NeedType::Safety,
                30.0f * personality.fearIntensityMultiplier());
            memory.addMemory(MemoryType::Combat, "Was attacked", -0.8f, e.attacker, 0.9f);
        }
        if (e.killed && e.attacker == id) {
            memory.addMemory(MemoryType::Combat, "Defeated an enemy", 0.3f, e.defender, 0.8f);
        }
    }

    void onWorldEvent(const WorldEvent& e) {
        if (e.severity > 0.5f) {
            emotions.addEmotion(EmotionType::Fearful,
                e.severity * 0.8f * personality.fearIntensityMultiplier(), 3.0f);
            emotions.depletNeed(NeedType::Safety,
                e.severity * 40.0f * personality.fearIntensityMultiplier());
            memory.addMemory(MemoryType::WorldEvent, e.description, -e.severity);
        }
    }

    void onQuestCompleted(const QuestCompletedEvent& e) {
        if (e.takerId != id && e.giverId != id) return;
        memory.addMemory(MemoryType::Interaction,
            "Quest completed: " + e.questId, 0.4f, std::nullopt, 0.6f);
    }

    void onQuestFailed(const QuestFailedEvent& e) {
        if (e.takerId != id) return;
        memory.addMemory(MemoryType::Interaction,
            "Quest failed: " + e.questId + " (" + e.reason + ")", -0.3f,
            std::nullopt, 0.5f);
    }

    void onSkillLevelUp(const SkillLevelUpEvent& e) {
        if (e.entityId != id) return;
        std::string msg = skillDomainToString(e.domain)
                        + " reached level " + std::to_string(e.newLevel);
        if (!e.perkUnlocked.empty()) msg += " — unlocked: " + e.perkUnlocked;
        memory.addMemory(MemoryType::Interaction, msg, 0.5f, std::nullopt, 0.7f);
        applySkillBonuses();
    }

    void subscribeToEvents(EventBus& events) {
        events.subscribe<CombatEvent>([this](const CombatEvent& e)    { onCombatEvent(e); });
        events.subscribe<WorldEvent> ([this](const WorldEvent& e)     { onWorldEvent(e);  });
        events.subscribe<QuestCompletedEvent>([this](const QuestCompletedEvent& e) {
            onQuestCompleted(e); });
        events.subscribe<QuestFailedEvent>([this](const QuestFailedEvent& e) {
            onQuestFailed(e); });
        events.subscribe<SkillLevelUpEvent>([this](const SkillLevelUpEvent& e) {
            onSkillLevelUp(e); });
        // Wire skill system XP subscriptions
        skills.subscribeToEvents(events);
    }

    // ─── Info ────────────────────────────────────────────────────────
    std::string getInfo() const {
        std::ostringstream ss;
        ss << name << " (" << npcTypeToString(type) << ")"
           << " at " << position.toString()
           << " | HP: "  << static_cast<int>(combat.stats.health)
           << "/"        << static_cast<int>(combat.stats.maxHealth)
           << " | State: " << fsm.currentState()
           << " | Mood: "  << emotions.getMoodString()
           << " | Traits: "<< personality.traitSummary();
        // Show non-zero skill levels
        bool first = true;
        for (auto d : {SkillDomain::Combat, SkillDomain::Trade,
                       SkillDomain::Farming, SkillDomain::Crafting,
                       SkillDomain::Social,  SkillDomain::Leadership}) {
            int lv = skills.level(d);
            if (lv > 0) {
                if (first) { ss << " | Skills:"; first = false; }
                ss << " " << skillDomainToString(d) << lv;
            }
        }
        return ss.str();
    }

    void log(const std::string& timeStr, const std::string& msg) const {
        if (verbose)
            std::cout << "[" << timeStr << "] " << name
                      << " (" << npcTypeToString(type) << "): " << msg << "\n";
    }
};

} // namespace npc
