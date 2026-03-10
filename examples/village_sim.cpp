/*
 * =======================================================================
 *  NPC Behavior System - Phase 2: Medieval Village Simulation Demo
 * =======================================================================
 *
 *  Demonstrates ALL subsystems working together with 4 major improvements:
 *   1. Hybrid AI: Utility AI + Behavior Tree + FSM integrated
 *   2. Social Interactions: Relationships, gossip, NPC-NPC trade
 *   3. Advanced Combat: Wolf pack tactics, GroupBehavior, detailed logs
 *   4. Dynamic World Events: Traveling merchant, thief, meeting, festival
 *
 *  NPCs: Alaric (Guard), Brina (Blacksmith), Cedric (Merchant),
 *         Dagna (Innkeeper), Elmund (Farmer), Farhan (Traveling Merchant)
 *
 *  Timeline: 06:00-22:00 with events every hour - NO silent periods
 * =======================================================================
 */

#include "npc/npc.hpp"
#include "npc/world/world.hpp"
#include "npc/social/faction_system.hpp"
#include "npc/social/group_behavior.hpp"
#include "npc/social/relationship_system.hpp"
#include "npc/social/influence_chain.hpp"
#include "npc/world/world_event_manager.hpp"
#include <iostream>
#include <iomanip>
#include <memory>
#include <cstdlib>
#include <set>

using namespace npc;

// === Faction IDs ===
constexpr FactionId VILLAGE_FACTION = 1;
constexpr FactionId WOLF_FACTION    = 2;

// === Item IDs ===
constexpr ItemId ITEM_SWORD       = 1;
constexpr ItemId ITEM_SHIELD      = 2;
constexpr ItemId ITEM_BREAD       = 3;
constexpr ItemId ITEM_ALE         = 4;
constexpr ItemId ITEM_IRON_ORE    = 5;
constexpr ItemId ITEM_HORSESHOE   = 6;
constexpr ItemId ITEM_HEALTH_POT  = 7;
constexpr ItemId ITEM_WHEAT       = 8;
constexpr ItemId ITEM_LEATHER     = 9;
constexpr ItemId ITEM_TOOLS       = 10;
constexpr ItemId ITEM_ENCHANTED_SWORD = 11;
constexpr ItemId ITEM_EXOTIC_SPICES   = 12;

// === Global state ===
static RelationshipSystem    g_relationships;
static GroupBehavior         g_wolfPack;
static InfluenceChainSystem  g_influence;
static bool g_wolfPackMoraleBroken = false;
static bool g_combatActive = false;
static bool g_combatResolved = false;

// === Forward declarations ===
void buildVillageMap(GameWorld& world);
void setupFactions(FactionSystem& factions);
void setupItems(TradeSystem& trade);
void setupAllItems(TradeSystem& trade);
std::shared_ptr<NPC> createAlaric(GameWorld& world, std::shared_ptr<Pathfinder> pf);
std::shared_ptr<NPC> createBrina(GameWorld& world, std::shared_ptr<Pathfinder> pf);
std::shared_ptr<NPC> createCedric(GameWorld& world, std::shared_ptr<Pathfinder> pf);
std::shared_ptr<NPC> createDagna(GameWorld& world, std::shared_ptr<Pathfinder> pf);
std::shared_ptr<NPC> createElmund(GameWorld& world, std::shared_ptr<Pathfinder> pf);
void setupUtilityAI(NPC& npc);
void setupCombatBT(NPC& npc, GameWorld& world);
void scheduleWorldEvents(GameWorld& world, FactionSystem& factions,
                         std::shared_ptr<Pathfinder> pf);
void runCombatRound(GameWorld& world, const std::string& timeStr);
void logRelationship(const std::string& timeStr, const std::string& a,
                     const std::string& b, EntityId idA, EntityId idB, float delta,
                     const PersonalityTraits* traitsA = nullptr,
                     const PersonalityTraits* traitsB = nullptr);
void processEmotionContagion(GameWorld& world);
void printEmotionContagionTable(const std::string& timeStr, GameWorld& world);
void processMemoryDecayNarrative(const std::string& timeStr, GameWorld& world);
void processInfluenceChains(const std::string& timeStr, GameWorld& world);
void printInfluenceChainSummary(GameWorld& world);

// =======================================================================
//  MAIN
// =======================================================================

int main() {
    std::cout << R"(
 +============================================================+
 |   NPC Behavior System - Phase 2: Full Village Simulation    |
 |   Hybrid AI + Social + Advanced Combat + World Events       |
 +============================================================+
)" << "\n";

    // --- Create world ---
    GameWorld world(40, 25);
    buildVillageMap(world);

    // --- Setup factions ---
    FactionSystem factions;
    setupFactions(factions);

    // --- Create shared pathfinder ---
    auto pathfinder = std::make_shared<Pathfinder>(
        world.width(), world.height(),
        [&world](int x, int y) { return world.isWalkable(x, y); },
        [&world](int x, int y) { return world.movementCost(x, y); }
    );

    // --- Create NPCs ---
    auto alaric = createAlaric(world, pathfinder);
    auto brina  = createBrina(world, pathfinder);
    auto cedric = createCedric(world, pathfinder);
    auto dagna  = createDagna(world, pathfinder);
    auto elmund = createElmund(world, pathfinder);

    // Register to factions
    factions.addMember(VILLAGE_FACTION, alaric->id);
    factions.addMember(VILLAGE_FACTION, brina->id);
    factions.addMember(VILLAGE_FACTION, cedric->id);
    factions.addMember(VILLAGE_FACTION, dagna->id);
    factions.addMember(VILLAGE_FACTION, elmund->id);

    // Subscribe to events
    alaric->subscribeToEvents(world.events());
    brina->subscribeToEvents(world.events());
    cedric->subscribeToEvents(world.events());
    dagna->subscribeToEvents(world.events());
    elmund->subscribeToEvents(world.events());

    // Add NPCs to world
    world.addNPC(alaric);
    world.addNPC(brina);
    world.addNPC(cedric);
    world.addNPC(dagna);
    world.addNPC(elmund);

    // --- Setup Utility AI for all NPCs ---
    setupUtilityAI(*alaric);
    setupUtilityAI(*brina);
    setupUtilityAI(*cedric);
    setupUtilityAI(*dagna);
    setupUtilityAI(*elmund);

    // --- Setup Alaric's Combat Behavior Tree ---
    setupCombatBT(*alaric, world);

    // --- Initialize relationships ---
    g_relationships.setValue(std::to_string(alaric->id), std::to_string(brina->id), 40.0f);
    g_relationships.setValue(std::to_string(alaric->id), std::to_string(cedric->id), 25.0f);
    g_relationships.setValue(std::to_string(alaric->id), std::to_string(dagna->id), 35.0f);
    g_relationships.setValue(std::to_string(alaric->id), std::to_string(elmund->id), 20.0f);
    g_relationships.setValue(std::to_string(brina->id), std::to_string(cedric->id), 30.0f);
    g_relationships.setValue(std::to_string(brina->id), std::to_string(dagna->id), 45.0f);
    g_relationships.setValue(std::to_string(brina->id), std::to_string(elmund->id), 25.0f);
    g_relationships.setValue(std::to_string(cedric->id), std::to_string(dagna->id), 35.0f);
    g_relationships.setValue(std::to_string(cedric->id), std::to_string(elmund->id), 30.0f);
    g_relationships.setValue(std::to_string(dagna->id), std::to_string(elmund->id), 40.0f);

    // --- Schedule world events ---
    scheduleWorldEvents(world, factions, pathfinder);

    // === Print map ===
    std::cout << "=== Village Map ===\n";
    world.printMap();
    std::cout << "\n  Legend: G=Guard B=Blacksmith M=Merchant I=Innkeeper F=Farmer\n"
              << "         .=Grass #=Road H=Building T=Forest ~=Water X=Wall\n\n";
    std::cout << "========================================\n";
    std::cout << "  SIMULATION START - Day 1, 06:00\n";
    std::cout << "========================================\n\n";

    // =================================================================
    //  SIMULATION LOOP
    // =================================================================

    float dt = 0.25f; // each step = 15 minutes

    for (float simTime = 0.0f; simTime < 16.0f; simTime += dt) {
        float currentHour = world.time().currentHour();
        int hourInt = static_cast<int>(currentHour);
        int minute = static_cast<int>((currentHour - hourInt) * 60);

        // --- Hour announcements ---
        if (minute == 0) {
            std::cout << "\n--- " << world.time().formatClock()
                      << " (" << timeOfDayToString(world.time().getTimeOfDay())
                      << ") ---\n";
            printEmotionContagionTable(world.time().formatClock(), world);
        }

        // --- Combat rounds ---
        if (g_combatActive && !g_combatResolved) {
            runCombatRound(world, world.time().formatClock());
        }

        // --- World update (triggers scheduled events + NPC updates) ---
        world.update(dt);

        // --- Emotion contagion (proximity-based spreading) ---
        processEmotionContagion(world);

        // --- Memory decay narrative ---
        processMemoryDecayNarrative(world.time().formatClock(), world);

        // --- Social influence chain propagation ---
        processInfluenceChains(world.time().formatClock(), world);
    }

    // =================================================================
    //  END OF DAY SUMMARY
    // =================================================================

    std::cout << "\n========================================\n";
    std::cout << "  END OF DAY SUMMARY\n";
    std::cout << "========================================\n\n";

    for (const auto& npc : world.npcs()) {
        if (npc->type == NPCType::Enemy) continue;
        if (npc->name == "Farhan") continue; // traveling merchant left

        std::cout << "  " << npc->getInfo() << "\n";
        std::cout << "    Personality: " << npc->personality.toString() << "\n";

        // Need summary
        const auto& needs = npc->emotions.needs();
        std::cout << "    Needs: ";
        for (const auto& [type, need] : needs) {
            if (need.isUrgent()) {
                std::cout << needToString(type) << "=" << static_cast<int>(need.value) << "! ";
            }
        }
        std::cout << "\n";

        // Relationships
        std::cout << "    Relationships: ";
        for (const auto& other : world.npcs()) {
            if (other->id == npc->id || other->type == NPCType::Enemy) continue;
            if (other->name == "Farhan") continue;
            float rel = g_relationships.getValue(std::to_string(npc->id), std::to_string(other->id));
            if (std::abs(rel) > 1.0f) {
                std::cout << other->name << "=" << static_cast<int>(rel) << " ";
            }
        }
        std::cout << "\n";

        // Memory decay status
        {
            const auto& mems = npc->memory.allMemories();
            if (!mems.empty()) {
                std::cout << "    Memory decay:\n";
                // Sort by strength ascending so weakest show first
                std::vector<const Memory*> sorted;
                for (const auto& m : mems) sorted.push_back(&m);
                std::sort(sorted.begin(), sorted.end(),
                    [](const Memory* a, const Memory* b){
                        return a->currentStrength < b->currentStrength; });
                int shown2 = 0;
                for (const auto* m : sorted) {
                    if (shown2++ >= 5) break;
                    float s = m->currentStrength;
                    const char* col;
                    const char* stage;
                    if (s > 0.9f)       { col = "\033[0;37m";  stage = "intact";           }
                    else if (s > 0.35f) { col = "\033[0;33m";  stage = "fading";            }
                    else if (s > 0.0f)  { col = "\033[1;35m";  stage = "nearly forgotten";  }
                    else                { col = "\033[0;90m";   stage = "forgotten";         }
                    char bar[6]; bar[5] = '\0';
                    int filled = static_cast<int>(std::round(s * 5.0f));
                    for (int i = 0; i < 5; ++i)
                        bar[i] = (i < filled) ? '\xe2' : '\xe2'; // placeholder
                    // Build bar manually
                    std::string sbar;
                    int f2 = std::max(0, std::min(5, static_cast<int>(std::round(s * 5.0f))));
                    for (int i = 0; i < f2; ++i)     sbar += "\u2588";
                    for (int i = f2; i < 5; ++i)     sbar += "\u2591";

                    std::string desc2 = m->description;
                    if (desc2.size() > 40) desc2 = desc2.substr(0, 37) + "...";
                    std::cout << "      " << col << sbar << " [" << stage << "] "
                              << "\"" << desc2 << "\""
                              << "\033[0m" << "\n";
                }
            }
        }

        // Recent memories
        auto memories = npc->memory.allMemories();
        if (!memories.empty()) {
            std::cout << "    Recent memories: ";
            int shown = 0;
            for (auto it = memories.rbegin(); it != memories.rend() && shown < 3; ++it, ++shown) {
                std::cout << "\"" << it->description << "\" ";
            }
            std::cout << "\n";
        }
        std::cout << "\n";
    }

    printInfluenceChainSummary(world);

    std::cout << "=== Final Village Map ===\n";
    world.printMap();
    std::cout << "\n  Phase 2 Simulation complete.\n";

    return 0;
}

// =======================================================================
//  EMOTION CONTAGION
// =======================================================================

// ANSI color codes per emotion type
static const char* emotionColor(EmotionType e) {
    switch (e) {
        case EmotionType::Happy:     return "\033[1;33m"; // bright yellow
        case EmotionType::Sad:       return "\033[1;34m"; // bright blue
        case EmotionType::Angry:     return "\033[1;31m"; // bright red
        case EmotionType::Fearful:   return "\033[1;35m"; // bright magenta
        case EmotionType::Disgusted: return "\033[0;32m"; // green
        case EmotionType::Surprised: return "\033[1;36m"; // bright cyan
        case EmotionType::Neutral:   return "\033[0;37m"; // grey
    }
    return "\033[0m";
}
static const char* RESET = "\033[0m";

// Intensity bar: 5 blocks, each block = 0.2
static std::string intensityBar(float intensity) {
    int filled = static_cast<int>(std::round(intensity * 5.0f));
    filled = std::max(0, std::min(5, filled));
    std::string bar;
    for (int i = 0; i < filled;     ++i) bar += "\u2588"; // █
    for (int i = filled; i < 5; ++i) bar += "\u2591"; // ░
    return bar;
}

// Max contagion range in world units
static constexpr float CONTAGION_RANGE = 8.0f;

void processEmotionContagion(GameWorld& world) {
    const auto& npcs = world.npcs();
    for (size_t i = 0; i < npcs.size(); ++i) {
        auto& source = *npcs[i];
        if (source.type == NPCType::Enemy) continue;

        auto aura = source.emotions.getEmotionalAura();
        if (aura.type == EmotionType::Neutral || aura.intensity < 0.1f) continue;

        for (size_t j = 0; j < npcs.size(); ++j) {
            if (i == j) continue;
            auto& receiver = *npcs[j];
            if (receiver.type == NPCType::Enemy) continue;

            float dist = source.position.distanceTo(receiver.position);
            if (dist >= CONTAGION_RANGE) continue;

            float proximity = 1.0f - (dist / CONTAGION_RANGE);
            float empathy   = receiver.personality.empathyMultiplier();
            receiver.emotions.applyContagion(aura.type, aura.intensity, empathy, proximity);
        }
    }
}

void printEmotionContagionTable(const std::string& timeStr, GameWorld& world) {
    const auto& npcs = world.npcs();

    // Collect only village NPCs
    std::vector<std::shared_ptr<NPC>> village;
    for (const auto& npc : npcs) {
        if (npc->type != NPCType::Enemy) village.push_back(npc);
    }
    if (village.empty()) return;

    std::cout << "\n  \033[1;37m┌─────────────────────────────────────────────────────────────────┐\033[0m\n";
    std::cout <<   "  \033[1;37m│  EMOTION CONTAGION MAP  ──  " << timeStr
              << std::string(37 - timeStr.size(), ' ') << "│\033[0m\n";
    std::cout <<   "  \033[1;37m├──────────────┬────────────┬───────┬────────────────────────────┤\033[0m\n";
    std::cout <<   "  \033[1;37m│ NPC          │ Emotion    │ Intens│ Spreading to...            │\033[0m\n";
    std::cout <<   "  \033[1;37m├──────────────┼────────────┼───────┼────────────────────────────┤\033[0m\n";

    for (const auto& npc : village) {
        auto aura = npc->emotions.getEmotionalAura();
        std::string emotionStr = emotionToString(aura.type);

        // Pad name and emotion to fixed widths
        std::string nameCol = npc->name;
        nameCol.resize(12, ' ');
        std::string emotCol = emotionStr;
        emotCol.resize(10, ' ');

        std::string bar = intensityBar(aura.intensity);

        // Find nearby NPCs this NPC is infecting
        std::string targets;
        for (const auto& other : village) {
            if (other->id == npc->id) continue;
            float dist = npc->position.distanceTo(other->position);
            if (dist < CONTAGION_RANGE && aura.type != EmotionType::Neutral && aura.intensity >= 0.1f) {
                float prox = 1.0f - (dist / CONTAGION_RANGE);
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%s(%.1f) ", other->name.c_str(), prox);
                targets += buf;
            }
        }
        if (targets.empty()) targets = "—";
        if (targets.size() > 26) targets = targets.substr(0, 23) + "...";
        targets.resize(26, ' ');

        std::cout << "  \033[1;37m│\033[0m "
                  << emotionColor(aura.type) << nameCol << RESET
                  << " \033[1;37m│\033[0m "
                  << emotionColor(aura.type) << emotCol << RESET
                  << " \033[1;37m│\033[0m "
                  << emotionColor(aura.type) << bar << RESET
                  << " \033[1;37m│\033[0m "
                  << targets
                  << "\033[1;37m│\033[0m\n";
    }

    std::cout << "  \033[1;37m└──────────────┴────────────┴───────┴────────────────────────────┘\033[0m\n\n";
}

// =======================================================================
//  MEMORY DECAY NARRATIVE
// =======================================================================

void processMemoryDecayNarrative(const std::string& timeStr, GameWorld& world) {
    for (const auto& npc : world.npcs()) {
        if (npc->type == NPCType::Enemy) continue;

        auto events = npc->memory.drainFadeEvents();
        for (const auto& ev : events) {
            const Memory& m = ev.snapshot;

            // Truncate long descriptions for readability
            std::string desc = m.description;
            if (desc.size() > 48) desc = desc.substr(0, 45) + "...";

            // Source tag
            const char* srcTag = (m.source == MemorySource::Hearsay)
                                 ? " \033[0;90m(hearsay)\033[0m" : "";

            switch (ev.stage) {
                case MemoryFadeStage::Fading: {
                    // Yellow italic — something is slipping away
                    std::cout << "[" << timeStr << "] "
                              << "\033[0;33m"
                              << npc->name << " finds the memory of \""
                              << desc << "\" growing hazy."
                              << "\033[0m" << srcTag << "\n";
                    // Entity-specific message
                    if (m.entityId.has_value()) {
                        for (const auto& other : world.npcs()) {
                            if (other->id == *m.entityId) {
                                std::cout << "  \033[0;33m"
                                          << "  \u2514 Their recollection of "
                                          << other->name << " is starting to blur."
                                          << "\033[0m\n";
                                break;
                            }
                        }
                    }
                    break;
                }
                case MemoryFadeStage::NearlyForgotten: {
                    // Magenta — barely a trace left
                    std::cout << "[" << timeStr << "] "
                              << "\033[1;35m"
                              << npc->name << " can barely recall \""
                              << desc << "\"."
                              << "\033[0m" << srcTag << "\n";
                    if (m.entityId.has_value()) {
                        for (const auto& other : world.npcs()) {
                            if (other->id == *m.entityId) {
                                std::cout << "  \033[1;35m"
                                          << "  \u2514 " << other->name
                                          << "'s face is almost unfamiliar to them now."
                                          << "\033[0m\n";
                                break;
                            }
                        }
                    }
                    break;
                }
                case MemoryFadeStage::Forgotten: {
                    // Dark grey strikethrough-style — gone
                    std::cout << "[" << timeStr << "] "
                              << "\033[0;90m"
                              << npc->name << " has forgotten: \""
                              << desc << "\"."
                              << "\033[0m" << srcTag << "\n";
                    if (m.entityId.has_value()) {
                        for (const auto& other : world.npcs()) {
                            if (other->id == *m.entityId) {
                                std::cout << "  \033[0;90m"
                                          << "  \u2514 " << npc->name
                                          << " no longer recognizes "
                                          << other->name << "."
                                          << "\033[0m\n";
                                break;
                            }
                        }
                    }
                    break;
                }
            }
        }
    }
}

// =======================================================================
//  SOCIAL INFLUENCE CHAINS
// =======================================================================

static constexpr float INFLUENCE_SOCIAL_RANGE = 6.0f;  // units
static constexpr float INFLUENCE_SPREAD_CHANCE = 0.08f; // per step per pair

// Per-step deduplication: don't spread the same message through the same
// directed pair twice within one sim tick.
static std::set<std::string> s_influencedThisStep;

static std::string influenceColor(float charge) {
    if (charge >  0.4f) return "\033[1;32m";  // bright green  — inspiring
    if (charge >  0.0f) return "\033[0;33m";  // yellow        — mildly positive
    if (charge > -0.4f) return "\033[0;35m";  // magenta       — unsettling
    return                      "\033[1;31m"; // bright red    — alarming
}

static std::string reliabilityBar(float r) {
    int f = std::max(0, std::min(5, static_cast<int>(std::round(r * 5.0f))));
    std::string s;
    for (int i = 0; i < f; ++i)     s += "\u2588";
    for (int i = f; i < 5; ++i)     s += "\u2591";
    return s;
}

void processInfluenceChains(const std::string& timeStr, GameWorld& world) {
    s_influencedThisStep.clear();
    const auto& npcs = world.npcs();

    for (size_t i = 0; i < npcs.size(); ++i) {
        auto& sender = *npcs[i];
        if (sender.type == NPCType::Enemy) continue;
        if (!sender.combat.stats.isAlive()) continue;

        for (size_t j = 0; j < npcs.size(); ++j) {
            if (i == j) continue;
            auto& receiver = *npcs[j];
            if (receiver.type == NPCType::Enemy) continue;
            if (!receiver.combat.stats.isAlive()) continue;

            float dist = sender.position.distanceTo(receiver.position);
            if (dist > INFLUENCE_SOCIAL_RANGE) continue;

            // Probability gate: close proximity boosts chance
            float proximity  = 1.0f - (dist / INFLUENCE_SOCIAL_RANGE);
            float baseChance = INFLUENCE_SPREAD_CHANCE
                             + proximity * 0.06f
                             + sender.personality.sociability * 0.04f;

            // NPC must be in a social state (not sleeping/fleeing) to gossip
            const StateId& st = sender.fsm.currentState();
            if (st == "Sleep" || st == "Flee") continue;

            float roll = static_cast<float>(std::rand()) / RAND_MAX;
            if (roll > baseChance) continue;

            float rel = g_relationships.getValue(
                std::to_string(sender.id), std::to_string(receiver.id));
            if (rel < -20.0f) continue; // hostile pairs don't share intel

            // Try each active influence message
            for (auto& msg : const_cast<std::vector<InfluenceMessage>&>(
                                 g_influence.messages())) {
                if (world.time().totalHours() > msg.expiresAt) continue;
                if (!msg.hasReached(sender.id))   continue; // sender doesn't have it
                if (msg.hasReached(receiver.id))  continue; // receiver already has it

                // Dedup within this step
                std::string pairKey = msg.id + "|" + std::to_string(sender.id)
                                    + ">" + std::to_string(receiver.id);
                if (s_influencedThisStep.count(pairKey)) continue;
                s_influencedThisStep.insert(pairKey);

                // ── Propagation math ──────────────────────────────────────
                // Reliability decays per hop; intelligent receivers are skeptical
                float newReliability = msg.reliability * 0.82f
                    * (1.0f - receiver.personality.intelligence * 0.15f);
                newReliability = std::max(0.0f, newReliability);

                // Sender willingness: impulsive (low patience) and social NPCs
                // spread faster; greedy NPCs hoard info
                float willingness = 0.4f
                    + sender.personality.sociability * 0.3f
                    + (1.0f - sender.personality.patience) * 0.2f
                    - sender.personality.greed * 0.15f
                    + (rel / 200.0f); // relationship bonus
                willingness = std::clamp(willingness, 0.05f, 0.95f);

                // Acceptance: intelligent receivers demand higher reliability
                float acceptThreshold = 0.08f
                    + receiver.personality.intelligence * 0.35f;
                bool accepted = (newReliability >= acceptThreshold)
                              && (willingness >= 0.4f);

                // ── Charge transformation ─────────────────────────────────
                float empathy   = receiver.personality.empathyMultiplier();
                float chargeOut = msg.charge * (0.65f + empathy * 0.35f);

                // Distortion: very unreliable messages can get garbled
                if (newReliability < 0.30f) {
                    float distort = (0.30f - newReliability) / 0.30f;
                    chargeOut *= (1.0f - distort * 1.4f);
                }
                chargeOut = std::clamp(chargeOut, -1.0f, 1.0f);

                // ── Narrative reaction line ───────────────────────────────
                std::string reaction;
                if (!accepted) {
                    reaction = receiver.name + " shrugs off what " + sender.name + " says.";
                } else {
                    if      (chargeOut < -0.65f) reaction = receiver.name + " goes pale with alarm.";
                    else if (chargeOut < -0.30f) reaction = receiver.name + " looks uneasy.";
                    else if (chargeOut < -0.05f) reaction = receiver.name + " frowns quietly.";
                    else if (chargeOut <  0.15f) reaction = receiver.name + " nods slowly.";
                    else if (chargeOut <  0.55f) reaction = receiver.name + " seems encouraged.";
                    else                         reaction = receiver.name + " lights up with pride.";
                }

                // ── Apply to receiver ─────────────────────────────────────
                if (accepted) {
                    Memory sourceMemory;
                    sourceMemory.type           = MemoryType::WorldEvent;
                    sourceMemory.description    = msg.topic + " (from " + sender.name + ")";
                    sourceMemory.emotionalImpact = chargeOut;
                    sourceMemory.importance     = 0.3f + std::abs(chargeOut) * 0.5f;
                    sourceMemory.timestamp      = world.time().totalHours();
                    sourceMemory.reliability    = newReliability;
                    sourceMemory.decayRate      = 0.01f;
                    sourceMemory.currentStrength = 1.0f;
                    receiver.memory.receiveGossip(sourceMemory, sender.id,
                        std::clamp(rel, -100.0f, 100.0f),
                        world.time().totalHours(), 1);

                    EmotionType emoType = (chargeOut < -0.35f) ? EmotionType::Fearful
                                       : (chargeOut < -0.05f) ? EmotionType::Sad
                                       : (chargeOut >  0.45f) ? EmotionType::Happy
                                       :                        EmotionType::Surprised;
                    if (std::abs(chargeOut) > 0.15f)
                        receiver.emotions.addEmotion(emoType,
                            std::abs(chargeOut) * 0.55f, 2.0f);

                    // Update the message's evolution
                    g_influence.recordHop(msg.id, receiver.id, receiver.name,
                                          newReliability, chargeOut);
                }

                // ── Print hop ─────────────────────────────────────────────
                const char* col = influenceColor(msg.charge).c_str();
                // rebuild col properly:
                std::string colStr = influenceColor(msg.charge);
                std::cout << "[" << timeStr << "] "
                          << "\033[1;36m" << "\u27f6 INFLUENCE"
                          << "\033[0m"
                          << " \033[0;90m[" << msg.id << "]\033[0m"
                          << " \"" << colStr << msg.topic << "\033[0m\"\n"
                          << "         "
                          << "\033[1;37m" << sender.name << "\033[0m"
                          << colStr << " \u2500\u2500[rel:"
                          << static_cast<int>(rel)
                          << " hop:" << (msg.hopCount + 1)
                          << "]\u2500\u2500\u25b6 " << "\033[0m"
                          << "\033[1;37m" << receiver.name << "\033[0m"
                          << "  rel:" << reliabilityBar(newReliability)
                          << "(" << std::fixed << std::setprecision(2)
                          << newReliability << ")"
                          << "  charge:" << (chargeOut >= 0 ? "+" : "")
                          << std::setprecision(2) << chargeOut
                          << (accepted ? "" : "  \033[0;90m[rejected]\033[0m")
                          << "\n"
                          << "         \u2514 " << reaction << "\n";

                // Show current chain
                if (accepted) {
                    std::cout << "         \033[0;90m"
                              << "Chain: " << msg.chainString()
                              << "\033[0m\n";
                }
                std::cout << "\n";
            }
        }
    }
}

void printInfluenceChainSummary(GameWorld& world) {
    const auto& msgs = g_influence.messages();
    if (msgs.empty()) return;

    std::cout << "\n\033[1;36m"
              << "========================================\n"
              << "  SOCIAL INFLUENCE CHAIN SUMMARY\n"
              << "========================================\033[0m\n\n";

    for (const auto& msg : msgs) {
        std::string colStr = influenceColor(msg.charge);
        int reached = static_cast<int>(msg.reachedIds.size());
        int total   = static_cast<int>(world.npcs().size());
        // exclude enemies from total
        int villagers = 0;
        for (const auto& n : world.npcs())
            if (n->type != NPCType::Enemy) ++villagers;

        std::cout << "  " << colStr << "\u25cf " << msg.topic << "\033[0m\n";
        std::cout << "    Seeded by : " << msg.originatorName << "\n";
        std::cout << "    Reached   : " << reached << " / " << villagers
                  << " villagers  (" << msg.hopCount << " hops)\n";
        std::cout << "    Reliability at last hop: "
                  << reliabilityBar(msg.reliability)
                  << " " << std::fixed << std::setprecision(2) << msg.reliability << "\n";
        std::cout << "    Charge drift: "
                  << influenceColor(msg.charge)
                  << (msg.charge >= 0 ? "+" : "") << msg.charge
                  << "\033[0m\n";
        std::cout << "    Propagation chain:\n      "
                  << "\033[1;37m" << msg.chainString() << "\033[0m\n\n";
    }
}

// =======================================================================
//  WORLD SETUP
// =======================================================================

void buildVillageMap(GameWorld& world) {
    // Roads (horizontal main road)
    for (int x = 0; x < 40; ++x) {
        world.setCell(x, 12, CellType::Road, 0.8f);
        world.setCell(x, 13, CellType::Road, 0.8f);
    }
    // Vertical road
    for (int y = 5; y < 20; ++y) {
        world.setCell(20, y, CellType::Road, 0.8f);
        world.setCell(21, y, CellType::Road, 0.8f);
    }

    // Tavern (top-left area)
    for (int y = 3; y <= 6; ++y)
        for (int x = 5; x <= 10; ++x)
            world.setCell(x, y, CellType::Building, 999.0f, false);
    world.setCell(8, 6, CellType::Door, 1.0f, true);

    // Forge (left of road)
    for (int y = 8; y <= 10; ++y)
        for (int x = 14; x <= 18; ++x)
            world.setCell(x, y, CellType::Building, 999.0f, false);
    world.setCell(16, 10, CellType::Door, 1.0f, true);

    // Market (right of crossroad)
    for (int y = 10; y <= 11; ++y)
        for (int x = 23; x <= 27; ++x)
            world.setCell(x, y, CellType::Building, 999.0f, false);
    world.setCell(25, 11, CellType::Door, 1.0f, true);

    // Houses
    for (int y = 16; y <= 18; ++y)
        for (int x = 5; x <= 8; ++x)
            world.setCell(x, y, CellType::Building, 999.0f, false);
    for (int y = 16; y <= 18; ++y)
        for (int x = 25; x <= 28; ++x)
            world.setCell(x, y, CellType::Building, 999.0f, false);

    // Farm area (bottom-right)
    for (int y = 19; y <= 23; ++y)
        for (int x = 30; x <= 38; ++x)
            world.setCell(x, y, CellType::Grass, 1.2f);

    // Forest (right edge)
    for (int y = 0; y <= 24; ++y)
        for (int x = 37; x <= 39; ++x)
            world.setCell(x, y, CellType::Forest, 2.0f);

    // Water (small pond)
    for (int y = 1; y <= 2; ++y)
        for (int x = 30; x <= 33; ++x)
            world.setCell(x, y, CellType::Water, 999.0f, false);

    // Village gate
    for (int x = 0; x <= 3; ++x) {
        world.setCell(x, 12, CellType::Wall, 999.0f, false);
        world.setCell(x, 13, CellType::Wall, 999.0f, false);
    }
    world.setCell(3, 12, CellType::Door, 1.0f, true);
    world.setCell(3, 13, CellType::Door, 1.0f, true);

    // Named locations
    world.addLocation("Tavern",     8.0f,  7.0f);
    world.addLocation("TavernRoom", 7.0f,  4.0f);
    world.addLocation("Forge",      16.0f, 11.0f);
    world.addLocation("SmithHouse", 6.0f,  17.0f);
    world.addLocation("Market",     25.0f, 12.0f);
    world.addLocation("MerchHouse", 26.0f, 17.0f);
    world.addLocation("FarmHouse",  26.0f, 17.0f);
    world.addLocation("Farm",       34.0f, 21.0f);
    world.addLocation("Square",     20.0f, 12.0f);
    world.addLocation("Gate",       3.0f,  12.0f);
    world.addLocation("Village",    20.0f, 12.0f);
    world.addLocation("ForestEdge", 36.0f, 12.0f);
}

void setupFactions(FactionSystem& factions) {
    factions.addFaction(VILLAGE_FACTION, "Village");
    factions.addFaction(WOLF_FACTION, "Wolves");
    factions.setRelation(VILLAGE_FACTION, WOLF_FACTION, -100.0f);
}

void setupItems(TradeSystem& trade) {
    trade.registerItem({ITEM_SWORD,      "Iron Sword",     ItemCategory::Weapon,   50.0f, 3.0f});
    trade.registerItem({ITEM_SHIELD,     "Wooden Shield",  ItemCategory::Armor,    30.0f, 4.0f});
    trade.registerItem({ITEM_BREAD,      "Fresh Bread",    ItemCategory::Food,      3.0f, 0.2f});
    trade.registerItem({ITEM_ALE,        "Ale",            ItemCategory::Food,      5.0f, 0.5f});
    trade.registerItem({ITEM_IRON_ORE,   "Iron Ore",       ItemCategory::Material, 15.0f, 5.0f});
    trade.registerItem({ITEM_HORSESHOE,  "Horseshoe",      ItemCategory::Tool,     12.0f, 1.0f});
    trade.registerItem({ITEM_HEALTH_POT, "Health Potion",  ItemCategory::Potion,   25.0f, 0.3f});
    trade.registerItem({ITEM_WHEAT,      "Wheat",          ItemCategory::Food,      2.0f, 1.0f});
    trade.registerItem({ITEM_LEATHER,    "Leather",        ItemCategory::Material,  8.0f, 1.5f});
    trade.registerItem({ITEM_TOOLS,      "Farming Tools",  ItemCategory::Tool,     20.0f, 3.0f});
}

void setupAllItems(TradeSystem& trade) {
    setupItems(trade);
    trade.registerItem({ITEM_ENCHANTED_SWORD, "Enchanted Sword", ItemCategory::Weapon, 150.0f, 3.0f});
    trade.registerItem({ITEM_EXOTIC_SPICES,   "Exotic Spices",   ItemCategory::Food,    40.0f, 0.5f});
}

// =======================================================================
//  UTILITY AI SETUP - per NPC type weights
// =======================================================================

void setupUtilityAI(NPC& npc) {
    npc.useUtilityAI = true;

    // Weight tables per type
    float fightW = 1.0f, fleeW = 1.0f, patrolW = 0.0f, workW = 0.0f;
    float tradeW = 0.0f, eatW = 1.0f, socializeW = 0.8f, sleepW = 1.0f;

    switch (npc.type) {
        case NPCType::Guard:
            fightW = 2.0f; fleeW = 0.3f; patrolW = 1.5f;
            eatW = 1.0f; socializeW = 0.8f;
            break;
        case NPCType::Blacksmith:
            fightW = 0.8f; fleeW = 1.0f; workW = 1.0f;
            eatW = 1.0f; socializeW = 1.0f;
            break;
        case NPCType::Merchant:
            fightW = 0.2f; fleeW = 1.5f; tradeW = 1.5f;
            eatW = 1.0f; socializeW = 0.8f;
            break;
        case NPCType::Innkeeper:
            fightW = 0.3f; fleeW = 1.2f; workW = 1.2f;
            eatW = 1.0f; socializeW = 1.0f;
            break;
        case NPCType::Farmer:
            fightW = 0.3f; fleeW = 2.0f; workW = 1.0f;
            tradeW = 0.5f; eatW = 1.0f; socializeW = 1.0f;
            break;
        default: break;
    }

    // Personality modulation of base weights
    float c = npc.personality.courage;
    float s = npc.personality.sociability;
    float g = npc.personality.greed;
    float p = npc.personality.patience;

    fightW *= (0.5f + c);       // courage=1 -> 1.5x, courage=0 -> 0.5x
    fleeW  *= (1.5f - c);       // courage=1 -> 0.5x, courage=0 -> 1.5x
    socializeW *= (0.5f + s);   // sociability=1 -> 1.5x
    tradeW *= (0.5f + g);       // greed=1 -> 1.5x
    workW   *= (0.5f + p);      // patience=1 -> 1.5x
    patrolW *= (0.5f + p);

    // Fight action
    npc.utilityAI.addAction("fight",
        [](const Blackboard& bb) -> float {
            if (!bb.getOr<bool>("has_threats", false)) return 0.0f;
            float hp = bb.getOr<float>("health_pct", 1.0f);
            return 0.5f + hp * 0.5f;
        },
        [&npc](Blackboard& bb) {
            bb.set<std::string>("utility_desired_state", "Combat");
        }, fightW);

    // Flee action
    npc.utilityAI.addAction("flee",
        [](const Blackboard& bb) -> float {
            if (!bb.getOr<bool>("has_threats", false)) return 0.0f;
            float flee = bb.getOr<float>("flee_modifier", 0.0f);
            float hp = bb.getOr<float>("health_pct", 1.0f);
            return flee * 0.6f + (1.0f - hp) * 0.4f;
        },
        [&npc](Blackboard& bb) {
            bb.set<std::string>("utility_desired_state", "Flee");
        }, fleeW);

    // Patrol action (guard only)
    if (patrolW > 0.0f) {
        npc.utilityAI.addAction("patrol",
            [](const Blackboard& bb) -> float {
                auto act = bb.get<std::string>("scheduled_activity");
                if (act && *act == "Patrol") return 0.7f;
                return 0.0f;
            },
            [](Blackboard& bb) {
                bb.set<std::string>("utility_desired_state", "Patrol");
                bb.set<bool>("wants_patrol", true);
            }, patrolW);
    }

    // Work action
    if (workW > 0.0f) {
        npc.utilityAI.addAction("work",
            [](const Blackboard& bb) -> float {
                auto act = bb.get<std::string>("scheduled_activity");
                if (act && *act == "Work") return 0.7f;
                return 0.0f;
            },
            [](Blackboard& bb) {
                bb.set<std::string>("utility_desired_state", "Work");
            }, workW);
    }

    // Trade action
    if (tradeW > 0.0f) {
        npc.utilityAI.addAction("trade",
            [](const Blackboard& bb) -> float {
                auto act = bb.get<std::string>("scheduled_activity");
                if (act && *act == "Trade") return 0.7f;
                return 0.0f;
            },
            [](Blackboard& bb) {
                bb.set<std::string>("utility_desired_state", "Trade");
            }, tradeW);
    }

    // Eat action
    npc.utilityAI.addAction("eat",
        [](const Blackboard& bb) -> float {
            auto act = bb.get<std::string>("scheduled_activity");
            float hunger = bb.getOr<float>("hunger_urgency", 0.0f);
            if (act && *act == "Eat") return 0.6f + hunger * 0.4f;
            if (hunger > 0.7f) return hunger;
            return 0.0f;
        },
        [](Blackboard& bb) {
            bb.set<std::string>("utility_desired_state", "Eat");
        }, eatW);

    // Socialize action
    npc.utilityAI.addAction("socialize",
        [](const Blackboard& bb) -> float {
            auto act = bb.get<std::string>("scheduled_activity");
            float social = bb.getOr<float>("social_urgency", 0.0f);
            if (act && *act == "Socialize") return 0.6f + social * 0.4f;
            if (social > 0.7f) return social * 0.5f;
            return 0.0f;
        },
        [](Blackboard& bb) {
            bb.set<std::string>("utility_desired_state", "Socialize");
        }, socializeW);

    // Sleep action
    npc.utilityAI.addAction("sleep",
        [](const Blackboard& bb) -> float {
            auto act = bb.get<std::string>("scheduled_activity");
            float sleepUrg = bb.getOr<float>("sleep_urgency", 0.0f);
            if (act && (*act == "Sleep" || *act == "Guard")) return 0.7f;
            if (sleepUrg > 0.8f) return sleepUrg;
            return 0.0f;
        },
        [](Blackboard& bb) {
            bb.set<std::string>("utility_desired_state", "Sleep");
        }, sleepW);
}

// =======================================================================
//  COMBAT BEHAVIOR TREE - Alaric's tactical combat AI
// =======================================================================

void setupCombatBT(NPC& npc, GameWorld& world) {
    BehaviorTreeBuilder builder;

    npc.combatBT = builder
        .selector("CombatRoot")
            // Branch 1: Heal check
            .sequence("HealCheck")
                .condition("HP low?", [&npc](const Blackboard& bb) {
                    return bb.getOr<float>("health_pct", 1.0f) < npc.personality.healThreshold();
                })
                .action("UseHealPotion", [&npc](Blackboard& bb) -> NodeStatus {
                    auto* healAbility = npc.combat.selectHealAbility();
                    if (healAbility) {
                        float healed = npc.combat.heal(*healAbility);
                        std::cout << "[" << formatTime(bb.getOr<float>("_time", 0.0f))
                                  << "] " << npc.name << " uses Health Potion! Healed "
                                  << static_cast<int>(healed) << " HP. HP: "
                                  << static_cast<int>(npc.combat.stats.health) << "/"
                                  << static_cast<int>(npc.combat.stats.maxHealth) << "\n";
                        return NodeStatus::Success;
                    }
                    return NodeStatus::Failure;
                })
            .end()
            // Branch 2: Flank and Attack
            .sequence("FlankAndAttack")
                .condition("HasTarget?", [&npc](const Blackboard& bb) {
                    return npc.combat.selectTarget().has_value();
                })
                .selector("PositionChoice")
                    .sequence("Flank")
                        .condition("CanFlank?", [&npc](const Blackboard& bb) {
                            auto target = npc.combat.selectTarget();
                            if (!target) return false;
                            float dist = npc.position.distanceTo(target->position);
                            float minFlankDist = 3.0f * (1.5f - npc.personality.patience);
                            return dist > minFlankDist && dist < 10.0f;
                        })
                        .action("MoveToFlank", [&npc](Blackboard& bb) -> NodeStatus {
                            auto target = npc.combat.selectTarget();
                            if (!target) return NodeStatus::Failure;
                            Vec2 flankPos = npc.combat.getFlankPosition(
                                npc.position, target->position);
                            npc.moveTo(flankPos);
                            std::cout << "[" << formatTime(bb.getOr<float>("_time", 0.0f))
                                      << "] " << npc.name
                                      << " flanking to better position.\n";
                            return NodeStatus::Success;
                        })
                    .end()
                    .action("ApproachTarget", [&npc](Blackboard& bb) -> NodeStatus {
                        auto target = npc.combat.selectTarget();
                        if (!target) return NodeStatus::Failure;
                        float dist = npc.position.distanceTo(target->position);
                        if (dist > 2.0f) {
                            npc.moveTo(target->position);
                        }
                        return NodeStatus::Success;
                    })
                .end()
                .selector("AttackChoice")
                    .sequence("StrongAttack")
                        .condition("SwordStrikeReady?", [&npc](const Blackboard& bb) {
                            for (const auto& ab : npc.combat.stats.abilities) {
                                if (ab.name == "Sword Strike" && ab.isReady())
                                    return true;
                            }
                            return false;
                        })
                        .action("SwordStrike", [&npc, &world](Blackboard& bb) -> NodeStatus {
                            auto target = npc.combat.selectTarget();
                            if (!target) return NodeStatus::Failure;
                            auto* enemy = world.findNPC(target->entityId);
                            if (!enemy || !enemy->combat.stats.isAlive())
                                return NodeStatus::Failure;
                            float dist = npc.position.distanceTo(enemy->position);
                            const Ability* ability = nullptr;
                            for (const auto& ab : npc.combat.stats.abilities) {
                                if (ab.name == "Sword Strike" && ab.isReady()) {
                                    ability = &ab;
                                    break;
                                }
                            }
                            if (!ability || dist > ability->range + 2.0f) return NodeStatus::Failure;
                            npc.isMoving = false;
                            auto result = npc.combat.dealDamage(enemy->combat, *ability);
                            std::cout << "[" << formatTime(bb.getOr<float>("_time", 0.0f))
                                      << "] " << npc.name << " uses Sword Strike on "
                                      << enemy->name << "! " << static_cast<int>(result.damageDealt)
                                      << " damage" << (result.isCrit ? " (CRIT!)" : "");
                            if (result.resistanceMultiplier < 0.9f) std::cout << " (RESISTED)";
                            else if (result.resistanceMultiplier > 1.1f) std::cout << " (WEAK!)";
                            std::cout << ". " << enemy->name << " HP: "
                                      << static_cast<int>(enemy->combat.stats.health) << "/"
                                      << static_cast<int>(enemy->combat.stats.maxHealth) << "\n";
                            if (result.targetKilled) {
                                std::cout << "[" << formatTime(bb.getOr<float>("_time", 0.0f))
                                          << "] " << enemy->name << " DEFEATED!\n";
                                npc.memory.addMemory(MemoryType::Combat,
                                    "Defeated " + enemy->name, 0.5f,
                                    enemy->id, 0.9f, bb.getOr<float>("_time", 0.0f));
                                npc.emotions.addEmotion(EmotionType::Happy, 0.4f, 1.0f);
                                world.events().publish(CombatEvent{
                                    npc.id, enemy->id, result.damageDealt, true, npc.position});
                                // Check wolf alpha
                                if (enemy->name == "Wolf_1") {
                                    g_wolfPackMoraleBroken = true;
                                    std::cout << "[" << formatTime(bb.getOr<float>("_time", 0.0f))
                                              << "] Wolf pack morale broken! Alpha is down!\n";
                                }
                            }
                            return NodeStatus::Success;
                        })
                    .end()
                    .action("ShieldBash", [&npc, &world](Blackboard& bb) -> NodeStatus {
                        auto target = npc.combat.selectTarget();
                        if (!target) return NodeStatus::Failure;
                        auto* enemy = world.findNPC(target->entityId);
                        if (!enemy || !enemy->combat.stats.isAlive())
                            return NodeStatus::Failure;
                        float dist = npc.position.distanceTo(enemy->position);
                        const Ability* ability = nullptr;
                        for (const auto& ab : npc.combat.stats.abilities) {
                            if (ab.name == "Shield Bash" && ab.isReady()) {
                                ability = &ab;
                                break;
                            }
                        }
                        if (!ability || dist > ability->range + 2.0f) return NodeStatus::Failure;
                        npc.isMoving = false;
                        auto result = npc.combat.dealDamage(enemy->combat, *ability);
                        std::cout << "[" << formatTime(bb.getOr<float>("_time", 0.0f))
                                  << "] " << npc.name << " uses Shield Bash on "
                                  << enemy->name << "! " << static_cast<int>(result.damageDealt)
                                  << " damage";
                        if (result.resistanceMultiplier < 0.9f) std::cout << " (RESISTED)";
                        else if (result.resistanceMultiplier > 1.1f) std::cout << " (WEAK!)";
                        std::cout << ". " << enemy->name << " HP: "
                                  << static_cast<int>(enemy->combat.stats.health) << "/"
                                  << static_cast<int>(enemy->combat.stats.maxHealth) << "\n";
                        if (result.targetKilled) {
                            std::cout << "[" << formatTime(bb.getOr<float>("_time", 0.0f))
                                      << "] " << enemy->name << " DEFEATED!\n";
                            npc.memory.addMemory(MemoryType::Combat,
                                "Defeated " + enemy->name, 0.5f,
                                enemy->id, 0.9f, bb.getOr<float>("_time", 0.0f));
                            world.events().publish(CombatEvent{
                                npc.id, enemy->id, result.damageDealt, true, npc.position});
                            if (enemy->name == "Wolf_1") {
                                g_wolfPackMoraleBroken = true;
                                std::cout << "[" << formatTime(bb.getOr<float>("_time", 0.0f))
                                          << "] Wolf pack morale broken! Alpha is down!\n";
                            }
                        }
                        return NodeStatus::Success;
                    })
                .end()
            .end()
            // Branch 3: Fallback patrol
            .action("FallbackPatrol", [&npc](Blackboard& bb) -> NodeStatus {
                npc.moveTo(Vec2(20.0f, 12.0f));
                return NodeStatus::Success;
            })
        .end()
        .build();
}

// =======================================================================
//  RELATIONSHIP LOG HELPER
// =======================================================================

void logRelationship(const std::string& timeStr, const std::string& a,
                     const std::string& b, EntityId idA, EntityId idB, float delta,
                     const PersonalityTraits* traitsA,
                     const PersonalityTraits* traitsB) {
    float adjustedDelta = delta;
    if (delta > 0.0f) {
        float avgSocMul = 1.0f;
        if (traitsA) avgSocMul *= traitsA->relationshipGainMultiplier();
        if (traitsB) avgSocMul *= traitsB->relationshipGainMultiplier();
        if (traitsA && traitsB) avgSocMul = std::sqrt(avgSocMul);
        adjustedDelta *= avgSocMul;
    } else {
        float avgPatMul = 1.0f;
        if (traitsA) avgPatMul *= traitsA->negativeRelationshipMultiplier();
        if (traitsB) avgPatMul *= traitsB->negativeRelationshipMultiplier();
        if (traitsA && traitsB) avgPatMul = std::sqrt(avgPatMul);
        adjustedDelta *= avgPatMul;
    }

    float before = g_relationships.getValue(std::to_string(idA), std::to_string(idB));
    g_relationships.modifyValue(std::to_string(idA), std::to_string(idB), adjustedDelta);
    float after = g_relationships.getValue(std::to_string(idA), std::to_string(idB));
    std::cout << "[" << timeStr << "] " << a << "-" << b << " relationship: "
              << static_cast<int>(before) << " -> " << static_cast<int>(after)
              << " (" << (adjustedDelta >= 0 ? "+" : "")
              << static_cast<int>(adjustedDelta) << ")\n";
}

// =======================================================================
//  COMBAT ROUND - detailed wolf vs villager combat
// =======================================================================

void runCombatRound(GameWorld& world, const std::string& timeStr) {
    // Remove dead wolves from perception
    for (auto& npc : world.npcs()) {
        if (npc->type != NPCType::Enemy) {
            for (auto& wolf : world.npcs()) {
                if (wolf->type == NPCType::Enemy && !wolf->combat.stats.isAlive()) {
                    npc->perception.forgetEntity(wolf->id);
                }
            }
        }
    }

    // Check if all wolves are dead
    bool anyWolfAlive = false;
    for (auto& npc : world.npcs()) {
        if (npc->type == NPCType::Enemy && npc->combat.stats.isAlive()) {
            anyWolfAlive = true;
            break;
        }
    }

    if (!anyWolfAlive) {
        g_combatResolved = true;
        g_combatActive = false;

        // Clear threat flags and perception on all villagers
        for (auto& npc : world.npcs()) {
            if (npc->type != NPCType::Enemy) {
                npc->fsm.blackboard().set<bool>("has_threats", false);
                npc->combat.inCombat = false;
                for (auto& other : world.npcs()) {
                    if (other->type == NPCType::Enemy) {
                        npc->perception.forgetEntity(other->id);
                    }
                }
            }
        }

        auto* alaric = world.findNPC("Alaric");
        auto* brina = world.findNPC("Brina");
        if (alaric && brina) {
            std::cout << "[" << timeStr << "] " << alaric->name
                      << ": \"The village is safe! Well fought, Brina!\"\n";
            std::cout << "[" << timeStr << "] " << brina->name
                      << ": \"That was close. My hammer arm is sore.\"\n";
            logRelationship(timeStr, "Alaric", "Brina",
                            alaric->id, brina->id, 20.0f,
                            &alaric->personality, &brina->personality);
        }
        return;
    }

    // Wolves flee if morale broken
    if (g_wolfPackMoraleBroken) {
        for (auto& npc : world.npcs()) {
            if (npc->type == NPCType::Enemy && npc->combat.stats.isAlive()) {
                Vec2 fleeTarget(38.0f, 12.0f);
                npc->moveTo(fleeTarget);
                if (npc->position.distanceTo(fleeTarget) < 3.0f) {
                    npc->combat.stats.health = 0.0f; // fled the map
                    std::cout << "[" << timeStr << "] " << npc->name
                              << " flees into the forest!\n";
                } else {
                    std::cout << "[" << timeStr << "] " << npc->name
                              << " attempts to flee! Running toward forest!\n";
                }
            }
        }
        // No early return - continue to Alaric BT and Brina attacks
    }

    // Wolf attacks
    for (auto& wolf : world.npcs()) {
        if (wolf->type != NPCType::Enemy || !wolf->combat.stats.isAlive()) continue;

        // Find nearest villager
        float bestDist = 999.0f;
        NPC* nearest = nullptr;
        for (auto& other : world.npcs()) {
            if (other->type == NPCType::Enemy) continue;
            if (!other->combat.stats.isAlive()) continue;
            float d = wolf->position.distanceTo(other->position);
            if (d < bestDist) {
                bestDist = d;
                nearest = other.get();
            }
        }

        if (nearest && bestDist <= 2.5f) {
            auto* ability = wolf->combat.selectAbility(bestDist);
            if (ability) {
                auto result = wolf->combat.dealDamage(nearest->combat, *ability);
                std::cout << "[" << timeStr << "] " << wolf->name << " bites "
                          << nearest->name << "! " << static_cast<int>(result.damageDealt)
                          << " damage";
                if (result.resistanceMultiplier < 0.9f) std::cout << " (RESISTED)";
                else if (result.resistanceMultiplier > 1.1f) std::cout << " (WEAK!)";
                std::cout << ". " << nearest->name << " HP: "
                          << static_cast<int>(nearest->combat.stats.health) << "/"
                          << static_cast<int>(nearest->combat.stats.maxHealth) << "\n";
            }
        } else if (nearest && bestDist > 2.5f) {
            wolf->moveTo(nearest->position);
        }
    }

    // Alaric attacks via Behavior Tree
    {
        auto* alaric = world.findNPC("Alaric");
        if (alaric && alaric->combat.stats.isAlive()) {
            // Remove dead enemies from perception before evaluating
            for (auto& npc : world.npcs()) {
                if (npc->type == NPCType::Enemy && !npc->combat.stats.isAlive()) {
                    alaric->perception.forgetEntity(npc->id);
                }
            }
            std::vector<PerceivedEntity> pv;
            for (const auto& [id, pe] : alaric->perception.perceived()) pv.push_back(pe);
            alaric->combat.evaluateThreats(pv, alaric->position);
            if (alaric->combat.threatCount() > 0) {
                auto& bb = alaric->fsm.blackboard();
                bb.set<float>("_time", world.time().totalHours());
                bb.set<float>("health_pct", alaric->combat.stats.healthPercent());
                alaric->combatBT.tick(bb);
            }
        }
    }

    // Brina joins combat if Alaric is fighting and she has enough HP
    auto* brina = world.findNPC("Brina");
    auto* alaricPtr = world.findNPC("Alaric");
    if (brina && alaricPtr && brina->combat.stats.isAlive()
        && brina->combat.stats.healthPercent() > 0.5f
        && alaricPtr->combat.inCombat) {

        // Find a wolf to attack
        for (auto& wolf : world.npcs()) {
            if (wolf->type != NPCType::Enemy || !wolf->combat.stats.isAlive()) continue;
            float dist = brina->position.distanceTo(wolf->position);
            if (dist <= 2.5f) {
                auto* ability = brina->combat.selectAbility(dist);
                if (ability) {
                    auto result = brina->combat.dealDamage(wolf->combat, *ability);
                    std::cout << "[" << timeStr << "] " << brina->name
                              << " uses Hammer Strike on " << wolf->name << "! "
                              << static_cast<int>(result.damageDealt) << " damage";
                    if (result.resistanceMultiplier < 0.9f) std::cout << " (RESISTED)";
                    else if (result.resistanceMultiplier > 1.1f) std::cout << " (WEAK!)";
                    std::cout << ". " << wolf->name << " HP: "
                              << static_cast<int>(wolf->combat.stats.health) << "/"
                              << static_cast<int>(wolf->combat.stats.maxHealth) << "\n";
                    if (result.targetKilled) {
                        std::cout << "[" << timeStr << "] " << wolf->name
                                  << " DEFEATED by " << brina->name << "!\n";
                        brina->memory.addMemory(MemoryType::Combat,
                            "Defeated " + wolf->name + " with hammer", 0.5f,
                            wolf->id, 0.8f);
                        world.events().publish(CombatEvent{
                            brina->id, wolf->id, result.damageDealt, true, brina->position});
                        // Alpha wolf killed - break pack morale
                        if (wolf->name == "Wolf_1") {
                            g_wolfPackMoraleBroken = true;
                            std::cout << "[" << timeStr
                                      << "] Wolf pack morale broken! Alpha is down!\n";
                        }
                    }
                }
                break; // one attack per round
            } else {
                brina->moveTo(wolf->position);
                if (brina->fsm.currentState() != "Combat") {
                    std::cout << "[" << timeStr << "] " << brina->name
                              << ": Alaric is fighting! Grabbing hammer to help!\n";
                }
                break;
            }
        }
    }

    // Update perception so NPCs see wolves
    for (auto& npcPtr : world.npcs()) {
        if (npcPtr->type == NPCType::Enemy && npcPtr->combat.stats.isAlive()) {
            for (auto& other : world.npcs()) {
                if (other->type != NPCType::Enemy && other->combat.stats.isAlive()) {
                    npcPtr->perception.forceAwareness(
                        other->id, other->position, AwarenessLevel::Combat, true,
                        world.time().totalHours());
                }
            }
        }
        if (npcPtr->type != NPCType::Enemy) {
            for (auto& wolf : world.npcs()) {
                if (wolf->type == NPCType::Enemy && wolf->combat.stats.isAlive()) {
                    npcPtr->perception.forceAwareness(
                        wolf->id, wolf->position, AwarenessLevel::Combat, true,
                        world.time().totalHours());
                }
            }
        }
    }
}


// =======================================================================
//  NPC CREATION
// =======================================================================

std::shared_ptr<NPC> createAlaric(GameWorld& world, std::shared_ptr<Pathfinder> pf) {
    auto npc = std::make_shared<NPC>(1, "Alaric", NPCType::Guard);
    npc->position = Vec2(20.0f, 12.0f);
    npc->pathfinder = pf;
    npc->factionId = VILLAGE_FACTION;
    npc->personality = PersonalityTraits::guard();
    npc->emotions.applyPersonality(npc->personality);
    npc->combat.applyPersonality(
        npc->personality.fleeThresholdMultiplier(),
        npc->personality.healThreshold(),
        npc->personality.threatAwarenessMultiplier());
    npc->perception.config.sightRange *= npc->personality.sightRangeMultiplier();
    npc->perception.config.awarenessDecayRate *= npc->personality.awarenessDecayMultiplier();
    npc->memory = MemorySystem(static_cast<size_t>(50 * npc->personality.memoryCapacityMultiplier()));
    npc->schedule = ScheduleSystem::createGuardSchedule();

    // Combat stats - strong fighter with plate armor
    npc->combat.stats = {120.0f, 120.0f, 20.0f, 15.0f, 6.0f, 0.15f, {}};
    npc->combat.stats.stamina = {120.0f, 120.0f, 6.0f, 18.0f};
    npc->combat.stats.resistances = {0.6f, 1.0f, 1.2f, 1.0f, 0.8f}; // Plate: Physical-resistant, Fire-weak
    npc->combat.stats.abilities.push_back(
        {"Sword Strike", AbilityType::Melee, DamageType::Physical, 15.0f, 2.0f, 0.05f, 0.0f, 0.0f, 0.0f, 10.0f});
    npc->combat.stats.abilities.push_back(
        {"Shield Bash", AbilityType::Melee, DamageType::Physical, 8.0f, 1.5f, 0.1f, 0.0f, 0.0f, 0.0f, 15.0f});

    // Patrol waypoints
    std::vector<Vec2> patrolRoute = {
        Vec2(5.0f, 12.0f), Vec2(20.0f, 12.0f),
        Vec2(35.0f, 12.0f), Vec2(20.0f, 6.0f),
        Vec2(20.0f, 18.0f), Vec2(20.0f, 12.0f)
    };
    int patrolIdx = 0;

    // FSM States
    npc->fsm.addState("Idle",
        [npc = npc.get()](Blackboard& bb, float dt) {
            auto activity = bb.get<std::string>("scheduled_activity");
            if (activity && *activity == "Patrol") {
                bb.set<bool>("wants_patrol", true);
            }
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)), "Standing idle.");
        });

    npc->fsm.addState("Patrol",
        [npc = npc.get(), patrolRoute, patrolIdx](Blackboard& bb, float dt) mutable {
            if (!npc->isMoving) {
                patrolIdx = (patrolIdx + 1) % patrolRoute.size();
                npc->moveTo(patrolRoute[patrolIdx]);
            }
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "Starting patrol route.");
        });

    npc->fsm.addState("Combat",
        [npc = npc.get(), &world](Blackboard& bb, float dt) {
            // Combat is now handled by Behavior Tree in NPC::update()
            auto target = npc->combat.selectTarget();
            if (target) {
                float dist = npc->position.distanceTo(target->position);
                if (dist > 2.0f) {
                    npc->moveTo(target->position);
                }
            }
        },
        [npc = npc.get()](Blackboard& bb) {
            auto threats = npc->combat.threatTable();
            std::string threatStr;
            for (const auto& t : threats) {
                threatStr += " (threat: " + std::to_string(static_cast<int>(t.threatValue)) + ")";
            }
            std::cout << "[" << formatTime(bb.getOr<float>("_time", 0.0f))
                      << "] Alaric: ALERT! " << threats.size()
                      << " wolves detected! Drawing sword!" << threatStr << "\n";
            npc->emotions.addEmotion(EmotionType::Angry, 0.5f, 2.0f);
            npc->emotions.depletNeed(NeedType::Safety, 20.0f);

            // Log Utility AI decision
            auto decision = bb.get<std::string>("utility_decision");
            auto score = bb.getOr<float>("utility_score", 0.0f);
            if (decision) {
                std::cout << "[" << formatTime(bb.getOr<float>("_time", 0.0f))
                          << "] Utility AI chose: " << *decision
                          << " (score: " << std::fixed << std::setprecision(2) << score << ")\n";
            }
        });

    npc->fsm.addState("Eat",
        [npc = npc.get(), &world](Blackboard& bb, float dt) {
            auto* loc = world.getLocation("Tavern");
            if (loc && !npc->isAtLocation(Vec2(loc->x, loc->y))) {
                npc->moveTo(Vec2(loc->x, loc->y));
            } else {
                npc->emotions.satisfyNeed(NeedType::Hunger, 20.0f * dt);
                npc->emotions.satisfyNeed(NeedType::Thirst, 15.0f * dt);
            }
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "Heading to the Tavern for a meal.");
        });

    npc->fsm.addState("Socialize",
        [npc = npc.get(), &world](Blackboard& bb, float dt) {
            auto* loc = world.getLocation("Square");
            if (loc && !npc->isAtLocation(Vec2(loc->x, loc->y))) {
                npc->moveTo(Vec2(loc->x, loc->y));
            } else {
                npc->emotions.satisfyNeed(NeedType::Social, 15.0f * dt);
                npc->emotions.satisfyNeed(NeedType::Fun, 5.0f * dt);
            }
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "Socializing at the square.");
        });

    npc->fsm.addState("Sleep",
        [npc = npc.get()](Blackboard& bb, float dt) {
            npc->emotions.satisfyNeed(NeedType::Sleep, 30.0f * dt);
            npc->emotions.satisfyNeed(NeedType::Comfort, 10.0f * dt);
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "Going to sleep. Night watch will wait.");
        });

    // FSM Transitions
    npc->fsm.addTransition("Idle", "Patrol",
        [](const Blackboard& bb) { return bb.getOr<bool>("wants_patrol", false); }, 1);
    npc->fsm.addTransition("Idle", "Eat",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Eat";
        }, 2);
    npc->fsm.addTransition("Patrol", "Combat",
        [](const Blackboard& bb) { return bb.getOr<bool>("has_threats", false); }, 10);
    npc->fsm.addTransition("Idle", "Combat",
        [](const Blackboard& bb) { return bb.getOr<bool>("has_threats", false); }, 10);
    npc->fsm.addTransition("Eat", "Combat",
        [](const Blackboard& bb) { return bb.getOr<bool>("has_threats", false); }, 10);
    npc->fsm.addTransition("Socialize", "Combat",
        [](const Blackboard& bb) { return bb.getOr<bool>("has_threats", false); }, 10);
    npc->fsm.addTransition("Combat", "Patrol",
        [](const Blackboard& bb) { return !bb.getOr<bool>("has_threats", false); }, 1);
    npc->fsm.addTransition("Patrol", "Eat",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Eat";
        }, 3);
    npc->fsm.addTransition("Eat", "Patrol",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Patrol";
        }, 1);
    npc->fsm.addTransition("Patrol", "Socialize",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Socialize";
        }, 1);
    npc->fsm.addTransition("Eat", "Socialize",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Socialize";
        }, 1);
    npc->fsm.addTransition("Patrol", "Sleep",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && (*act == "Sleep" || *act == "Guard");
        }, 1);

    npc->fsm.setInitialState("Idle");

    // ─── GOAP: Guard long-term planning ─────────────────────────────
    npc->useGOAP = true;

    // World state builder — extracts relevant state from blackboard
    npc->goap.worldStateBuilder = [](const Blackboard& bb) -> GOAPState {
        GOAPState ws;
        ws["is_fed"] = GOAPValue(bb.getOr<float>("hunger_urgency", 0.0f) < 0.5f);
        ws["is_rested"] = GOAPValue(bb.getOr<float>("sleep_urgency", 0.0f) < 0.5f);
        ws["area_patrolled"] = GOAPValue(false);
        ws["village_safe"] = GOAPValue(!bb.getOr<bool>("has_threats", false));
        ws["is_social"] = GOAPValue(bb.getOr<float>("social_urgency", 0.0f) < 0.5f);
        auto act = bb.getOr<std::string>("scheduled_activity", "");
        if (act == "Patrol") ws["area_patrolled"] = GOAPValue(true);
        return ws;
    };

    // Action completion check
    npc->goap.isActionComplete = [](const GOAPAction& action, const Blackboard& bb) -> bool {
        float timeInState = bb.getOr<float>("time_in_state", 0.0f);
        if (action.name == "Patrol Area") return timeInState > 0.5f;
        if (action.name == "Eat Meal") return bb.getOr<float>("hunger_urgency", 0.0f) < 0.3f;
        if (action.name == "Rest") return bb.getOr<float>("sleep_urgency", 0.0f) < 0.3f;
        if (action.name == "Socialize") return timeInState > 0.3f;
        return timeInState > 0.2f;
    };

    // FSM state setter
    npc->goap.onActionStart = [npc = npc.get()](const std::string& fsmState, Blackboard& bb) {
        bb.set<std::string>("goap_desired_state", fsmState);
        (void)npc;
    };

    // Available actions
    npc->goap.actions = {
        {"Eat Meal",     1.0f, {}, {{"is_fed", GOAPValue(true)}}, "Eat"},
        {"Rest",         1.0f, {}, {{"is_rested", GOAPValue(true)}}, "Sleep"},
        {"Patrol Area",  2.0f, {{"is_fed", GOAPValue(true)}}, {{"area_patrolled", GOAPValue(true)}}, "Patrol"},
        {"Engage Threat", 3.0f, {{"area_patrolled", GOAPValue(true)}}, {{"village_safe", GOAPValue(true)}}, "Patrol"},
        {"Socialize",    1.5f, {{"is_fed", GOAPValue(true)}}, {{"is_social", GOAPValue(true)}}, "Socialize"},
    };

    // Goals (priority can be dynamic)
    npc->goap.goals = {
        {"Keep Village Safe", 10.0f, {{"village_safe", GOAPValue(true)}},
            [](const Blackboard& bb) {
                return bb.getOr<bool>("has_threats", false) ? 15.0f : 5.0f;
            }},
        {"Stay Combat Ready", 5.0f, {{"is_fed", GOAPValue(true)}, {"is_rested", GOAPValue(true)}},
            [](const Blackboard& bb) {
                float hunger = bb.getOr<float>("hunger_urgency", 0.0f);
                float sleep = bb.getOr<float>("sleep_urgency", 0.0f);
                return (hunger + sleep) * 8.0f;
            }},
        {"Maintain Morale", 3.0f, {{"is_social", GOAPValue(true)}},
            [](const Blackboard& bb) {
                return bb.getOr<float>("social_urgency", 0.0f) * 6.0f;
            }},
    };

    return npc;
}

std::shared_ptr<NPC> createBrina(GameWorld& world, std::shared_ptr<Pathfinder> pf) {
    auto npc = std::make_shared<NPC>(2, "Brina", NPCType::Blacksmith);
    npc->position = Vec2(16.0f, 11.0f);
    npc->pathfinder = pf;
    npc->factionId = VILLAGE_FACTION;
    npc->personality = PersonalityTraits::blacksmith();
    npc->emotions.applyPersonality(npc->personality);
    npc->combat.applyPersonality(
        npc->personality.fleeThresholdMultiplier(),
        npc->personality.healThreshold(),
        npc->personality.threatAwarenessMultiplier());
    npc->perception.config.sightRange *= npc->personality.sightRangeMultiplier();
    npc->perception.config.awarenessDecayRate *= npc->personality.awarenessDecayMultiplier();
    npc->memory = MemorySystem(static_cast<size_t>(50 * npc->personality.memoryCapacityMultiplier()));
    npc->schedule = ScheduleSystem::createBlacksmithSchedule();

    npc->combat.stats = {80.0f, 80.0f, 15.0f, 10.0f, 4.0f, 0.1f, {}};
    npc->combat.stats.stamina = {100.0f, 100.0f, 5.0f, 15.0f};
    npc->combat.stats.resistances = {0.8f, 1.0f, 0.6f, 1.0f, 1.0f}; // Blacksmith: Fire-resistant
    npc->combat.stats.abilities.push_back(
        {"Hammer Strike", AbilityType::Melee, DamageType::Physical, 12.0f, 1.5f, 0.08f, 0.0f, 0.0f, 0.0f, 12.0f});

    setupItems(npc->trade);
    npc->trade.inventory.addItem(ITEM_SWORD, 3);
    npc->trade.inventory.addItem(ITEM_SHIELD, 2);
    npc->trade.inventory.addItem(ITEM_HORSESHOE, 5);
    npc->trade.inventory.addItem(ITEM_IRON_ORE, 10);

    npc->fsm.addState("Work",
        [npc = npc.get()](Blackboard& bb, float dt) {
            npc->emotions.satisfyNeed(NeedType::Fun, 2.0f * dt);
            npc->trade.updatePrices();
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "Working at the forge. Hammering iron.");
        });

    npc->fsm.addState("Eat",
        [npc = npc.get(), &world](Blackboard& bb, float dt) {
            auto* loc = world.getLocation("Tavern");
            if (loc && !npc->isAtLocation(Vec2(loc->x, loc->y))) {
                npc->moveTo(Vec2(loc->x, loc->y));
            } else {
                npc->emotions.satisfyNeed(NeedType::Hunger, 20.0f * dt);
                npc->emotions.satisfyNeed(NeedType::Social, 5.0f * dt);
            }
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)), "Taking a break. Heading to eat.");
        });

    npc->fsm.addState("Socialize",
        [npc = npc.get(), &world](Blackboard& bb, float dt) {
            auto* loc = world.getLocation("Square");
            if (loc && !npc->isAtLocation(Vec2(loc->x, loc->y))) {
                npc->moveTo(Vec2(loc->x, loc->y));
            } else {
                npc->emotions.satisfyNeed(NeedType::Social, 15.0f * dt);
                npc->emotions.satisfyNeed(NeedType::Fun, 5.0f * dt);
            }
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "Socializing at the square. Mood: " + npc->emotions.getMoodString());
        });

    npc->fsm.addState("Sleep",
        [npc = npc.get()](Blackboard& bb, float dt) {
            npc->emotions.satisfyNeed(NeedType::Sleep, 25.0f * dt);
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)), "Going home to rest.");
        });

    npc->fsm.addState("Flee",
        [npc = npc.get(), &world](Blackboard& bb, float dt) {
            auto* loc = world.getLocation("SmithHouse");
            if (loc) npc->moveTo(Vec2(loc->x, loc->y));
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "Danger! Retreating to safety!");
            npc->emotions.addEmotion(EmotionType::Fearful, 0.6f, 2.0f);
        });

    npc->fsm.addState("Combat",
        [npc = npc.get(), &world](Blackboard& bb, float dt) {
            // Brina helps in combat - handled by runCombatRound
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "Grabbing hammer! Joining the fight!");
        });

    // Transitions
    npc->fsm.addTransition("Work", "Eat",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Eat";
        }, 2);
    npc->fsm.addTransition("Eat", "Work",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Work";
        }, 1);
    npc->fsm.addTransition("Work", "Socialize",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Socialize";
        }, 1);
    npc->fsm.addTransition("Socialize", "Eat",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Eat";
        }, 2);
    npc->fsm.addTransition("Eat", "Sleep",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Sleep";
        }, 1);
    npc->fsm.addTransition("Work", "Sleep",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Sleep";
        }, 2);
    npc->fsm.addTransition("Socialize", "Sleep",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Sleep";
        }, 2);
    // Brina joins combat if Alaric is fighting & her HP is ok
    npc->fsm.addTransition("Work", "Combat",
        [](const Blackboard& bb) {
            return bb.getOr<bool>("has_threats", false) &&
                   bb.getOr<float>("health_pct", 1.0f) > 0.5f;
        }, 8);
    npc->fsm.addTransition("Work", "Flee",
        [](const Blackboard& bb) {
            return bb.getOr<bool>("has_threats", false) &&
                   bb.getOr<float>("health_pct", 1.0f) <= 0.5f;
        }, 7);
    npc->fsm.addTransition("Flee", "Work",
        [](const Blackboard& bb) { return !bb.getOr<bool>("has_threats", false); }, 1);
    npc->fsm.addTransition("Combat", "Work",
        [](const Blackboard& bb) { return !bb.getOr<bool>("has_threats", false); }, 1);
    npc->fsm.addTransition("Combat", "Eat",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return !bb.getOr<bool>("has_threats", false) && act && *act == "Eat";
        }, 2);
    npc->fsm.addTransition("Combat", "Sleep",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return !bb.getOr<bool>("has_threats", false) && act && *act == "Sleep";
        }, 2);
    npc->fsm.addTransition("Flee", "Eat",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return !bb.getOr<bool>("has_threats", false) && act && *act == "Eat";
        }, 2);
    npc->fsm.addTransition("Flee", "Sleep",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return !bb.getOr<bool>("has_threats", false) && act && *act == "Sleep";
        }, 2);

    npc->fsm.setInitialState("Work");

    // ─── GOAP: Blacksmith long-term planning ────────────────────────
    npc->useGOAP = true;

    npc->goap.worldStateBuilder = [](const Blackboard& bb) -> GOAPState {
        GOAPState ws;
        ws["is_fed"] = GOAPValue(bb.getOr<float>("hunger_urgency", 0.0f) < 0.5f);
        ws["is_rested"] = GOAPValue(bb.getOr<float>("sleep_urgency", 0.0f) < 0.5f);
        ws["has_produced"] = GOAPValue(false);
        ws["is_social"] = GOAPValue(bb.getOr<float>("social_urgency", 0.0f) < 0.5f);
        auto act = bb.getOr<std::string>("scheduled_activity", "");
        if (act == "Work") ws["has_produced"] = GOAPValue(true);
        return ws;
    };

    npc->goap.isActionComplete = [](const GOAPAction& action, const Blackboard& bb) -> bool {
        float t = bb.getOr<float>("time_in_state", 0.0f);
        if (action.name == "Forge Items") return t > 0.5f;
        if (action.name == "Eat Meal") return bb.getOr<float>("hunger_urgency", 0.0f) < 0.3f;
        if (action.name == "Rest") return bb.getOr<float>("sleep_urgency", 0.0f) < 0.3f;
        return t > 0.2f;
    };

    npc->goap.onActionStart = [](const std::string&, Blackboard&) {};

    npc->goap.actions = {
        {"Eat Meal",     1.0f, {}, {{"is_fed", GOAPValue(true)}}, "Eat"},
        {"Rest",         1.0f, {}, {{"is_rested", GOAPValue(true)}}, "Sleep"},
        {"Forge Items",  2.0f, {{"is_fed", GOAPValue(true)}}, {{"has_produced", GOAPValue(true)}}, "Work"},
        {"Socialize",    1.5f, {{"is_fed", GOAPValue(true)}}, {{"is_social", GOAPValue(true)}}, "Socialize"},
    };

    npc->goap.goals = {
        {"Produce Goods", 8.0f, {{"has_produced", GOAPValue(true)}}, nullptr},
        {"Stay Healthy", 5.0f, {{"is_fed", GOAPValue(true)}, {"is_rested", GOAPValue(true)}},
            [](const Blackboard& bb) {
                return (bb.getOr<float>("hunger_urgency", 0.0f) + bb.getOr<float>("sleep_urgency", 0.0f)) * 8.0f;
            }},
        {"Maintain Morale", 3.0f, {{"is_social", GOAPValue(true)}},
            [](const Blackboard& bb) {
                return bb.getOr<float>("social_urgency", 0.0f) * 6.0f;
            }},
    };

    return npc;
}

std::shared_ptr<NPC> createCedric(GameWorld& world, std::shared_ptr<Pathfinder> pf) {
    auto npc = std::make_shared<NPC>(3, "Cedric", NPCType::Merchant);
    npc->position = Vec2(25.0f, 12.0f);
    npc->pathfinder = pf;
    npc->factionId = VILLAGE_FACTION;
    npc->personality = PersonalityTraits::merchant();
    npc->emotions.applyPersonality(npc->personality);
    npc->combat.applyPersonality(
        npc->personality.fleeThresholdMultiplier(),
        npc->personality.healThreshold(),
        npc->personality.threatAwarenessMultiplier());
    npc->perception.config.sightRange *= npc->personality.sightRangeMultiplier();
    npc->perception.config.awarenessDecayRate *= npc->personality.awarenessDecayMultiplier();
    npc->memory = MemorySystem(static_cast<size_t>(50 * npc->personality.memoryCapacityMultiplier()));
    npc->trade.applyPersonality(
        npc->personality.buyMarkupMultiplier(),
        npc->personality.sellMarkdownMultiplier(),
        npc->personality.scarcityMultiplier(),
        npc->personality.relationshipDiscountMultiplier());
    npc->schedule = ScheduleSystem::createMerchantSchedule();

    npc->combat.stats = {60.0f, 60.0f, 8.0f, 5.0f, 3.0f, 0.05f, {}};

    setupItems(npc->trade);
    npc->trade.inventory = Inventory(300.0f, 200.0f);
    npc->trade.inventory.addItem(ITEM_BREAD, 20);
    npc->trade.inventory.addItem(ITEM_ALE, 15);
    npc->trade.inventory.addItem(ITEM_HEALTH_POT, 5);
    npc->trade.inventory.addItem(ITEM_LEATHER, 8);
    npc->trade.inventory.addItem(ITEM_TOOLS, 3);

    npc->fsm.addState("Trade",
        [npc = npc.get()](Blackboard& bb, float dt) {
            npc->trade.updatePrices();
            npc->emotions.satisfyNeed(NeedType::Fun, 1.0f * dt);
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "Opening shop. Stock: " + std::to_string(npc->trade.inventory.totalItems()) +
                     " items. Gold: " + std::to_string(static_cast<int>(npc->trade.inventory.gold())));
        });

    npc->fsm.addState("Eat",
        [npc = npc.get(), &world](Blackboard& bb, float dt) {
            auto* loc = world.getLocation("Tavern");
            if (loc && !npc->isAtLocation(Vec2(loc->x, loc->y))) {
                npc->moveTo(Vec2(loc->x, loc->y));
            } else {
                npc->emotions.satisfyNeed(NeedType::Hunger, 20.0f * dt);
            }
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)), "Lunch break at the Tavern.");
        });

    npc->fsm.addState("Socialize",
        [npc = npc.get(), &world](Blackboard& bb, float dt) {
            auto* loc = world.getLocation("Tavern");
            if (loc && !npc->isAtLocation(Vec2(loc->x, loc->y))) {
                npc->moveTo(Vec2(loc->x, loc->y));
            } else {
                npc->emotions.satisfyNeed(NeedType::Social, 15.0f * dt);
                npc->emotions.satisfyNeed(NeedType::Fun, 8.0f * dt);
            }
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)), "Socializing at the Tavern.");
        });

    npc->fsm.addState("Sleep",
        [npc = npc.get()](Blackboard& bb, float dt) {
            npc->emotions.satisfyNeed(NeedType::Sleep, 25.0f * dt);
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)), "Closing shop. Time for rest.");
        });

    npc->fsm.addState("Flee",
        [npc = npc.get(), &world](Blackboard& bb, float dt) {
            auto* loc = world.getLocation("MerchHouse");
            if (loc) npc->moveTo(Vec2(loc->x, loc->y));
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "Wolves?! Running for cover! My goods!");
            npc->emotions.addEmotion(EmotionType::Fearful, 0.8f, 3.0f);
        });

    npc->fsm.addTransition("Trade", "Eat",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Eat";
        }, 2);
    npc->fsm.addTransition("Eat", "Trade",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Trade";
        }, 1);
    npc->fsm.addTransition("Trade", "Socialize",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Socialize";
        }, 1);
    npc->fsm.addTransition("Socialize", "Eat",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Eat";
        }, 2);
    npc->fsm.addTransition("Trade", "Sleep",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Sleep";
        }, 1);
    npc->fsm.addTransition("Trade", "Flee",
        [](const Blackboard& bb) { return bb.getOr<bool>("has_threats", false); }, 8);
    npc->fsm.addTransition("Eat", "Flee",
        [](const Blackboard& bb) { return bb.getOr<bool>("has_threats", false); }, 8);
    npc->fsm.addTransition("Flee", "Trade",
        [](const Blackboard& bb) { return !bb.getOr<bool>("has_threats", false); }, 1);

    npc->fsm.setInitialState("Trade");
    return npc;
}

std::shared_ptr<NPC> createDagna(GameWorld& world, std::shared_ptr<Pathfinder> pf) {
    auto npc = std::make_shared<NPC>(4, "Dagna", NPCType::Innkeeper);
    npc->position = Vec2(8.0f, 7.0f);
    npc->pathfinder = pf;
    npc->factionId = VILLAGE_FACTION;
    npc->personality = PersonalityTraits::innkeeper();
    npc->emotions.applyPersonality(npc->personality);
    npc->combat.applyPersonality(
        npc->personality.fleeThresholdMultiplier(),
        npc->personality.healThreshold(),
        npc->personality.threatAwarenessMultiplier());
    npc->perception.config.sightRange *= npc->personality.sightRangeMultiplier();
    npc->perception.config.awarenessDecayRate *= npc->personality.awarenessDecayMultiplier();
    npc->memory = MemorySystem(static_cast<size_t>(50 * npc->personality.memoryCapacityMultiplier()));
    npc->trade.applyPersonality(
        npc->personality.buyMarkupMultiplier(),
        npc->personality.sellMarkdownMultiplier(),
        npc->personality.scarcityMultiplier(),
        npc->personality.relationshipDiscountMultiplier());
    npc->schedule = ScheduleSystem::createInnkeeperSchedule();

    npc->combat.stats = {70.0f, 70.0f, 10.0f, 8.0f, 3.0f, 0.05f, {}};

    setupItems(npc->trade);
    npc->trade.inventory.addItem(ITEM_BREAD, 30);
    npc->trade.inventory.addItem(ITEM_ALE, 25);

    // Dialog setup
    DialogTree greetTree("greeting");
    DialogNode root;
    root.id = "root";
    root.speakerText = "Welcome to the Tavern! What can I get you?";
    root.friendlyText = "Ah, my favorite customer! The usual?";
    root.hostileText = "What do you want? Make it quick.";
    root.options = {
        {"I'd like some bread and ale.", "serve", nullptr, -100},
        {"Any news from the village?", "gossip", nullptr, -100},
        {"Just passing through.", "END", nullptr, -100}
    };
    greetTree.addNode(root);

    DialogNode serveNode;
    serveNode.id = "serve";
    serveNode.speakerText = "Coming right up! That'll be 8 gold.";
    serveNode.options = {{"Thanks!", "END", nullptr, -100}};
    greetTree.addNode(serveNode);

    DialogNode gossipNode;
    gossipNode.id = "gossip";
    gossipNode.speakerText = "I heard wolves have been spotted near the forest. Be careful out there!";
    gossipNode.options = {
        {"I'll keep my eyes open. Thanks.", "END", nullptr, -100},
        {"Wolves? Maybe I should talk to the guard.", "END", nullptr, -100}
    };
    greetTree.addNode(gossipNode);
    npc->dialog.addTree("greeting", std::move(greetTree));

    npc->fsm.addState("Work",
        [npc = npc.get()](Blackboard& bb, float dt) {
            npc->emotions.satisfyNeed(NeedType::Fun, 1.0f * dt);
            npc->emotions.satisfyNeed(NeedType::Social, 3.0f * dt);
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "Serving customers at the Tavern.");
        });

    npc->fsm.addState("Eat",
        [npc = npc.get()](Blackboard& bb, float dt) {
            npc->emotions.satisfyNeed(NeedType::Hunger, 25.0f * dt);
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)), "Taking a meal break.");
        });

    npc->fsm.addState("Socialize",
        [npc = npc.get()](Blackboard& bb, float dt) {
            npc->emotions.satisfyNeed(NeedType::Social, 15.0f * dt);
            npc->emotions.satisfyNeed(NeedType::Fun, 8.0f * dt);
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "Chatting with tavern guests.");
        });

    npc->fsm.addState("Sleep",
        [npc = npc.get()](Blackboard& bb, float dt) {
            npc->emotions.satisfyNeed(NeedType::Sleep, 30.0f * dt);
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "Closing up the Tavern. Goodnight!");
        });

    npc->fsm.addTransition("Work", "Eat",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Eat";
        }, 2);
    npc->fsm.addTransition("Eat", "Work",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Work";
        }, 1);
    npc->fsm.addTransition("Work", "Sleep",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Sleep";
        }, 1);

    npc->fsm.setInitialState("Work");
    return npc;
}

std::shared_ptr<NPC> createElmund(GameWorld& world, std::shared_ptr<Pathfinder> pf) {
    auto npc = std::make_shared<NPC>(5, "Elmund", NPCType::Farmer);
    npc->position = Vec2(34.0f, 21.0f);
    npc->pathfinder = pf;
    npc->factionId = VILLAGE_FACTION;
    npc->personality = PersonalityTraits::farmer();
    npc->emotions.applyPersonality(npc->personality);
    npc->combat.applyPersonality(
        npc->personality.fleeThresholdMultiplier(),
        npc->personality.healThreshold(),
        npc->personality.threatAwarenessMultiplier());
    npc->perception.config.sightRange *= npc->personality.sightRangeMultiplier();
    npc->perception.config.awarenessDecayRate *= npc->personality.awarenessDecayMultiplier();
    npc->memory = MemorySystem(static_cast<size_t>(50 * npc->personality.memoryCapacityMultiplier()));
    npc->schedule = ScheduleSystem::createFarmerSchedule();

    npc->combat.stats = {50.0f, 50.0f, 5.0f, 3.0f, 4.0f, 0.02f, {}};
    npc->combat.stats.stamina = {60.0f, 60.0f, 4.0f, 12.0f};
    npc->combat.stats.abilities.push_back(
        {"Pitchfork Jab", AbilityType::Melee, DamageType::Physical, 5.0f, 1.5f, 0.1f, 0.0f, 0.0f, 0.0f, 5.0f});

    setupItems(npc->trade);
    npc->trade.inventory.addItem(ITEM_WHEAT, 30);
    npc->trade.inventory.addItem(ITEM_BREAD, 5);

    npc->fsm.addState("Work",
        [npc = npc.get(), &world](Blackboard& bb, float dt) {
            auto* loc = world.getLocation("Farm");
            if (loc && !npc->isAtLocation(Vec2(loc->x, loc->y))) {
                npc->moveTo(Vec2(loc->x, loc->y));
            } else {
                npc->emotions.satisfyNeed(NeedType::Fun, 1.0f * dt);
            }
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "Working in the fields. Good harvest today.");
        });

    npc->fsm.addState("Eat",
        [npc = npc.get(), &world](Blackboard& bb, float dt) {
            auto* loc = world.getLocation("Tavern");
            if (loc && !npc->isAtLocation(Vec2(loc->x, loc->y))) {
                npc->moveTo(Vec2(loc->x, loc->y));
            } else {
                npc->emotions.satisfyNeed(NeedType::Hunger, 20.0f * dt);
                npc->emotions.satisfyNeed(NeedType::Social, 5.0f * dt);
            }
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "Heading to the Tavern for a meal.");
        });

    npc->fsm.addState("Socialize",
        [npc = npc.get(), &world](Blackboard& bb, float dt) {
            auto* loc = world.getLocation("Square");
            if (loc && !npc->isAtLocation(Vec2(loc->x, loc->y))) {
                npc->moveTo(Vec2(loc->x, loc->y));
            }
            npc->emotions.satisfyNeed(NeedType::Social, 10.0f * dt);
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "Meeting friends at the square.");
        });

    npc->fsm.addState("Sleep",
        [npc = npc.get()](Blackboard& bb, float dt) {
            npc->emotions.satisfyNeed(NeedType::Sleep, 25.0f * dt);
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)), "Long day. Time for bed.");
        });

    npc->fsm.addState("Flee",
        [npc = npc.get(), &world](Blackboard& bb, float dt) {
            auto* loc = world.getLocation("Tavern");
            if (loc) npc->moveTo(Vec2(loc->x, loc->y));
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "WOLVES! Running to the tavern for safety!!");
            npc->emotions.addEmotion(EmotionType::Fearful, 0.9f, 4.0f);
            npc->emotions.depletNeed(NeedType::Safety, 50.0f);
            npc->memory.addMemory(MemoryType::WorldEvent,
                "Fled from wolves near the farm", -0.7f, std::nullopt, 0.8f,
                bb.getOr<float>("_time", 0.0f));
        });

    // Transitions
    npc->fsm.addTransition("Work", "Eat",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Eat";
        }, 2);
    npc->fsm.addTransition("Eat", "Work",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Work";
        }, 1);
    npc->fsm.addTransition("Work", "Socialize",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Socialize";
        }, 1);
    npc->fsm.addTransition("Socialize", "Eat",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Eat";
        }, 2);
    npc->fsm.addTransition("Eat", "Sleep",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Sleep";
        }, 1);
    npc->fsm.addTransition("Work", "Flee",
        [](const Blackboard& bb) { return bb.getOr<bool>("has_threats", false); }, 10);
    npc->fsm.addTransition("Eat", "Flee",
        [](const Blackboard& bb) { return bb.getOr<bool>("has_threats", false); }, 10);
    npc->fsm.addTransition("Socialize", "Flee",
        [](const Blackboard& bb) { return bb.getOr<bool>("has_threats", false); }, 10);
    npc->fsm.addTransition("Flee", "Work",
        [](const Blackboard& bb) { return !bb.getOr<bool>("has_threats", false); }, 1);
    npc->fsm.addTransition("Flee", "Eat",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return !bb.getOr<bool>("has_threats", false) && act && *act == "Eat";
        }, 2);
    npc->fsm.addTransition("Flee", "Sleep",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return !bb.getOr<bool>("has_threats", false) && act && *act == "Sleep";
        }, 2);
    npc->fsm.addTransition("Flee", "Socialize",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return !bb.getOr<bool>("has_threats", false) && act && *act == "Socialize";
        }, 1);

    npc->fsm.setInitialState("Work");
    return npc;
}


// =======================================================================
//  WORLD EVENTS - Timeline from 06:00 to 22:00
// =======================================================================

void scheduleWorldEvents(GameWorld& world, FactionSystem& factions,
                         std::shared_ptr<Pathfinder> pf) {

    // === 06:00 - Day begins ===
    world.eventManager().scheduleEvent(6.0f, "day_start", [](GameWorld& w) {
        std::cout << "\n  ** A new day begins in the village. **\n";
        for (auto& npc : w.npcs()) {
            if (npc->type != NPCType::Enemy) {
                npc->emotions.satisfyNeed(NeedType::Sleep, 10.0f);
            }
        }
    });

    // === 07:00 - Everyone goes to work ===
    world.eventManager().scheduleEvent(7.0f, "work_start", [](GameWorld& w) {
        std::cout << "\n  ** The village comes alive. Everyone heads to work. **\n";

        // Seed influence: Alaric noticed unusual tracks during dawn patrol
        auto* alaric = w.findNPC("Alaric");
        if (alaric) {
            InfluenceMessage msg;
            msg.id            = "strange_tracks";
            msg.topic         = "strange tracks near the forest at dawn";
            msg.originatorId  = alaric->id;
            msg.originatorName = "Alaric";
            msg.charge        = -0.45f;  // somewhat unsettling
            msg.reliability   = 1.0f;
            msg.createdAt     = w.time().totalHours();
            msg.expiresAt     = 22.0f;
            msg.reachedIds.push_back(alaric->id);
            msg.reachedNames.push_back(alaric->name);
            g_influence.seed(std::move(msg));
        }
    });

    // === 08:00 - Traveling merchant arrives ===
    world.eventManager().scheduleEvent(8.0f, "merchant_arrives", [pf](GameWorld& w) {
        std::cout << "\n  !! A TRAVELING MERCHANT ARRIVES AT THE GATE !!\n\n";

        auto farhan = std::make_shared<NPC>(10, "Farhan", NPCType::Merchant);
        farhan->position = Vec2(3.0f, 12.0f);
        farhan->pathfinder = pf;
        farhan->verbose = true;

        farhan->combat.stats = {50.0f, 50.0f, 5.0f, 5.0f, 3.0f, 0.05f, {}};

        setupAllItems(farhan->trade);
        farhan->trade.inventory = Inventory(500.0f, 500.0f);
        farhan->trade.inventory.addItem(ITEM_ENCHANTED_SWORD, 1);
        farhan->trade.inventory.addItem(ITEM_EXOTIC_SPICES, 5);
        farhan->trade.inventory.addItem(ITEM_HEALTH_POT, 10);
        farhan->trade.inventory.addItem(ITEM_LEATHER, 15);

        farhan->fsm.addState("Trade",
            [farhan = farhan.get()](Blackboard& bb, float dt) {
                farhan->emotions.satisfyNeed(NeedType::Fun, 2.0f * dt);
            },
            [farhan = farhan.get()](Blackboard& bb) {
                farhan->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                    "Greetings! I am Farhan, a traveling merchant. I have rare wares!");
            });
        farhan->fsm.blackboard().set<float>("_time", w.time().totalHours());
        farhan->fsm.setInitialState("Trade");

        farhan->subscribeToEvents(w.events());
        w.addNPC(farhan);

        // Farhan moves to market
        farhan->moveTo(Vec2(25.0f, 12.0f));

        // Cedric reacts
        auto* cedric = w.findNPC("Cedric");
        if (cedric) {
            std::cout << "[" << w.time().formatClock() << "] Cedric: \"A competitor! "
                      << "Let me see what he's selling...\"\n";
            cedric->memory.addMemory(MemoryType::Interaction,
                "Traveling merchant Farhan arrived", 0.1f, 10, 0.6f,
                w.time().totalHours());

            // Cedric buys exotic spices from Farhan
            auto result = farhan->trade.sell(ITEM_EXOTIC_SPICES, 2,
                                             cedric->trade.inventory);
            if (result.success) {
                std::cout << "[" << w.time().formatClock() << "] " << result.message << "\n";
                std::cout << "[" << w.time().formatClock() << "] Cedric: \"Exotic spices! "
                          << "My customers will love these.\"\n";
                logRelationship(w.time().formatClock(), "Cedric", "Farhan", 3, 10, 10.0f);
            }
        }
    });

    // === 09:00 - Alaric-Brina meeting at Square ===
    world.eventManager().scheduleEvent(9.0f, "morning_social", [](GameWorld& w) {
        std::cout << "\n  ** Morning social hour at the Square **\n\n";
        auto* alaric = w.findNPC("Alaric");
        auto* brina = w.findNPC("Brina");
        if (alaric && brina) {
            std::cout << "[" << w.time().formatClock() << "] Brina meets Alaric at the Square.\n";
            std::cout << "[" << w.time().formatClock()
                      << "] Brina: \"Good morning, Alaric. Heard anything on patrol?\"\n";
            std::cout << "[" << w.time().formatClock()
                      << "] Alaric: \"All clear so far. Stay safe.\"\n";

            logRelationship(w.time().formatClock(), "Alaric", "Brina",
                            alaric->id, brina->id, 3.0f);

            alaric->emotions.satisfyNeed(NeedType::Social, 10.0f);
            brina->emotions.satisfyNeed(NeedType::Social, 10.0f);
            alaric->memory.addMemory(MemoryType::Interaction,
                "Morning chat with Brina", 0.3f, brina->id, 0.4f,
                w.time().totalHours());
            brina->memory.addMemory(MemoryType::Interaction,
                "Morning chat with Alaric", 0.3f, alaric->id, 0.4f,
                w.time().totalHours());
        }
    });

    // === 10:00 - Elmund-Cedric trade ===
    world.eventManager().scheduleEvent(10.0f, "morning_trade", [](GameWorld& w) {
        std::cout << "\n  ** Trade time at the Market **\n\n";

        auto* cedric = w.findNPC("Cedric");
        auto* elmund = w.findNPC("Elmund");
        if (!cedric || !elmund) return;

        float price = cedric->trade.getPrice(ITEM_TOOLS, true);
        std::cout << "[" << w.time().formatClock() << "] Elmund visits Cedric's shop.\n";
        std::cout << "[" << w.time().formatClock()
                  << "] Cedric's price for Farming Tools: "
                  << static_cast<int>(price) << " gold\n";
        std::cout << "[" << w.time().formatClock() << "] Elmund's gold: "
                  << static_cast<int>(elmund->trade.inventory.gold()) << "\n";

        auto result = cedric->trade.sell(ITEM_TOOLS, 1, elmund->trade.inventory);
        std::cout << "[" << w.time().formatClock() << "] Trade result: "
                  << result.message << "\n";

        if (result.success) {
            cedric->memory.addMemory(MemoryType::Trade,
                "Sold farming tools to Elmund", 0.3f, elmund->id, 0.5f,
                w.time().totalHours());
            elmund->memory.addMemory(MemoryType::Trade,
                "Bought farming tools from Cedric", 0.2f, cedric->id, 0.5f,
                w.time().totalHours());
            logRelationship(w.time().formatClock(), "Cedric", "Elmund",
                            cedric->id, elmund->id, 5.0f);
            w.events().publish(TradeEvent{elmund->id, cedric->id, ITEM_TOOLS, 1, result.price});
        }
    });

    // === 11:00 - Thief event! ===
    world.eventManager().scheduleEvent(11.0f, "thief_event", [](GameWorld& w) {
        std::cout << "\n  !! THIEF SPOTTED AT THE MARKET !!\n\n";
        std::cout << "[" << w.time().formatClock()
                  << "] A hooded figure is seen sneaking around Cedric's stall!\n";

        w.events().publish(WorldEvent{
            "thief_spotted", "A thief was spotted at the market!",
            Vec2(25.0f, 12.0f), 0.4f
        });

        auto* alaric = w.findNPC("Alaric");
        auto* cedric = w.findNPC("Cedric");

        if (alaric) {
            std::cout << "[" << w.time().formatClock()
                      << "] Alaric: \"Halt! I see you, thief! Stop right there!\"\n";
            alaric->moveTo(Vec2(25.0f, 12.0f));
            alaric->memory.addMemory(MemoryType::WorldEvent,
                "Chased a thief at the market", 0.2f, std::nullopt, 0.7f,
                w.time().totalHours());

            // Chase sequence
            bool caught = (std::rand() % 2 == 0);
            if (caught) {
                std::cout << "[" << w.time().formatClock()
                          << "] Alaric catches the thief! \"You're not getting away!\"\n";
                alaric->emotions.addEmotion(EmotionType::Happy, 0.3f, 1.0f);
            } else {
                std::cout << "[" << w.time().formatClock()
                          << "] The thief escapes into the alley! Alaric: \"Blast! He's gone.\"\n";
                alaric->emotions.addEmotion(EmotionType::Angry, 0.3f, 1.0f);
            }
        }

        if (cedric) {
            std::cout << "[" << w.time().formatClock()
                      << "] Cedric: \"My goods! Thank the gods for Alaric.\"\n";
            cedric->memory.addMemory(MemoryType::WorldEvent,
                "Thief tried to steal from my shop", -0.4f, std::nullopt, 0.7f,
                w.time().totalHours());
            if (alaric) {
                logRelationship(w.time().formatClock(), "Cedric", "Alaric",
                                cedric->id, alaric->id, 8.0f);
            }
        }

        // Everyone remembers the thief
        for (auto& npc : w.npcs()) {
            if (npc->type != NPCType::Enemy && npc->name != "Alaric" && npc->name != "Cedric") {
                npc->memory.addMemory(MemoryType::WorldEvent,
                    "Heard about thief at the market", -0.2f, std::nullopt, 0.5f,
                    w.time().totalHours());
            }
        }
    });

    // === 12:00 - Lunch & Dialog ===
    world.eventManager().scheduleEvent(12.0f, "lunch_dialog", [](GameWorld& w) {
        std::cout << "\n  ** Lunchtime at Dagna's Tavern **\n\n";

        auto* dagna = w.findNPC("Dagna");
        if (!dagna) return;

        float reputation = 30.0f;
        float mood = dagna->emotions.getMood();

        if (dagna->dialog.startDialog("greeting", reputation)) {
            dagna->dialog.printCurrent(dagna->name, reputation, mood);
            std::cout << "  [Villager chooses: Any news from the village?]\n";
            dagna->dialog.selectOption(1);

            if (dagna->dialog.isInDialog()) {
                dagna->dialog.printCurrent(dagna->name, reputation, mood);
                std::cout << "  [Villager chooses: I'll keep my eyes open.]\n";
                dagna->dialog.selectOption(0);
            }

            dagna->memory.addMemory(MemoryType::Interaction,
                "Had a conversation about wolves", 0.1f, std::nullopt, 0.4f,
                w.time().totalHours());
        }

        // Dagna serves food to everyone
        for (auto& npc : w.npcs()) {
            if (npc->type != NPCType::Enemy && npc->name != "Dagna") {
                npc->emotions.satisfyNeed(NeedType::Hunger, 15.0f);
                npc->emotions.satisfyNeed(NeedType::Social, 5.0f);
            }
        }
        std::cout << "[" << w.time().formatClock()
                  << "] Dagna: \"Stew's ready! Come and get it while it's hot!\"\n";
    });

    // === 13:00 - Farhan leaves ===
    world.eventManager().scheduleEvent(13.0f, "farhan_leaves", [](GameWorld& w) {
        std::cout << "\n  ** Afternoon begins. Back to work. **\n\n";
        auto* farhan = w.findNPC("Farhan");
        if (farhan) {
            std::cout << "[" << w.time().formatClock()
                      << "] Farhan: \"It was good trading with you all! "
                      << "Until next time!\"\n";
            farhan->moveTo(Vec2(3.0f, 12.0f)); // head to gate
            farhan->combat.stats.health = 0.0f; // mark as "left"
            farhan->verbose = false;
        }
    });

    // === 13:15 - Show Utility AI decisions (schedule now returns Work/Patrol/Trade) ===
    world.eventManager().scheduleEvent(13.25f, "utility_ai_log", [](GameWorld& w) {
        for (auto& npc : w.npcs()) {
            if (npc->type != NPCType::Enemy && npc->combat.stats.isAlive()
                && npc->name != "Farhan") {
                auto decision = npc->fsm.blackboard().get<std::string>("utility_decision");
                auto score = npc->fsm.blackboard().getOr<float>("utility_score", 0.0f);
                if (decision) {
                    std::cout << "[" << w.time().formatClock() << "] "
                              << npc->name << " Utility AI chose: " << *decision
                              << " (score: " << std::fixed << std::setprecision(2)
                              << score << ")\n";
                }
            }
        }
    });

    // === 14:00 - WOLF ATTACK! ===
    world.eventManager().scheduleEvent(14.0f, "wolf_attack",
        [&factions, pf](GameWorld& w) {

        std::cout << "\n"
                  << "  !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"
                  << "  !! WOLF PACK SPOTTED NEAR THE VILLAGE! !!\n"
                  << "  !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n\n";

        w.events().publish(WorldEvent{
            "wolf_attack", "A pack of wolves approaches the village!",
            Vec2(35.0f, 12.0f), 0.8f
        });

        // Seed influence: wolf attack spreading panic through village
        auto* alaricSeed = w.findNPC("Alaric");
        if (alaricSeed) {
            InfluenceMessage msg;
            msg.id            = "wolves_attack";
            msg.topic         = "wolves are attacking the village";
            msg.originatorId  = alaricSeed->id;
            msg.originatorName = "Alaric";
            msg.charge        = -0.85f;  // very alarming
            msg.reliability   = 1.0f;
            msg.createdAt     = w.time().totalHours();
            msg.expiresAt     = 22.0f;
            msg.reachedIds.push_back(alaricSeed->id);
            msg.reachedNames.push_back(alaricSeed->name);
            g_influence.seed(std::move(msg));
        }

        // Spawn wolves with GroupBehavior
        g_wolfPack.setLeader(100);
        g_wolfPack.setFormation(FormationType::Wedge);

        for (int i = 0; i < 3; ++i) {
            EntityId wid = 100 + i;
            auto wolf = std::make_shared<NPC>(wid, "Wolf_" + std::to_string(i + 1), NPCType::Enemy);
            wolf->position = Vec2(21.0f, 11.0f + i * 1.0f);
            wolf->pathfinder = pf;
            wolf->factionId = WOLF_FACTION;
            wolf->verbose = false;
            wolf->moveSpeed = 8.0f;

            // Different stats per wolf - Alpha is strongest
            if (i == 0) {
                wolf->combat.stats = {50.0f, 50.0f, 15.0f, 5.0f, 8.0f, 0.15f, {}};
                wolf->combat.stats.stamina = {80.0f, 80.0f, 6.0f, 0.0f};
            } else if (i == 1) {
                wolf->combat.stats = {35.0f, 35.0f, 11.0f, 3.0f, 7.0f, 0.08f, {}};
                wolf->combat.stats.stamina = {60.0f, 60.0f, 5.0f, 0.0f};
            } else {
                wolf->combat.stats = {30.0f, 30.0f, 9.0f, 2.0f, 6.0f, 0.05f, {}};
                wolf->combat.stats.stamina = {50.0f, 50.0f, 4.0f, 0.0f};
            }
            // Wolves: Ice-weak, Poison-resistant (natural fur)
            wolf->combat.stats.resistances = {1.0f, 1.0f, 0.8f, 1.5f, 0.7f};
            wolf->combat.stats.abilities.push_back(
                {"Bite", AbilityType::Melee, DamageType::Physical, 10.0f, 3.0f, 0.03f, 0.0f, 0.0f, 0.0f, 8.0f});

            wolf->fsm.addState("Hunt",
                [wolf = wolf.get(), &w](Blackboard& bb, float dt) {
                    Vec2 target(20.0f, 12.0f);
                    float bestDist = 999.0f;
                    NPC* nearest = nullptr;
                    for (auto& other : w.npcs()) {
                        if (other->type == NPCType::Enemy) continue;
                        if (!other->combat.stats.isAlive()) continue;
                        float d = wolf->position.distanceTo(other->position);
                        if (d < bestDist) {
                            bestDist = d;
                            nearest = other.get();
                        }
                    }
                    if (nearest && bestDist < 15.0f) {
                        if (bestDist > 2.0f) wolf->moveTo(nearest->position);
                    } else {
                        wolf->moveTo(target);
                    }
                });
            wolf->fsm.setInitialState("Hunt");

            if (i > 0) g_wolfPack.addMember(wid);
            factions.addMember(WOLF_FACTION, wid);
            wolf->subscribeToEvents(w.events());

            // Force villagers to perceive wolves
            for (auto& villager : w.npcs()) {
                if (villager->type != NPCType::Enemy) {
                    villager->perception.forceAwareness(
                        wid, wolf->position, AwarenessLevel::Combat, true,
                        w.time().totalHours());
                }
            }

            w.addNPC(wolf);
        }

        // Wolf formation log
        std::cout << "[" << w.time().formatClock()
                  << "] Wolf_1 (Alpha) leads pack in Wedge formation toward village.\n";

        // Alaric rushes to intercept wolves
        auto* alaric = w.findNPC("Alaric");
        if (alaric) {
            alaric->position = Vec2(21.0f, 12.0f); // Rush to combat position
            alaric->isMoving = false;
            alaric->fsm.blackboard().set<bool>("has_threats", true);
            alaric->combat.inCombat = true;

            { std::vector<PerceivedEntity> pv;
              for (const auto& [id, pe] : alaric->perception.perceived()) pv.push_back(pe);
              alaric->combat.evaluateThreats(pv, alaric->position); }
            auto& threats = alaric->combat.threatTable();
            std::cout << "[" << w.time().formatClock()
                      << "] Alaric evaluates threats:";
            for (const auto& t : threats) {
                auto* wolf = w.findNPC(t.entityId);
                if (wolf) {
                    std::cout << " " << wolf->name << " (threat: "
                              << static_cast<int>(t.threatValue) << ")";
                }
            }
            std::cout << "\n";
            std::cout << "[" << w.time().formatClock()
                      << "] Alaric targets Wolf_1 (Alpha). Moving to engage.\n";
        }

        // Brina rushes to help
        auto* brina = w.findNPC("Brina");
        if (brina) {
            brina->position = Vec2(20.0f, 11.0f); // Rush to nearby position
            brina->isMoving = false;
        }

        g_combatActive = true;
    });

    // === 15:00 - Combat resolution ===
    world.eventManager().scheduleEvent(15.5f, "combat_end", [](GameWorld& w) {
        if (!g_combatResolved) {
            // Force resolve combat
            g_combatResolved = true;
            g_combatActive = false;
            std::cout << "[" << w.time().formatClock()
                      << "] The last wolf flees into the forest. The village is safe.\n";
            for (auto& npc : w.npcs()) {
                if (npc->type == NPCType::Enemy) {
                    npc->combat.stats.health = 0.0f;
                }
            }
        }
        // Clear threat flags, perception, and post-combat emotions
        for (auto& npc : w.npcs()) {
            if (npc->type != NPCType::Enemy && npc->combat.stats.isAlive()) {
                npc->fsm.blackboard().set<bool>("has_threats", false);
                npc->combat.inCombat = false;
                // Remove dead wolves from perception
                for (auto& other : w.npcs()) {
                    if (other->type == NPCType::Enemy) {
                        npc->perception.forgetEntity(other->id);
                    }
                }
                npc->emotions.satisfyNeed(NeedType::Safety, 20.0f);
                npc->emotions.addEmotion(EmotionType::Happy, 0.3f, 2.0f);
            }
        }

        // Seed influence: Brina spreads the heroic account of Alaric's defence
        auto* brina = w.findNPC("Brina");
        if (brina) {
            InfluenceMessage msg;
            msg.id            = "alaric_hero";
            msg.topic         = "Alaric held the gate alone against the wolves";
            msg.originatorId  = brina->id;
            msg.originatorName = "Brina";
            msg.charge        = +0.75f;  // inspiring / uplifting
            msg.reliability   = 1.0f;
            msg.createdAt     = w.time().totalHours();
            msg.expiresAt     = 22.0f;
            msg.reachedIds.push_back(brina->id);
            msg.reachedNames.push_back(brina->name);
            g_influence.seed(std::move(msg));
        }
    });

    // === 16:00 - Village meeting ===
    world.eventManager().scheduleEvent(16.0f, "village_meeting", [](GameWorld& w) {
        std::cout << "\n  ** VILLAGE MEETING AT THE SQUARE **\n\n";
        std::cout << "[" << w.time().formatClock()
                  << "] All villagers gather at the Square to discuss the wolf attack.\n\n";

        // Physically move all living villagers to the square so
        // proximity-based systems (contagion, influence) fire correctly.
        for (auto& npc : w.npcs()) {
            if (npc->type != NPCType::Enemy && npc->combat.stats.isAlive()) {
                npc->position = Vec2(20.0f + (static_cast<float>(npc->id % 3) - 1.0f),
                                     12.0f + (static_cast<float>(npc->id % 2) * 0.8f));
            }
        }

        // Everyone gathers and talks
        auto* alaric = w.findNPC("Alaric");
        auto* brina = w.findNPC("Brina");
        auto* cedric = w.findNPC("Cedric");
        auto* dagna = w.findNPC("Dagna");
        auto* elmund = w.findNPC("Elmund");

        if (alaric) {
            std::cout << "[" << w.time().formatClock()
                      << "] Alaric: \"The wolves attacked from the east. "
                      << "We need better patrols near the forest.\"\n";
        }
        if (brina) {
            std::cout << "[" << w.time().formatClock()
                      << "] Brina: \"I fought alongside Alaric. "
                      << "My hammer proved useful!\"\n";
        }
        if (cedric) {
            std::cout << "[" << w.time().formatClock()
                      << "] Cedric: \"Between the thief and the wolves, "
                      << "it's been a dangerous day for business.\"\n";
        }
        if (dagna) {
            std::cout << "[" << w.time().formatClock()
                      << "] Dagna: \"Free drinks tonight for our brave defenders!\"\n";
        }
        if (elmund) {
            std::cout << "[" << w.time().formatClock()
                      << "] Elmund: \"The wolves were near my farm! "
                      << "Thank goodness for Alaric.\"\n";
        }

        std::cout << "\n";

        // Gossip - memory sharing
        if (alaric && dagna) {
            std::cout << "[" << w.time().formatClock()
                      << "] Alaric gossips to Dagna: \"Did you hear? "
                      << "We caught a thief at the market earlier!\"\n";
            dagna->memory.addMemory(MemoryType::WorldEvent,
                "Thief at the market (from Alaric's gossip)", -0.2f,
                std::nullopt, 0.4f, w.time().totalHours());
            std::cout << "[" << w.time().formatClock()
                      << "] Dagna remembers: \"Thief at the market\" (from gossip)\n";
            logRelationship(w.time().formatClock(), "Alaric", "Dagna",
                            alaric->id, dagna->id, 3.0f);
        }

        if (brina && elmund) {
            std::cout << "[" << w.time().formatClock()
                      << "] Brina gossips to Elmund: \"Did you hear? "
                      << "Wolves attacked the village!\"\n";
            elmund->memory.addMemory(MemoryType::WorldEvent,
                "Wolf attack near village (from Brina's gossip)", -0.3f,
                std::nullopt, 0.5f, w.time().totalHours());
            std::cout << "[" << w.time().formatClock()
                      << "] Elmund remembers: \"Wolf attack near village\" (from gossip)\n";
            logRelationship(w.time().formatClock(), "Brina", "Elmund",
                            brina->id, elmund->id, 3.0f);
        }

        // Shared danger strengthens bonds
        std::cout << "\n  [Shared danger strengthens village bonds]\n";
        for (auto& a : w.npcs()) {
            if (a->type == NPCType::Enemy || !a->combat.stats.isAlive()) continue;
            if (a->name == "Farhan") continue;
            for (auto& b : w.npcs()) {
                if (b->id <= a->id) continue;
                if (b->type == NPCType::Enemy || !b->combat.stats.isAlive()) continue;
                if (b->name == "Farhan") continue;
                g_relationships.modifyValue(std::to_string(a->id), std::to_string(b->id), 5.0f);
            }
            a->emotions.satisfyNeed(NeedType::Social, 20.0f);
        }
    });

    // === 17:00 - Elmund-Dagna trade ===
    world.eventManager().scheduleEvent(17.0f, "afternoon_trade", [](GameWorld& w) {
        std::cout << "\n  ** Afternoon trade **\n\n";
        auto* elmund = w.findNPC("Elmund");
        auto* dagna = w.findNPC("Dagna");
        if (!elmund || !dagna) return;

        std::cout << "[" << w.time().formatClock()
                  << "] Elmund sells 10x Wheat to Dagna for 25 gold.\n";
        elmund->trade.inventory.removeItem(ITEM_WHEAT, 10);
        elmund->trade.inventory.addGold(25.0f);
        dagna->trade.inventory.addItem(ITEM_WHEAT, 10);
        dagna->trade.inventory.spendGold(25.0f);

        std::cout << "[" << w.time().formatClock()
                  << "] Dagna: \"Fresh wheat! The tavern stew will be excellent tonight.\"\n";

        elmund->memory.addMemory(MemoryType::Trade,
            "Sold wheat to Dagna", 0.3f, dagna->id, 0.5f, w.time().totalHours());
        dagna->memory.addMemory(MemoryType::Trade,
            "Bought wheat from Elmund", 0.2f, elmund->id, 0.5f, w.time().totalHours());

        logRelationship(w.time().formatClock(), "Elmund", "Dagna",
                        elmund->id, dagna->id, 5.0f);

        w.events().publish(TradeEvent{dagna->id, elmund->id, ITEM_WHEAT, 10, 25.0f});

        // Dagna shares bread with Brina
        auto* brina = w.findNPC("Brina");
        if (brina) {
            std::cout << "[" << w.time().formatClock()
                      << "] Dagna gives 3x Fresh Bread to Brina as thanks for fighting wolves.\n";
            dagna->trade.inventory.removeItem(ITEM_BREAD, 3);
            brina->trade.inventory.addItem(ITEM_BREAD, 3);
            logRelationship(w.time().formatClock(), "Dagna", "Brina",
                            dagna->id, brina->id, 5.0f);
        }
    });

    // === 18:00 - Evening festival ===
    world.eventManager().scheduleEvent(18.0f, "evening_festival", [](GameWorld& w) {
        std::cout << "\n  ** EVENING FESTIVAL AT THE TAVERN **\n\n";
        std::cout << "[" << w.time().formatClock()
                  << "] The village gathers at Dagna's Tavern to celebrate surviving the day.\n\n";

        for (auto& npc : w.npcs()) {
            if (npc->type == NPCType::Enemy || !npc->combat.stats.isAlive()) continue;
            if (npc->name == "Farhan") continue;
            npc->emotions.satisfyNeed(NeedType::Social, 30.0f);
            npc->emotions.satisfyNeed(NeedType::Fun, 25.0f);
            npc->emotions.satisfyNeed(NeedType::Comfort, 15.0f);
            npc->emotions.satisfyNeed(NeedType::Hunger, 20.0f);
            npc->emotions.addEmotion(EmotionType::Happy, 0.5f, 3.0f);

            std::cout << "[" << w.time().formatClock() << "] " << npc->name
                      << " enjoys the festival. Mood: " << npc->emotions.getMoodString() << "\n";
        }
    });

    // === 19:00 - Festival gossip ===
    world.eventManager().scheduleEvent(19.0f, "festival_gossip", [](GameWorld& w) {
        std::cout << "\n  ** Festival continues - gossip and stories **\n\n";

        auto* dagna = w.findNPC("Dagna");
        auto* cedric = w.findNPC("Cedric");
        auto* alaric = w.findNPC("Alaric");
        auto* brina = w.findNPC("Brina");
        auto* elmund = w.findNPC("Elmund");

        if (dagna && cedric) {
            std::cout << "[" << w.time().formatClock()
                      << "] Dagna gossips to Cedric: \"Did you hear? "
                      << "Wolves attacked earlier!\"\n";
            cedric->memory.addMemory(MemoryType::WorldEvent,
                "Wolf attack near village (from Dagna's gossip)", -0.3f,
                std::nullopt, 0.4f, w.time().totalHours());
            std::cout << "[" << w.time().formatClock()
                      << "] Cedric remembers: \"Wolf attack near village\" (from gossip)\n";
        }

        if (alaric && elmund) {
            std::cout << "[" << w.time().formatClock()
                      << "] Alaric tells Elmund the story of defeating the Wolf Alpha.\n";
            elmund->memory.addMemory(MemoryType::Interaction,
                "Alaric's tale of defeating the Alpha Wolf", 0.4f,
                alaric->id, 0.7f, w.time().totalHours());
            logRelationship(w.time().formatClock(), "Alaric", "Elmund",
                            alaric->id, elmund->id, 5.0f);
        }

        if (brina && dagna) {
            std::cout << "[" << w.time().formatClock()
                      << "] Brina: \"I never thought my blacksmith hammer would be a weapon!\"\n";
            std::cout << "[" << w.time().formatClock()
                      << "] Dagna: \"Here's to Brina, the warrior-smith! Another ale!\"\n";
            logRelationship(w.time().formatClock(), "Brina", "Dagna",
                            brina->id, dagna->id, 3.0f);
        }

        // All relationships improve during festival
        for (auto& a : w.npcs()) {
            if (a->type == NPCType::Enemy || !a->combat.stats.isAlive()) continue;
            if (a->name == "Farhan") continue;
            for (auto& b : w.npcs()) {
                if (b->id <= a->id) continue;
                if (b->type == NPCType::Enemy || !b->combat.stats.isAlive()) continue;
                if (b->name == "Farhan") continue;
                g_relationships.modifyValue(std::to_string(a->id), std::to_string(b->id), 2.0f);
            }
        }
    });

    // === 20:00 - Night routine ===
    world.eventManager().scheduleEvent(20.0f, "night_routine", [](GameWorld& w) {
        std::cout << "\n  ** Night falls. The village winds down. **\n\n";

        auto* alaric = w.findNPC("Alaric");
        if (alaric) {
            std::cout << "[" << w.time().formatClock()
                      << "] Alaric: \"Time for the night watch. "
                      << "I'll keep the village safe.\"\n";
            alaric->moveTo(Vec2(3.0f, 12.0f)); // Go to gate
        }

        for (auto& npc : w.npcs()) {
            if (npc->type == NPCType::Enemy) continue;
            if (npc->name == "Farhan") continue;
            if (npc->name == "Alaric") continue;
            if (npc->combat.stats.isAlive()) {
                std::cout << "[" << w.time().formatClock() << "] " << npc->name
                          << " heads home to rest. Mood: "
                          << npc->emotions.getMoodString() << "\n";
            }
        }
    });
}
