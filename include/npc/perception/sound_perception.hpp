#pragma once
// sound_perception.hpp — world-level noise events and hearing propagation
// Part of Aithena (C++17, header-only)
//
// PerceptionSystem models each NPC's continuous senses (sight cone + ambient
// noise). SoundscapeSystem is the complement: DISCRETE noise events — a
// scream, a door slam, a sword clash — emitted into the world, attenuated by
// distance and walls, and delivered to whoever could hear them. Listeners
// get an InvestigationTarget they can feed into their AI (walk over, look
// around), or push straight into a PerceptionSystem via toSensoryInput().

#include "../core/types.hpp"
#include "../core/vec2.hpp"
#include "perception_system.hpp"
#include <string>
#include <vector>
#include <algorithm>
#include <functional>
#include <sstream>

namespace npc {

// ─── Noise types ──────────────────────────────────────────────────────────────

enum class NoiseType {
    Footstep,
    Speech,
    Music,
    DoorSlam,
    GlassBreak,
    Shout,
    CombatClash,
    Scream,
    Explosion
};

inline const char* noiseTypeName(NoiseType t) {
    switch (t) {
        case NoiseType::Footstep:    return "Footstep";
        case NoiseType::Speech:      return "Speech";
        case NoiseType::Music:       return "Music";
        case NoiseType::DoorSlam:    return "DoorSlam";
        case NoiseType::GlassBreak:  return "GlassBreak";
        case NoiseType::Shout:       return "Shout";
        case NoiseType::CombatClash: return "CombatClash";
        case NoiseType::Scream:      return "Scream";
        case NoiseType::Explosion:   return "Explosion";
    }
    return "Unknown";
}

// Base loudness 0..1 — scales the audible radius
inline float noiseBaseLoudness(NoiseType t) {
    switch (t) {
        case NoiseType::Footstep:    return 0.15f;
        case NoiseType::Speech:      return 0.30f;
        case NoiseType::Music:       return 0.40f;
        case NoiseType::DoorSlam:    return 0.50f;
        case NoiseType::GlassBreak:  return 0.55f;
        case NoiseType::Shout:       return 0.70f;
        case NoiseType::CombatClash: return 0.80f;
        case NoiseType::Scream:      return 0.90f;
        case NoiseType::Explosion:   return 1.00f;
    }
    return 0.3f;
}

// How urgently a listener should investigate (0 = ignorable, 1 = drop everything)
inline float noiseUrgency(NoiseType t) {
    switch (t) {
        case NoiseType::Footstep:    return 0.05f;
        case NoiseType::Speech:      return 0.05f;
        case NoiseType::Music:       return 0.02f;
        case NoiseType::DoorSlam:    return 0.25f;
        case NoiseType::GlassBreak:  return 0.55f;
        case NoiseType::Shout:       return 0.50f;
        case NoiseType::CombatClash: return 0.85f;
        case NoiseType::Scream:      return 0.95f;
        case NoiseType::Explosion:   return 1.00f;
    }
    return 0.2f;
}

// ─── Noise event ──────────────────────────────────────────────────────────────

struct NoiseEvent {
    uint32_t  id        = 0;
    NoiseType type      = NoiseType::Speech;
    Vec2      position;
    EntityId  emitter   = INVALID_ENTITY;
    float     loudness  = 0.3f;   // 0..1+, defaults from noiseBaseLoudness
    double    timeEmitted = 0.0;
    std::string note;             // optional context ("wolf howl")
};

// What a specific listener perceived of a noise event
struct HeardSound {
    uint32_t  eventId    = 0;
    NoiseType type       = NoiseType::Speech;
    Vec2      position;            // TRUE source position
    Vec2      perceivedPosition;   // degraded estimate (faint = vaguer)
    EntityId  emitter    = INVALID_ENTITY;
    float     intensity  = 0.0f;   // 0..1 how loud it was AT the listener
    float     urgency    = 0.0f;   // noiseUrgency * intensity
    double    timeHeard  = 0.0;

    bool worthInvestigating(float threshold = 0.3f) const {
        return urgency >= threshold;
    }
};

// ─── SoundscapeSystem ─────────────────────────────────────────────────────────

class SoundscapeSystem {
public:
    struct Config {
        float baseAudibleRange   = 20.0f;  // full-loudness (1.0) audible radius
        float wallDamping        = 0.5f;   // per-wall multiplier (matches PerceptionConfig)
        float eventLifetime      = 0.25f;  // hours an event stays queryable
        float minIntensity       = 0.05f;  // below this the sound is inaudible
        float positionErrorScale = 6.0f;   // how far off faint sounds are perceived
        int   maxEvents          = 256;
    };

    Config config;

    // Same wall-counting callback PerceptionSystem uses (optional)
    WallCounter wallCounter;

    // ── Emission ──────────────────────────────────────────────────────────────
    uint32_t emit(NoiseType type, Vec2 position, double simTime,
                  EntityId emitter = INVALID_ENTITY,
                  float loudnessOverride = -1.0f,
                  const std::string& note = {}) {
        NoiseEvent ev;
        ev.id          = nextId_++;
        ev.type        = type;
        ev.position    = position;
        ev.emitter     = emitter;
        ev.loudness    = loudnessOverride >= 0.0f ? loudnessOverride
                                                  : noiseBaseLoudness(type);
        ev.timeEmitted = simTime;
        ev.note        = note;
        events_.push_back(std::move(ev));
        if (static_cast<int>(events_.size()) > config.maxEvents)
            events_.erase(events_.begin());
        return events_.back().id;
    }

    // ── Listening ─────────────────────────────────────────────────────────────

    // Intensity of one event at a listener position (0 = inaudible)
    float intensityAt(const NoiseEvent& ev, Vec2 listenerPos) const {
        float dist  = ev.position.distanceTo(listenerPos);
        float range = config.baseAudibleRange * ev.loudness;
        if (range <= 0.0f || dist >= range) return 0.0f;

        float intensity = ev.loudness * (1.0f - dist / range);
        if (wallCounter) {
            int walls = wallCounter(listenerPos, ev.position);
            for (int i = 0; i < walls; ++i) intensity *= config.wallDamping;
        }
        return intensity >= config.minIntensity ? intensity : 0.0f;
    }

    // Everything a listener at `pos` can currently hear.
    // `hearingAcuity` scales the listener's sensitivity (1 = normal).
    std::vector<HeardSound> listen(Vec2 pos, double simTime,
                                   float hearingAcuity = 1.0f,
                                   EntityId selfId = INVALID_ENTITY) const {
        std::vector<HeardSound> out;
        for (const auto& ev : events_) {
            if (simTime - ev.timeEmitted > config.eventLifetime) continue;
            if (ev.emitter != INVALID_ENTITY && ev.emitter == selfId) continue;

            float intensity = intensityAt(ev, pos) * hearingAcuity;
            if (intensity < config.minIntensity) continue;

            HeardSound hs;
            hs.eventId   = ev.id;
            hs.type      = ev.type;
            hs.position  = ev.position;
            hs.emitter   = ev.emitter;
            hs.intensity = std::min(1.0f, intensity);
            hs.urgency   = noiseUrgency(ev.type) * hs.intensity;
            hs.timeHeard = simTime;

            // Faint sounds are localized poorly: offset the perceived position
            // deterministically (hash of ids) so repeated queries agree.
            float error = (1.0f - hs.intensity) * config.positionErrorScale;
            uint32_t h  = ev.id * 2654435761u + static_cast<uint32_t>(selfId) * 40503u;
            float ang   = static_cast<float>(h % 6283) / 1000.0f; // 0..2π
            hs.perceivedPosition = ev.position +
                Vec2(std::cos(ang), std::sin(ang)) * error;

            out.push_back(hs);
        }
        // Loudest first
        std::sort(out.begin(), out.end(),
                  [](const HeardSound& a, const HeardSound& b){
                      return a.intensity > b.intensity;
                  });
        return out;
    }

    // The single most urgent sound for a listener, if any crosses the threshold
    std::optional<HeardSound> mostUrgent(Vec2 pos, double simTime,
                                         float urgencyThreshold = 0.3f,
                                         float hearingAcuity = 1.0f,
                                         EntityId selfId = INVALID_ENTITY) const {
        auto heard = listen(pos, simTime, hearingAcuity, selfId);
        const HeardSound* best = nullptr;
        for (const auto& hs : heard)
            if (hs.urgency >= urgencyThreshold && (!best || hs.urgency > best->urgency))
                best = &hs;
        return best ? std::optional<HeardSound>(*best) : std::nullopt;
    }

    // Bridge into PerceptionSystem: convert a heard sound to a SensoryInput
    // (feed it into PerceptionSystem::update alongside visual contacts).
    static SensoryInput toSensoryInput(const HeardSound& hs, bool hostile = false) {
        SensoryInput in;
        in.entityId   = hs.emitter;
        in.position   = hs.perceivedPosition;
        in.noiseLevel = hs.intensity;
        in.isHostile  = hostile;
        return in;
    }

    // ── Maintenance ───────────────────────────────────────────────────────────
    void update(double simTime) {
        events_.erase(std::remove_if(events_.begin(), events_.end(),
                      [&](const NoiseEvent& ev){
                          return simTime - ev.timeEmitted > config.eventLifetime;
                      }),
                      events_.end());
    }

    void clear() { events_.clear(); }

    const std::vector<NoiseEvent>& activeEvents() const { return events_; }
    std::size_t eventCount() const { return events_.size(); }

    std::string debugString() const {
        std::ostringstream ss;
        ss << "SoundscapeSystem: " << events_.size() << " active events\n";
        for (const auto& ev : events_) {
            ss << "  #" << ev.id << " " << noiseTypeName(ev.type)
               << " @(" << ev.position.x << "," << ev.position.y << ")"
               << " loudness=" << ev.loudness;
            if (!ev.note.empty()) ss << " (" << ev.note << ")";
            ss << "\n";
        }
        return ss.str();
    }

private:
    std::vector<NoiseEvent> events_;
    uint32_t                nextId_ = 1;
};

} // namespace npc
