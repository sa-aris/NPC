#include "npc/world/world.hpp"
#include "npc/npc.hpp"

namespace npc {

NPC* GameWorld::findNPC(EntityId id) {
    for (auto& npc : npcs_) {
        if (npc->id == id) return npc.get();
    }
    return nullptr;
}

NPC* GameWorld::findNPC(const std::string& name) {
    for (auto& npc : npcs_) {
        if (npc->name == name) return npc.get();
    }
    return nullptr;
}

void GameWorld::update(float dt) {
    time_.update(dt, events_);

    // Process scheduled world events
    eventManager_.update(time_.currentHour(), *this);

    for (auto& npc : npcs_) {
        npc->update(dt, *this);
    }
}

void GameWorld::printMap() const {
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            // Check if any NPC is at this position
            bool npcHere = false;
            for (const auto& npc : npcs_) {
                if (npc->position.gridX() == x && npc->position.gridY() == y) {
                    // NPC marker
                    char c = '?';
                    switch (npc->type) {
                        case NPCType::Guard:      c = 'G'; break;
                        case NPCType::Merchant:   c = 'M'; break;
                        case NPCType::Blacksmith:  c = 'B'; break;
                        case NPCType::Villager:   c = 'V'; break;
                        case NPCType::Innkeeper:  c = 'I'; break;
                        case NPCType::Farmer:     c = 'F'; break;
                        case NPCType::Enemy:      c = 'W'; break;
                    }
                    std::cout << c;
                    npcHere = true;
                    break;
                }
            }
            if (npcHere) continue;

            switch (grid_[y][x].type) {
                case CellType::Grass:    std::cout << '.'; break;
                case CellType::Road:     std::cout << '#'; break;
                case CellType::Building: std::cout << 'H'; break;
                case CellType::Water:    std::cout << '~'; break;
                case CellType::Forest:   std::cout << 'T'; break;
                case CellType::Wall:     std::cout << 'X'; break;
                case CellType::Door:     std::cout << 'D'; break;
            }
        }
        std::cout << '\n';
    }
}

} // namespace npc
