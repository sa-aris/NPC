#pragma once
// Shared Blackboard — world-level key-value store accessible by all NPCs.
//
//  SharedBlackboard   Core store: string keys → std::any values, with TTL,
//                     version counters, and watcher callbacks.
//  BlackboardView     Read-only scoped proxy into a namespace prefix.
//  WorldBlackboard    Typed accessors for standard world namespaces:
//                       world/*  market/*  faction/*  combat/*  event/*
//  BlackboardSync     Bridge: pull shared→local or push local→shared.

#include "blackboard.hpp"
#include "../core/types.hpp"

#include <any>
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <functional>
#include <algorithm>
#include <list>
#include <cstdint>

namespace npc {

// ═══════════════════════════════════════════════════════════════════════
// BBEntry — a single value in the SharedBlackboard
// ═══════════════════════════════════════════════════════════════════════

struct BBEntry {
    std::any   value;
    float      writtenAt  = 0.f;
    float      expiresAt  = -1.f;   // -1 = never expires
    uint64_t   version    = 0;
    EntityId   writer     = INVALID_ENTITY;
};

// ═══════════════════════════════════════════════════════════════════════
// SharedBlackboard
// ═══════════════════════════════════════════════════════════════════════

class SharedBlackboard {
public:
    using WatcherId      = uint32_t;
    using ChangeCallback = std::function<void(const std::string& key,
                                              const std::any&    value,
                                              const BBEntry&     entry)>;

    // ── Write ────────────────────────────────────────────────────────

    template<typename T>
    void set(const std::string& key, const T& value,
             float    currentTime = 0.f,
             float    ttl         = -1.f,
             EntityId writer      = INVALID_ENTITY)
    {
        auto& e = data_[key];
        e.value     = value;
        e.writtenAt = currentTime;
        e.expiresAt = (ttl > 0.f) ? currentTime + ttl : -1.f;
        e.version++;
        e.writer = writer;
        notifyWatchers(key, e);
    }

    // Overwrite only if key doesn't already exist (or is expired)
    template<typename T>
    bool setIfAbsent(const std::string& key, const T& value,
                     float currentTime = 0.f,
                     float ttl         = -1.f,
                     EntityId writer   = INVALID_ENTITY)
    {
        if (has(key, currentTime)) return false;
        set(key, value, currentTime, ttl, writer);
        return true;
    }

    // ── Read ─────────────────────────────────────────────────────────

    template<typename T>
    std::optional<T> get(const std::string& key,
                         float currentTime = 0.f) const {
        auto it = data_.find(key);
        if (it == data_.end() || isExpired(it->second, currentTime))
            return std::nullopt;
        try { return std::any_cast<T>(it->second.value); }
        catch (...) { return std::nullopt; }
    }

    template<typename T>
    T getOr(const std::string& key, const T& def,
            float currentTime = 0.f) const {
        return get<T>(key, currentTime).value_or(def);
    }

    const BBEntry* entry(const std::string& key,
                         float currentTime = 0.f) const {
        auto it = data_.find(key);
        if (it == data_.end() || isExpired(it->second, currentTime))
            return nullptr;
        return &it->second;
    }

    bool has(const std::string& key, float currentTime = 0.f) const {
        auto it = data_.find(key);
        return it != data_.end() && !isExpired(it->second, currentTime);
    }

    uint64_t version(const std::string& key) const {
        auto it = data_.find(key);
        return it != data_.end() ? it->second.version : 0;
    }

    // ── Remove / expire ──────────────────────────────────────────────

    void remove(const std::string& key) { data_.erase(key); }

    // Expire all entries with TTL < currentTime
    int pruneExpired(float currentTime) {
        int removed = 0;
        for (auto it = data_.begin(); it != data_.end(); ) {
            if (isExpired(it->second, currentTime)) {
                it = data_.erase(it); ++removed;
            } else { ++it; }
        }
        return removed;
    }

    // Type-agnostic set (for BlackboardSync::pushAll)
    void setAny(const std::string& key, const std::any& value,
                float currentTime = 0.f, float ttl = -1.f,
                EntityId writer = INVALID_ENTITY)
    {
        auto& e = data_[key];
        e.value     = value;
        e.writtenAt = currentTime;
        e.expiresAt = (ttl > 0.f) ? currentTime + ttl : -1.f;
        e.version++;
        e.writer    = writer;
        notifyWatchers(key, e);
    }

    void clear() { data_.clear(); }

    // ── Namespace queries ────────────────────────────────────────────

    std::vector<std::string> keysWithPrefix(const std::string& prefix,
                                             float currentTime = 0.f) const {
        std::vector<std::string> out;
        for (auto& [k, e] : data_)
            if (!isExpired(e, currentTime) &&
                k.size() >= prefix.size() &&
                k.compare(0, prefix.size(), prefix) == 0)
                out.push_back(k);
        return out;
    }

    void forEach(const std::function<void(const std::string&,
                                           const BBEntry&)>& fn,
                 float currentTime = 0.f) const {
        for (auto& [k, e] : data_)
            if (!isExpired(e, currentTime)) fn(k, e);
    }

    void forEachInNamespace(const std::string& prefix,
                             const std::function<void(const std::string&,
                                                       const BBEntry&)>& fn,
                             float currentTime = 0.f) const {
        for (auto& [k, e] : data_)
            if (!isExpired(e, currentTime) &&
                k.size() >= prefix.size() &&
                k.compare(0, prefix.size(), prefix) == 0)
                fn(k, e);
    }

    size_t size(float currentTime = 0.f) const {
        if (currentTime <= 0.f) return data_.size();
        size_t n = 0;
        for (auto& [k, e] : data_) if (!isExpired(e, currentTime)) ++n;
        return n;
    }

    // ── Watchers ─────────────────────────────────────────────────────
    // Subscribe to changes on a key or any key matching a prefix.

    WatcherId watch(const std::string& keyOrPrefix, ChangeCallback cb) {
        WatcherId id = nextWatchId_++;
        watchers_.push_back({id, keyOrPrefix, std::move(cb)});
        return id;
    }

    void unwatch(WatcherId id) {
        watchers_.erase(std::remove_if(watchers_.begin(), watchers_.end(),
            [id](const Watcher& w){ return w.id == id; }), watchers_.end());
    }

    // ── Scoped watcher RAII ──────────────────────────────────────────

    struct ScopedWatch {
        ScopedWatch() = default;
        ScopedWatch(SharedBlackboard& bb, WatcherId id) : bb_(&bb), id_(id) {}
        ~ScopedWatch() { if (bb_) bb_->unwatch(id_); }
        ScopedWatch(const ScopedWatch&)            = delete;
        ScopedWatch& operator=(const ScopedWatch&) = delete;
        ScopedWatch(ScopedWatch&& o) noexcept : bb_(o.bb_), id_(o.id_)
            { o.bb_ = nullptr; }
        ScopedWatch& operator=(ScopedWatch&& o) noexcept {
            if (this != &o) { if (bb_) bb_->unwatch(id_); bb_=o.bb_; id_=o.id_; o.bb_=nullptr; }
            return *this;
        }
        void release() { bb_ = nullptr; }
    private:
        SharedBlackboard* bb_ = nullptr;
        WatcherId         id_ = 0;
    };

    ScopedWatch watchScoped(const std::string& prefix, ChangeCallback cb) {
        WatcherId id = watch(prefix, std::move(cb));
        return ScopedWatch(*this, id);
    }

private:
    static bool isExpired(const BBEntry& e, float t) {
        return e.expiresAt > 0.f && t > e.expiresAt;
    }

    void notifyWatchers(const std::string& key, const BBEntry& e) {
        for (auto& w : watchers_) {
            if (key.size() >= w.prefix.size() &&
                key.compare(0, w.prefix.size(), w.prefix) == 0)
                w.cb(key, e.value, e);
        }
    }

    struct Watcher {
        WatcherId      id;
        std::string    prefix;
        ChangeCallback cb;
    };

    std::unordered_map<std::string, BBEntry> data_;
    std::vector<Watcher>                     watchers_;
    WatcherId nextWatchId_ = 1;
};

// ═══════════════════════════════════════════════════════════════════════
// BlackboardView — read-only scoped proxy into a namespace
// ═══════════════════════════════════════════════════════════════════════

class BlackboardView {
public:
    BlackboardView(const SharedBlackboard& bb,
                   std::string             ns,
                   float                   currentTime = 0.f)
        : bb_(bb), ns_(std::move(ns)), time_(currentTime) {}

    void setTime(float t) { time_ = t; }

    template<typename T>
    std::optional<T> get(const std::string& localKey) const {
        return bb_.get<T>(ns_ + localKey, time_);
    }

    template<typename T>
    T getOr(const std::string& localKey, const T& def) const {
        return bb_.getOr<T>(ns_ + localKey, def, time_);
    }

    bool has(const std::string& localKey) const {
        return bb_.has(ns_ + localKey, time_);
    }

    uint64_t version(const std::string& localKey) const {
        return bb_.version(ns_ + localKey);
    }

    std::vector<std::string> keys() const {
        auto full = bb_.keysWithPrefix(ns_, time_);
        std::vector<std::string> out;
        out.reserve(full.size());
        for (auto& k : full) out.push_back(k.substr(ns_.size()));
        return out;
    }

    const std::string& ns() const { return ns_; }

private:
    const SharedBlackboard& bb_;
    std::string             ns_;
    float                   time_;
};

// ═══════════════════════════════════════════════════════════════════════
// WorldBlackboard — typed accessors for standard world namespaces
// ═══════════════════════════════════════════════════════════════════════
// SimulationManager writes here; NPC subsystems read via views.
//
//  world/*     — time, weather, day/night
//  market/*    — item prices, supply, demand
//  faction/*   — alert states, relation values
//  combat/*    — active fights (with TTL auto-expiry)
//  event/*     — world events (with TTL)

class WorldBlackboard {
public:
    explicit WorldBlackboard(SharedBlackboard& bb) : bb_(bb) {}

    SharedBlackboard& raw() { return bb_; }
    const SharedBlackboard& raw() const { return bb_; }

    BlackboardView viewOf(const std::string& ns,
                          float t = 0.f) const {
        return BlackboardView(bb_, ns, t);
    }

    // ── world/* ──────────────────────────────────────────────────────

    void setTime(float hour, float t = 0.f) {
        bb_.set("world/hour", hour, t);
    }
    float time(float t = 0.f) const {
        return bb_.getOr<float>("world/hour", 0.f, t);
    }

    void setDay(int day, float t = 0.f) {
        bb_.set("world/day", day, t);
    }
    int day(float t = 0.f) const {
        return bb_.getOr<int>("world/day", 1, t);
    }

    void setWeather(const std::string& weather, float intensity,
                    float t = 0.f) {
        bb_.set("world/weather",           weather,   t);
        bb_.set("world/weather_intensity", intensity, t);
    }
    std::string weather(float t = 0.f) const {
        return bb_.getOr<std::string>("world/weather", "Clear", t);
    }
    float weatherIntensity(float t = 0.f) const {
        return bb_.getOr<float>("world/weather_intensity", 0.f, t);
    }

    void setTimeOfDay(const std::string& tod, float t = 0.f) {
        bb_.set("world/time_of_day", tod, t);
    }
    std::string timeOfDay(float t = 0.f) const {
        return bb_.getOr<std::string>("world/time_of_day", "Morning", t);
    }

    // ── market/* ─────────────────────────────────────────────────────

    void setItemPrice(ItemId item, float price, float t = 0.f) {
        bb_.set("market/price/" + std::to_string(item), price, t);
    }
    float itemPrice(ItemId item, float def = 1.f, float t = 0.f) const {
        return bb_.getOr<float>("market/price/" + std::to_string(item),
                                def, t);
    }

    void setItemDemand(ItemId item, float demand, float t = 0.f) {
        bb_.set("market/demand/" + std::to_string(item), demand, t);
    }
    float itemDemand(ItemId item, float def = 1.f, float t = 0.f) const {
        return bb_.getOr<float>("market/demand/" + std::to_string(item),
                                def, t);
    }

    void setItemAvailable(ItemId item, bool avail, float t = 0.f) {
        bb_.set("market/available/" + std::to_string(item), avail, t);
    }
    bool itemAvailable(ItemId item, float t = 0.f) const {
        return bb_.getOr<bool>("market/available/" + std::to_string(item),
                               true, t);
    }

    // ── faction/* ────────────────────────────────────────────────────

    void setFactionAlert(FactionId faction, bool alert,
                         float t = 0.f, float ttl = 120.f) {
        bb_.set("faction/" + std::to_string(faction) + "/alert",
                alert, t, ttl);
    }
    bool factionAlert(FactionId faction, float t = 0.f) const {
        return bb_.getOr<bool>(
            "faction/" + std::to_string(faction) + "/alert", false, t);
    }

    void setFactionRelation(FactionId a, FactionId b,
                             float rel, float t = 0.f) {
        bb_.set(relationKey(a, b), rel, t);
    }
    float factionRelation(FactionId a, FactionId b,
                           float t = 0.f) const {
        return bb_.getOr<float>(relationKey(a, b), 0.f, t);
    }

    void setFactionWar(FactionId a, FactionId b,
                        bool atWar, float t = 0.f) {
        bb_.set("faction/war/" + std::to_string(std::min(a,b)) +
                "/" + std::to_string(std::max(a,b)), atWar, t);
    }
    bool factionAtWar(FactionId a, FactionId b, float t = 0.f) const {
        return bb_.getOr<bool>(
            "faction/war/" + std::to_string(std::min(a,b)) +
            "/" + std::to_string(std::max(a,b)), false, t);
    }

    // ── combat/* (with TTL — auto-expires if fight ends) ─────────────

    void setCombatActive(const std::string& zoneId,
                          EntityId attacker, EntityId defender,
                          Vec2 location,
                          float t = 0.f, float ttl = 30.f) {
        std::string pfx = "combat/" + zoneId + "/";
        bb_.set(pfx + "active",   true,     t, ttl);
        bb_.set(pfx + "attacker", attacker, t, ttl);
        bb_.set(pfx + "defender", defender, t, ttl);
        bb_.set(pfx + "loc_x",    location.x, t, ttl);
        bb_.set(pfx + "loc_y",    location.y, t, ttl);
    }

    bool isCombatActive(const std::string& zoneId,
                         float t = 0.f) const {
        return bb_.getOr<bool>("combat/" + zoneId + "/active", false, t);
    }

    Vec2 combatLocation(const std::string& zoneId,
                         float t = 0.f) const {
        std::string pfx = "combat/" + zoneId + "/";
        return { bb_.getOr<float>(pfx + "loc_x", 0.f, t),
                 bb_.getOr<float>(pfx + "loc_y", 0.f, t) };
    }

    // Refresh TTL while fight is still ongoing
    void keepCombatAlive(const std::string& zoneId,
                          float t, float ttl = 30.f) {
        for (auto& key : bb_.keysWithPrefix("combat/" + zoneId + "/", t))
            if (auto* e = bb_.entry(key, t))
                bb_.set(key, e->value, t, ttl, e->writer);
    }

    // ── event/* (with TTL) ───────────────────────────────────────────

    void broadcastEvent(const std::string& name,
                         const std::string& description,
                         float severity,
                         float t = 0.f, float ttl = 120.f) {
        std::string pfx = "event/" + name + "/";
        bb_.set(pfx + "active",   true,        t, ttl);
        bb_.set(pfx + "desc",     description, t, ttl);
        bb_.set(pfx + "severity", severity,    t, ttl);
        bb_.set(pfx + "time",     t,           t, ttl);
    }

    bool isEventActive(const std::string& name, float t = 0.f) const {
        return bb_.getOr<bool>("event/" + name + "/active", false, t);
    }
    std::string eventDesc(const std::string& name,
                           float t = 0.f) const {
        return bb_.getOr<std::string>("event/" + name + "/desc", "", t);
    }
    float eventSeverity(const std::string& name,
                         float t = 0.f) const {
        return bb_.getOr<float>("event/" + name + "/severity", 0.f, t);
    }

    // List all currently active events
    std::vector<std::string> activeEvents(float t = 0.f) const {
        std::vector<std::string> out;
        for (auto& key : bb_.keysWithPrefix("event/", t)) {
            // key format: event/<name>/active
            if (key.size() > 7 && key.substr(key.rfind('/')+1) == "active") {
                if (bb_.getOr<bool>(key, false, t)) {
                    // extract <name>
                    auto start = 6u; // len("event/")
                    auto end   = key.rfind('/');
                    out.push_back(key.substr(start, end - start));
                }
            }
        }
        return out;
    }

    // ── Maintenance ──────────────────────────────────────────────────

    // Call from SimulationManager::update() to expire stale entries
    int prune(float currentTime) { return bb_.pruneExpired(currentTime); }

private:
    static std::string relationKey(FactionId a, FactionId b) {
        return "faction/rel/" + std::to_string(std::min(a,b)) +
               "/" + std::to_string(std::max(a,b));
    }

    SharedBlackboard& bb_;
};

// ═══════════════════════════════════════════════════════════════════════
// BlackboardSync — bridge between local NPC BB and SharedBlackboard
// ═══════════════════════════════════════════════════════════════════════

class BlackboardSync {
public:
    // Pull all entries from a SharedBlackboard namespace into local BB.
    // Keys stored as-is (full path) unless stripPrefix=true.
    static void pull(Blackboard&              local,
                     const SharedBlackboard&  shared,
                     const std::string&       ns,
                     bool                     stripPrefix  = true,
                     float                    currentTime  = 0.f)
    {
        shared.forEachInNamespace(ns, [&](const std::string& key,
                                          const BBEntry& entry) {
            std::string localKey = stripPrefix ? key.substr(ns.size()) : key;
            local.setAny(localKey, entry.value);
        }, currentTime);
    }

    // Push a single typed value from local BB to SharedBlackboard.
    template<typename T>
    static bool push(const Blackboard& local,
                     SharedBlackboard& shared,
                     const std::string& localKey,
                     const std::string& sharedKey,
                     float currentTime = 0.f,
                     float ttl         = -1.f,
                     EntityId writer   = INVALID_ENTITY)
    {
        auto v = local.get<T>(localKey);
        if (!v) return false;
        shared.set(sharedKey, *v, currentTime, ttl, writer);
        return true;
    }

    // Push all local BB entries with localNs prefix → shared with sharedNs prefix.
    // Type-agnostic: copies std::any directly.
    static void pushAll(const Blackboard& local,
                        SharedBlackboard& shared,
                        const std::string& localNs,
                        const std::string& sharedNs,
                        float currentTime = 0.f,
                        float ttl         = -1.f,
                        EntityId writer   = INVALID_ENTITY)
    {
        local.forEach([&](const std::string& key, const std::any& value) {
            if (key.size() >= localNs.size() &&
                key.compare(0, localNs.size(), localNs) == 0) {
                std::string sharedKey = sharedNs + key.substr(localNs.size());
                shared.setAny(sharedKey, value, currentTime, ttl, writer);
            }
        });
    }
};

} // namespace npc
