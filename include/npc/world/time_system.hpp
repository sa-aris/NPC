#pragma once

#include "../core/types.hpp"
#include "../event/event_system.hpp"
#include <string>
#include <functional>
#include <vector>

namespace npc {

// ─── Day of Week ──────────────────────────────────────────────────────────────
enum class DayOfWeek {
    Monday = 0,
    Tuesday,
    Wednesday,
    Thursday,
    Friday,
    Saturday,
    Sunday
};

inline std::string dayOfWeekToString(DayOfWeek d) {
    switch (d) {
        case DayOfWeek::Monday:    return "Monday";
        case DayOfWeek::Tuesday:   return "Tuesday";
        case DayOfWeek::Wednesday: return "Wednesday";
        case DayOfWeek::Thursday:  return "Thursday";
        case DayOfWeek::Friday:    return "Friday";
        case DayOfWeek::Saturday:  return "Saturday";
        case DayOfWeek::Sunday:    return "Sunday";
    }
    return "Unknown";
}

inline bool isWeekend(DayOfWeek d) {
    return d == DayOfWeek::Saturday || d == DayOfWeek::Sunday;
}
inline bool isWeekday(DayOfWeek d) { return !isWeekend(d); }

// ─── Calendar Date ────────────────────────────────────────────────────────────
struct CalendarDate {
    int        day;        // absolute day number (1-based)
    int        weekNumber; // 1-based week within simulation
    DayOfWeek  dayOfWeek;
    int        hour;
    int        minute;

    std::string toString() const {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Day %d (%s) Week %d %02d:%02d",
            day, dayOfWeekToString(dayOfWeek).c_str(), weekNumber, hour, minute);
        return std::string(buf);
    }

    std::string shortDate() const {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%s %02d:%02d",
            dayOfWeekToString(dayOfWeek).c_str(), hour, minute);
        return std::string(buf);
    }
};

// ─── Events ───────────────────────────────────────────────────────────────────
struct NewDayEvent {
    int        day;
    int        weekNumber;
    DayOfWeek  dayOfWeek;
};

struct NewWeekEvent {
    int weekNumber;
    DayOfWeek firstDay;  // always Monday
};

// ─── TimeSystem ───────────────────────────────────────────────────────────────
class TimeSystem {
public:
    // startDay: 1-based; startDayOfWeek: which weekday day-1 corresponds to
    explicit TimeSystem(float startHour = 6.0f,
                        int   startDay  = 1,
                        DayOfWeek startDayOfWeek = DayOfWeek::Monday)
        : currentHour_(startHour)
        , day_(startDay)
        , startDayOfWeek_(startDayOfWeek) {}

    // ── Update ────────────────────────────────────────────────────────────────
    void update(float dt, EventBus& events) {
        float prevHour = currentHour_;
        currentHour_ += dt;

        // New-day rollover
        while (currentHour_ >= 24.0f) {
            currentHour_ -= 24.0f;
            ++day_;

            DayOfWeek dow = dayOfWeek();
            int        wk = weekNumber();

            events.publish(NewDayEvent{day_, wk, dow});

            // New week (Monday rollover)
            if (dow == DayOfWeek::Monday)
                events.publish(NewWeekEvent{wk, DayOfWeek::Monday});
        }

        // TimeOfDay transitions
        auto prevTod = getTimeOfDayAt(prevHour);
        auto currTod = getTimeOfDay();
        if (prevTod != currTod)
            events.publish(TimeEvent{currentHour_, currTod, day_});
    }

    // ── Accessors ─────────────────────────────────────────────────────────────
    float      currentHour()  const { return currentHour_; }
    int        day()          const { return day_; }
    float      totalHours()   const { return (day_ - 1) * 24.0f + currentHour_; }
    int        weekNumber()   const { return ((day_ - 1) / 7) + 1; }
    int        dayInWeek()    const { return (day_ - 1) % 7; }  // 0=first day of sim

    DayOfWeek  dayOfWeek() const {
        int offset = (day_ - 1) % 7;
        return static_cast<DayOfWeek>(
            (static_cast<int>(startDayOfWeek_) + offset) % 7);
    }

    bool isWeekend()  const { return npc::isWeekend(dayOfWeek()); }
    bool isWeekday()  const { return npc::isWeekday(dayOfWeek()); }

    CalendarDate date() const {
        int h = static_cast<int>(currentHour_) % 24;
        int m = static_cast<int>((currentHour_ - static_cast<int>(currentHour_)) * 60.0f);
        return {day_, weekNumber(), dayOfWeek(), h, m};
    }

    // How many game-hours ago was the start of day N?
    float hoursAgoForDay(int targetDay) const {
        float targetTotal = (targetDay - 1) * 24.0f;
        return totalHours() - targetTotal;
    }

    // Returns "today", "yesterday", "Monday", "2 days ago" etc.
    std::string relativeDay(int targetDay) const {
        int diff = day_ - targetDay;
        if (diff == 0) return "today";
        if (diff == 1) return "yesterday";
        if (diff <= 6) return std::to_string(diff) + " days ago";
        if (diff <= 13) return "last " + dayOfWeekToString(dayOfWeekForDay(targetDay));
        return "day " + std::to_string(targetDay);
    }

    DayOfWeek dayOfWeekForDay(int targetDay) const {
        int offset = (targetDay - 1) % 7;
        return static_cast<DayOfWeek>(
            (static_cast<int>(startDayOfWeek_) + offset) % 7);
    }

    TimeOfDay getTimeOfDay()           const { return getTimeOfDayAt(currentHour_); }
    bool      isDayTime()              const { return getTimeOfDay() != TimeOfDay::Night; }
    bool      isNightTime()            const { return !isDayTime(); }

    // ── Formatting ────────────────────────────────────────────────────────────
    std::string formatClock() const {
        int h = static_cast<int>(currentHour_) % 24;
        int m = static_cast<int>((currentHour_ - static_cast<int>(currentHour_)) * 60.0f);
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
        return std::string(buf);
    }

    std::string formatFull() const {
        return "Day " + std::to_string(day_)
             + " (" + dayOfWeekToString(dayOfWeek()) + ") "
             + formatClock();
    }

    void setHour(float h) { currentHour_ = h; }

private:
    static TimeOfDay getTimeOfDayAt(float hour) {
        if (hour >= 5.0f  && hour < 7.0f)  return TimeOfDay::Dawn;
        if (hour >= 7.0f  && hour < 12.0f) return TimeOfDay::Morning;
        if (hour >= 12.0f && hour < 13.0f) return TimeOfDay::Noon;
        if (hour >= 13.0f && hour < 17.0f) return TimeOfDay::Afternoon;
        if (hour >= 17.0f && hour < 20.0f) return TimeOfDay::Evening;
        return TimeOfDay::Night;
    }

    float     currentHour_;
    int       day_;
    DayOfWeek startDayOfWeek_;
};

} // namespace npc
