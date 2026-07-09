#pragma once
// reputation_system.hpp — witnessed crimes, public reputation, bounties
// Part of Aithena (C++17, header-only)
//
// Design: reputation is what the COMMUNITY believes about an NPC, as opposed
// to RelationshipSystem (what one NPC feels about another). A crime only
// damages reputation if somebody witnessed it; unwitnessed crimes are stored
// as unsolved and can surface later (e.g. via gossip or investigation).
// Integrates loosely: expose hooks so callers can fan crime reports out
// through MemorySystem gossip or the event bus.

#include "../core/types.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <functional>
#include <sstream>
#include <cmath>

namespace npc {

// ─── Crime types ──────────────────────────────────────────────────────────────

enum class CrimeType {
    Trespassing,
    Vandalism,
    Pickpocketing,
    Theft,
    Smuggling,
    Assault,
    Murder
};

inline const char* crimeTypeName(CrimeType t) {
    switch (t) {
        case CrimeType::Trespassing:   return "Trespassing";
        case CrimeType::Vandalism:     return "Vandalism";
        case CrimeType::Pickpocketing: return "Pickpocketing";
        case CrimeType::Theft:         return "Theft";
        case CrimeType::Smuggling:     return "Smuggling";
        case CrimeType::Assault:       return "Assault";
        case CrimeType::Murder:        return "Murder";
    }
    return "Unknown";
}

// Base severity 0..100 — drives reputation damage and bounty size
inline float crimeSeverity(CrimeType t) {
    switch (t) {
        case CrimeType::Trespassing:   return  5.0f;
        case CrimeType::Vandalism:     return 10.0f;
        case CrimeType::Pickpocketing: return 15.0f;
        case CrimeType::Theft:         return 25.0f;
        case CrimeType::Smuggling:     return 30.0f;
        case CrimeType::Assault:       return 45.0f;
        case CrimeType::Murder:        return 90.0f;
    }
    return 0.0f;
}

// ─── Good deeds (the positive counterpart) ───────────────────────────────────

enum class DeedType {
    Donation,
    HelpedStranger,
    DefendedTown,
    CaughtCriminal,
    SavedLife,
    CompletedContract
};

inline const char* deedTypeName(DeedType t) {
    switch (t) {
        case DeedType::Donation:          return "Donation";
        case DeedType::HelpedStranger:    return "Helped a stranger";
        case DeedType::DefendedTown:      return "Defended the town";
        case DeedType::CaughtCriminal:    return "Caught a criminal";
        case DeedType::SavedLife:         return "Saved a life";
        case DeedType::CompletedContract: return "Completed a contract";
    }
    return "Unknown";
}

inline float deedValue(DeedType t) {
    switch (t) {
        case DeedType::Donation:          return  5.0f;
        case DeedType::HelpedStranger:    return  8.0f;
        case DeedType::DefendedTown:      return 25.0f;
        case DeedType::CaughtCriminal:    return 20.0f;
        case DeedType::SavedLife:         return 30.0f;
        case DeedType::CompletedContract: return 10.0f;
    }
    return 0.0f;
}

// ─── Crime record ─────────────────────────────────────────────────────────────

struct CrimeRecord {
    uint32_t                 id          = 0;
    CrimeType                type        = CrimeType::Theft;
    std::string              perpetrator;          // NPC id
    std::string              victim;               // NPC id (may be empty, e.g. smuggling)
    std::string              location;             // where it happened (optional)
    double                   simTime     = 0.0;    // hours since sim start
    float                    severity    = 0.0f;   // resolved from type * magnitude
    std::vector<std::string> witnesses;            // NPC ids that saw it happen
    bool                     reported    = false;  // has any witness told the authorities?
    bool                     solved      = false;  // bounty collected / justice served
    float                    bounty      = 0.0f;   // gold on the perpetrator's head

    bool witnessed() const { return !witnesses.empty(); }
};

// ─── Reputation labels ────────────────────────────────────────────────────────

inline const char* reputationLabel(float v) {
    if (v >=  75) return "Revered";
    if (v >=  50) return "Honored";
    if (v >=  25) return "Respected";
    if (v >=   5) return "Liked";
    if (v >=  -5) return "Neutral";
    if (v >= -25) return "Distrusted";
    if (v >= -50) return "Despised";
    if (v >= -75) return "Outlaw";
    return "Villain";
}

// ─── ReputationSystem ─────────────────────────────────────────────────────────

class ReputationSystem {
public:
    struct Config {
        float decayRatePerHour   = 0.01f;  // |reputation| drifts toward 0
        float witnessDamageScale = 0.35f;  // rep damage per witness (diminishing)
        float bountyPerSeverity  = 2.0f;   // gold posted per severity point
        float bountyThreshold    = 20.0f;  // severity below this posts no bounty
        float outlawThreshold    = -50.0f; // rep at/below → guards act on sight
        float reportChance       = 1.0f;   // fraction of witnessed crimes reported (hook for stealth)
    };

    Config config;

    // Optional hook — fired once per witness when a crime is recorded, so the
    // caller can push a Memory into that witness or publish an event.
    // Args: witness id, crime record.
    std::function<void(const std::string&, const CrimeRecord&)> onCrimeWitnessed;

    // ── Recording crimes ──────────────────────────────────────────────────────

    // Record a crime. Reputation only suffers if there are witnesses.
    // Returns the stored record's id.
    uint32_t recordCrime(CrimeType type, const std::string& perpetrator,
                         const std::string& victim, double simTime,
                         std::vector<std::string> witnesses = {},
                         float magnitude = 1.0f,
                         const std::string& location = {}) {
        CrimeRecord rec;
        rec.id          = nextCrimeId_++;
        rec.type        = type;
        rec.perpetrator = perpetrator;
        rec.victim      = victim;
        rec.location    = location;
        rec.simTime     = simTime;
        rec.severity    = crimeSeverity(type) * magnitude;
        rec.witnesses   = std::move(witnesses);

        // The victim always counts as a witness of assault/theft against them
        if (!rec.victim.empty() &&
            (type == CrimeType::Assault || type == CrimeType::Theft ||
             type == CrimeType::Pickpocketing) &&
            std::find(rec.witnesses.begin(), rec.witnesses.end(), rec.victim)
                == rec.witnesses.end()) {
            rec.witnesses.push_back(rec.victim);
        }

        if (rec.witnessed()) {
            // Diminishing returns: 1st witness hurts most
            float witnessFactor = 0.0f;
            for (std::size_t i = 0; i < rec.witnesses.size(); ++i)
                witnessFactor += config.witnessDamageScale / static_cast<float>(i + 1);
            witnessFactor = std::min(1.0f, witnessFactor);

            modifyReputation(perpetrator, -rec.severity * (0.5f + witnessFactor));

            rec.reported = true;
            if (rec.severity >= config.bountyThreshold)
                rec.bounty = rec.severity * config.bountyPerSeverity;

            if (onCrimeWitnessed)
                for (const auto& w : rec.witnesses)
                    onCrimeWitnessed(w, rec);
        }

        crimes_.push_back(std::move(rec));
        return crimes_.back().id;
    }

    // A previously unwitnessed crime comes to light (investigation, gossip…)
    bool exposeCrime(uint32_t crimeId, const std::string& discoverer) {
        auto* rec = findCrime(crimeId);
        if (!rec || rec->reported || rec->solved) return false;
        rec->witnesses.push_back(discoverer);
        rec->reported = true;
        modifyReputation(rec->perpetrator, -rec->severity * 0.75f);
        if (rec->severity >= config.bountyThreshold)
            rec->bounty = rec->severity * config.bountyPerSeverity;
        if (onCrimeWitnessed) onCrimeWitnessed(discoverer, *rec);
        return true;
    }

    // Justice served: clears the bounty, restores a little reputation
    // (paying your debt to society). Returns collected bounty.
    float resolveCrime(uint32_t crimeId) {
        auto* rec = findCrime(crimeId);
        if (!rec || rec->solved) return 0.0f;
        rec->solved = true;
        float collected = rec->bounty;
        rec->bounty = 0.0f;
        modifyReputation(rec->perpetrator, rec->severity * 0.25f);
        return collected;
    }

    // ── Recording good deeds ──────────────────────────────────────────────────

    void recordDeed(DeedType type, const std::string& actor,
                    double /*simTime*/, float magnitude = 1.0f) {
        modifyReputation(actor, deedValue(type) * magnitude);
    }

    // ── Queries ───────────────────────────────────────────────────────────────

    float reputationOf(const std::string& id) const {
        auto it = reputation_.find(id);
        return it != reputation_.end() ? it->second : 0.0f;
    }

    const char* labelOf(const std::string& id) const {
        return reputationLabel(reputationOf(id));
    }

    bool isOutlaw(const std::string& id) const {
        return reputationOf(id) <= config.outlawThreshold || totalBountyOn(id) > 0.0f;
    }

    // Should a guard intervene against this NPC on sight?
    bool guardShouldIntervene(const std::string& id) const {
        return isOutlaw(id);
    }

    float totalBountyOn(const std::string& id) const {
        float total = 0.0f;
        for (const auto& c : crimes_)
            if (!c.solved && c.perpetrator == id) total += c.bounty;
        return total;
    }

    // All reported-but-unsolved crimes (quest generator fodder)
    std::vector<const CrimeRecord*> openCrimes() const {
        std::vector<const CrimeRecord*> out;
        for (const auto& c : crimes_)
            if (c.reported && !c.solved) out.push_back(&c);
        return out;
    }

    // Crimes nobody has discovered yet
    std::vector<const CrimeRecord*> undiscoveredCrimes() const {
        std::vector<const CrimeRecord*> out;
        for (const auto& c : crimes_)
            if (!c.reported && !c.solved) out.push_back(&c);
        return out;
    }

    std::vector<const CrimeRecord*> crimesBy(const std::string& id) const {
        std::vector<const CrimeRecord*> out;
        for (const auto& c : crimes_)
            if (c.perpetrator == id) out.push_back(&c);
        return out;
    }

    const CrimeRecord* getCrime(uint32_t id) const {
        for (const auto& c : crimes_) if (c.id == id) return &c;
        return nullptr;
    }

    // Worst N reputations (most wanted board)
    std::vector<std::pair<std::string, float>> worstOffenders(std::size_t n = 5) const {
        std::vector<std::pair<std::string, float>> out(reputation_.begin(),
                                                       reputation_.end());
        std::sort(out.begin(), out.end(),
                  [](auto& a, auto& b){ return a.second < b.second; });
        if (out.size() > n) out.resize(n);
        return out;
    }

    // ── Direct manipulation ───────────────────────────────────────────────────

    void setReputation(const std::string& id, float v) {
        reputation_[id] = std::clamp(v, -100.0f, 100.0f);
    }

    void modifyReputation(const std::string& id, float delta) {
        auto& r = reputation_[id];
        r = std::clamp(r + delta, -100.0f, 100.0f);
    }

    // ── Restore (save/load support) ───────────────────────────────────────────
    // Re-insert a crime record verbatim (no reputation side effects).
    void restoreCrime(CrimeRecord rec) {
        nextCrimeId_ = std::max(nextCrimeId_, rec.id + 1);
        crimes_.push_back(std::move(rec));
    }

    void clearAll() {
        reputation_.clear();
        crimes_.clear();
        nextCrimeId_ = 1;
    }

    // ── Tick ──────────────────────────────────────────────────────────────────

    void update(float elapsedHours) {
        if (elapsedHours <= 0.0f || config.decayRatePerHour <= 0.0f) return;
        float step = config.decayRatePerHour * elapsedHours;
        for (auto& [id, rep] : reputation_) {
            if      (rep >  step) rep -= step;
            else if (rep < -step) rep += step;
            else                  rep  = 0.0f;
        }
    }

    // ── Debug / iteration ─────────────────────────────────────────────────────

    const std::unordered_map<std::string, float>& allReputations() const {
        return reputation_;
    }
    const std::vector<CrimeRecord>& allCrimes() const { return crimes_; }
    std::size_t crimeCount() const { return crimes_.size(); }

    std::string debugString() const {
        std::ostringstream ss;
        ss << "ReputationSystem: " << reputation_.size() << " NPCs, "
           << crimes_.size() << " crimes on record\n";
        for (const auto& [id, rep] : reputation_) {
            ss << "  " << id << ": " << static_cast<int>(rep)
               << " [" << reputationLabel(rep) << "]";
            float bounty = totalBountyOn(id);
            if (bounty > 0.0f) ss << " — bounty " << static_cast<int>(bounty) << "g";
            ss << "\n";
        }
        return ss.str();
    }

private:
    CrimeRecord* findCrime(uint32_t id) {
        for (auto& c : crimes_) if (c.id == id) return &c;
        return nullptr;
    }

    std::unordered_map<std::string, float> reputation_; // -100..+100
    std::vector<CrimeRecord>               crimes_;
    uint32_t                               nextCrimeId_ = 1;
};

} // namespace npc
