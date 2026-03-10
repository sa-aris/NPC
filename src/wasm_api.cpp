// wasm_api.cpp — WASM entry point for the NPC Behavior System browser demo.
// Compiles only with Emscripten (guarded in CMakeLists.txt).
// Exposes C functions that return JSON state each simulation step.

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define WASM_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define WASM_EXPORT
#endif

#include "npc/npc.hpp"
#include "npc/world/world.hpp"
#include "npc/social/relationship_system.hpp"
#include "npc/social/influence_chain.hpp"

#include <memory>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <map>
#include <set>
#include <cstdlib>
#include <cmath>
#include <algorithm>

using namespace npc;

// ─── Log ──────────────────────────────────────────────────────────────────────

enum class LogType { Info, Social, Influence, Memory, Emotion, Event };

struct LogEntry {
    std::string time;
    LogType     type;
    std::string text;
};

// ─── Globals ──────────────────────────────────────────────────────────────────

static std::unique_ptr<GameWorld>   g_world;
static RelationshipSystem           g_rel;
static InfluenceChainSystem         g_influence;
static bool                         g_initialized = false;
static std::vector<LogEntry>        g_pendingLog;
static std::map<std::string, bool>  g_fired;
static std::set<std::string>        g_influencedThisStep;
static std::string                  s_stateJson;

static constexpr float SIM_END         = 16.0f; // 16 sim hours = 06:00-22:00
static constexpr float CONTAGION_RANGE = 8.0f;
static constexpr float INFLUENCE_RANGE = 6.0f;

static void logE(LogType t, const std::string& text) {
    g_pendingLog.push_back({g_world->time().formatClock(), t, text});
}

// ─── NPC creation ─────────────────────────────────────────────────────────────

static std::shared_ptr<NPC> makeNPC(EntityId id, const std::string& name,
                                     NPCType type, Vec2 pos, PersonalityTraits p) {
    auto n = std::make_shared<NPC>(id, name, type);
    n->position   = pos;
    n->personality = p;
    n->emotions.applyPersonality(p);
    return n;
}

// ─── Influence chain processing ───────────────────────────────────────────────

static void processInfluence(float currentHour) {
    g_influencedThisStep.clear();
    const auto& npcs = g_world->npcs();
    for (size_t i = 0; i < npcs.size(); ++i) {
        auto& sender = *npcs[i];
        for (size_t j = 0; j < npcs.size(); ++j) {
            if (i == j) continue;
            auto& receiver = *npcs[j];
            float dist = sender.position.distanceTo(receiver.position);
            if (dist > INFLUENCE_RANGE) continue;
            float prox   = 1.0f - dist / INFLUENCE_RANGE;
            float chance = 0.08f + prox * 0.06f + sender.personality.sociability * 0.04f;
            if (static_cast<float>(std::rand()) / RAND_MAX > chance) continue;
            float rel = g_rel.getValue(std::to_string(sender.id), std::to_string(receiver.id));
            if (rel < -20.0f) continue;

            for (auto& msg : const_cast<std::vector<InfluenceMessage>&>(g_influence.messages())) {
                if (currentHour > msg.expiresAt) continue;
                if (!msg.hasReached(sender.id))   continue;
                if (msg.hasReached(receiver.id))  continue;
                std::string key = msg.id + "|" + std::to_string(sender.id)
                                + ">" + std::to_string(receiver.id);
                if (g_influencedThisStep.count(key)) continue;
                g_influencedThisStep.insert(key);

                float newRel  = msg.reliability * 0.82f
                              * (1.0f - receiver.personality.intelligence * 0.15f);
                newRel        = std::max(0.0f, newRel);
                float willing = 0.4f + sender.personality.sociability * 0.3f
                              + (1.0f - sender.personality.patience) * 0.2f
                              + rel / 200.0f;
                bool accepted = (newRel >= 0.08f + receiver.personality.intelligence * 0.35f)
                             && (willing >= 0.4f);

                float empathy  = receiver.personality.empathyMultiplier();
                float chargeOut = msg.charge * (0.65f + empathy * 0.35f);
                if (newRel < 0.30f)
                    chargeOut *= (1.0f - ((0.30f - newRel) / 0.30f) * 1.4f);
                chargeOut = std::clamp(chargeOut, -1.0f, 1.0f);

                if (accepted) {
                    Memory m;
                    m.type           = MemoryType::WorldEvent;
                    m.description    = msg.topic + " (from " + sender.name + ")";
                    m.emotionalImpact = chargeOut;
                    m.importance     = 0.3f + std::abs(chargeOut) * 0.5f;
                    m.timestamp      = currentHour;
                    m.reliability    = newRel;
                    m.decayRate      = 0.01f;
                    m.currentStrength = 1.0f;
                    receiver.memory.receiveGossip(m, sender.id,
                        std::clamp(rel, -100.0f, 100.0f), currentHour, 1);

                    EmotionType emo = chargeOut < -0.35f ? EmotionType::Fearful
                                    : chargeOut < -0.05f ? EmotionType::Sad
                                    : chargeOut >  0.45f ? EmotionType::Happy
                                    :                      EmotionType::Surprised;
                    if (std::abs(chargeOut) > 0.15f)
                        receiver.emotions.addEmotion(emo, std::abs(chargeOut) * 0.55f, 2.0f);

                    g_influence.recordHop(msg.id, receiver.id, receiver.name,
                                          newRel, chargeOut);

                    std::string reaction;
                    if      (chargeOut < -0.6f) reaction = receiver.name + " goes pale.";
                    else if (chargeOut < -0.2f) reaction = receiver.name + " looks uneasy.";
                    else if (chargeOut <  0.2f) reaction = receiver.name + " nods slowly.";
                    else                        reaction = receiver.name + " lights up.";

                    logE(LogType::Influence,
                         "CHAIN [" + msg.id + "] "
                         + sender.name + " -> " + receiver.name
                         + " (hop " + std::to_string(msg.hopCount) + ")"
                         + " rel:" + std::to_string(newRel).substr(0, 4)
                         + " | " + reaction);
                } else {
                    logE(LogType::Influence,
                         "CHAIN [" + msg.id + "] "
                         + receiver.name + " dismisses " + sender.name + "."
                         + " (rel too low: " + std::to_string(newRel).substr(0,4) + ")");
                }
            }
        }
    }
}

// ─── Contagion ────────────────────────────────────────────────────────────────

static void processContagion() {
    const auto& npcs = g_world->npcs();
    for (size_t i = 0; i < npcs.size(); ++i) {
        auto& src  = *npcs[i];
        auto  aura = src.emotions.getEmotionalAura();
        if (aura.type == EmotionType::Neutral || aura.intensity < 0.1f) continue;
        for (size_t j = 0; j < npcs.size(); ++j) {
            if (i == j) continue;
            auto& rcv = *npcs[j];
            float dist = src.position.distanceTo(rcv.position);
            if (dist >= CONTAGION_RANGE) continue;
            float prox = 1.0f - dist / CONTAGION_RANGE;
            rcv.emotions.applyContagion(aura.type, aura.intensity,
                                         rcv.personality.empathyMultiplier(), prox);
        }
    }
}

// ─── Memory decay narrative ───────────────────────────────────────────────────

static void processMemoryDecay() {
    for (const auto& npc : g_world->npcs()) {
        for (const auto& ev : npc->memory.drainFadeEvents()) {
            std::string stage =
                ev.stage == MemoryFadeStage::Fading          ? "fading"
              : ev.stage == MemoryFadeStage::NearlyForgotten ? "nearly forgotten"
              :                                                 "forgotten";
            std::string desc = ev.snapshot.description;
            if (desc.size() > 40) desc = desc.substr(0, 37) + "...";
            logE(LogType::Memory,
                 "MEMORY [" + stage + "] " + npc->name + ": \"" + desc + "\"");
        }
    }
}

// ─── Influence seed helper ────────────────────────────────────────────────────

static void seedInfluence(EntityId originId, const std::string& origName,
                           const std::string& id, const std::string& topic,
                           float charge, float currentHour) {
    InfluenceMessage msg;
    msg.id             = id;
    msg.topic          = topic;
    msg.originatorId   = originId;
    msg.originatorName = origName;
    msg.charge         = charge;
    msg.reliability    = 1.0f;
    msg.createdAt      = currentHour;
    msg.expiresAt      = 22.0f;
    msg.reachedIds.push_back(originId);
    msg.reachedNames.push_back(origName);
    g_influence.seed(std::move(msg));
    logE(LogType::Influence,
         "SEEDED [" + id + "] by " + origName + ": \"" + topic + "\"");
}

// ─── Scheduled events ─────────────────────────────────────────────────────────

static void fireEvents(float h) {
    const auto& npcs = g_world->npcs();
    auto find = [&](const std::string& name) -> NPC* {
        for (auto& n : npcs) if (n->name == name) return n.get();
        return nullptr;
    };

    // 07:00 — Dawn patrol report
    if (h >= 7.0f && !g_fired["patrol"]) {
        g_fired["patrol"] = true;
        auto* a = find("Alaric");
        if (a) {
            seedInfluence(a->id, "Alaric", "patrol_report",
                          "strange tracks near the forest at dawn", -0.45f, h);
            a->emotions.addEmotion(EmotionType::Surprised, 0.4f, 1.5f);
            a->memory.addMemory(MemoryType::WorldEvent,
                                "Unusual tracks spotted on patrol",
                                -0.2f, std::nullopt, 0.6f, h);
        }
        logE(LogType::Event, "Alaric returns from dawn patrol — noticed unusual tracks.");
    }

    // 09:00 — Morning social: everyone converges to square
    if (h >= 9.0f && !g_fired["social_am"]) {
        g_fired["social_am"] = true;
        for (size_t k = 0; k < npcs.size(); ++k) {
            float ox = static_cast<float>(static_cast<int>(npcs[k]->id) % 3 - 1);
            float oy = static_cast<float>(static_cast<int>(npcs[k]->id) % 2) * 0.6f;
            npcs[k]->position = Vec2(20.0f + ox, 12.0f + oy);
            npcs[k]->emotions.satisfyNeed(NeedType::Social, 15.0f);
        }
        logE(LogType::Social, "Morning social hour — villagers gather at the Square.");
    }

    // 10:00 — Cedric-Elmund trade
    if (h >= 10.0f && !g_fired["trade"]) {
        g_fired["trade"] = true;
        auto* c = find("Cedric"); auto* e = find("Elmund");
        if (c && e) {
            c->memory.addMemory(MemoryType::Trade, "Sold tools to Elmund",
                                0.1f, e->id, 0.4f, h);
            e->memory.addMemory(MemoryType::Trade, "Bought tools from Cedric",
                                0.1f, c->id, 0.4f, h);
            g_rel.modifyValue(std::to_string(c->id), std::to_string(e->id),  5.0f);
            g_rel.modifyValue(std::to_string(e->id), std::to_string(c->id),  5.0f);
            logE(LogType::Social, "Cedric sold farming tools to Elmund.");
        }
    }

    // 11:00 — Thief at market
    if (h >= 11.0f && !g_fired["thief"]) {
        g_fired["thief"] = true;
        auto* a = find("Alaric"); auto* c = find("Cedric");
        if (a) {
            a->emotions.addEmotion(EmotionType::Angry, 0.6f, 2.0f);
            a->memory.addMemory(MemoryType::WorldEvent, "Chased a thief at the market",
                                0.3f, std::nullopt, 0.7f, h);
        }
        if (c) {
            c->emotions.addEmotion(EmotionType::Angry, 0.4f, 1.5f);
            c->memory.addMemory(MemoryType::WorldEvent, "Thief tried to rob my stall",
                                -0.4f, std::nullopt, 0.6f, h);
        }
        logE(LogType::Event, "!! THIEF SPOTTED at the market — Alaric gives chase!");
    }

    // 14:00 — Wolf attack: fear wave + panic influence
    if (h >= 14.0f && !g_fired["wolf"]) {
        g_fired["wolf"] = true;
        auto* a = find("Alaric"); auto* b = find("Brina");
        // Scatter NPCs (wolf attack pushes them)
        for (size_t k = 0; k < npcs.size(); ++k) {
            float fear = (npcs[k]->name == "Elmund") ? 0.95f : 0.65f;
            npcs[k]->emotions.addEmotion(EmotionType::Fearful, fear, 3.5f);
            npcs[k]->emotions.depletNeed(NeedType::Safety, 35.0f);
            npcs[k]->memory.addMemory(MemoryType::WorldEvent,
                                      "Wolf pack attacked the village",
                                      -0.8f, std::nullopt, 0.9f, h);
        }
        if (a) {
            a->position = Vec2(21.0f, 12.0f);
            a->emotions.addEmotion(EmotionType::Angry,  0.8f, 4.0f);
            seedInfluence(a->id, "Alaric", "wolf_attack",
                          "wolves are attacking the village", -0.85f, h);
        }
        if (b) {
            b->position = Vec2(20.0f, 11.0f);
            b->emotions.addEmotion(EmotionType::Angry, 0.7f, 4.0f);
            b->memory.addMemory(MemoryType::Combat, "Fought wolves alongside Alaric",
                                -0.5f, a ? std::make_optional(a->id) : std::nullopt, 0.9f, h);
        }
        logE(LogType::Event, "!!! WOLF PACK ATTACKS THE VILLAGE !!!");
    }

    // 15:30 — Post-combat: relief + hero narrative
    if (h >= 15.5f && !g_fired["postcombat"]) {
        g_fired["postcombat"] = true;
        auto* b = find("Brina"); auto* a = find("Alaric");
        for (size_t k = 0; k < npcs.size(); ++k) {
            npcs[k]->emotions.addEmotion(EmotionType::Happy, 0.5f, 3.0f);
            npcs[k]->emotions.satisfyNeed(NeedType::Safety, 20.0f);
        }
        if (b) seedInfluence(b->id, "Brina", "alaric_hero",
                              "Alaric held the gate alone against the wolves",
                              +0.75f, h);
        if (a) a->emotions.addEmotion(EmotionType::Happy, 0.8f, 4.0f);
        logE(LogType::Event, "Wolves defeated! Village celebrates Alaric's bravery.");
    }

    // 16:00 — Village meeting: everyone to square
    if (h >= 16.0f && !g_fired["meeting"]) {
        g_fired["meeting"] = true;
        for (size_t k = 0; k < npcs.size(); ++k) {
            float ox = static_cast<float>(static_cast<int>(npcs[k]->id) % 3 - 1);
            float oy = static_cast<float>(static_cast<int>(npcs[k]->id) % 2) * 0.8f;
            npcs[k]->position = Vec2(20.0f + ox, 12.0f + oy);
            npcs[k]->emotions.satisfyNeed(NeedType::Social, 10.0f);
        }
        logE(LogType::Social, "Village meeting at the Square — all gather to discuss the attack.");
    }

    // 19:00 — Evening festival
    if (h >= 19.0f && !g_fired["festival"]) {
        g_fired["festival"] = true;
        for (size_t k = 0; k < npcs.size(); ++k) {
            npcs[k]->emotions.satisfyNeed(NeedType::Fun,    20.0f);
            npcs[k]->emotions.satisfyNeed(NeedType::Social, 20.0f);
            float ox = static_cast<float>(static_cast<int>(npcs[k]->id) % 5 - 2) * 0.5f;
            float oy = static_cast<float>(static_cast<int>(npcs[k]->id) % 3 - 1) * 0.5f;
            npcs[k]->position = Vec2(20.0f + ox, 12.0f + oy);
        }
        logE(LogType::Social, "Festival begins — villagers celebrate, gossip and dance.");
    }
}

// ─── JSON helpers ─────────────────────────────────────────────────────────────

static std::string jstr(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if      (c == '"')  o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "";
        else                o += c;
    }
    return o + "\"";
}

static std::string buildStateJson() {
    std::ostringstream j;
    j << std::fixed << std::setprecision(3);
    j << "{";
    j << "\"time\":"   << jstr(g_world->time().formatClock()) << ",";
    j << "\"tod\":"    << jstr(timeOfDayToString(g_world->time().getTimeOfDay())) << ",";
    j << "\"hour\":"   << g_world->time().currentHour() << ",";
    j << "\"complete\":" << (g_world->time().currentHour() >= 22.0f ? 1 : 0) << ",";

    // NPCs
    j << "\"npcs\":[";
    const auto& npcs = g_world->npcs();
    for (size_t i = 0; i < npcs.size(); ++i) {
        if (i > 0) j << ",";
        const auto& n  = *npcs[i];
        auto  dom  = n.emotions.getDominantEmotion();
        auto  aura = n.emotions.getEmotionalAura();
        j << "{";
        j << "\"name\":"      << jstr(n.name) << ",";
        j << "\"type\":"      << jstr(npcTypeToString(n.type)) << ",";
        j << "\"emotion\":"   << jstr(emotionToString(dom)) << ",";
        j << "\"intensity\":" << aura.intensity << ",";
        j << "\"mood\":"      << jstr(n.emotions.getMoodString()) << ",";
        j << "\"moodScore\":" << n.emotions.getMood() << ",";
        j << "\"x\":"         << n.position.x << ",";
        j << "\"y\":"         << n.position.y;
        j << "}";
    }
    j << "],";

    // Influence chains
    j << "\"chains\":[";
    const auto& msgs = g_influence.messages();
    for (size_t i = 0; i < msgs.size(); ++i) {
        if (i > 0) j << ",";
        const auto& m = msgs[i];
        j << "{";
        j << "\"id\":"          << jstr(m.id) << ",";
        j << "\"topic\":"       << jstr(m.topic) << ",";
        j << "\"charge\":"      << m.charge << ",";
        j << "\"reliability\":" << m.reliability << ",";
        j << "\"hops\":"        << m.hopCount << ",";
        j << "\"reached\":"     << m.reachedIds.size() << ",";
        j << "\"chain\":"       << jstr(m.chainString());
        j << "}";
    }
    j << "],";

    // Log entries since last step
    j << "\"log\":[";
    for (size_t i = 0; i < g_pendingLog.size(); ++i) {
        if (i > 0) j << ",";
        const auto& e = g_pendingLog[i];
        const char* t = e.type == LogType::Info      ? "info"
                      : e.type == LogType::Social    ? "social"
                      : e.type == LogType::Influence ? "influence"
                      : e.type == LogType::Memory    ? "memory"
                      : e.type == LogType::Emotion   ? "emotion"
                      :                                "event";
        j << "{\"time\":" << jstr(e.time)
          << ",\"type\":" << jstr(t)
          << ",\"text\":" << jstr(e.text) << "}";
    }
    j << "]";
    j << "}";
    g_pendingLog.clear();
    return j.str();
}

// ─── C exports ────────────────────────────────────────────────────────────────

extern "C" {

WASM_EXPORT void npc_init() {
    g_world       = std::make_unique<GameWorld>(40, 25);
    g_rel         = RelationshipSystem{};
    g_influence   = InfluenceChainSystem{};
    g_fired.clear();
    g_pendingLog.clear();
    g_influencedThisStep.clear();

    // Map: roads
    for (int x = 0; x < 40; ++x) {
        g_world->setCell(x, 12, CellType::Road, 0.8f);
        g_world->setCell(x, 13, CellType::Road, 0.8f);
    }
    for (int y = 5; y < 20; ++y) {
        g_world->setCell(20, y, CellType::Road, 0.8f);
        g_world->setCell(21, y, CellType::Road, 0.8f);
    }
    // Buildings
    for (int y = 3; y <= 6; ++y) for (int x = 5;  x <= 10; ++x)
        g_world->setCell(x, y, CellType::Building, 999.0f, false);
    for (int y = 8; y <= 10; ++y) for (int x = 14; x <= 18; ++x)
        g_world->setCell(x, y, CellType::Building, 999.0f, false);
    for (int y = 10; y <= 11; ++y) for (int x = 23; x <= 27; ++x)
        g_world->setCell(x, y, CellType::Building, 999.0f, false);
    // Forest
    for (int y = 0; y < 25; ++y) for (int x = 37; x <= 39; ++x)
        g_world->setCell(x, y, CellType::Forest, 2.0f);
    // Gate walls
    for (int x = 0; x <= 2; ++x) {
        g_world->setCell(x, 12, CellType::Wall, 999.0f, false);
        g_world->setCell(x, 13, CellType::Wall, 999.0f, false);
    }
    // Locations
    g_world->addLocation("Tavern",  8.0f,  7.0f);
    g_world->addLocation("Forge",   16.0f, 11.0f);
    g_world->addLocation("Market",  25.0f, 12.0f);
    g_world->addLocation("Square",  20.0f, 12.0f);
    g_world->addLocation("Farm",    34.0f, 21.0f);

    // NPCs
    auto alaric = makeNPC(1,"Alaric",NPCType::Guard,    Vec2(20.0f,12.0f), PersonalityTraits::guard());
    auto brina  = makeNPC(2,"Brina", NPCType::Blacksmith,Vec2(16.0f,11.0f),PersonalityTraits::blacksmith());
    auto cedric = makeNPC(3,"Cedric",NPCType::Merchant,  Vec2(25.0f,12.0f),PersonalityTraits::merchant());
    auto dagna  = makeNPC(4,"Dagna", NPCType::Innkeeper, Vec2(8.0f, 7.0f), PersonalityTraits::innkeeper());
    auto elmund = makeNPC(5,"Elmund",NPCType::Farmer,    Vec2(34.0f,21.0f),PersonalityTraits::farmer());

    g_world->addNPC(alaric);
    g_world->addNPC(brina);
    g_world->addNPC(cedric);
    g_world->addNPC(dagna);
    g_world->addNPC(elmund);

    // Initial relationships
    auto id = [](const std::shared_ptr<NPC>& n){ return std::to_string(n->id); };
    g_rel.setValue(id(alaric), id(brina),   40.0f); g_rel.setValue(id(brina),  id(alaric), 40.0f);
    g_rel.setValue(id(alaric), id(cedric),  25.0f); g_rel.setValue(id(cedric), id(alaric), 25.0f);
    g_rel.setValue(id(alaric), id(dagna),   35.0f); g_rel.setValue(id(dagna),  id(alaric), 35.0f);
    g_rel.setValue(id(brina),  id(dagna),   45.0f); g_rel.setValue(id(dagna),  id(brina),  45.0f);
    g_rel.setValue(id(cedric), id(elmund),  30.0f); g_rel.setValue(id(elmund), id(cedric), 30.0f);
    g_rel.setValue(id(dagna),  id(elmund),  40.0f); g_rel.setValue(id(elmund), id(dagna),  40.0f);

    g_initialized = true;
    logE(LogType::Event, "Village simulation ready. Day begins at 06:00.");
}

WASM_EXPORT const char* npc_step(float dt) {
    if (!g_initialized) npc_init();
    float h = g_world->time().currentHour();
    if (h >= 22.0f) {
        s_stateJson = buildStateJson();
        return s_stateJson.c_str();
    }
    fireEvents(h);
    g_world->update(dt);
    g_rel.update(h, dt);
    processContagion();
    processInfluence(h);
    processMemoryDecay();
    s_stateJson = buildStateJson();
    return s_stateJson.c_str();
}

WASM_EXPORT int npc_is_complete() {
    return (g_world && g_world->time().currentHour() >= 22.0f) ? 1 : 0;
}

} // extern "C"
