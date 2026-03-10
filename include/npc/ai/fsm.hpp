#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <memory>
#include <iostream>
#include "blackboard.hpp"

namespace npc {

using StateId = std::string;

// ─── State Base ──────────────────────────────────────────────────────

class State {
public:
    explicit State(std::string name) : name_(std::move(name)) {}
    virtual ~State() = default;

    virtual void onEnter(Blackboard& bb)              { (void)bb; }
    virtual void onUpdate(Blackboard& bb, float dt)   { (void)bb; (void)dt; }
    virtual void onExit(Blackboard& bb)               { (void)bb; }

    const std::string& name() const { return name_; }

private:
    std::string name_;
};

// ─── Lambda State (convenience) ──────────────────────────────────────

class LambdaState : public State {
public:
    using Callback = std::function<void(Blackboard&, float)>;
    using HookCallback = std::function<void(Blackboard&)>;

    explicit LambdaState(std::string name,
                         Callback onUpdate = nullptr,
                         HookCallback onEnter = nullptr,
                         HookCallback onExit = nullptr)
        : State(std::move(name))
        , updateFn_(std::move(onUpdate))
        , enterFn_(std::move(onEnter))
        , exitFn_(std::move(onExit)) {}

    void onEnter(Blackboard& bb) override {
        if (enterFn_) enterFn_(bb);
    }

    void onUpdate(Blackboard& bb, float dt) override {
        if (updateFn_) updateFn_(bb, dt);
    }

    void onExit(Blackboard& bb) override {
        if (exitFn_) exitFn_(bb);
    }

private:
    Callback updateFn_;
    HookCallback enterFn_;
    HookCallback exitFn_;
};

// ─── Transition ──────────────────────────────────────────────────────

struct Transition {
    StateId from;
    StateId to;
    std::function<bool(const Blackboard&)> guard;
    int priority = 0;
};

// ─── State Transition Record ────────────────────────────────────────

struct StateTransitionRecord {
    StateId from;
    StateId to;
    float timestamp = 0.0f;  // game-time when transition occurred
    float duration = 0.0f;   // time spent in 'from' state
};

// ─── Finite State Machine ────────────────────────────────────────────

class FSM {
public:
    void addState(std::unique_ptr<State> state) {
        auto name = state->name();
        states_[name] = std::move(state);
    }

    void addState(const StateId& id,
                  LambdaState::Callback onUpdate = nullptr,
                  LambdaState::HookCallback onEnter = nullptr,
                  LambdaState::HookCallback onExit = nullptr) {
        states_[id] = std::make_unique<LambdaState>(id, std::move(onUpdate),
                                                     std::move(onEnter),
                                                     std::move(onExit));
    }

    void addTransition(StateId from, StateId to,
                       std::function<bool(const Blackboard&)> guard,
                       int priority = 0) {
        transitions_.push_back({std::move(from), std::move(to),
                               std::move(guard), priority});
    }

    void setInitialState(const StateId& id) {
        currentState_ = id;
        if (auto* s = getState(currentState_)) {
            s->onEnter(blackboard_);
        }
    }

    void update(float dt) {
        elapsedInState_ += dt;

        // Check transitions sorted by priority
        std::vector<Transition*> validTransitions;
        for (auto& t : transitions_) {
            if (t.from == currentState_ && t.guard && t.guard(blackboard_)) {
                validTransitions.push_back(&t);
            }
        }

        if (!validTransitions.empty()) {
            std::sort(validTransitions.begin(), validTransitions.end(),
                [](const Transition* a, const Transition* b) {
                    return a->priority > b->priority;
                });

            auto& best = *validTransitions.front();
            transitionTo(best.to);
        }

        // Update current state
        if (auto* s = getState(currentState_)) {
            s->onUpdate(blackboard_, dt);
        }
    }

    void forceTransition(const StateId& to) {
        transitionTo(to);
    }

    const StateId& currentState() const { return currentState_; }
    float timeInCurrentState() const { return elapsedInState_; }
    const std::vector<StateTransitionRecord>& transitionHistory() const { return history_; }

    Blackboard& blackboard() { return blackboard_; }
    const Blackboard& blackboard() const { return blackboard_; }

private:
    State* getState(const StateId& id) {
        auto it = states_.find(id);
        return (it != states_.end()) ? it->second.get() : nullptr;
    }

    void transitionTo(const StateId& to) {
        if (auto* current = getState(currentState_)) {
            current->onExit(blackboard_);
        }

        // Record transition
        float gameTime = blackboard_.getOr<float>("_time", 0.0f);
        history_.push_back({currentState_, to, gameTime, elapsedInState_});
        if (history_.size() > maxHistory_) {
            history_.erase(history_.begin());
        }

        currentState_ = to;
        elapsedInState_ = 0.0f;

        if (auto* next = getState(currentState_)) {
            next->onEnter(blackboard_);
        }
    }

    std::unordered_map<StateId, std::unique_ptr<State>> states_;
    std::vector<Transition> transitions_;
    StateId currentState_;
    Blackboard blackboard_;
    float elapsedInState_ = 0.0f;
    std::vector<StateTransitionRecord> history_;
    static constexpr size_t maxHistory_ = 10;
};

} // namespace npc
