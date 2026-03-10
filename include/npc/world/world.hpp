#pragma once

#include "../core/types.hpp"
#include "../core/vec2.hpp"
#include "../event/event_system.hpp"
#include "time_system.hpp"
#include "world_event_manager.hpp"
#include <vector>
#include <map>
#include <string>
#include <memory>
#include <iostream>

namespace npc {

// Forward declaration
class NPC;

struct WorldCell {
    CellType type = CellType::Grass;
    float movementCost = 1.0f;
    bool walkable = true;
};

class GameWorld {
public:
    GameWorld(int width, int height)
        : width_(width), height_(height)
        , grid_(height, std::vector<WorldCell>(width)) {
    }

    // ─── Grid ────────────────────────────────────────────────────────
    int width() const { return width_; }
    int height() const { return height_; }

    bool inBounds(int x, int y) const {
        return x >= 0 && x < width_ && y >= 0 && y < height_;
    }

    WorldCell& cell(int x, int y) { return grid_[y][x]; }
    const WorldCell& cell(int x, int y) const { return grid_[y][x]; }

    void setCell(int x, int y, CellType type, float cost = 1.0f, bool walkable = true) {
        if (!inBounds(x, y)) return;
        grid_[y][x] = {type, cost, walkable};
    }

    bool isWalkable(int x, int y) const {
        if (!inBounds(x, y)) return false;
        return grid_[y][x].walkable;
    }

    float movementCost(int x, int y) const {
        if (!inBounds(x, y)) return 999.0f;
        return grid_[y][x].movementCost;
    }

    // ─── Locations ───────────────────────────────────────────────────
    void addLocation(const std::string& name, float x, float y, float radius = 2.0f) {
        locations_[name] = {name, x, y, radius};
    }

    const Location* getLocation(const std::string& name) const {
        auto it = locations_.find(name);
        return (it != locations_.end()) ? &it->second : nullptr;
    }

    const std::map<std::string, Location>& locations() const { return locations_; }

    // ─── NPCs ────────────────────────────────────────────────────────
    void addNPC(std::shared_ptr<NPC> npc) {
        npcs_.push_back(std::move(npc));
    }

    const std::vector<std::shared_ptr<NPC>>& npcs() const { return npcs_; }
    std::vector<std::shared_ptr<NPC>>& npcs() { return npcs_; }

    NPC* findNPC(EntityId id);
    NPC* findNPC(const std::string& name);

    // ─── Time & Events ───────────────────────────────────────────────
    TimeSystem& time() { return time_; }
    const TimeSystem& time() const { return time_; }
    EventBus& events() { return events_; }
    WorldEventManager& eventManager() { return eventManager_; }

    // ─── Simulation step ─────────────────────────────────────────────
    void update(float dt);

    // ─── Log ─────────────────────────────────────────────────────────
    void log(const std::string& message) {
        std::cout << "[" << time_.formatClock() << "] " << message << "\n";
    }

    // ─── ASCII visualization ─────────────────────────────────────────
    void printMap() const;

private:
    int width_, height_;
    std::vector<std::vector<WorldCell>> grid_;
    std::map<std::string, Location> locations_;
    std::vector<std::shared_ptr<NPC>> npcs_;
    TimeSystem time_{6.0f};
    EventBus events_;
    WorldEventManager eventManager_;
};

} // namespace npc
