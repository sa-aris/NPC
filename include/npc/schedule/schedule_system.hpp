#pragma once

#include "../core/types.hpp"
#include "../core/vec2.hpp"
#include "../event/event_system.hpp"
#include "../../npc/world/time_system.hpp"
#include <vector>
#include <string>
#include <optional>
#include <algorithm>
#include <cmath>
#include <functional>

namespace npc {

// ─── Day Mask ─────────────────────────────────────────────────────────────────
// Bitmask: bit 0 = Monday, bit 6 = Sunday.  0x7F = every day.
using DayMask = uint8_t;
constexpr DayMask EVERY_DAY  = 0x7F;
constexpr DayMask WEEKDAYS   = 0x1F;  // Mon–Fri
constexpr DayMask WEEKENDS   = 0x60;  // Sat–Sun

inline DayMask dayBit(DayOfWeek d) {
    return static_cast<DayMask>(1u << static_cast<int>(d));
}
inline bool dayActive(DayMask mask, DayOfWeek d) {
    return (mask & dayBit(d)) != 0;
}

// ─── Schedule Entry ───────────────────────────────────────────────────────────
struct ScheduleEntry {
    int          startHour;
    int          endHour;
    ActivityType activity;
    std::string  location;
    int          priority    = 0;
    bool         essential   = false;
    DayMask      activeDays  = EVERY_DAY;  // which days this entry is active

    bool isActiveAt(int hour) const {
        if (startHour <= endHour) return hour >= startHour && hour < endHour;
        return hour >= startHour || hour < endHour;
    }
    bool isActiveOn(DayOfWeek day) const { return dayActive(activeDays, day); }
    bool isActiveAt(int hour, DayOfWeek day) const {
        return isActiveOn(day) && isActiveAt(hour);
    }

    float durationHours() const {
        if (endHour >= startHour) return static_cast<float>(endHour - startHour);
        return static_cast<float>(24 - startHour + endHour);
    }
};

// ─── Schedule Override ────────────────────────────────────────────────────────
struct ScheduleOverride {
    ActivityType activity;
    std::string  location;
    float        duration    = 0.0f;
    float        startedAt   = 0.0f;
    int          priority    = 10;
    std::string  reason;

    bool isExpired(float currentTime) const {
        return duration > 0.0f && (currentTime - startedAt) >= duration;
    }
};

// ─── NPC Conditions ───────────────────────────────────────────────────────────
struct ScheduleConditions {
    float fatigue  = 0.0f;
    float sickness = 0.0f;
    bool  isSick   = false;

    static constexpr float FATIGUE_SKIP_THRESHOLD  = 0.80f;
    static constexpr float SICKNESS_SKIP_THRESHOLD = 0.50f;
    static constexpr float FATIGUE_FORCED_REST     = 0.95f;
    static constexpr float SICKNESS_FORCED_REST    = 0.85f;

    bool shouldSkip(ActivityType act) const {
        bool nonEssential = (act != ActivityType::Sleep && act != ActivityType::Eat);
        if (!nonEssential) return false;
        if (fatigue  >= FATIGUE_SKIP_THRESHOLD)  return true;
        if (isSick && sickness >= SICKNESS_SKIP_THRESHOLD) return true;
        return false;
    }
    bool mustRest() const {
        return fatigue  >= FATIGUE_FORCED_REST ||
               (isSick  && sickness >= SICKNESS_FORCED_REST);
    }
};

// ─── Resolved Activity ────────────────────────────────────────────────────────
struct ResolvedActivity {
    ActivityType activity;
    std::string  location;
    bool         isOverride   = false;
    bool         isSkipped    = false;
    bool         isTravelling = false;
    float        travelETA    = 0.0f;
    std::string  reason;
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
                  const std::string& location,
                  int priority = 0, bool essential = false,
                  DayMask days = EVERY_DAY) {
        addEntry({startHour, endHour, activity, location, priority, essential, days});
    }

    // Convenience: add entry active only on a specific day
    void addDayEntry(DayOfWeek day, int startHour, int endHour,
                     ActivityType activity, const std::string& location,
                     int priority = 0, bool essential = false) {
        addEntry({startHour, endHour, activity, location, priority, essential, dayBit(day)});
    }

    // Convenience: weekdays only / weekends only
    void addWeekdayEntry(int startHour, int endHour, ActivityType activity,
                         const std::string& location, int priority = 0) {
        addEntry({startHour, endHour, activity, location, priority, false, WEEKDAYS});
    }
    void addWeekendEntry(int startHour, int endHour, ActivityType activity,
                         const std::string& location, int priority = 0) {
        addEntry({startHour, endHour, activity, location, priority, false, WEEKENDS});
    }

    void clearSchedule()                      { schedule_.clear(); }
    const std::vector<ScheduleEntry>& entries() const { return schedule_; }

    // ── Overrides ─────────────────────────────────────────────────────────────
    void applyOverride(ScheduleOverride ov) {
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
    void clearAllOverrides()                       { overrides_.clear(); }
    bool hasOverride(const std::string& r) const {
        for (const auto& o : overrides_) if (o.reason == r) return true;
        return false;
    }

    // ── Conditions ────────────────────────────────────────────────────────────
    void setConditions(const ScheduleConditions& c) { conditions_ = c; }
    const ScheduleConditions& conditions() const { return conditions_; }

    float updateFatigue(float dt, ActivityType act) {
        float& f = conditions_.fatigue;
        switch (act) {
            case ActivityType::Sleep:   f -= 0.12f * dt; break;
            case ActivityType::Idle:    f -= 0.03f * dt; break;
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

    // ── Travel helpers ────────────────────────────────────────────────────────
    static float travelTime(Vec2 from, Vec2 to, float speed = 5.0f) {
        return speed > 0.0f ? from.distanceTo(to) / speed : 0.0f;
    }
    static bool canReachInTime(Vec2 from, Vec2 to, float currentHour,
                                const ScheduleEntry& entry, float speed = 5.0f) {
        return travelTime(from, to, speed) < entry.durationHours() * 0.80f;
    }

    // ── Core resolve — day-of-week aware ──────────────────────────────────────
    ResolvedActivity resolve(float currentHour,
                              DayOfWeek today,
                              float currentTime = -1.0f) const {
        if (currentTime < 0.0f) currentTime = currentHour;

        if (conditions_.mustRest())
            return {ActivityType::Sleep, "Bed", false, false, false, 0.0f, "exhausted"};

        // Active overrides
        const ScheduleOverride* bestOv = nullptr;
        for (const auto& ov : overrides_) {
            if (ov.isExpired(currentTime)) continue;
            if (!bestOv || ov.priority > bestOv->priority) bestOv = &ov;
        }
        if (bestOv)
            return {bestOv->activity, bestOv->location, true, false, false, 0.0f, bestOv->reason};

        // Normal schedule filtered by day
        int hour = static_cast<int>(currentHour) % 24;
        for (const auto& entry : schedule_) {
            if (!entry.isActiveAt(hour, today)) continue;
            if (conditions_.shouldSkip(entry.activity))
                return {ActivityType::Idle, entry.location, false, true, false, 0.0f, "too_tired"};
            return {entry.activity, entry.location, false, false, false, 0.0f, ""};
        }
        return {ActivityType::Idle, "", false, false, false, 0.0f, "no_entry"};
    }

    // Travel-aware resolve
    ResolvedActivity resolveWithTravel(
            float currentHour, float currentTime, DayOfWeek today,
            Vec2 npcPos,
            const std::function<std::optional<Vec2>(const std::string&)>& locationPos,
            float npcSpeed = 5.0f) const
    {
        ResolvedActivity base = resolve(currentHour, today, currentTime);
        if (base.isOverride || base.isSkipped || base.location.empty()) return base;

        auto dest = locationPos(base.location);
        if (!dest.has_value()) return base;
        float hours = travelTime(npcPos, *dest, npcSpeed);
        if (hours < 0.05f) return base;

        int hour = static_cast<int>(currentHour) % 24;
        for (const auto& entry : schedule_) {
            if (!entry.isActiveAt(hour, today)) continue;
            if (!canReachInTime(npcPos, *dest, currentHour, entry, npcSpeed)) {
                auto next = getNextActivity(currentHour, today);
                if (next) {
                    auto nd = locationPos(next->location);
                    float nh = nd ? travelTime(npcPos, *nd, npcSpeed) : 0.0f;
                    return {next->activity, next->location, false, false, true, nh, "travel_skip"};
                }
            }
            return {base.activity, base.location, false, false, true, hours, "en_route"};
        }
        return base;
    }

    // ── Getters ───────────────────────────────────────────────────────────────
    std::optional<ScheduleEntry> getCurrentActivity(float currentHour,
                                                     DayOfWeek today = DayOfWeek::Monday) const {
        int hour = static_cast<int>(currentHour) % 24;
        for (const auto& entry : schedule_)
            if (entry.isActiveAt(hour, today)) return entry;
        return std::nullopt;
    }

    // Legacy overload (ignores day)
    std::optional<ScheduleEntry> getCurrentActivity(float currentHour) const {
        return getCurrentActivity(currentHour, DayOfWeek::Monday);
    }

    std::optional<ScheduleEntry> getNextActivity(float currentHour,
                                                   DayOfWeek today = DayOfWeek::Monday) const {
        int hour = static_cast<int>(currentHour) % 24;
        int bestDist = 25;
        const ScheduleEntry* best = nullptr;
        for (const auto& entry : schedule_) {
            if (!entry.isActiveOn(today)) continue;
            int dist = (entry.startHour - hour + 24) % 24;
            if (dist > 0 && dist < bestDist) { bestDist = dist; best = &entry; }
        }
        return best ? std::optional<ScheduleEntry>(*best) : std::nullopt;
    }

    SubscriptionId subscribeToEvents(EventBus& bus, float& currentTimeRef) {
        return bus.subscribe<WorldEvent>([this, &currentTimeRef](const WorldEvent& ev) {
            onWorldEvent(ev, currentTimeRef);
        });
    }

    // ── Preset schedules (day-aware) ──────────────────────────────────────────
    static ScheduleSystem createGuardSchedule() {
        ScheduleSystem s;
        s.addEntry(6,  7,  ActivityType::Eat,       "Tavern",   0, true);
        s.addEntry(7,  12, ActivityType::Patrol,    "Village",  1, false);
        s.addEntry(12, 13, ActivityType::Eat,       "Tavern",   0, true);
        s.addEntry(13, 19, ActivityType::Patrol,    "Village",  1, false);
        s.addEntry(19, 20, ActivityType::Eat,       "Tavern",   0, true);
        // Weekday evenings: socialize; weekend evenings: leisure
        s.addEntry(20, 22, ActivityType::Socialize, "Tavern",   0, false, WEEKDAYS);
        s.addEntry(20, 23, ActivityType::Leisure,   "Tavern",   0, false, WEEKENDS);
        s.addEntry(22, 6,  ActivityType::Guard,     "Gate",     2, false, WEEKDAYS);
        s.addEntry(23, 6,  ActivityType::Sleep,     "Barracks", 0, true,  WEEKENDS);
        return s;
    }

    static ScheduleSystem createFarmerSchedule() {
        ScheduleSystem s;
        s.addEntry(5,  6,  ActivityType::Eat,       "Tavern",   0, true);
        s.addEntry(6,  12, ActivityType::Work,      "Farm",     1, false, WEEKDAYS);
        s.addEntry(12, 13, ActivityType::Eat,       "Tavern",   0, true);
        s.addEntry(13, 17, ActivityType::Work,      "Farm",     1, false, WEEKDAYS);
        s.addEntry(17, 19, ActivityType::Socialize, "Square",   0, false, WEEKDAYS);
        // Market day on Saturday
        s.addEntry(7,  14, ActivityType::Trade,     "Market",   2, false,
                   dayBit(DayOfWeek::Saturday));
        // Sunday: rest and leisure
        s.addEntry(7,  20, ActivityType::Leisure,   "Square",   1, false,
                   dayBit(DayOfWeek::Sunday));
        s.addEntry(19, 20, ActivityType::Eat,       "Tavern",   0, true);
        s.addEntry(20, 5,  ActivityType::Sleep,     "FarmHouse",0, true);
        return s;
    }

    static ScheduleSystem createMerchantSchedule() {
        ScheduleSystem s;
        s.addEntry(6,  7,  ActivityType::Eat,       "Tavern",     0, true);
        // Market days: Mon, Wed, Fri, Sat
        DayMask marketDays = dayBit(DayOfWeek::Monday)   |
                             dayBit(DayOfWeek::Wednesday)|
                             dayBit(DayOfWeek::Friday)   |
                             dayBit(DayOfWeek::Saturday);
        s.addEntry(7,  18, ActivityType::Trade,     "Market",     1, false, marketDays);
        // Off days: bookkeeping at home
        DayMask offDays = EVERY_DAY & ~marketDays;
        s.addEntry(8,  12, ActivityType::Work,      "MerchHouse", 1, false, offDays);
        s.addEntry(12, 13, ActivityType::Eat,       "Tavern",     0, true);
        s.addEntry(13, 17, ActivityType::Work,      "MerchHouse", 0, false, offDays);
        s.addEntry(18, 20, ActivityType::Socialize, "Tavern",     0);
        s.addEntry(20, 6,  ActivityType::Sleep,     "MerchHouse", 0, true);
        return s;
    }

    static ScheduleSystem createBlacksmithSchedule() {
        ScheduleSystem s;
        s.addEntry(6,  7,  ActivityType::Eat,       "Tavern",     0, true);
        s.addEntry(7,  12, ActivityType::Work,      "Forge",      1, false, WEEKDAYS);
        s.addEntry(12, 13, ActivityType::Eat,       "Tavern",     0, true);
        s.addEntry(13, 17, ActivityType::Work,      "Forge",      1, false, WEEKDAYS);
        s.addEntry(17, 19, ActivityType::Socialize, "Square",     0, false, WEEKDAYS);
        s.addEntry(9,  13, ActivityType::Work,      "Forge",      1, false,
                   dayBit(DayOfWeek::Saturday)); // half day Saturday
        s.addEntry(10, 20, ActivityType::Leisure,   "Square",     0, false,
                   dayBit(DayOfWeek::Sunday));
        s.addEntry(19, 20, ActivityType::Eat,       "Tavern",     0, true);
        s.addEntry(20, 6,  ActivityType::Sleep,     "SmithHouse", 0, true);
        return s;
    }

    static ScheduleSystem createInnkeeperSchedule() {
        ScheduleSystem s;
        s.addEntry(5,  22, ActivityType::Work,  "Tavern",     1);
        s.addEntry(12, 14, ActivityType::Eat,   "Tavern",     0, true);
        s.addEntry(22, 5,  ActivityType::Sleep, "TavernRoom", 0, true);
        return s;
    }

private:
    void sortSchedule() {
        std::stable_sort(schedule_.begin(), schedule_.end(),
            [](const ScheduleEntry& a, const ScheduleEntry& b){
                return a.priority > b.priority;
            });
    }

    void onWorldEvent(const WorldEvent& ev, float currentTime) {
        if (ev.eventType == "wolf_attack" || ev.eventType == "beast_attack")
            applyOverride(ActivityType::Patrol, "Village", ev.eventType, 3.0f, currentTime, 9);
        else if (ev.eventType == "fire")
            applyOverride(ActivityType::Idle,   "Square",  "fire",       2.0f, currentTime, 8);
        else if (ev.eventType == "alarm" || ev.eventType == "raid")
            applyOverride(ActivityType::Guard,  "Gate",    ev.eventType, 4.0f, currentTime, 10);
        else if (ev.eventType == "festival")
            applyOverride(ActivityType::Socialize,"Square","festival",   6.0f, currentTime, 5);
        else if (ev.eventType == "market_day")
            applyOverride(ActivityType::Trade,  "Market",  "market_day", 8.0f, currentTime, 6);
        else if (ev.eventType == "curfew")
            applyOverride(ActivityType::Sleep,  "Home",    "curfew",     8.0f, currentTime, 11);
        else if (ev.severity >= 0.8f)
            applyOverride(ActivityType::Idle,   "Home",    ev.eventType,
                          2.0f * ev.severity, currentTime, 7);
    }

    std::vector<ScheduleEntry>    schedule_;
    std::vector<ScheduleOverride> overrides_;
    ScheduleConditions            conditions_;
};

} // namespace npc
