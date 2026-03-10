#pragma once

#include <vector>
#include <memory>
#include <functional>
#include <string>
#include <algorithm>
#include <random>
#include <sstream>
#include <chrono>
#include <numeric>
#include "../core/types.hpp"
#include "blackboard.hpp"

namespace npc {

// ─── Node Debug Info ──────────────────────────────────────────────────

enum class BTNodeType {
    Action, Condition,
    Sequence, Selector, Parallel, RandomSelector,
    Inverter, Repeater, Cooldown, ConditionGuard,
    AlwaysSucceed, UntilFail,
    Service, Timeout, Retry,
    Unknown
};

inline const char* nodeTypeName(BTNodeType t) {
    switch (t) {
        case BTNodeType::Action:         return "Action";
        case BTNodeType::Condition:      return "Condition";
        case BTNodeType::Sequence:       return "Sequence";
        case BTNodeType::Selector:       return "Selector";
        case BTNodeType::Parallel:       return "Parallel";
        case BTNodeType::RandomSelector: return "RandomSelector";
        case BTNodeType::Inverter:       return "Inverter";
        case BTNodeType::Repeater:       return "Repeater";
        case BTNodeType::Cooldown:       return "Cooldown";
        case BTNodeType::ConditionGuard: return "ConditionGuard";
        case BTNodeType::AlwaysSucceed:  return "AlwaysSucceed";
        case BTNodeType::UntilFail:      return "UntilFail";
        case BTNodeType::Service:        return "Service";
        case BTNodeType::Timeout:        return "Timeout";
        case BTNodeType::Retry:          return "Retry";
        default:                         return "Unknown";
    }
}

inline const char* statusName(NodeStatus s) {
    switch (s) {
        case NodeStatus::Success: return "Success";
        case NodeStatus::Failure: return "Failure";
        case NodeStatus::Running: return "Running";
        default:                  return "Invalid";
    }
}

struct NodeDebugInfo {
    std::string  name;
    BTNodeType   type        = BTNodeType::Unknown;
    NodeStatus   lastStatus  = NodeStatus::Failure;
    uint64_t     tickCount   = 0;
    int          depth       = 0;
    std::vector<NodeDebugInfo> children;

    std::string toString(int indent = 0) const {
        std::string pad(indent * 2, ' ');
        std::ostringstream ss;
        ss << pad << "[" << nodeTypeName(type) << "] " << name
           << "  status=" << statusName(lastStatus)
           << "  ticks=" << tickCount << "\n";
        for (auto& c : children)
            ss << c.toString(indent + 1);
        return ss.str();
    }
};

// ─── Base Node ───────────────────────────────────────────────────────

class BTNode {
public:
    virtual ~BTNode() = default;

    // Public tick wrapper — records status + tick count for debug
    NodeStatus tick(Blackboard& bb) {
        ++tickCount_;
        lastStatus_ = doTick(bb);
        return lastStatus_;
    }

    virtual void reset() { lastStatus_ = NodeStatus::Failure; tickCount_ = 0; }
    virtual BTNodeType nodeType() const { return BTNodeType::Unknown; }
    virtual NodeDebugInfo debugInfo(int depth = 0) const {
        NodeDebugInfo info;
        info.name       = name_;
        info.type       = nodeType();
        info.lastStatus = lastStatus_;
        info.tickCount  = tickCount_;
        info.depth      = depth;
        return info;
    }

    void setName(const std::string& name) { name_ = name; }
    const std::string& name() const { return name_; }
    NodeStatus lastStatus() const { return lastStatus_; }
    uint64_t   tickCount()  const { return tickCount_; }

protected:
    virtual NodeStatus doTick(Blackboard& bb) = 0;

    std::string name_       = "unnamed";
    NodeStatus  lastStatus_ = NodeStatus::Failure;
    uint64_t    tickCount_  = 0;
};

using BTNodePtr = std::unique_ptr<BTNode>;

// ═══════════════════════════════════════════════════════════════════════
// LEAF NODES
// ═══════════════════════════════════════════════════════════════════════

class ActionNode : public BTNode {
public:
    using ActionFn = std::function<NodeStatus(Blackboard&)>;

    ActionNode(std::string name, ActionFn fn)
        : fn_(std::move(fn)) { name_ = std::move(name); }

    BTNodeType nodeType() const override { return BTNodeType::Action; }

protected:
    NodeStatus doTick(Blackboard& bb) override { return fn_(bb); }

private:
    ActionFn fn_;
};

class ConditionNode : public BTNode {
public:
    using ConditionFn = std::function<bool(const Blackboard&)>;

    ConditionNode(std::string name, ConditionFn fn)
        : fn_(std::move(fn)) { name_ = std::move(name); }

    BTNodeType nodeType() const override { return BTNodeType::Condition; }

protected:
    NodeStatus doTick(Blackboard& bb) override {
        return fn_(bb) ? NodeStatus::Success : NodeStatus::Failure;
    }

private:
    ConditionFn fn_;
};

// ═══════════════════════════════════════════════════════════════════════
// COMPOSITE NODES
// ═══════════════════════════════════════════════════════════════════════

class SequenceNode : public BTNode {
public:
    explicit SequenceNode(std::string name = "Sequence") { name_ = std::move(name); }

    void addChild(BTNodePtr child) { children_.push_back(std::move(child)); }
    BTNodeType nodeType() const override { return BTNodeType::Sequence; }

    NodeDebugInfo debugInfo(int depth = 0) const override {
        auto info = BTNode::debugInfo(depth);
        for (auto& c : children_) info.children.push_back(c->debugInfo(depth + 1));
        return info;
    }

    void reset() override {
        BTNode::reset();
        runningIdx_ = 0;
        for (auto& c : children_) c->reset();
    }

protected:
    NodeStatus doTick(Blackboard& bb) override {
        for (size_t i = runningIdx_; i < children_.size(); ++i) {
            auto status = children_[i]->tick(bb);
            if (status == NodeStatus::Running) { runningIdx_ = i; return NodeStatus::Running; }
            if (status == NodeStatus::Failure) { runningIdx_ = 0; return NodeStatus::Failure; }
        }
        runningIdx_ = 0;
        return NodeStatus::Success;
    }

private:
    std::vector<BTNodePtr> children_;
    size_t runningIdx_ = 0;
};

class SelectorNode : public BTNode {
public:
    explicit SelectorNode(std::string name = "Selector") { name_ = std::move(name); }

    void addChild(BTNodePtr child) { children_.push_back(std::move(child)); }
    BTNodeType nodeType() const override { return BTNodeType::Selector; }

    NodeDebugInfo debugInfo(int depth = 0) const override {
        auto info = BTNode::debugInfo(depth);
        for (auto& c : children_) info.children.push_back(c->debugInfo(depth + 1));
        return info;
    }

    void reset() override {
        BTNode::reset();
        runningIdx_ = 0;
        for (auto& c : children_) c->reset();
    }

protected:
    NodeStatus doTick(Blackboard& bb) override {
        for (size_t i = runningIdx_; i < children_.size(); ++i) {
            auto status = children_[i]->tick(bb);
            if (status == NodeStatus::Running) { runningIdx_ = i; return NodeStatus::Running; }
            if (status == NodeStatus::Success) { runningIdx_ = 0; return NodeStatus::Success; }
        }
        runningIdx_ = 0;
        return NodeStatus::Failure;
    }

private:
    std::vector<BTNodePtr> children_;
    size_t runningIdx_ = 0;
};

class ParallelNode : public BTNode {
public:
    ParallelNode(int successThreshold, std::string name = "Parallel")
        : successThreshold_(successThreshold) { name_ = std::move(name); }

    void addChild(BTNodePtr child) { children_.push_back(std::move(child)); }
    BTNodeType nodeType() const override { return BTNodeType::Parallel; }

    NodeDebugInfo debugInfo(int depth = 0) const override {
        auto info = BTNode::debugInfo(depth);
        for (auto& c : children_) info.children.push_back(c->debugInfo(depth + 1));
        return info;
    }

    void reset() override {
        BTNode::reset();
        for (auto& c : children_) c->reset();
    }

protected:
    NodeStatus doTick(Blackboard& bb) override {
        int successCount = 0, failureCount = 0;
        for (auto& child : children_) {
            auto status = child->tick(bb);
            if (status == NodeStatus::Success) ++successCount;
            else if (status == NodeStatus::Failure) ++failureCount;
        }
        if (successCount >= successThreshold_) return NodeStatus::Success;
        if (failureCount > static_cast<int>(children_.size()) - successThreshold_)
            return NodeStatus::Failure;
        return NodeStatus::Running;
    }

private:
    std::vector<BTNodePtr> children_;
    int successThreshold_;
};

// ─── RandomSelectorNode ───────────────────────────────────────────────
// Picks a child weighted-randomly each invocation; tries until one
// succeeds or all have failed this round.

class RandomSelectorNode : public BTNode {
public:
    struct WeightedChild {
        BTNodePtr node;
        float     weight = 1.0f;
    };

    explicit RandomSelectorNode(std::string name = "RandomSelector",
                                 unsigned seed = std::random_device{}())
        : rng_(seed) { name_ = std::move(name); }

    void addChild(BTNodePtr child, float weight = 1.0f) {
        children_.push_back({std::move(child), weight});
    }

    BTNodeType nodeType() const override { return BTNodeType::RandomSelector; }

    NodeDebugInfo debugInfo(int depth = 0) const override {
        auto info = BTNode::debugInfo(depth);
        for (auto& wc : children_) info.children.push_back(wc.node->debugInfo(depth + 1));
        return info;
    }

    void reset() override {
        BTNode::reset();
        order_.clear();
        orderIdx_ = 0;
        for (auto& wc : children_) wc.node->reset();
    }

protected:
    NodeStatus doTick(Blackboard& bb) override {
        // Build new random order at start of each attempt
        if (order_.empty()) {
            order_.resize(children_.size());
            std::iota(order_.begin(), order_.end(), 0u);
            // Weighted shuffle: pick proportionally to weight
            std::vector<float> weights;
            weights.reserve(children_.size());
            for (auto& wc : children_) weights.push_back(wc.weight);
            weightedShuffle(order_, weights);
            orderIdx_ = 0;
        }

        for (; orderIdx_ < order_.size(); ++orderIdx_) {
            auto& wc = children_[order_[orderIdx_]];
            auto status = wc.node->tick(bb);
            if (status == NodeStatus::Running) return NodeStatus::Running;
            if (status == NodeStatus::Success) {
                order_.clear(); orderIdx_ = 0;
                return NodeStatus::Success;
            }
        }
        order_.clear(); orderIdx_ = 0;
        return NodeStatus::Failure;
    }

private:
    void weightedShuffle(std::vector<size_t>& indices, std::vector<float>& weights) {
        // Fisher-Yates with weighted selection
        size_t n = indices.size();
        for (size_t i = 0; i < n - 1; ++i) {
            float total = 0.f;
            for (size_t j = i; j < n; ++j) total += weights[indices[j]];
            std::uniform_real_distribution<float> dist(0.f, total);
            float pick = dist(rng_);
            float acc  = 0.f;
            for (size_t j = i; j < n; ++j) {
                acc += weights[indices[j]];
                if (acc >= pick) { std::swap(indices[i], indices[j]); break; }
            }
        }
    }

    std::vector<WeightedChild> children_;
    std::mt19937               rng_;
    std::vector<size_t>        order_;
    size_t                     orderIdx_ = 0;
};

// ═══════════════════════════════════════════════════════════════════════
// DECORATOR NODES
// ═══════════════════════════════════════════════════════════════════════

class InverterNode : public BTNode {
public:
    explicit InverterNode(BTNodePtr child) : child_(std::move(child)) { name_ = "Inverter"; }
    BTNodeType nodeType() const override { return BTNodeType::Inverter; }
    NodeDebugInfo debugInfo(int depth = 0) const override {
        auto info = BTNode::debugInfo(depth);
        info.children.push_back(child_->debugInfo(depth + 1));
        return info;
    }
    void reset() override { BTNode::reset(); child_->reset(); }
protected:
    NodeStatus doTick(Blackboard& bb) override {
        auto s = child_->tick(bb);
        if (s == NodeStatus::Success) return NodeStatus::Failure;
        if (s == NodeStatus::Failure) return NodeStatus::Success;
        return NodeStatus::Running;
    }
private:
    BTNodePtr child_;
};

class RepeaterNode : public BTNode {
public:
    RepeaterNode(BTNodePtr child, int maxRepeats = -1)
        : child_(std::move(child)), maxRepeats_(maxRepeats) { name_ = "Repeater"; }
    BTNodeType nodeType() const override { return BTNodeType::Repeater; }
    NodeDebugInfo debugInfo(int depth = 0) const override {
        auto info = BTNode::debugInfo(depth);
        info.children.push_back(child_->debugInfo(depth + 1));
        return info;
    }
    void reset() override { BTNode::reset(); count_ = 0; child_->reset(); }
protected:
    NodeStatus doTick(Blackboard& bb) override {
        if (maxRepeats_ > 0 && count_ >= maxRepeats_) { count_ = 0; return NodeStatus::Success; }
        auto status = child_->tick(bb);
        if (status == NodeStatus::Running) return NodeStatus::Running;
        ++count_;
        if (maxRepeats_ < 0) return NodeStatus::Running;
        if (count_ >= maxRepeats_) { count_ = 0; return NodeStatus::Success; }
        return NodeStatus::Running;
    }
private:
    BTNodePtr child_;
    int maxRepeats_;
    int count_ = 0;
};

class CooldownNode : public BTNode {
public:
    CooldownNode(BTNodePtr child, float cooldownTime)
        : child_(std::move(child)), cooldownTime_(cooldownTime) { name_ = "Cooldown"; }
    BTNodeType nodeType() const override { return BTNodeType::Cooldown; }
    NodeDebugInfo debugInfo(int depth = 0) const override {
        auto info = BTNode::debugInfo(depth);
        info.children.push_back(child_->debugInfo(depth + 1));
        return info;
    }
    void reset() override { BTNode::reset(); readyTime_ = 0.0f; child_->reset(); }
protected:
    NodeStatus doTick(Blackboard& bb) override {
        auto now = bb.getOr<float>("_time", 0.0f);
        if (now < readyTime_) return NodeStatus::Failure;
        auto status = child_->tick(bb);
        if (status != NodeStatus::Running) readyTime_ = now + cooldownTime_;
        return status;
    }
private:
    BTNodePtr child_;
    float cooldownTime_;
    float readyTime_ = 0.0f;
};

class ConditionGuardNode : public BTNode {
public:
    using ConditionFn = std::function<bool(const Blackboard&)>;
    ConditionGuardNode(ConditionFn cond, BTNodePtr child)
        : cond_(std::move(cond)), child_(std::move(child)) { name_ = "ConditionGuard"; }
    BTNodeType nodeType() const override { return BTNodeType::ConditionGuard; }
    NodeDebugInfo debugInfo(int depth = 0) const override {
        auto info = BTNode::debugInfo(depth);
        info.children.push_back(child_->debugInfo(depth + 1));
        return info;
    }
    void reset() override { BTNode::reset(); child_->reset(); }
protected:
    NodeStatus doTick(Blackboard& bb) override {
        if (!cond_(bb)) return NodeStatus::Failure;
        return child_->tick(bb);
    }
private:
    ConditionFn cond_;
    BTNodePtr   child_;
};

class AlwaysSucceedNode : public BTNode {
public:
    explicit AlwaysSucceedNode(BTNodePtr child) : child_(std::move(child)) { name_ = "AlwaysSucceed"; }
    BTNodeType nodeType() const override { return BTNodeType::AlwaysSucceed; }
    NodeDebugInfo debugInfo(int depth = 0) const override {
        auto info = BTNode::debugInfo(depth);
        info.children.push_back(child_->debugInfo(depth + 1));
        return info;
    }
    void reset() override { BTNode::reset(); child_->reset(); }
protected:
    NodeStatus doTick(Blackboard& bb) override { child_->tick(bb); return NodeStatus::Success; }
private:
    BTNodePtr child_;
};

class UntilFailNode : public BTNode {
public:
    explicit UntilFailNode(BTNodePtr child) : child_(std::move(child)) { name_ = "UntilFail"; }
    BTNodeType nodeType() const override { return BTNodeType::UntilFail; }
    NodeDebugInfo debugInfo(int depth = 0) const override {
        auto info = BTNode::debugInfo(depth);
        info.children.push_back(child_->debugInfo(depth + 1));
        return info;
    }
    void reset() override { BTNode::reset(); child_->reset(); }
protected:
    NodeStatus doTick(Blackboard& bb) override {
        auto s = child_->tick(bb);
        if (s == NodeStatus::Failure) return NodeStatus::Success;
        return NodeStatus::Running;
    }
private:
    BTNodePtr child_;
};

// ─── ServiceNode ──────────────────────────────────────────────────────
// Runs a background callback every `intervalTicks` ticks regardless of
// child result; child's status is returned transparently.

class ServiceNode : public BTNode {
public:
    using ServiceFn = std::function<void(Blackboard&)>;

    ServiceNode(BTNodePtr child, ServiceFn service, int intervalTicks = 1,
                std::string name = "Service")
        : child_(std::move(child))
        , service_(std::move(service))
        , intervalTicks_(intervalTicks) {
        name_ = std::move(name);
    }

    BTNodeType nodeType() const override { return BTNodeType::Service; }

    NodeDebugInfo debugInfo(int depth = 0) const override {
        auto info = BTNode::debugInfo(depth);
        info.children.push_back(child_->debugInfo(depth + 1));
        return info;
    }

    void reset() override { BTNode::reset(); serviceTick_ = 0; child_->reset(); }

protected:
    NodeStatus doTick(Blackboard& bb) override {
        ++serviceTick_;
        if (serviceTick_ >= intervalTicks_) {
            service_(bb);
            serviceTick_ = 0;
        }
        return child_->tick(bb);
    }

private:
    BTNodePtr child_;
    ServiceFn service_;
    int       intervalTicks_;
    int       serviceTick_ = 0;
};

// ─── TimeoutDecorator ─────────────────────────────────────────────────
// Fails the child if it has been Running for more than `timeoutSecs`
// seconds (read from bb["_time"]).  Resets the child on timeout.

class TimeoutDecorator : public BTNode {
public:
    TimeoutDecorator(BTNodePtr child, float timeoutSecs,
                     std::string name = "Timeout")
        : child_(std::move(child)), timeoutSecs_(timeoutSecs) {
        name_ = std::move(name);
    }

    BTNodeType nodeType() const override { return BTNodeType::Timeout; }

    NodeDebugInfo debugInfo(int depth = 0) const override {
        auto info = BTNode::debugInfo(depth);
        info.children.push_back(child_->debugInfo(depth + 1));
        return info;
    }

    void reset() override {
        BTNode::reset();
        startTime_ = -1.0f;
        running_   = false;
        child_->reset();
    }

protected:
    NodeStatus doTick(Blackboard& bb) override {
        float now = bb.getOr<float>("_time", 0.0f);

        if (!running_) {
            startTime_ = now;
            running_   = true;
        }

        if (now - startTime_ >= timeoutSecs_) {
            child_->reset();
            running_ = false;
            return NodeStatus::Failure;
        }

        auto status = child_->tick(bb);
        if (status != NodeStatus::Running) {
            running_ = false;
        }
        return status;
    }

private:
    BTNodePtr child_;
    float     timeoutSecs_;
    float     startTime_ = -1.0f;
    bool      running_   = false;
};

// ─── RetryDecorator ───────────────────────────────────────────────────
// On child Failure, resets and retries up to `maxRetries` times.
// Returns Success as soon as child succeeds; Failure after all retries.

class RetryDecorator : public BTNode {
public:
    RetryDecorator(BTNodePtr child, int maxRetries,
                   std::string name = "Retry")
        : child_(std::move(child)), maxRetries_(maxRetries) {
        name_ = std::move(name);
    }

    BTNodeType nodeType() const override { return BTNodeType::Retry; }

    NodeDebugInfo debugInfo(int depth = 0) const override {
        auto info = BTNode::debugInfo(depth);
        info.children.push_back(child_->debugInfo(depth + 1));
        return info;
    }

    void reset() override {
        BTNode::reset();
        attempts_ = 0;
        child_->reset();
    }

protected:
    NodeStatus doTick(Blackboard& bb) override {
        auto status = child_->tick(bb);

        if (status == NodeStatus::Success) {
            attempts_ = 0;
            return NodeStatus::Success;
        }
        if (status == NodeStatus::Running) {
            return NodeStatus::Running;
        }

        // Failure
        ++attempts_;
        if (attempts_ >= maxRetries_) {
            attempts_ = 0;
            return NodeStatus::Failure;
        }
        child_->reset();
        return NodeStatus::Running; // still trying
    }

private:
    BTNodePtr child_;
    int       maxRetries_;
    int       attempts_ = 0;
};

// ═══════════════════════════════════════════════════════════════════════
// BEHAVIOR TREE
// ═══════════════════════════════════════════════════════════════════════

class BehaviorTree {
public:
    BehaviorTree() = default;
    explicit BehaviorTree(BTNodePtr root) : root_(std::move(root)) {}

    void setRoot(BTNodePtr root) { root_ = std::move(root); }

    NodeStatus tick(Blackboard& bb) {
        if (!root_) return NodeStatus::Failure;
        return root_->tick(bb);
    }

    void reset() {
        if (root_) root_->reset();
    }

    // ── Debug API ────────────────────────────────────────────────────

    // Returns the full tree structure with per-node last status and tick counts.
    NodeDebugInfo debugSnapshot() const {
        if (!root_) return {};
        return root_->debugInfo(0);
    }

    // Returns a human-readable multi-line string of the tree.
    std::string debugString() const {
        return debugSnapshot().toString();
    }

    // Walk every node and call visitor(info, depth).
    void walkDebug(const std::function<void(const NodeDebugInfo&)>& visitor) const {
        walkNode(debugSnapshot(), visitor);
    }

private:
    static void walkNode(const NodeDebugInfo& info,
                         const std::function<void(const NodeDebugInfo&)>& visitor) {
        visitor(info);
        for (auto& child : info.children) walkNode(child, visitor);
    }

    BTNodePtr root_;
};

// ═══════════════════════════════════════════════════════════════════════
// BUILDER (Fluent API)
// ═══════════════════════════════════════════════════════════════════════

class BehaviorTreeBuilder {
public:
    // ── Composites ──────────────────────────────────────────────────

    BehaviorTreeBuilder& selector(const std::string& name = "Selector") {
        pushComposite(std::make_unique<SelectorNode>(name));
        return *this;
    }

    BehaviorTreeBuilder& sequence(const std::string& name = "Sequence") {
        pushComposite(std::make_unique<SequenceNode>(name));
        return *this;
    }

    BehaviorTreeBuilder& parallel(int successThreshold,
                                   const std::string& name = "Parallel") {
        pushComposite(std::make_unique<ParallelNode>(successThreshold, name));
        return *this;
    }

    // Weighted random selector — call addRandomChild() before end()
    BehaviorTreeBuilder& randomSelector(const std::string& name = "RandomSelector",
                                         unsigned seed = std::random_device{}()) {
        pushComposite(std::make_unique<RandomSelectorNode>(name, seed));
        return *this;
    }

    // ── Leaves ──────────────────────────────────────────────────────

    BehaviorTreeBuilder& action(const std::string& name,
                                 ActionNode::ActionFn fn) {
        addLeaf(std::make_unique<ActionNode>(name, std::move(fn)));
        return *this;
    }

    BehaviorTreeBuilder& condition(const std::string& name,
                                    ConditionNode::ConditionFn fn) {
        addLeaf(std::make_unique<ConditionNode>(name, std::move(fn)));
        return *this;
    }

    // ── Simple Decorators (wrap next leaf/sub-tree) ──────────────────

    BehaviorTreeBuilder& inverter() {
        pendingDecorators_.push_back(DecoratorType::Inverter);
        return *this;
    }

    BehaviorTreeBuilder& alwaysSucceed() {
        pendingDecorators_.push_back(DecoratorType::AlwaysSucceed);
        return *this;
    }

    BehaviorTreeBuilder& untilFail() {
        pendingDecorators_.push_back(DecoratorType::UntilFail);
        return *this;
    }

    // ── Parameterised Decorators (wrap the last closed sub-tree) ────

    // Must be called after end() to wrap the sub-tree just closed.
    BehaviorTreeBuilder& withService(ServiceNode::ServiceFn fn,
                                      int intervalTicks = 1,
                                      const std::string& name = "Service") {
        if (!pendingWrap_) return *this;
        pendingWrap_ = std::make_unique<ServiceNode>(
            std::move(pendingWrap_), std::move(fn), intervalTicks, name);
        return *this;
    }

    BehaviorTreeBuilder& withTimeout(float seconds,
                                      const std::string& name = "Timeout") {
        if (!pendingWrap_) return *this;
        pendingWrap_ = std::make_unique<TimeoutDecorator>(
            std::move(pendingWrap_), seconds, name);
        return *this;
    }

    BehaviorTreeBuilder& withRetry(int maxRetries,
                                    const std::string& name = "Retry") {
        if (!pendingWrap_) return *this;
        pendingWrap_ = std::make_unique<RetryDecorator>(
            std::move(pendingWrap_), maxRetries, name);
        return *this;
    }

    // Flush pendingWrap_ into the current composite
    BehaviorTreeBuilder& attachWrap() {
        if (pendingWrap_) {
            addToCurrentComposite(std::move(pendingWrap_));
        }
        return *this;
    }

    // ── Standalone service / timeout / retry around a single action ─

    BehaviorTreeBuilder& serviceAction(const std::string& name,
                                        ActionNode::ActionFn childFn,
                                        ServiceNode::ServiceFn serviceFn,
                                        int intervalTicks = 1) {
        auto inner = std::make_unique<ActionNode>(name, std::move(childFn));
        addLeaf(std::make_unique<ServiceNode>(std::move(inner),
                                              std::move(serviceFn),
                                              intervalTicks,
                                              "Service<" + name + ">"));
        return *this;
    }

    BehaviorTreeBuilder& timeoutAction(const std::string& name,
                                        ActionNode::ActionFn fn,
                                        float seconds) {
        auto inner = std::make_unique<ActionNode>(name, std::move(fn));
        addLeaf(std::make_unique<TimeoutDecorator>(std::move(inner), seconds,
                                                   "Timeout<" + name + ">"));
        return *this;
    }

    BehaviorTreeBuilder& retryAction(const std::string& name,
                                      ActionNode::ActionFn fn,
                                      int maxRetries) {
        auto inner = std::make_unique<ActionNode>(name, std::move(fn));
        addLeaf(std::make_unique<RetryDecorator>(std::move(inner), maxRetries,
                                                 "Retry<" + name + ">"));
        return *this;
    }

    // ── Weighted child helper (for use inside randomSelector()) ─────

    BehaviorTreeBuilder& weightedAction(const std::string& name,
                                         ActionNode::ActionFn fn,
                                         float weight) {
        auto leaf = std::make_unique<ActionNode>(name, std::move(fn));
        auto* raw = compositeStack_.empty() ? nullptr : compositeStack_.back().get();
        if (auto* rs = dynamic_cast<RandomSelectorNode*>(raw)) {
            rs->addChild(std::move(leaf), weight);
        } else {
            addToCurrentComposite(std::move(leaf));
        }
        return *this;
    }

    // ── Tree structure ───────────────────────────────────────────────

    BehaviorTreeBuilder& end() {
        if (compositeStack_.size() > 1) {
            pendingWrap_ = std::move(compositeStack_.back());
            compositeStack_.pop_back();
            // Apply simple decorators first
            applyPendingDecorators(pendingWrap_);
            addToCurrentComposite(std::move(pendingWrap_));
        }
        return *this;
    }

    BehaviorTree build() {
        while (compositeStack_.size() > 1) end();
        BehaviorTree tree;
        if (!compositeStack_.empty()) {
            auto root = std::move(compositeStack_.front());
            applyPendingDecorators(root);
            tree.setRoot(std::move(root));
        }
        compositeStack_.clear();
        pendingWrap_.reset();
        return tree;
    }

private:
    enum class DecoratorType { Inverter, AlwaysSucceed, UntilFail };

    void pushComposite(BTNodePtr node) {
        compositeStack_.push_back(std::move(node));
    }

    void applyPendingDecorators(BTNodePtr& node) {
        while (!pendingDecorators_.empty()) {
            auto dec = pendingDecorators_.back();
            pendingDecorators_.pop_back();
            switch (dec) {
                case DecoratorType::Inverter:
                    node = std::make_unique<InverterNode>(std::move(node)); break;
                case DecoratorType::AlwaysSucceed:
                    node = std::make_unique<AlwaysSucceedNode>(std::move(node)); break;
                case DecoratorType::UntilFail:
                    node = std::make_unique<UntilFailNode>(std::move(node)); break;
            }
        }
    }

    void addLeaf(BTNodePtr leaf) {
        applyPendingDecorators(leaf);
        addToCurrentComposite(std::move(leaf));
    }

    void addToCurrentComposite(BTNodePtr child) {
        if (compositeStack_.empty()) {
            compositeStack_.push_back(std::move(child));
            return;
        }
        auto* raw = compositeStack_.back().get();
        if (auto* seq = dynamic_cast<SequenceNode*>(raw))
            seq->addChild(std::move(child));
        else if (auto* sel = dynamic_cast<SelectorNode*>(raw))
            sel->addChild(std::move(child));
        else if (auto* par = dynamic_cast<ParallelNode*>(raw))
            par->addChild(std::move(child));
        else if (auto* rs = dynamic_cast<RandomSelectorNode*>(raw))
            rs->addChild(std::move(child));
    }

    std::vector<BTNodePtr> compositeStack_;
    std::vector<DecoratorType> pendingDecorators_;
    BTNodePtr pendingWrap_;
};

} // namespace npc
