#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <map>
#include <unordered_map>
#include <optional>
#include <variant>
#include <any>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <cassert>
#include <cmath>
#include <limits>

namespace npc {

// ─── Entity Identification ───────────────────────────────────────────
using EntityId = uint32_t;
constexpr EntityId INVALID_ENTITY = 0;

// ─── Faction Identification ──────────────────────────────────────────
using FactionId = uint32_t;
constexpr FactionId NO_FACTION = 0;

// ─── Item Identification ─────────────────────────────────────────────
using ItemId = uint32_t;

// ─── Game Time ───────────────────────────────────────────────────────
using GameTime = float;  // hours since simulation start

// ─── NPC Types ───────────────────────────────────────────────────────
enum class NPCType {
    Guard,
    Merchant,
    Blacksmith,
    Villager,
    Innkeeper,
    Farmer,
    Enemy
};

inline std::string npcTypeToString(NPCType t) {
    switch (t) {
        case NPCType::Guard:      return "Guard";
        case NPCType::Merchant:   return "Merchant";
        case NPCType::Blacksmith: return "Blacksmith";
        case NPCType::Villager:   return "Villager";
        case NPCType::Innkeeper:  return "Innkeeper";
        case NPCType::Farmer:     return "Farmer";
        case NPCType::Enemy:      return "Enemy";
    }
    return "Unknown";
}

// ─── Cell Types (World Grid) ─────────────────────────────────────────
enum class CellType {
    Grass,
    Road,
    Building,
    Water,
    Forest,
    Wall,
    Door
};

// ─── Time of Day ─────────────────────────────────────────────────────
enum class TimeOfDay {
    Dawn,       // 05:00 - 07:00
    Morning,    // 07:00 - 12:00
    Noon,       // 12:00 - 13:00
    Afternoon,  // 13:00 - 17:00
    Evening,    // 17:00 - 20:00
    Night       // 20:00 - 05:00
};

inline std::string timeOfDayToString(TimeOfDay t) {
    switch (t) {
        case TimeOfDay::Dawn:      return "Dawn";
        case TimeOfDay::Morning:   return "Morning";
        case TimeOfDay::Noon:      return "Noon";
        case TimeOfDay::Afternoon: return "Afternoon";
        case TimeOfDay::Evening:   return "Evening";
        case TimeOfDay::Night:     return "Night";
    }
    return "Unknown";
}

// ─── Activity Types ──────────────────────────────────────────────────
enum class ActivityType {
    Sleep,
    Eat,
    Work,
    Patrol,
    Socialize,
    Trade,
    Worship,
    Train,
    Leisure,
    Guard,
    Idle
};

inline std::string activityToString(ActivityType a) {
    switch (a) {
        case ActivityType::Sleep:     return "Sleep";
        case ActivityType::Eat:       return "Eat";
        case ActivityType::Work:      return "Work";
        case ActivityType::Patrol:    return "Patrol";
        case ActivityType::Socialize: return "Socialize";
        case ActivityType::Trade:     return "Trade";
        case ActivityType::Worship:   return "Worship";
        case ActivityType::Train:     return "Train";
        case ActivityType::Leisure:   return "Leisure";
        case ActivityType::Guard:     return "Guard";
        case ActivityType::Idle:      return "Idle";
    }
    return "Unknown";
}

// ─── Emotion Types ───────────────────────────────────────────────────
enum class EmotionType {
    Happy,
    Sad,
    Angry,
    Fearful,
    Disgusted,
    Surprised,
    Neutral
};

inline std::string emotionToString(EmotionType e) {
    switch (e) {
        case EmotionType::Happy:     return "Happy";
        case EmotionType::Sad:       return "Sad";
        case EmotionType::Angry:     return "Angry";
        case EmotionType::Fearful:   return "Fearful";
        case EmotionType::Disgusted: return "Disgusted";
        case EmotionType::Surprised: return "Surprised";
        case EmotionType::Neutral:   return "Neutral";
    }
    return "Unknown";
}

// ─── Need Types ──────────────────────────────────────────────────────
enum class NeedType {
    Hunger,
    Thirst,
    Sleep,
    Social,
    Fun,
    Safety,
    Comfort
};

inline std::string needToString(NeedType n) {
    switch (n) {
        case NeedType::Hunger:  return "Hunger";
        case NeedType::Thirst:  return "Thirst";
        case NeedType::Sleep:   return "Sleep";
        case NeedType::Social:  return "Social";
        case NeedType::Fun:     return "Fun";
        case NeedType::Safety:  return "Safety";
        case NeedType::Comfort: return "Comfort";
    }
    return "Unknown";
}

// ─── Memory Types ────────────────────────────────────────────────────
enum class MemoryType {
    Interaction,
    Combat,
    Trade,
    DialogChoice,
    WorldEvent,
    Location
};

// ─── Awareness Levels ────────────────────────────────────────────────
enum class AwarenessLevel {
    Unaware,
    Suspicious,
    Alert,
    Combat
};

inline std::string awarenessToString(AwarenessLevel a) {
    switch (a) {
        case AwarenessLevel::Unaware:    return "Unaware";
        case AwarenessLevel::Suspicious: return "Suspicious";
        case AwarenessLevel::Alert:      return "Alert";
        case AwarenessLevel::Combat:     return "Combat";
    }
    return "Unknown";
}

// ─── Damage Types ────────────────────────────────────────────────────
enum class DamageType {
    Physical,
    Magical,
    Fire,
    Ice,
    Poison
};

// ─── Ability Types ───────────────────────────────────────────────────
enum class AbilityType {
    Melee,
    Ranged,
    Heal,
    Buff,
    AoE
};

// ─── Item Categories ─────────────────────────────────────────────────
enum class ItemCategory {
    Weapon,
    Armor,
    Potion,
    Food,
    Material,
    Tool,
    Misc
};

inline std::string itemCategoryToString(ItemCategory c) {
    switch (c) {
        case ItemCategory::Weapon:   return "Weapon";
        case ItemCategory::Armor:    return "Armor";
        case ItemCategory::Potion:   return "Potion";
        case ItemCategory::Food:     return "Food";
        case ItemCategory::Material: return "Material";
        case ItemCategory::Tool:     return "Tool";
        case ItemCategory::Misc:     return "Misc";
    }
    return "Unknown";
}

// ─── Formation Types ─────────────────────────────────────────────────
enum class FormationType {
    Line,
    Circle,
    Wedge,
    Column
};

// ─── Behavior Tree Node Status ───────────────────────────────────────
enum class NodeStatus {
    Success,
    Failure,
    Running
};

// ─── Location Struct ─────────────────────────────────────────────────
struct Location {
    std::string name;
    float x = 0.0f;
    float y = 0.0f;
    float radius = 1.0f;
};

// ─── Log Utility ─────────────────────────────────────────────────────
inline std::string formatTime(float hoursSinceStart) {
    int totalMinutes = static_cast<int>(hoursSinceStart * 60.0f);
    int hours = (totalMinutes / 60) % 24;
    int minutes = totalMinutes % 60;
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", hours, minutes);
    return std::string(buf);
}

} // namespace npc
