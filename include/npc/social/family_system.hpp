#pragma once
// family_system.hpp — households, marriage, children, aging, inheritance
// Part of Aithena (C++17, header-only)
//
// Adds a generational layer to the simulation: NPCs belong to households,
// marry (optionally gated on RelationshipSystem values), raise children,
// age through life stages, and pass wealth to heirs when they die. All
// transitions surface as LifecycleEvents the caller drains each tick —
// perfect for wiring into the event bus, memories, or quest generation.

#include "../core/types.hpp"
#include "../core/random.hpp"
#include "relationship_system.hpp"
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <functional>
#include <sstream>
#include <cmath>

namespace npc {

// ─── Life stages ──────────────────────────────────────────────────────────────

enum class LifeStage {
    Child,       // 0 .. adolescentAge
    Adolescent,  // adolescentAge .. adultAge
    Adult,       // adultAge .. elderAge
    Elder        // elderAge ..
};

inline const char* lifeStageName(LifeStage s) {
    switch (s) {
        case LifeStage::Child:      return "Child";
        case LifeStage::Adolescent: return "Adolescent";
        case LifeStage::Adult:      return "Adult";
        case LifeStage::Elder:      return "Elder";
    }
    return "Unknown";
}

// ─── Lifecycle events ─────────────────────────────────────────────────────────

struct LifecycleEvent {
    enum class Type { Born, CameOfAge, Married, BecameElder, Died, Inherited };
    Type        type;
    std::string subject;      // the NPC this happened to
    std::string other;        // spouse / parent / deceased (contextual)
    float       amount = 0.0f; // inheritance gold, where relevant
    double      simTime = 0.0;

    std::string sentence() const {
        switch (type) {
            case Type::Born:        return subject + " was born to " + other;
            case Type::CameOfAge:   return subject + " came of age";
            case Type::Married:     return subject + " married " + other;
            case Type::BecameElder: return subject + " became an elder";
            case Type::Died:        return subject + " passed away";
            case Type::Inherited:   return subject + " inherited " +
                                           std::to_string(static_cast<int>(amount)) +
                                           " gold from " + other;
        }
        return subject + ": lifecycle event";
    }
};

// ─── Family member ────────────────────────────────────────────────────────────

struct FamilyMember {
    std::string              id;
    float                    age        = 25.0f;  // years
    bool                     alive      = true;
    std::string              spouse;              // empty = unmarried
    std::vector<std::string> parents;             // 0..2 known parents
    std::vector<std::string> children;
    uint32_t                 householdId = 0;
    float                    personalWealth = 0.0f;
};

// ─── Household ────────────────────────────────────────────────────────────────

struct Household {
    uint32_t                 id = 0;
    std::string              name;         // "Alaric's household"
    std::string              homeLocation; // optional location name
    std::vector<std::string> members;
    float                    wealth = 100.0f;
};

// ─── FamilySystem ─────────────────────────────────────────────────────────────

class FamilySystem {
public:
    struct Config {
        float hoursPerYear        = 240.0f; // sim compression: 10 sim-days = 1 year
        float adolescentAge       = 12.0f;
        float adultAge            = 18.0f;
        float elderAge            = 60.0f;
        float lifeExpectancy      = 75.0f;  // deaths ramp up around this age
        float mortalityScale      = 0.15f;  // yearly death chance slope past elderAge
        float marriageMinRel      = 50.0f;  // mutual relationship needed to marry
        float birthChancePerYear  = 0.35f;  // per married adult couple
        int   maxChildrenPerCouple = 4;
        float spouseInheritShare  = 0.5f;   // spouse gets this, children split the rest
    };

    Config config;

    // Optional: consult relationship values before allowing marriages
    RelationshipSystem* relationships = nullptr;

    // Optional: name newborn children ("child #n of A and B" by default)
    std::function<std::string(const std::string& parentA,
                              const std::string& parentB, int n)> childNamer;

    // ── Registration ──────────────────────────────────────────────────────────
    FamilyMember& registerNPC(const std::string& id, float age = 25.0f,
                              float personalWealth = 0.0f) {
        auto& m = members_[id];
        m.id  = id;
        m.age = age;
        m.personalWealth = personalWealth;
        if (m.householdId == 0) {
            Household h;
            h.id   = nextHouseholdId_++;
            h.name = id + "'s household";
            h.members.push_back(id);
            m.householdId = h.id;
            households_[h.id] = std::move(h);
        }
        return m;
    }

    bool isRegistered(const std::string& id) const { return members_.count(id) > 0; }

    // ── Marriage ──────────────────────────────────────────────────────────────
    bool canMarry(const std::string& a, const std::string& b) const {
        const auto* ma = tryGet(a);
        const auto* mb = tryGet(b);
        if (!ma || !mb || a == b) return false;
        if (!ma->alive || !mb->alive) return false;
        if (!ma->spouse.empty() || !mb->spouse.empty()) return false;
        if (stageOf(*ma) != LifeStage::Adult && stageOf(*ma) != LifeStage::Elder) return false;
        if (stageOf(*mb) != LifeStage::Adult && stageOf(*mb) != LifeStage::Elder) return false;
        if (areRelated(a, b)) return false;
        if (relationships &&
            (relationships->getValue(a, b) < config.marriageMinRel ||
             relationships->getValue(b, a) < config.marriageMinRel)) return false;
        return true;
    }

    bool marry(const std::string& a, const std::string& b, double simTime) {
        if (!canMarry(a, b)) return false;
        auto& ma = members_[a];
        auto& mb = members_[b];
        ma.spouse = b;
        mb.spouse = a;
        mergeHouseholds(ma, mb);
        pushEvent({LifecycleEvent::Type::Married, a, b, 0.0f, simTime});
        return true;
    }

    // ── Children ──────────────────────────────────────────────────────────────
    // Explicit birth (update() also rolls births for married couples).
    std::string haveChild(const std::string& parentA, const std::string& parentB,
                          double simTime) {
        auto* ma = tryGetMutable(parentA);
        auto* mb = tryGetMutable(parentB);
        if (!ma || !mb || !ma->alive || !mb->alive) return {};

        int n = static_cast<int>(ma->children.size()) + 1;
        std::string childId = childNamer
            ? childNamer(parentA, parentB, n)
            : parentA + "_child" + std::to_string(n);
        if (members_.count(childId)) return {}; // name collision — caller's namer must disambiguate

        FamilyMember child;
        child.id  = childId;
        child.age = 0.0f;
        child.parents = {parentA, parentB};
        child.householdId = ma->householdId;
        members_[childId] = child;

        households_[ma->householdId].members.push_back(childId);
        ma->children.push_back(childId);
        mb->children.push_back(childId);
        pushEvent({LifecycleEvent::Type::Born, childId,
                   parentA + " and " + parentB, 0.0f, simTime});
        return childId;
    }

    // ── Death & inheritance ───────────────────────────────────────────────────
    void kill(const std::string& id, double simTime) {
        auto* m = tryGetMutable(id);
        if (!m || !m->alive) return;
        m->alive = false;
        pushEvent({LifecycleEvent::Type::Died, id, {}, 0.0f, simTime});
        distributeInheritance(*m, simTime);
        if (!m->spouse.empty()) {
            if (auto* sp = tryGetMutable(m->spouse)) sp->spouse.clear();
            m->spouse.clear();
        }
    }

    // ── Tick ──────────────────────────────────────────────────────────────────
    void update(double simTime, float elapsedHours, Random& rng = Random::instance()) {
        if (elapsedHours <= 0.0f || config.hoursPerYear <= 0.0f) return;
        float years = elapsedHours / config.hoursPerYear;

        // Aging + stage transitions + mortality
        for (auto& [id, m] : members_) {
            if (!m.alive) continue;
            LifeStage before = stageOf(m);
            m.age += years;
            LifeStage after = stageOf(m);

            if (before != after) {
                if (after == LifeStage::Adult)
                    pushEvent({LifecycleEvent::Type::CameOfAge, id, {}, 0.0f, simTime});
                else if (after == LifeStage::Elder)
                    pushEvent({LifecycleEvent::Type::BecameElder, id, {}, 0.0f, simTime});
            }

            // Mortality: negligible before elderAge, ramps toward lifeExpectancy
            if (m.age > config.elderAge) {
                float over = (m.age - config.elderAge) /
                             std::max(1.0f, config.lifeExpectancy - config.elderAge);
                float yearlyChance = config.mortalityScale * over * over;
                if (rng.chance(std::min(0.95f, yearlyChance * years)))
                    dying_.push_back(id);
            }
        }
        for (const auto& id : dying_) kill(id, simTime);
        dying_.clear();

        // Births for married adult couples
        for (auto& [id, m] : members_) {
            if (!m.alive || m.spouse.empty() || id > m.spouse) continue; // once per couple
            const auto* sp = tryGet(m.spouse);
            if (!sp || !sp->alive) continue;
            if (stageOf(m) != LifeStage::Adult || stageOf(*sp) != LifeStage::Adult) continue;
            if (static_cast<int>(m.children.size()) >= config.maxChildrenPerCouple) continue;
            if (rng.chance(config.birthChancePerYear * years))
                birthsDue_.push_back({id, m.spouse});
        }
        for (const auto& [a, b] : birthsDue_) haveChild(a, b, simTime);
        birthsDue_.clear();
    }

    // ── Queries ───────────────────────────────────────────────────────────────
    const FamilyMember* tryGet(const std::string& id) const {
        auto it = members_.find(id);
        return it != members_.end() ? &it->second : nullptr;
    }

    LifeStage lifeStageOf(const std::string& id) const {
        const auto* m = tryGet(id);
        return m ? stageOf(*m) : LifeStage::Adult;
    }

    float ageOf(const std::string& id) const {
        const auto* m = tryGet(id);
        return m ? m->age : 0.0f;
    }

    bool isAlive(const std::string& id) const {
        const auto* m = tryGet(id);
        return m && m->alive;
    }

    const Household* householdOf(const std::string& id) const {
        const auto* m = tryGet(id);
        if (!m) return nullptr;
        auto it = households_.find(m->householdId);
        return it != households_.end() ? &it->second : nullptr;
    }

    Household* householdOfMutable(const std::string& id) {
        const auto* m = tryGet(id);
        if (!m) return nullptr;
        auto it = households_.find(m->householdId);
        return it != households_.end() ? &it->second : nullptr;
    }

    // Blood relatives within two generations (parents, children, siblings,
    // grandparents/grandchildren) — used to block marriages.
    bool areRelated(const std::string& a, const std::string& b) const {
        if (a == b) return true;
        return kinAsMap(a).count(b) > 0;
    }

    // Ordered heirs: spouse first, then living children by age (oldest first)
    std::vector<std::string> heirsOf(const std::string& id) const {
        std::vector<std::string> out;
        const auto* m = tryGet(id);
        if (!m) return out;
        if (!m->spouse.empty() && isAlive(m->spouse)) out.push_back(m->spouse);
        std::vector<const FamilyMember*> kids;
        for (const auto& c : m->children)
            if (const auto* k = tryGet(c); k && k->alive) kids.push_back(k);
        std::sort(kids.begin(), kids.end(),
                  [](const FamilyMember* x, const FamilyMember* y){
                      return x->age > y->age;
                  });
        for (const auto* k : kids) out.push_back(k->id);
        return out;
    }

    // ── Events ────────────────────────────────────────────────────────────────
    std::vector<LifecycleEvent> drainEvents() {
        std::vector<LifecycleEvent> out;
        out.swap(events_);
        return out;
    }

    // ── Introspection ─────────────────────────────────────────────────────────
    const std::unordered_map<std::string, FamilyMember>& allMembers()    const { return members_; }
    const std::map<uint32_t, Household>&                 allHouseholds() const { return households_; }

    std::string familyTreeString(const std::string& id) const {
        const auto* m = tryGet(id);
        if (!m) return "(unknown)";
        std::ostringstream ss;
        ss << id << " (age " << static_cast<int>(m->age)
           << ", " << lifeStageName(stageOf(*m))
           << (m->alive ? "" : ", deceased") << ")";
        if (!m->spouse.empty()) ss << "  spouse: " << m->spouse;
        if (!m->parents.empty()) {
            ss << "  parents:";
            for (const auto& p : m->parents) ss << " " << p;
        }
        if (!m->children.empty()) {
            ss << "  children:";
            for (const auto& c : m->children) ss << " " << c;
        }
        return ss.str();
    }

    std::string debugString() const {
        std::ostringstream ss;
        ss << "FamilySystem: " << members_.size() << " members, "
           << households_.size() << " households\n";
        for (const auto& [hid, h] : households_) {
            ss << "  [" << h.name << "] wealth="
               << static_cast<int>(h.wealth) << "g:";
            for (const auto& mid : h.members) ss << " " << mid;
            ss << "\n";
        }
        return ss.str();
    }

    // Direct member access for save/load
    FamilyMember* tryGetMutable(const std::string& id) {
        auto it = members_.find(id);
        return it != members_.end() ? &it->second : nullptr;
    }
    std::map<uint32_t, Household>& allHouseholdsMutable() { return households_; }

    // ── Restore (save/load support) ───────────────────────────────────────────
    void restoreMember(FamilyMember m) { members_[m.id] = std::move(m); }

    void restoreHousehold(Household h) {
        nextHouseholdId_ = std::max(nextHouseholdId_, h.id + 1);
        households_[h.id] = std::move(h);
    }

    void clearAll() {
        members_.clear();
        households_.clear();
        events_.clear();
        nextHouseholdId_ = 1;
    }

private:
    LifeStage stageOf(const FamilyMember& m) const {
        if (m.age < config.adolescentAge) return LifeStage::Child;
        if (m.age < config.adultAge)      return LifeStage::Adolescent;
        if (m.age < config.elderAge)      return LifeStage::Adult;
        return LifeStage::Elder;
    }

    // Parents, children, siblings, grandparents, grandchildren of `id`
    std::unordered_map<std::string, bool> kinAsMap(const std::string& id) const {
        std::unordered_map<std::string, bool> kin;
        const auto* m = tryGet(id);
        if (!m) return kin;

        for (const auto& p : m->parents) {
            kin[p] = true;
            if (const auto* pm = tryGet(p)) {
                for (const auto& gp : pm->parents) kin[gp] = true;   // grandparents
                for (const auto& sib : pm->children)                 // siblings
                    if (sib != id) kin[sib] = true;
            }
        }
        for (const auto& c : m->children) {
            kin[c] = true;
            if (const auto* cm = tryGet(c))
                for (const auto& gc : cm->children) kin[gc] = true;  // grandchildren
        }
        return kin;
    }

    void mergeHouseholds(FamilyMember& a, FamilyMember& b) {
        if (a.householdId == b.householdId) return;
        auto& target = households_[a.householdId];
        auto  srcIt  = households_.find(b.householdId);
        if (srcIt == households_.end()) { b.householdId = a.householdId; return; }

        target.wealth += srcIt->second.wealth;
        for (const auto& mid : srcIt->second.members) {
            target.members.push_back(mid);
            if (auto* mm = tryGetMutable(mid)) mm->householdId = target.id;
        }
        households_.erase(srcIt);
    }

    void distributeInheritance(FamilyMember& deceased, double simTime) {
        float estate = deceased.personalWealth;
        deceased.personalWealth = 0.0f;
        if (estate <= 0.0f) return;

        auto heirs = heirsOf(deceased.id);
        if (heirs.empty()) {
            // Estate falls to the household
            if (auto it = households_.find(deceased.householdId);
                it != households_.end())
                it->second.wealth += estate;
            return;
        }

        std::size_t idx = 0;
        float remaining = estate;
        if (isAlive(deceased.spouse) && !heirs.empty() && heirs[0] == deceased.spouse) {
            float share = estate * config.spouseInheritShare;
            if (heirs.size() == 1) share = estate; // sole heir takes all
            grantInheritance(heirs[0], deceased.id, share, simTime);
            remaining -= share;
            idx = 1;
        }
        std::size_t childCount = heirs.size() - idx;
        if (childCount > 0) {
            float each = remaining / static_cast<float>(childCount);
            for (; idx < heirs.size(); ++idx)
                grantInheritance(heirs[idx], deceased.id, each, simTime);
        }
    }

    void grantInheritance(const std::string& heir, const std::string& from,
                          float amount, double simTime) {
        if (auto* h = tryGetMutable(heir)) h->personalWealth += amount;
        pushEvent({LifecycleEvent::Type::Inherited, heir, from, amount, simTime});
    }

    void pushEvent(LifecycleEvent ev) { events_.push_back(std::move(ev)); }

    std::unordered_map<std::string, FamilyMember> members_;
    std::map<uint32_t, Household>                 households_;
    std::vector<LifecycleEvent>                   events_;
    std::vector<std::string>                      dying_;
    std::vector<std::pair<std::string, std::string>> birthsDue_;
    uint32_t                                      nextHouseholdId_ = 1;
};

} // namespace npc
