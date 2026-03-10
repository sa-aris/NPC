#pragma once

#include <vector>
#include <string>
#include <functional>
#include <algorithm>
#include <cmath>
#include <optional>
#include "blackboard.hpp"

namespace npc {

// ─── Scoring Curves ──────────────────────────────────────────────────

namespace curves {

// Linear: f(x) = slope * x + intercept, clamped to [0,1]
inline std::function<float(float)> linear(float slope = 1.0f, float intercept = 0.0f) {
    return [slope, intercept](float x) {
        return std::clamp(slope * x + intercept, 0.0f, 1.0f);
    };
}

// Exponential: f(x) = x^exponent
inline std::function<float(float)> exponential(float exponent = 2.0f) {
    return [exponent](float x) {
        return std::clamp(std::pow(std::clamp(x, 0.0f, 1.0f), exponent), 0.0f, 1.0f);
    };
}

// Sigmoid: f(x) = 1 / (1 + e^(-steepness*(x - midpoint)))
inline std::function<float(float)> sigmoid(float steepness = 10.0f, float midpoint = 0.5f) {
    return [steepness, midpoint](float x) {
        return 1.0f / (1.0f + std::exp(-steepness * (x - midpoint)));
    };
}

// Step: returns 1 if x >= threshold, else 0
inline std::function<float(float)> step(float threshold = 0.5f) {
    return [threshold](float x) {
        return x >= threshold ? 1.0f : 0.0f;
    };
}

// Inverse: f(x) = 1 - x
inline std::function<float(float)> inverse() {
    return [](float x) {
        return 1.0f - std::clamp(x, 0.0f, 1.0f);
    };
}

} // namespace curves

// ─── Utility Action ──────────────────────────────────────────────────

struct UtilityAction {
    std::string name;
    std::function<float(const Blackboard&)> scoreFn;
    std::function<void(Blackboard&)> actionFn;
    float weight = 1.0f;
    float lastScore = 0.0f;
};

// ─── Utility AI ──────────────────────────────────────────────────────

class UtilityAI {
public:
    void addAction(const std::string& name,
                   std::function<float(const Blackboard&)> scoreFn,
                   std::function<void(Blackboard&)> actionFn,
                   float weight = 1.0f) {
        actions_.push_back({name, std::move(scoreFn), std::move(actionFn), weight, 0.0f});
    }

    struct Decision {
        std::string actionName;
        float score;
    };

    std::optional<Decision> evaluate(Blackboard& bb) {
        if (actions_.empty()) return std::nullopt;

        float bestScore = -1.0f;
        UtilityAction* bestAction = nullptr;

        for (auto& action : actions_) {
            float raw = action.scoreFn(bb);
            action.lastScore = raw * action.weight;
            if (action.lastScore > bestScore) {
                bestScore = action.lastScore;
                bestAction = &action;
            }
        }

        if (bestAction && bestScore > 0.0f) {
            bestAction->actionFn(bb);
            return Decision{bestAction->name, bestScore};
        }

        return std::nullopt;
    }

    const std::vector<UtilityAction>& actions() const { return actions_; }

    void clearActions() { actions_.clear(); }

private:
    std::vector<UtilityAction> actions_;
};

} // namespace npc
