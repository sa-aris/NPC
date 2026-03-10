#pragma once

#include "../core/types.hpp"
#include "../core/vec2.hpp"
#include "../event/event_system.hpp"
#include <vector>
#include <string>
#include <optional>
#include <algorithm>
#include <cmath>
#include <functional>

namespace npc {

// ─── Schedule Entry ───────────────────────────────────────────────────────────
struct ScheduleEntry {
    int          startHour;
    int          endHour;
    ActivityType activity;
    std::string  location;
    int          priority    = 0;
    bool         essential   = false;  // Sleep/Eat → skipped only when very ill

    bool isActiveAt(int hour) const {
        if (startHour <= endHour) return hour >= startHour && hour < endHour;
        return hour >= startHour || hour < endHour;  // wrapping (e.g. 22–06)
    }
    float durationHours() const {
        if (endHour >= startHour) return static_cast<float>(endHour - startHour);
        return static_cast<float>(24 - startHour + endHour);
    }
};

// ─── Schedule Override ────────────────────────────────────────────────────────
// Temporarily supersedes the normal schedule due to a world event or emergency.
struct ScheduleOverride {
    ActivityType activity;
    std::string  location;
    float        duration    = 0.0f;  // game hours; 0 = indefinite
    float        startedAt   = 0.0f;  // game time when inserted
    int          priority    = 10;    // higher than normal entries
    std::string  reason;              // "wolf_attack", "fire", "alarm", …

    bool isExpired(float currentTime) const {
        return duration > 0.0f && (currentTime - startedAt) >= duration;
    }
};

// ─── NPC Conditions ───────────────────────────────────────────────────────────
struct ScheduleConditions {
    float fatigue  = 0.0f;   // 0 (rested) → 1 (exhausted)
    float sickness = 0.0f;   // 0 (healthy) → 1 (bedridden)
    bool  isSick   = false;

    static constexpr float FATIGUE_SKIP_THRESHOLD  = 0.80f; // skip non-essential if above
    static constexpr float SICKNESS_SKIP_THRESHOLD = 0.50f;
    static constexpr float FATIGUE_FORCED_REST     = 0.95f; // force Sleep regardless
    static constexpr float SICKNESS_FORCED_REST    = 0.85f;

    // Returns true when the NPC should skip a non-essential activity
    bool shouldSkip(ActivityType act) const {
        bool nonEssential = (act != ActivityType::Sleep && act != ActivityType::Eat);
        if (!nonEssential) return false;
        if (fatigue  >= FATIGUE_SKIP_THRESHOLD)  return true;
        if (isSick && sickness >= SICKNESS_SKIP_THRESHOLD) return true;
        return false;
    }

    // Returns true when the NPC is so exhausted/ill they must rest immediately
    bool mustRest() const {
        return fatigue  >= FATIGUE_FORCED_REST ||
               (isSick  && sickness >= SICKNESS_FORCED_REST);
    }
};

// ─── Travel Info ──────────────────────────────────────────────────────────────
struct TravelInfo {
    float distanceUnits      = 0.0f;
    float speedUnitsPerHour  = 5.0f;  // walking speed
    float travelHours()  const { return speedUnitsPerHour > 0.0f
                                        ? distanceUnits / speedUnitsPerHour : 0.0f; }
    bool  arrivesInTime(float departHour, int entryEndHour) const {
        float arrival = std::fmod(departHour + travelHours(), 24.0f);
        int   arr     = static_cast<int>(arrival);
        return arr < entryEndHour;
    }
};

// ─── Resolve Result ───────────────────────────────────────────────────────────
struct ResolvedActivity {
    ActivityType activity;
    std::string  location;
    bool         isOverride  = false;  // came from an override
    bool         isSkipped   = false;  // NPC too tired/sick, doing Idle instead
    bool         isTravelling= false;  // NPC currently en route
    float        travelETA   = 0.0f;   // hours until arrival (if travelling)
    std::string  reason;               // override reason or skip reason
};

// ─── ScheduleSystem ───────────────────────────────────────────────────────────
class ScheduleSystem {
public:
    // ── Base schedule ─────────────────────────────────────────────────────────
    void addEntry(ScheduleEntry entry) {
        schedule_.push_back(std::move(entry));
        sortSchedule();
    }
    void addEntry(int startHour, int endHour, ActivityType activity,
                  const std::string& location, int priority = 0, bool essential = false) {
        addEntry({startHour, endHour, activity, location, priority, essential});
    }
    void clearSchedule()              { schedule_.clear(); }
    const std::vector<ScheduleEntry>& entries() const { return schedule_; }

    // ── Overrides ─────────────────────────────────────────────────────────────
    // Push an emergency override (wolf attack, fire alarm, etc.)
    void applyOverride(ScheduleOverride ov) {
        // Replace existing override with the same reason to avoid duplicates
        removeOverride(ov.reason);
        overrides_.push_back(std::move(ov));
    }

    void applyOverride(ActivityType activity, const std::string& location,
                       const std::string& reason,
                       float duration = 0.0f, float startedAt = 0.0f,
                       int priority = 10) {
        applyOverride({activity, location, duration, startedAt, priority, reason});
    }

    void removeOverride(const std::string& reason) {
        overrides_.erase(
            std::remove_if(overrides_.begin(), overrides_.end(),
                [&](const ScheduleOverride& o){ return o.reason == reason; }),
            overrides_.end());
    }

    void clearExpiredOverrides(float currentTime) {
        overrides_.erase(
            std::remove_if(overrides_.begin(), overrides_.end(),
                [currentTime](const ScheduleOverride& o){ return o.isExpired(currentTime); }),
            overrides_.end());
    }

    void clearAllOverrides() { overrides_.clear(); }
    const std::vector<ScheduleOverride>& overrides() const { return overrides_; }
    bool hasOverride(const std::string& reason) const {
        for (const auto& o : overrides_) if (o.reason == reason) return true;
        return false;
    }

    // ── Conditions ────────────────────────────────────────────────────────────
    void setConditions(const ScheduleConditions& c) { conditions_ = c; }
    const ScheduleConditions& conditions() const { return conditions_; }

    // Update fatigue based on current activity and elapsed time.
    // Returns updated fatigue value.
    float updateFatigue(float dt, ActivityType currentActivity) {
        float& f = conditions_.fatigue;
        switch (currentActivity) {
            case ActivityType::Sleep:   f -= 0.12f * dt; break;  // fast recovery
            case ActivityType::Idle:    f -= 0.03f * dt; break;  // slow recovery
            case ActivityType::Eat:     f -= 0.01f * dt; break;
            case ActivityType::Patrol:
            case ActivityType::Train:   f += 0.05f * dt; break;
            case ActivityType::Work:    f += 0.03f * dt; break;
            case ActivityType::Guard:   f += 0.04f * dt; break;
            default:                    f += 0.01f * dt; break;
        }
        conditions_.fatigue = std::clamp(f, 0.0f, 1.0f);
        return conditions_.fatigue;
    }

    // ── Travel time ───────────────────────────────────────────────────────────
    // Returns hours needed to walk from `from` to `to` at `speedUnitsPerHour`.
    static float travelTime(Vec2 from, Vec2 to, float speedUnitsPerHour = 5.0f) {
        float dist = from.distanceTo(to);
        return speedUnitsPerHour > 0.0f ? dist / speedUnitsPerHour : 0.0f;
    }

    // Can the NPC reach `destination` and still have meaningful time in the activity?
    // Returns false if travel would consume more than 80 % of the activity window.
    static bool canReachInTime(Vec2 from, Vec2 to,
                                float currentHour, const ScheduleEntry& entry,
                                float speedUnitsPerHour = 5.0f) {
        float travelHrs  = travelTime(from, to, speedUnitsPerHour);
        float windowHrs  = entry.durationHours();
        return travelHrs < windowHrs * 0.80f;
    }

    // ── Core resolution ───────────────────────────────────────────────────────
    // Simple version (no position awareness)
    ResolvedActivity resolve(float currentHour, float currentTime = -1.0f) const {
        if (currentTime < 0.0f) currentTime = currentHour;

        // 1. Forced rest overrides everything
        if (conditions_.mustRest()) {
            return {ActivityType::Sleep, "Bed", false, false, false, 0.0f, "exhausted"};
        }

        // 2. Check active overrides (highest priority wins)
        const ScheduleOverride* bestOv = nullptr;
        for (const auto& ov : overrides_) {
            if (ov.isExpired(currentTime)) continue;
            if (!bestOv || ov.priority > bestOv->priority) bestOv = &ov;
        }
        if (bestOv) {
            return {bestOv->activity, bestOv->location, true, false, false, 0.0f, bestOv->reason};
        }

        // 3. Normal schedule
        int hour = static_cast<int>(currentHour) % 24;
        for (const auto& entry : schedule_) {
            if (!entry.isActiveAt(hour)) continue;
            if (conditions_.shouldSkip(entry.activity)) {
                return {ActivityType::Idle, entry.location, false, true, false, 0.0f, "too_tired"};
            }
            return {entry.activity, entry.location, false, false, false, 0.0f, ""};
        }

        return {ActivityType::Idle, "", false, false, false, 0.0f, "no_entry"};
    }

    // Position-aware version: accounts for travel time to the scheduled location.
    // locationPositions: maps location name → world Vec2
    ResolvedActivity resolveWithTravel(
            float currentHour, float currentTime,
            Vec2 npcPos,
            const std::function<std::optional<Vec2>(const std::string&)>& locationPos,
            float npcSpeed = 5.0f) const
    {
        ResolvedActivity base = resolve(currentHour, currentTime);

        if (base.isOverride || base.isSkipped || base.location.empty())
            return base;

        auto dest = locationPos(base.location);
        if (!dest.has_value()) return base;

        float hours = travelTime(npcPos, *dest, npcSpeed);
        if (hours < 0.05f) return base;  // already there

        // Check upcoming entry — if we won't make it, skip to the one after
        int hour = static_cast<int>(currentHour) % 24;
        for (const auto& entry : schedule_) {
            if (!entry.isActiveAt(hour)) continue;
            if (!canReachInTime(npcPos, *dest, currentHour, entry, npcSpeed)) {
                // Travel will eat most of this window — go to next activity instead
                auto next = getNextActivity(currentHour);
                if (next) {
                    auto ndest = locationPos(next->location);
                    float nhours = ndest ? travelTime(npcPos, *ndest, npcSpeed) : 0.0f;
                    return {next->activity, next->location,
                            false, false, true, nhours, "travel_skip"};
                }
            }
            // Travelling to this location
            return {base.activity, base.location, false, false, true, hours, "en_route"};
        }
        return base;
    }

    // ── Convenience getters (legacy-compatible) ────────────────────────────────
    std::optional<ScheduleEntry> getCurrentActivity(float currentHour) const {
        int hour = static_cast<int>(currentHour) % 24;
        for (const auto& entry : schedule_)
            if (entry.isActiveAt(hour)) return entry;
        return std::nullopt;
    }

    std::optional<ScheduleEntry> getNextActivity(float currentHour) const {
        int hour     = static_cast<int>(currentHour) % 24;
        int bestDist = 25;
        const ScheduleEntry* best = nullptr;
        for (const auto& entry : schedule_) {
            int dist = (entry.startHour - hour + 24) % 24;
            if (dist > 0 && dist < bestDist) { bestDist = dist; best = &entry; }
        }
        return best ? std::optional<ScheduleEntry>(*best) : std::nullopt;
    }

    // ── EventBus integration ──────────────────────────────────────────────────
    // Subscribe to WorldEvents and auto-insert matching overrides.
    // Returns subscription ID (store to unsubscribe later).
    SubscriptionId subscribeToEvents(EventBus& bus, float& currentTimeRef) {
        return bus.subscribe<WorldEvent>([this, &currentTimeRef](const WorldEvent& ev) {
            onWorldEvent(ev, currentTimeRef);
        });
    }

    // ── Preset schedules ──────────────────────────────────────────────────────
    static ScheduleSystem createGuardSchedule() {
        ScheduleSystem s;
        s.addEntry(6,  7,  ActivityType::Eat,       "Tavern",   0, true);
        s.addEntry(7,  12, ActivityType::Patrol,    "Village",  1);
        s.addEntry(12, 13, ActivityType::Eat,       "Tavern",   0, true);
        s.addEntry(13, 19, ActivityType::Patrol,    "Village",  1);
        s.addEntry(19, 20, ActivityType::Eat,       "Tavern",   0, true);
        s.addEntry(20, 22, ActivityType::Socialize, "Tavern",   0);
        s.addEntry(22, 6,  ActivityType::Guard,     "Gate",     2);
        return s;
    }

    static ScheduleSystem createBlacksmithSchedule() {
        ScheduleSystem s;
        s.addEntry(6,  7,  ActivityType::Eat,       "Tavern",     0, true);
        s.addEntry(7,  12, ActivityType::Work,      "Forge",      1);
        s.addEntry(12, 13, ActivityType::Eat,       "Tavern",     0, true);
        s.addEntry(13, 17, ActivityType::Work,      "Forge",      1);
        s.addEntry(17, 19, ActivityType::Socialize, "Square",     0);
        s.addEntry(19, 20, ActivityType::Eat,       "Tavern",     0, true);
        s.addEntry(20, 6,  ActivityType::Sleep,     "SmithHouse", 0, true);
        return s;
    }

    static ScheduleSystem createMerchantSchedule() {
        ScheduleSystem s;
        s.addEntry(6,  7,  ActivityType::Eat,       "Tavern",     0, true);
        s.addEntry(7,  12, ActivityType::Trade,     "Market",     1);
        s.addEntry(12, 13, ActivityType::Eat,       "Tavern",     0, true);
        s.addEntry(13, 18, ActivityType::Trade,     "Market",     1);
        s.addEntry(18, 20, ActivityType::Socialize, "Tavern",     0);
        s.addEntry(20, 6,  ActivityType::Sleep,     "MerchHouse", 0, true);
        return s;
    }

    static ScheduleSystem createInnkeeperSchedule() {
        ScheduleSystem s;
        s.addEntry(5,  6,  ActivityType::Work,  "Tavern",     1);
        s.addEntry(6,  12, ActivityType::Work,  "Tavern",     1);
        s.addEntry(12, 14, ActivityType::Eat,   "Tavern",     0, true);
        s.addEntry(14, 22, ActivityType::Work,  "Tavern",     1);
        s.addEntry(22, 5,  ActivityType::Sleep, "TavernRoom", 0, true);
        return s;
    }

    static ScheduleSystem createFarmerSchedule() {
        ScheduleSystem s;
        s.addEntry(5,  6,  ActivityType::Eat,       "Tavern",   0, true);
        s.addEntry(6,  12, ActivityType::Work,      "Farm",     1);
        s.addEntry(12, 13, ActivityType::Eat,       "Tavern",   0, true);
        s.addEntry(13, 17, ActivityType::Work,      "Farm",     1);
        s.addEntry(17, 19, ActivityType::Socialize, "Square",   0);
        s.addEntry(19, 20, ActivityType::Eat,       "Tavern",   0, true);
        s.addEntry(20, 5,  ActivityType::Sleep,     "FarmHouse",0, true);
        return s;
    }

private:
    void sortSchedule() {
        std::stable_sort(schedule_.begin(), schedule_.end(),
            [](const ScheduleEntry& a, const ScheduleEntry& b){
                return a.priority > b.priority;
            });
    }

    // Translate WorldEvent.eventType strings into schedule overrides.
    void onWorldEvent(const WorldEvent& ev, float currentTime) {
        // Wolf / beast attack → extend patrol, guards mobilise
        if (ev.eventType == "wolf_attack" || ev.eventType == "beast_attack") {
            applyOverride(ActivityType::Patrol, "Village",
                          ev.eventType, 3.0f, currentTime, 9);
            return;
        }
        // Fire → evacuate building, gather at square
        if (ev.eventType == "fire") {
            applyOverride(ActivityType::Idle, "Square",
                          "fire", 2.0f, currentTime, 8);
            return;
        }
        // Alarm / attack on settlement
        if (ev.eventType == "alarm" || ev.eventType == "raid") {
            applyOverride(ActivityType::Guard, "Gate",
                          ev.eventType, 4.0f, currentTime, 10);
            return;
        }
        // Festival / celebration → socialise
        if (ev.eventType == "festival") {
            applyOverride(ActivityType::Socialize, "Square",
                          "festival", 6.0f, currentTime, 5);
            return;
        }
        // Market day → trade
        if (ev.eventType == "market_day") {
            applyOverride(ActivityType::Trade, "Market",
                          "market_day", 8.0f, currentTime, 6);
            return;
        }
        // Curfew → everyone indoors
        if (ev.eventType == "curfew") {
            applyOverride(ActivityType::Sleep, "Home",
                          "curfew", 8.0f, currentTime, 11);
            return;
        }
        // Generic high-severity event → seek safety
        if (ev.severity >= 0.8f) {
            applyOverride(ActivityType::Idle, "Home",
                          ev.eventType, 2.0f * ev.severity, currentTime, 7);
        }
    }

    std::vector<ScheduleEntry>    schedule_;
    std::vector<ScheduleOverride> overrides_;
    ScheduleConditions            conditions_;
};

} // namespace npc
