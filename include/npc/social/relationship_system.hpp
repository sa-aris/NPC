#pragma once
// relationship_system.hpp — NPC relationship graph with event history, decay, narrative
// Part of the NPC behavior system (C++17, header-only)
// G28: event history, numeric value, decay, narrative summary

#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <optional>
#include <functional>

namespace npc {

// ─── Relationship event types ────────────────────────────────────────────────

enum class RelationshipEventType {
    Saved,          // A saved B's life
    Betrayed,       // A betrayed B
    Traded,         // Successful trade
    Threatened,     // A threatened B
    Helped,         // General help
    Insulted,       // Verbal/social insult
    Gifted,         // A gave gift to B
    Attacked,       // A attacked B
    Defended,       // A defended B from third party
    SharedSecret,   // A shared information with B
    Lied,           // A deceived B
    Healed,         // A healed B
    Competed,       // A competed against B (neutral)
    Apologized,     // A apologized to B
    Mourned,        // A mourned with B
    Custom          // User-defined event
};

inline const char* relEventTypeName(RelationshipEventType t) {
    switch (t) {
        case RelationshipEventType::Saved:        return "saved";
        case RelationshipEventType::Betrayed:     return "betrayed";
        case RelationshipEventType::Traded:       return "traded with";
        case RelationshipEventType::Threatened:   return "threatened";
        case RelationshipEventType::Helped:       return "helped";
        case RelationshipEventType::Insulted:     return "insulted";
        case RelationshipEventType::Gifted:       return "gifted";
        case RelationshipEventType::Attacked:     return "attacked";
        case RelationshipEventType::Defended:     return "defended";
        case RelationshipEventType::SharedSecret: return "shared secret with";
        case RelationshipEventType::Lied:         return "lied to";
        case RelationshipEventType::Healed:       return "healed";
        case RelationshipEventType::Competed:     return "competed with";
        case RelationshipEventType::Apologized:   return "apologized to";
        case RelationshipEventType::Mourned:      return "mourned with";
        case RelationshipEventType::Custom:       return "custom event with";
    }
    return "interacted with";
}

// ─── Base value deltas for relationship event types ──────────────────────────
// Positive = improves relationship FROM subject's perspective TO target

inline float relEventBaseDelta(RelationshipEventType t) {
    switch (t) {
        case RelationshipEventType::Saved:        return +35.0f;
        case RelationshipEventType::Betrayed:     return -50.0f;
        case RelationshipEventType::Traded:       return  +5.0f;
        case RelationshipEventType::Threatened:   return -20.0f;
        case RelationshipEventType::Helped:       return +10.0f;
        case RelationshipEventType::Insulted:     return -15.0f;
        case RelationshipEventType::Gifted:       return +12.0f;
        case RelationshipEventType::Attacked:     return -40.0f;
        case RelationshipEventType::Defended:     return +25.0f;
        case RelationshipEventType::SharedSecret: return +15.0f;
        case RelationshipEventType::Lied:         return -25.0f;
        case RelationshipEventType::Healed:       return +20.0f;
        case RelationshipEventType::Competed:     return  -2.0f;
        case RelationshipEventType::Apologized:   return  +8.0f;
        case RelationshipEventType::Mourned:      return +18.0f;
        case RelationshipEventType::Custom:       return   0.0f;
    }
    return 0.0f;
}

// ─── RelationshipEvent ───────────────────────────────────────────────────────

struct RelationshipEvent {
    RelationshipEventType type       = RelationshipEventType::Custom;
    std::string           initiator;    // NPC id who acted
    std::string           target;       // NPC id who received
    float                 delta        = 0.0f; // actual value change applied
    float                 magnitude    = 1.0f; // scaling factor (1 = normal)
    double                simTime      = 0.0;  // sim time in hours when it happened
    std::string           note;                // optional free-form description
    std::string           location;            // where it happened (optional)

    // Decayed weight at query time
    // decayHalfLifeHours: time for event to lose half its remembered weight
    float decayedWeight(double currentTime, float halfLifeHours = 168.0f) const {
        if (halfLifeHours <= 0.0f) return 1.0f;
        float elapsed = static_cast<float>(currentTime - simTime);
        if (elapsed <= 0.0f) return 1.0f;
        return std::pow(0.5f, elapsed / halfLifeHours);
    }

    // Human-readable sentence
    std::string sentence(double currentTime = -1.0) const {
        std::ostringstream ss;
        ss << initiator << " " << relEventTypeName(type) << " " << target;
        if (!note.empty()) ss << " (" << note << ")";
        if (currentTime >= 0.0 && simTime >= 0.0) {
            float hoursAgo = static_cast<float>(currentTime - simTime);
            if (hoursAgo < 1.0f)
                ss << " [just now]";
            else if (hoursAgo < 24.0f)
                ss << " [" << static_cast<int>(hoursAgo) << "h ago]";
            else {
                int days = static_cast<int>(hoursAgo / 24.0f);
                ss << " [" << days << " day" << (days != 1 ? "s" : "") << " ago]";
            }
        }
        return ss.str();
    }
};

// ─── RelationshipData — holds value + event history for A→B ─────────────────

struct RelationshipData {
    float                        value       = 0.0f; // -100..+100
    std::vector<RelationshipEvent> history;
    static constexpr std::size_t  MAX_HISTORY = 64;

    // Decay config (per-pair overrideable, set from RelationshipSystem::Config on first access)
    bool  initialized_     = false; // set to true by RelationshipSystem::get()
    float decayRatePerHour = 0.02f; // how much |value| drifts toward 0 per hour
    float decayFloor       = 0.0f;  // value below which decay stops

    // Trust — separate channel that degrades on betrayal/lie, hard to rebuild
    float trust = 50.0f; // 0..100

    void clamp() {
        value = std::max(-100.0f, std::min(100.0f, value));
        trust = std::max(0.0f, std::min(100.0f, trust));
    }

    void addEvent(RelationshipEvent ev) {
        history.push_back(std::move(ev));
        if (history.size() > MAX_HISTORY) {
            history.erase(history.begin()); // drop oldest
        }
    }

    // Apply time-based decay toward neutral
    void applyDecay(float elapsedHours) {
        if (elapsedHours <= 0.0f || decayRatePerHour <= 0.0f) return;
        float direction = (value > decayFloor) ? -1.0f : (value < decayFloor ? 1.0f : 0.0f);
        float change    = direction * decayRatePerHour * elapsedHours;
        value += change;
        // Don't overshoot floor
        if (direction < 0 && value < decayFloor) value = decayFloor;
        if (direction > 0 && value > decayFloor) value = decayFloor;
        clamp();
    }

    // Weighted recent-events summary value
    float weightedHistoryValue(double currentTime, float halfLifeHours = 168.0f) const {
        float total = 0.0f;
        float wsum  = 0.0f;
        for (auto& ev : history) {
            float w = ev.decayedWeight(currentTime, halfLifeHours);
            total += ev.delta * w;
            wsum  += std::abs(w);
        }
        return (wsum > 0.001f) ? (total / wsum) : 0.0f;
    }

    // Recent events (last N, or since a sim time)
    std::vector<const RelationshipEvent*> recentEvents(std::size_t n = 5) const {
        std::vector<const RelationshipEvent*> out;
        std::size_t start = history.size() > n ? history.size() - n : 0;
        for (std::size_t i = start; i < history.size(); ++i)
            out.push_back(&history[i]);
        return out;
    }

    std::vector<const RelationshipEvent*> eventsSince(double simTime) const {
        std::vector<const RelationshipEvent*> out;
        for (auto& ev : history)
            if (ev.simTime >= simTime) out.push_back(&ev);
        return out;
    }

    // Most impactful negative event
    const RelationshipEvent* worstEvent() const {
        const RelationshipEvent* worst = nullptr;
        for (auto& ev : history)
            if (!worst || ev.delta < worst->delta) worst = &ev;
        return worst;
    }

    // Most impactful positive event
    const RelationshipEvent* bestEvent() const {
        const RelationshipEvent* best = nullptr;
        for (auto& ev : history)
            if (!best || ev.delta > best->delta) best = &ev;
        return best;
    }
};

// ─── Relationship label helpers ──────────────────────────────────────────────

inline const char* relationshipLabel(float v) {
    if (v >= 80)  return "Devoted";
    if (v >= 60)  return "Close Friend";
    if (v >= 40)  return "Friend";
    if (v >= 20)  return "Friendly";
    if (v >= 5)   return "Acquaintance";
    if (v >= -5)  return "Neutral";
    if (v >= -20) return "Wary";
    if (v >= -40) return "Unfriendly";
    if (v >= -60) return "Hostile";
    if (v >= -80) return "Enemy";
    return "Sworn Enemy";
}

inline const char* trustLabel(float t) {
    if (t >= 80) return "Complete Trust";
    if (t >= 60) return "High Trust";
    if (t >= 40) return "Moderate Trust";
    if (t >= 20) return "Low Trust";
    if (t >= 5)  return "Suspicious";
    return "No Trust";
}

// ─── RelationshipSystem ──────────────────────────────────────────────────────

class RelationshipSystem {
public:
    struct Config {
        float defaultDecayRatePerHour = 0.02f;
        float decayHalfLifeHours      = 168.0f; // 1 week
        float decayFloor              = 0.0f;
        float trustDecayPerHour       = 0.005f;
        bool  symmetricDecay          = true; // both sides decay together
    };

    Config config;

    // ── Registration ─────────────────────────────────────────────────────────
    void registerNPC(const std::string& id) {
        (void)id; // relationships are created lazily on first interaction
    }

    void removeNPC(const std::string& id) {
        // Remove all entries where id is A or B
        for (auto it = rels_.begin(); it != rels_.end(); ) {
            if (it->first.first == id || it->first.second == id)
                it = rels_.erase(it);
            else ++it;
        }
    }

    // ── Core record access ────────────────────────────────────────────────────
    RelationshipData& get(const std::string& a, const std::string& b) {
        auto& d = rels_[{a, b}];
        if (!d.initialized_) {
            d.initialized_     = true;
            d.decayRatePerHour = config.defaultDecayRatePerHour;
            d.decayFloor       = config.decayFloor;
        }
        return d;
    }

    const RelationshipData* tryGet(const std::string& a, const std::string& b) const {
        auto it = rels_.find({a, b});
        return it != rels_.end() ? &it->second : nullptr;
    }

    float getValue(const std::string& a, const std::string& b) const {
        auto* d = tryGet(a, b);
        return d ? d->value : 0.0f;
    }

    float getTrust(const std::string& a, const std::string& b) const {
        auto* d = tryGet(a, b);
        return d ? d->trust : 50.0f;
    }

    // ── Event recording ───────────────────────────────────────────────────────

    // Record a one-sided event (a did something to b, from b's perspective: how b feels about a)
    void recordEvent(const std::string& a, const std::string& b,
                     RelationshipEventType type, double simTime,
                     float magnitudeScale = 1.0f,
                     const std::string& note = {},
                     const std::string& location = {}) {
        float base  = relEventBaseDelta(type) * magnitudeScale;
        auto& dAtoB = get(a, b); // b's view of a

        // Trust impact
        if (type == RelationshipEventType::Betrayed || type == RelationshipEventType::Lied)
            dAtoB.trust -= 30.0f * magnitudeScale;
        else if (type == RelationshipEventType::Saved || type == RelationshipEventType::Defended)
            dAtoB.trust += 10.0f * magnitudeScale;
        else if (type == RelationshipEventType::Helped || type == RelationshipEventType::Gifted)
            dAtoB.trust += 3.0f * magnitudeScale;

        dAtoB.value += base;
        dAtoB.clamp();

        RelationshipEvent ev;
        ev.type      = type;
        ev.initiator = a;
        ev.target    = b;
        ev.delta     = base;
        ev.magnitude = magnitudeScale;
        ev.simTime   = simTime;
        ev.note      = note;
        ev.location  = location;
        dAtoB.addEvent(std::move(ev));
    }

    // Record a mutual/symmetric event (both sides affected equally)
    void recordMutualEvent(const std::string& a, const std::string& b,
                           RelationshipEventType type, double simTime,
                           float magnitudeScale = 1.0f,
                           const std::string& note = {}) {
        recordEvent(a, b, type, simTime, magnitudeScale, note);
        recordEvent(b, a, type, simTime, magnitudeScale, note);
    }

    // Set value directly (for initialization or quest outcomes)
    void setValue(const std::string& a, const std::string& b, float v) {
        auto& d = get(a, b);
        d.value = std::max(-100.0f, std::min(100.0f, v));
    }

    void modifyValue(const std::string& a, const std::string& b, float delta) {
        auto& d = get(a, b);
        d.value = std::max(-100.0f, std::min(100.0f, d.value + delta));
    }

    void setTrust(const std::string& a, const std::string& b, float t) {
        auto& d = get(a, b);
        d.trust = std::max(0.0f, std::min(100.0f, t));
    }

    // ── Decay update ──────────────────────────────────────────────────────────
    void update(double /*simTime*/, float elapsedHours) {
        for (auto& [key, data] : rels_) {
            data.applyDecay(elapsedHours);
            // Trust also decays very slowly toward 50
            float trustDir = (data.trust > 50.0f) ? -1.0f : (data.trust < 50.0f ? 1.0f : 0.0f);
            data.trust += trustDir * config.trustDecayPerHour * elapsedHours;
            data.clamp();
        }
    }

    // ── Narrative ─────────────────────────────────────────────────────────────

    // "How does A feel about B?"
    std::string narrative(const std::string& a, const std::string& b,
                          double currentTime = -1.0) const {
        auto* d = tryGet(a, b);
        if (!d || d->history.empty()) {
            return a + " has no strong feelings toward " + b + ".";
        }

        std::ostringstream ss;
        ss << a << " feels " << relationshipLabel(d->value)
           << " toward " << b
           << " [" << static_cast<int>(d->value) << "/100, "
           << trustLabel(d->trust) << "].";

        // Highlight defining events
        auto* best  = d->bestEvent();
        auto* worst = d->worstEvent();

        if (best && best->delta > 5.0f) {
            ss << " Notably, " << best->sentence(currentTime) << ".";
        }
        if (worst && worst->delta < -5.0f && worst != best) {
            ss << " But also, " << worst->sentence(currentTime) << ".";
        }

        // Recent activity summary
        auto recent = d->recentEvents(3);
        if (!recent.empty()) {
            ss << " Recent interactions: ";
            for (std::size_t i = 0; i < recent.size(); ++i) {
                if (i > 0) ss << "; ";
                ss << recent[i]->sentence(currentTime);
            }
            ss << ".";
        }

        return ss.str();
    }

    // Full history as human-readable string
    std::string historyString(const std::string& a, const std::string& b,
                              double currentTime = -1.0) const {
        auto* d = tryGet(a, b);
        if (!d || d->history.empty()) return "(no history)";
        std::ostringstream ss;
        ss << "=== History: " << a << " → " << b << " ===\n";
        for (auto& ev : d->history)
            ss << "  " << ev.sentence(currentTime) << " [Δ" << ev.delta << "]\n";
        ss << "  Current value: " << d->value << "  Trust: " << d->trust;
        return ss.str();
    }

    // "Can A remember that B saved them?" — exact event recall
    bool remembers(const std::string& rememberer, const std::string& actor,
                   RelationshipEventType type, double since = 0.0) const {
        auto* d = tryGet(actor, rememberer); // actor→rememberer direction
        if (!d) return false;
        for (auto& ev : d->history)
            if (ev.type == type && ev.simTime >= since) return true;
        return false;
    }

    // Dialogue hint: "You saved my life 3 days ago"
    std::optional<std::string> recallSentence(const std::string& speaker,
                                               const std::string& about,
                                               RelationshipEventType type,
                                               double currentTime) const {
        auto* d = tryGet(about, speaker); // about acted on speaker
        if (!d) return std::nullopt;
        for (auto it = d->history.rbegin(); it != d->history.rend(); ++it) {
            if (it->type == type)
                return it->sentence(currentTime);
        }
        return std::nullopt;
    }

    // ── Query helpers ─────────────────────────────────────────────────────────
    bool areHostile(const std::string& a, const std::string& b) const {
        return getValue(a, b) <= -40.0f || getValue(b, a) <= -40.0f;
    }

    bool areFriendly(const std::string& a, const std::string& b) const {
        return getValue(a, b) >= 20.0f && getValue(b, a) >= 20.0f;
    }

    bool areClose(const std::string& a, const std::string& b) const {
        return getValue(a, b) >= 60.0f && getValue(b, a) >= 60.0f;
    }

    bool trustsEnough(const std::string& a, const std::string& b, float threshold = 40.0f) const {
        return getTrust(b, a) >= threshold; // does b trust a?
    }

    // All NPCs that 'id' has any relationship with
    std::vector<std::string> knownBy(const std::string& id) const {
        std::vector<std::string> out;
        for (auto& [key, _] : rels_) {
            if (key.first == id)       out.push_back(key.second);
            else if (key.second == id) out.push_back(key.first);
        }
        // deduplicate
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }

    // Best N friends of 'id' (by value of id→other)
    std::vector<std::pair<std::string, float>> topFriends(const std::string& id,
                                                            std::size_t n = 5) const {
        std::vector<std::pair<std::string, float>> out;
        for (auto& [key, data] : rels_) {
            if (key.first == id) out.push_back({key.second, data.value});
        }
        std::sort(out.begin(), out.end(),
                  [](auto& a, auto& b){ return a.second > b.second; });
        if (out.size() > n) out.resize(n);
        return out;
    }

    // Worst N enemies of 'id'
    std::vector<std::pair<std::string, float>> topEnemies(const std::string& id,
                                                            std::size_t n = 5) const {
        std::vector<std::pair<std::string, float>> out;
        for (auto& [key, data] : rels_) {
            if (key.first == id) out.push_back({key.second, data.value});
        }
        std::sort(out.begin(), out.end(),
                  [](auto& a, auto& b){ return a.second < b.second; });
        if (out.size() > n) out.resize(n);
        return out;
    }

    // ── Iteration / debug ─────────────────────────────────────────────────────
    void forEach(std::function<void(const std::string&, const std::string&,
                                    const RelationshipData&)> fn) const {
        for (auto& [key, data] : rels_)
            fn(key.first, key.second, data);
    }

    std::size_t pairCount() const { return rels_.size(); }

    std::string debugString() const {
        std::ostringstream ss;
        ss << "RelationshipSystem: " << rels_.size() << " directed pairs\n";
        for (auto& [key, data] : rels_) {
            ss << "  " << key.first << "→" << key.second
               << "  val=" << static_cast<int>(data.value)
               << "  trust=" << static_cast<int>(data.trust)
               << "  events=" << data.history.size()
               << "  [" << relationshipLabel(data.value) << "]\n";
        }
        return ss.str();
    }

private:
    struct PairHash {
        std::size_t operator()(const std::pair<std::string,std::string>& p) const {
            std::size_t h1 = std::hash<std::string>{}(p.first);
            std::size_t h2 = std::hash<std::string>{}(p.second);
            return h1 ^ (h2 * 2654435761ULL);
        }
    };
    std::unordered_map<std::pair<std::string,std::string>,
                       RelationshipData,
                       PairHash> rels_;
};

} // namespace npc
