#pragma once

#include "../core/types.hpp"
#include <vector>
#include <string>
#include <functional>
#include <algorithm>

namespace npc {

// Forward declaration
class GameWorld;

struct ScheduledEvent {
    float triggerHour;       // Hour of day (0-24) when event triggers
    std::string name;
    std::function<void(GameWorld&)> handler;
    bool triggered = false;
    int priority = 0;        // Higher = triggers first if same hour
};

class WorldEventManager {
public:
    void scheduleEvent(float hour, const std::string& name,
                       std::function<void(GameWorld&)> handler, int priority = 0) {
        events_.push_back({hour, name, std::move(handler), false, priority});
        // Sort by trigger hour, then by priority (higher first)
        std::sort(events_.begin(), events_.end(),
            [](const ScheduledEvent& a, const ScheduledEvent& b) {
                if (a.triggerHour != b.triggerHour)
                    return a.triggerHour < b.triggerHour;
                return a.priority > b.priority;
            });
    }

    void update(float currentHour, GameWorld& world) {
        for (auto& event : events_) {
            if (!event.triggered && currentHour >= event.triggerHour) {
                event.handler(world);
                event.triggered = true;
            }
        }
    }

    // Reset all events for a new day
    void resetAll() {
        for (auto& event : events_) {
            event.triggered = false;
        }
    }

    bool isTriggered(const std::string& name) const {
        for (const auto& event : events_) {
            if (event.name == name) return event.triggered;
        }
        return false;
    }

    const std::vector<ScheduledEvent>& events() const { return events_; }

private:
    std::vector<ScheduledEvent> events_;
};

} // namespace npc
