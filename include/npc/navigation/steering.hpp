#pragma once

#include "../core/types.hpp"
#include "../core/vec2.hpp"
#include <vector>
#include <functional>
#include <algorithm>
#include <cmath>

namespace npc {

// ─── Agent Snapshot ───────────────────────────────────────────────────────────
// Immutable view of an agent's current state, used as input to steering.
struct SteeringAgent {
    EntityId id;
    Vec2     position;
    Vec2     velocity;
    float    radius       = 0.5f;   // collision radius in world units
    float    maxSpeed     = 3.0f;
    float    maxForce     = 10.0f;
    int      priority     = 0;      // higher = others yield to this agent
};

// ─── Obstacle ─────────────────────────────────────────────────────────────────
struct SteeringObstacle {
    Vec2  center;
    float radius;
};

// ─── Per-agent output ─────────────────────────────────────────────────────────
struct SteeringOutput {
    EntityId id;
    Vec2     steeringForce;   // raw force to add to velocity this frame
    Vec2     desiredVelocity; // suggested final velocity (already clamped to maxSpeed)
    bool     isBlocked = false; // true when all paths obstructed
};

// ─── Steering weights config ──────────────────────────────────────────────────
struct SteeringConfig {
    float seekWeight       = 1.0f;
    float separationWeight = 1.8f;
    float obstacleWeight   = 2.5f;
    float arrivalSlowRadius= 2.5f;  // world units — start slowing down at this dist
    float arrivalStopRadius= 0.25f; // world units — consider arrived
    float separationRadius = 1.8f;  // agent–agent repulsion distance (× sum of radii)
    float obstacleProbeLen = 2.5f;  // how far ahead to probe for obstacles
    float yieldBias        = 0.6f;  // lower-priority agent yields this fraction
};

// ─── SteeringSystem ───────────────────────────────────────────────────────────
class SteeringSystem {
public:
    explicit SteeringSystem(SteeringConfig cfg = {}) : cfg_(cfg) {}

    // ── Primitive behaviours ──────────────────────────────────────────────────

    // Move toward target at full speed.
    static Vec2 seek(Vec2 agentPos, Vec2 target, float maxSpeed) {
        return (target - agentPos).normalized() * maxSpeed;
    }

    // Decelerate when inside slowRadius, stop inside stopRadius.
    Vec2 arrive(Vec2 agentPos, Vec2 target, float maxSpeed) const {
        Vec2  toTarget = target - agentPos;
        float dist     = toTarget.length();
        if (dist < cfg_.arrivalStopRadius) return Vec2{};
        float speed = (dist < cfg_.arrivalSlowRadius)
                      ? maxSpeed * (dist / cfg_.arrivalSlowRadius)
                      : maxSpeed;
        return toTarget.normalized() * speed;
    }

    // Push agent away from all nearby agents (weighted by overlap).
    Vec2 separate(const SteeringAgent& self,
                  const std::vector<SteeringAgent>& others,
                  float maxForce) const {
        Vec2 force;
        for (const auto& other : others) {
            if (other.id == self.id) continue;
            Vec2  diff = self.position - other.position;
            float dist = diff.length();
            float minDist = (self.radius + other.radius) * cfg_.separationRadius;
            if (dist > minDist || dist < 1e-4f) continue;
            // Stronger push the closer they are
            float strength = (minDist - dist) / minDist;
            force += diff.normalized() * strength * maxForce;
        }
        return force;
    }

    // Steer around circular obstacles by probing ahead.
    Vec2 avoidObstacles(const SteeringAgent& agent,
                        const std::vector<SteeringObstacle>& obstacles) const {
        if (agent.velocity.lengthSquared() < 1e-6f) return Vec2{};

        Vec2  ahead      = agent.position + agent.velocity.normalized() * cfg_.obstacleProbeLen;
        Vec2  aheadHalf  = agent.position + agent.velocity.normalized() * (cfg_.obstacleProbeLen * 0.5f);
        float bestThreat = cfg_.obstacleProbeLen + 1.0f;
        const SteeringObstacle* closest = nullptr;

        for (const auto& obs : obstacles) {
            float d = ahead.distanceTo(obs.center);
            float d2= aheadHalf.distanceTo(obs.center);
            float combinedR = obs.radius + agent.radius;
            if ((d < combinedR || d2 < combinedR) && obs.center.distanceTo(agent.position) < bestThreat) {
                bestThreat = obs.center.distanceTo(agent.position);
                closest    = &obs;
            }
        }

        if (!closest) return Vec2{};
        // Push away from obstacle center in the direction perpendicular to approach
        Vec2 avoidDir = (ahead - closest->center).normalized();
        return avoidDir * agent.maxForce * cfg_.obstacleWeight;
    }

    // ── Priority yielding ─────────────────────────────────────────────────────
    // Lower-priority agent steers away from a higher-priority one.
    Vec2 yieldToHigherPriority(const SteeringAgent& self,
                                const std::vector<SteeringAgent>& others) const {
        Vec2 force;
        for (const auto& other : others) {
            if (other.id == self.id) continue;
            if (other.priority <= self.priority) continue;
            Vec2  diff = self.position - other.position;
            float dist = diff.length();
            float minDist = (self.radius + other.radius) * cfg_.separationRadius * 1.5f;
            if (dist > minDist || dist < 1e-4f) continue;
            float strength = (minDist - dist) / minDist * cfg_.yieldBias;
            force += diff.normalized() * strength * self.maxForce;
        }
        return force;
    }

    // ── Main update ───────────────────────────────────────────────────────────
    // Compute steering output for every agent given their goal positions.
    // goals: maps EntityId → desired destination
    // obstacles: static world obstacles (buildings, walls, etc.)
    std::vector<SteeringOutput> update(
            const std::vector<SteeringAgent>& agents,
            const std::function<Vec2(EntityId)>& goalOf,
            const std::vector<SteeringObstacle>& obstacles = {}) const
    {
        std::vector<SteeringOutput> out;
        out.reserve(agents.size());

        for (const auto& agent : agents) {
            Vec2 goal = goalOf(agent.id);

            // 1. Arrive at goal
            Vec2 arriveForce = arrive(agent.position, goal, agent.maxSpeed)
                               * cfg_.seekWeight;

            // 2. Separate from other agents
            Vec2 sepForce = separate(agent, agents, agent.maxForce)
                            * cfg_.separationWeight;

            // 3. Avoid static obstacles
            Vec2 obsForce = avoidObstacles(agent, obstacles);

            // 4. Yield to higher-priority agents
            Vec2 yieldForce = yieldToHigherPriority(agent, agents);

            // Sum forces
            Vec2 total = arriveForce + sepForce + obsForce + yieldForce;

            // Clamp to maxForce
            if (total.length() > agent.maxForce)
                total = total.normalized() * agent.maxForce;

            // Integrate into velocity (caller applies with their dt)
            Vec2 newVel = agent.velocity + total;
            if (newVel.length() > agent.maxSpeed)
                newVel = newVel.normalized() * agent.maxSpeed;

            // Snap to zero near goal
            bool arrived = agent.position.distanceTo(goal) < cfg_.arrivalStopRadius;
            if (arrived) newVel = Vec2{};

            SteeringOutput o;
            o.id              = agent.id;
            o.steeringForce   = total;
            o.desiredVelocity = newVel;
            o.isBlocked       = !arrived && newVel.lengthSquared() < 1e-4f
                                && total.lengthSquared() > 0.1f;
            out.push_back(o);
        }
        return out;
    }

    // ── Utility: resolve path-level collision (swap vs. path around) ──────────
    // Two agents heading toward each other: returns adjusted goals to step aside.
    static std::pair<Vec2, Vec2> resolveHeadOn(
            const SteeringAgent& a, Vec2 goalA,
            const SteeringAgent& b, Vec2 goalB,
            float sideStepDist = 1.2f)
    {
        Vec2 toA   = (goalA - a.position).normalized();
        Vec2 toB   = (goalB - b.position).normalized();
        float dot  = toA.dot(toB * -1.0f);  // ~1 when head-on

        if (dot < 0.7f) return {goalA, goalB};  // not head-on, no adjustment

        // Side-step: A goes right, B goes left (relative to their direction)
        Vec2 rightA = Vec2{-toA.y,  toA.x};
        Vec2 rightB = Vec2{ toB.y, -toB.x};
        return {
            goalA + rightA * sideStepDist,
            goalB + rightB * sideStepDist
        };
    }

    // ── Formation-aware override ──────────────────────────────────────────────
    // If an agent is within tolerance of its formation slot, zero out steering
    // (let the group move it) rather than fighting individual seeks.
    static Vec2 formationOverride(Vec2 agentPos, Vec2 formationSlot,
                                   Vec2 steeringForce, float tolerance = 0.5f) {
        float d = agentPos.distanceTo(formationSlot);
        if (d < tolerance) return Vec2{};          // in slot — stop fighting
        if (d < tolerance * 3.0f) {
            // Blend: closer = less individual steering
            float blend = (d - tolerance) / (tolerance * 2.0f);
            return steeringForce * blend;
        }
        return steeringForce;
    }

    const SteeringConfig& config() const { return cfg_; }

private:
    SteeringConfig cfg_;
};

} // namespace npc
