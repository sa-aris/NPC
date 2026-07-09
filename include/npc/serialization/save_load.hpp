#pragma once
// save_load.hpp — whole-world snapshot: one JSON file for every system
// Part of Aithena (C++17, header-only)
//
// Complements NpcSerializer (per-NPC state) with serializers for the shared,
// world-level systems — relationships, reputation & crimes, the economy, and
// families — plus a WorldSaveGame orchestrator that bundles any combination
// of them (and custom caller sections) into a single versioned save file.
//
// Static data (item registries, recipes, quest templates) is code-defined:
// recreate it as usual on startup, then call load() to restore dynamic state.

#include "json.hpp"
#include "../social/relationship_system.hpp"
#include "../social/reputation_system.hpp"
#include "../social/family_system.hpp"
#include "../economy/economy_system.hpp"
#include <string>
#include <functional>
#include <map>

namespace npc {

struct WorldSerializer {
    using J = serial::JsonValue;

    // ═══ RelationshipSystem ═════════════════════════════════════════════

    static J toJson(const RelationshipSystem& rs) {
        serial::JsonArray pairs;
        rs.forEach([&](const std::string& a, const std::string& b,
                       const RelationshipData& d) {
            serial::JsonObject p;
            p["a"]     = a;
            p["b"]     = b;
            p["value"] = d.value;
            p["trust"] = d.trust;
            serial::JsonArray history;
            for (const auto& ev : d.history) {
                serial::JsonObject e;
                e["type"]      = static_cast<int64_t>(ev.type);
                e["initiator"] = ev.initiator;
                e["target"]    = ev.target;
                e["delta"]     = ev.delta;
                e["magnitude"] = ev.magnitude;
                e["time"]      = ev.simTime;
                if (!ev.note.empty())     e["note"]     = ev.note;
                if (!ev.location.empty()) e["location"] = ev.location;
                history.push_back(std::move(e));
            }
            p["history"] = std::move(history);
            pairs.push_back(std::move(p));
        });
        serial::JsonObject root;
        root["pairs"] = std::move(pairs);
        return root;
    }

    static void fromJson(RelationshipSystem& rs, const J& j) {
        for (const auto& p : j["pairs"].asArray()) {
            const std::string a = p["a"].asString();
            const std::string b = p["b"].asString();
            auto& d = rs.get(a, b);
            d.value = p["value"].asFloat();
            d.trust = p["trust"].asFloat();
            d.history.clear();
            for (const auto& e : p["history"].asArray()) {
                RelationshipEvent ev;
                ev.type      = static_cast<RelationshipEventType>(e["type"].asInt());
                ev.initiator = e["initiator"].asString();
                ev.target    = e["target"].asString();
                ev.delta     = e["delta"].asFloat();
                ev.magnitude = e["magnitude"].asFloat();
                ev.simTime   = e["time"].asDouble();
                ev.note      = e["note"].asString();
                ev.location  = e["location"].asString();
                d.addEvent(std::move(ev));
            }
        }
    }

    // ═══ ReputationSystem ═══════════════════════════════════════════════

    static J toJson(const ReputationSystem& rs) {
        serial::JsonObject reps;
        for (const auto& [id, rep] : rs.allReputations()) reps[id] = rep;

        serial::JsonArray crimes;
        for (const auto& c : rs.allCrimes()) {
            serial::JsonObject j;
            j["id"]          = static_cast<int64_t>(c.id);
            j["type"]        = static_cast<int64_t>(c.type);
            j["perpetrator"] = c.perpetrator;
            j["victim"]      = c.victim;
            j["location"]    = c.location;
            j["time"]        = c.simTime;
            j["severity"]    = c.severity;
            j["reported"]    = c.reported;
            j["solved"]      = c.solved;
            j["bounty"]      = c.bounty;
            serial::JsonArray ws;
            for (const auto& w : c.witnesses) ws.push_back(w);
            j["witnesses"] = std::move(ws);
            crimes.push_back(std::move(j));
        }

        serial::JsonObject root;
        root["reputations"] = std::move(reps);
        root["crimes"]      = std::move(crimes);
        return root;
    }

    static void fromJson(ReputationSystem& rs, const J& j) {
        rs.clearAll();
        for (const auto& [id, rep] : j["reputations"].asObject())
            rs.setReputation(id, rep.asFloat());
        for (const auto& c : j["crimes"].asArray()) {
            CrimeRecord rec;
            rec.id          = static_cast<uint32_t>(c["id"].asInt());
            rec.type        = static_cast<CrimeType>(c["type"].asInt());
            rec.perpetrator = c["perpetrator"].asString();
            rec.victim      = c["victim"].asString();
            rec.location    = c["location"].asString();
            rec.simTime     = c["time"].asDouble();
            rec.severity    = c["severity"].asFloat();
            rec.reported    = c["reported"].asBool();
            rec.solved      = c["solved"].asBool();
            rec.bounty      = c["bounty"].asFloat();
            for (const auto& w : c["witnesses"].asArray())
                rec.witnesses.push_back(w.asString());
            rs.restoreCrime(std::move(rec));
        }
    }

    // ═══ EconomySystem (dynamic state only) ═════════════════════════════

    static J toJson(const EconomySystem& es) {
        serial::JsonArray settlements;
        for (const auto& [name, s] : es.settlements()) {
            serial::JsonObject j;
            j["name"]       = s.name;
            j["population"] = static_cast<int64_t>(s.population);
            j["treasury"]   = s.treasury;
            serial::JsonObject stock, target;
            for (const auto& [id, qty] : s.stockpile)
                stock[std::to_string(id)] = static_cast<int64_t>(qty);
            for (const auto& [id, qty] : s.targetStock)
                target[std::to_string(id)] = static_cast<int64_t>(qty);
            j["stockpile"]   = std::move(stock);
            j["targetStock"] = std::move(target);
            settlements.push_back(std::move(j));
        }

        serial::JsonArray producers;
        for (const auto& p : es.producers()) {
            serial::JsonObject j;
            j["npc"]         = p.npcId;
            j["recipe"]      = p.recipeId;
            j["settlement"]  = p.settlement;
            j["efficiency"]  = p.efficiency;
            j["hoursWorked"] = p.hoursWorked;
            j["batches"]     = static_cast<int64_t>(p.batchesDone);
            producers.push_back(std::move(j));
        }

        serial::JsonArray caravans;
        for (const auto& c : es.caravans()) {
            if (c.arrived) continue; // delivered goods are already in stockpiles
            serial::JsonObject j;
            j["id"]          = static_cast<int64_t>(c.id);
            j["from"]        = c.from;
            j["to"]          = c.to;
            j["item"]        = static_cast<int64_t>(c.item);
            j["qty"]         = static_cast<int64_t>(c.quantity);
            j["departTime"]  = c.departTime;
            j["travelHours"] = c.travelHours;
            caravans.push_back(std::move(j));
        }

        serial::JsonObject root;
        root["settlements"] = std::move(settlements);
        root["producers"]   = std::move(producers);
        root["caravans"]    = std::move(caravans);
        return root;
    }

    static void fromJson(EconomySystem& es, const J& j) {
        es.clearDynamicState();
        for (const auto& sj : j["settlements"].asArray()) {
            auto& s = es.addSettlement(sj["name"].asString(),
                                       static_cast<int>(sj["population"].asInt()));
            s.treasury = sj["treasury"].asFloat();
            for (const auto& [id, qty] : sj["stockpile"].asObject())
                s.stockpile[static_cast<ItemId>(std::stoul(id))] =
                    static_cast<int>(qty.asInt());
            for (const auto& [id, qty] : sj["targetStock"].asObject())
                s.targetStock[static_cast<ItemId>(std::stoul(id))] =
                    static_cast<int>(qty.asInt());
        }
        for (const auto& pj : j["producers"].asArray()) {
            es.assignProducer(pj["npc"].asString(), pj["recipe"].asString(),
                              pj["settlement"].asString(),
                              pj["efficiency"].asFloat(1.0f));
            if (!es.producersMutable().empty()) {
                auto& p = es.producersMutable().back();
                p.hoursWorked = pj["hoursWorked"].asFloat();
                p.batchesDone = static_cast<int>(pj["batches"].asInt());
            }
        }
        for (const auto& cj : j["caravans"].asArray()) {
            Caravan c;
            c.id          = static_cast<uint32_t>(cj["id"].asInt());
            c.from        = cj["from"].asString();
            c.to          = cj["to"].asString();
            c.item        = static_cast<ItemId>(cj["item"].asInt());
            c.quantity    = static_cast<int>(cj["qty"].asInt());
            c.departTime  = cj["departTime"].asDouble();
            c.travelHours = cj["travelHours"].asFloat();
            es.restoreCaravan(std::move(c));
        }
    }

    // ═══ FamilySystem ═══════════════════════════════════════════════════

    static J toJson(const FamilySystem& fs) {
        serial::JsonArray members;
        for (const auto& [id, m] : fs.allMembers()) {
            serial::JsonObject j;
            j["id"]        = m.id;
            j["age"]       = m.age;
            j["alive"]     = m.alive;
            j["spouse"]    = m.spouse;
            j["household"] = static_cast<int64_t>(m.householdId);
            j["wealth"]    = m.personalWealth;
            serial::JsonArray ps, cs;
            for (const auto& p : m.parents)  ps.push_back(p);
            for (const auto& c : m.children) cs.push_back(c);
            j["parents"]  = std::move(ps);
            j["children"] = std::move(cs);
            members.push_back(std::move(j));
        }

        serial::JsonArray households;
        for (const auto& [id, h] : fs.allHouseholds()) {
            serial::JsonObject j;
            j["id"]     = static_cast<int64_t>(h.id);
            j["name"]   = h.name;
            j["home"]   = h.homeLocation;
            j["wealth"] = h.wealth;
            serial::JsonArray ms;
            for (const auto& m : h.members) ms.push_back(m);
            j["members"] = std::move(ms);
            households.push_back(std::move(j));
        }

        serial::JsonObject root;
        root["members"]    = std::move(members);
        root["households"] = std::move(households);
        return root;
    }

    static void fromJson(FamilySystem& fs, const J& j) {
        fs.clearAll();
        for (const auto& hj : j["households"].asArray()) {
            Household h;
            h.id           = static_cast<uint32_t>(hj["id"].asInt());
            h.name         = hj["name"].asString();
            h.homeLocation = hj["home"].asString();
            h.wealth       = hj["wealth"].asFloat();
            for (const auto& m : hj["members"].asArray())
                h.members.push_back(m.asString());
            fs.restoreHousehold(std::move(h));
        }
        for (const auto& mj : j["members"].asArray()) {
            FamilyMember m;
            m.id             = mj["id"].asString();
            m.age            = mj["age"].asFloat();
            m.alive          = mj["alive"].asBool(true);
            m.spouse         = mj["spouse"].asString();
            m.householdId    = static_cast<uint32_t>(mj["household"].asInt());
            m.personalWealth = mj["wealth"].asFloat();
            for (const auto& p : mj["parents"].asArray())
                m.parents.push_back(p.asString());
            for (const auto& c : mj["children"].asArray())
                m.children.push_back(c.asString());
            fs.restoreMember(std::move(m));
        }
    }
};

// ─── WorldSaveGame — bundle everything into one versioned file ────────────────

class WorldSaveGame {
public:
    static constexpr int64_t FORMAT_VERSION = 1;

    using SaveFn = std::function<serial::JsonValue()>;
    using LoadFn = std::function<void(const serial::JsonValue&)>;

    // Attach systems — any subset works; nullptrs are simply skipped.
    RelationshipSystem* relationships = nullptr;
    ReputationSystem*   reputation    = nullptr;
    EconomySystem*      economy       = nullptr;
    FamilySystem*       families      = nullptr;

    // Custom sections for anything else the game wants in the same file
    void addSection(const std::string& name, SaveFn saveFn, LoadFn loadFn) {
        custom_[name] = {std::move(saveFn), std::move(loadFn)};
    }

    // ── Snapshot ──────────────────────────────────────────────────────────────
    serial::JsonValue toJson(double simTime) const {
        serial::JsonObject root;
        root["format"]  = FORMAT_VERSION;
        root["simTime"] = simTime;
        if (relationships) root["relationships"] = WorldSerializer::toJson(*relationships);
        if (reputation)    root["reputation"]    = WorldSerializer::toJson(*reputation);
        if (economy)       root["economy"]       = WorldSerializer::toJson(*economy);
        if (families)      root["families"]      = WorldSerializer::toJson(*families);
        for (const auto& [name, fns] : custom_)
            if (fns.first) root[name] = fns.first();
        return root;
    }

    // Returns the restored simTime (0 if missing).
    double fromJson(const serial::JsonValue& j) {
        if (relationships && j.has("relationships"))
            WorldSerializer::fromJson(*relationships, j["relationships"]);
        if (reputation && j.has("reputation"))
            WorldSerializer::fromJson(*reputation, j["reputation"]);
        if (economy && j.has("economy"))
            WorldSerializer::fromJson(*economy, j["economy"]);
        if (families && j.has("families"))
            WorldSerializer::fromJson(*families, j["families"]);
        for (const auto& [name, fns] : custom_)
            if (fns.second && j.has(name)) fns.second(j[name]);
        return j["simTime"].asDouble();
    }

    // ── File I/O ──────────────────────────────────────────────────────────────
    bool save(const std::string& path, double simTime, bool pretty = true) const {
        return serial::saveFile(toJson(simTime), path, pretty);
    }

    // Returns simTime on success; std::nullopt if the file is missing,
    // unparseable, or from an incompatible format version.
    std::optional<double> load(const std::string& path) {
        serial::JsonValue j;
        if (!serial::tryLoadFile(path, j)) return std::nullopt;
        if (j["format"].asInt() != FORMAT_VERSION) return std::nullopt;
        return fromJson(j);
    }

private:
    std::map<std::string, std::pair<SaveFn, LoadFn>> custom_;
};

} // namespace npc
