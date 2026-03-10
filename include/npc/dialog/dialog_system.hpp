#pragma once

#include "../core/types.hpp"
#include "../personality/personality_traits.hpp"
#include "../core/random.hpp"
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <functional>
#include <iostream>
#include <variant>

namespace npc {

// ─── Skill Check ──────────────────────────────────────────────────────────────
enum class DialogSkill {
    Persuade,   // Reason / appeal to logic or emotion
    Intimidate, // Threaten or project dominance
    Deceive     // Lie, misdirect, bluff
};

inline std::string dialogSkillToString(DialogSkill s) {
    switch (s) {
        case DialogSkill::Persuade:   return "Persuade";
        case DialogSkill::Intimidate: return "Intimidate";
        case DialogSkill::Deceive:    return "Deceive";
    }
    return "Unknown";
}

struct SkillCheckConfig {
    DialogSkill skill;
    float       difficulty      = 0.5f;   // 0 (trivial) → 1 (near-impossible)
    float       relationBonus   = true;   // whether relationship score applies
    float       successRelDelta = +8.0f;  // relationship change on success
    float       failRelDelta    = -12.0f; // relationship change on failure
    float       critFailRoll    = 0.10f;  // rolls below this = critical failure
    float       critSuccessRoll = 0.90f;  // rolls above this = critical success
};

struct SkillCheckResult {
    bool        success;
    bool        critSuccess   = false;
    bool        critFail      = false;
    float       roll;          // 0-1 final roll after modifiers
    float       threshold;     // what roll needed to beat
    float       relationDelta; // applied to relationship
    std::string narrative;     // human-readable outcome description
};

// ─── Story Flags ──────────────────────────────────────────────────────────────
// Persistent key-value store that tracks dialog-driven story state.
using StoryValue = std::variant<bool, int, float, std::string>;

class StoryFlags {
public:
    void set(const std::string& key, StoryValue value) { flags_[key] = std::move(value); }
    void setTrue(const std::string& key)  { flags_[key] = true; }
    void increment(const std::string& key, int by = 1) {
        int cur = getInt(key);
        flags_[key] = cur + by;
    }

    bool        getBool(const std::string& key, bool def = false) const {
        auto it = flags_.find(key);
        if (it == flags_.end()) return def;
        if (auto* v = std::get_if<bool>(&it->second)) return *v;
        return def;
    }
    int         getInt(const std::string& key, int def = 0) const {
        auto it = flags_.find(key);
        if (it == flags_.end()) return def;
        if (auto* v = std::get_if<int>(&it->second)) return *v;
        return def;
    }
    float       getFloat(const std::string& key, float def = 0.0f) const {
        auto it = flags_.find(key);
        if (it == flags_.end()) return def;
        if (auto* v = std::get_if<float>(&it->second)) return *v;
        return def;
    }
    std::string getString(const std::string& key, const std::string& def = "") const {
        auto it = flags_.find(key);
        if (it == flags_.end()) return def;
        if (auto* v = std::get_if<std::string>(&it->second)) return *v;
        return def;
    }
    bool has(const std::string& key) const { return flags_.count(key) > 0; }
    void clear(const std::string& key) { flags_.erase(key); }
    const std::map<std::string, StoryValue>& all() const { return flags_; }

private:
    std::map<std::string, StoryValue> flags_;
};

// ─── Dialog Option ────────────────────────────────────────────────────────────
struct DialogOption {
    std::string text;
    std::string nextNodeId;

    // Visibility / gate
    std::function<bool(float reputation, float mood)> condition;
    int minReputation = -100;

    // Optional skill check attached to this option
    std::optional<SkillCheckConfig> skillCheck;

    // Node to jump to on skill-check failure (if empty, uses nextNodeId)
    std::string failNodeId;

    // Side effects: called on selection (after any skill check resolves)
    std::function<void(bool skillSuccess, StoryFlags&)> effect;

    // Story flag to set when this option is chosen
    std::string setsFlag;
    StoryValue  flagValue = true;
};

// ─── Dialog Node ──────────────────────────────────────────────────────────────
struct DialogNode {
    std::string               id;
    std::string               speakerText;
    std::vector<DialogOption> options;
    bool                      isTerminal    = false;

    std::string friendlyText;  // reputation > 50
    std::string hostileText;   // reputation < -50

    // Story-flag-conditional text override
    std::string                      flagCondition;  // flag key
    std::string                      flagText;       // shown if flag is true

    std::string getText(float reputation, const StoryFlags& flags) const {
        if (!flagCondition.empty() && flags.getBool(flagCondition) && !flagText.empty())
            return flagText;
        if (reputation > 50.0f && !friendlyText.empty()) return friendlyText;
        if (reputation < -50.0f && !hostileText.empty()) return hostileText;
        return speakerText;
    }
    // Legacy overload
    std::string getText(float reputation) const {
        StoryFlags empty;
        return getText(reputation, empty);
    }
};

// ─── Skill Check Engine ───────────────────────────────────────────────────────
class SkillCheckEngine {
public:
    // Perform a skill check.
    // npcTraits: the TARGET NPC's personality (resists the check)
    // actorSkill: the INITIATOR's relevant skill value (0-1), e.g. player charisma
    // relationship: [-100, 100] between initiator and target
    static SkillCheckResult check(
            const SkillCheckConfig& cfg,
            const PersonalityTraits& npcTraits,
            float actorSkill,
            float relationship,
            RandomGenerator& rng)
    {
        // Base success probability
        float prob = 0.50f;

        // Actor's skill bonus: +/- 25 % at extremes
        prob += (actorSkill - 0.5f) * 0.50f;

        // Difficulty penalty
        prob -= (cfg.difficulty - 0.5f) * 0.40f;

        // Relationship bonus
        prob += (relationship / 100.0f) * 0.20f;

        // NPC resistance per skill type
        switch (cfg.skill) {
            case DialogSkill::Persuade:
                // Patient NPCs are harder to persuade (they think it through)
                prob -= (npcTraits.patience - 0.5f) * 0.30f;
                break;
            case DialogSkill::Intimidate:
                // Courageous NPCs don't scare easily
                prob -= (npcTraits.courage - 0.5f) * 0.40f;
                // Intimidation against friends is less effective
                if (relationship > 30.0f) prob -= 0.10f;
                break;
            case DialogSkill::Deceive:
                // Intelligent NPCs see through lies
                prob -= (npcTraits.intelligence - 0.5f) * 0.35f;
                // Hard to deceive close friends (they know you too well)
                if (relationship > 60.0f) prob -= 0.15f;
                break;
        }

        prob = std::clamp(prob, 0.05f, 0.95f);

        float roll = rng.range(0.0f, 1.0f);
        bool  success  = roll <= prob;
        bool  critSucc = roll >= cfg.critSuccessRoll;
        bool  critFail = roll <= cfg.critFailRoll;

        float relDelta = success
            ? cfg.successRelDelta  * (critSucc ? 1.5f : 1.0f)
            : cfg.failRelDelta     * (critFail ? 1.8f : 1.0f);

        // Intimidation that fails badly makes the NPC hostile
        if (!success && cfg.skill == DialogSkill::Intimidate && critFail)
            relDelta -= 10.0f;

        SkillCheckResult res;
        res.success       = success;
        res.critSuccess   = success && critSucc;
        res.critFail      = !success && critFail;
        res.roll          = roll;
        res.threshold     = prob;
        res.relationDelta = relDelta;
        res.narrative     = buildNarrative(cfg.skill, success, critSucc, critFail);
        return res;
    }

private:
    static std::string buildNarrative(DialogSkill skill, bool success,
                                       bool critSucc, bool critFail) {
        if (critSucc) {
            switch (skill) {
                case DialogSkill::Persuade:   return "They are completely convinced.";
                case DialogSkill::Intimidate: return "They back down, visibly shaken.";
                case DialogSkill::Deceive:    return "They believe every word without question.";
            }
        }
        if (critFail) {
            switch (skill) {
                case DialogSkill::Persuade:   return "Your argument backfires — they're now more opposed.";
                case DialogSkill::Intimidate: return "They laugh at your threat. Relations have soured badly.";
                case DialogSkill::Deceive:    return "They see right through you. Trust is shattered.";
            }
        }
        if (success) {
            switch (skill) {
                case DialogSkill::Persuade:   return "They nod, persuaded by your reasoning.";
                case DialogSkill::Intimidate: return "They yield, not wanting trouble.";
                case DialogSkill::Deceive:    return "They accept your story.";
            }
        }
        switch (skill) {
            case DialogSkill::Persuade:   return "They remain unconvinced.";
            case DialogSkill::Intimidate: return "They stand their ground.";
            case DialogSkill::Deceive:    return "They don't believe you.";
        }
        return "";
    }
};

// ─── Dialog Tree ──────────────────────────────────────────────────────────────
class DialogTree {
public:
    DialogTree() = default;
    explicit DialogTree(std::string id) : treeId_(std::move(id)) {}

    void addNode(DialogNode node) {
        if (nodes_.empty()) rootId_ = node.id;
        nodes_[node.id] = std::move(node);
    }
    void setRoot(const std::string& id) { rootId_ = id; }

    const DialogNode* getNode(const std::string& id) const {
        auto it = nodes_.find(id);
        return (it != nodes_.end()) ? &it->second : nullptr;
    }
    const std::string& rootId() const { return rootId_; }
    const std::string& treeId() const { return treeId_; }

private:
    std::string treeId_;
    std::string rootId_;
    std::map<std::string, DialogNode> nodes_;
};

// ─── Dialog System ────────────────────────────────────────────────────────────
class DialogSystem {
public:
    void addTree(const std::string& id, DialogTree tree) {
        trees_[id] = std::move(tree);
    }

    bool startDialog(const std::string& treeId, float reputation = 0.0f) {
        (void)reputation;
        auto it = trees_.find(treeId);
        if (it == trees_.end()) return false;
        activeTree_    = treeId;
        currentNodeId_ = it->second.rootId();
        lastCheck_     = std::nullopt;
        return true;
    }

    const DialogNode* currentNode() const {
        if (!activeTree_) return nullptr;
        auto it = trees_.find(*activeTree_);
        if (it == trees_.end()) return nullptr;
        return it->second.getNode(currentNodeId_);
    }

    std::vector<std::pair<int, const DialogOption*>> getAvailableOptions(
            float reputation, float mood) const {
        std::vector<std::pair<int, const DialogOption*>> result;
        auto* node = currentNode();
        if (!node) return result;
        for (int i = 0; i < static_cast<int>(node->options.size()); ++i) {
            const auto& opt = node->options[i];
            bool show = true;
            if (opt.condition) show = opt.condition(reputation, mood);
            if (reputation < opt.minReputation) show = false;
            if (show) result.push_back({i, &opt});
        }
        return result;
    }

    // Select an option with full skill-check resolution.
    // npcTraits: target NPC personality.  actorSkill: initiator's skill [0-1].
    // relationship: current relationship value [-100,100].
    // relationshipOut: receives the delta to apply to relationship system.
    bool selectOption(int index,
                      float reputation, float mood,
                      const PersonalityTraits& npcTraits,
                      float actorSkill,
                      float relationship,
                      RandomGenerator& rng,
                      StoryFlags& storyFlags,
                      float& relationshipDeltaOut)
    {
        auto* node = currentNode();
        if (!node || index < 0 || index >= static_cast<int>(node->options.size()))
            return false;

        const auto& opt = node->options[index];
        relationshipDeltaOut = 0.0f;
        bool skillSuccess = true;
        lastCheck_ = std::nullopt;

        // Resolve skill check if present
        if (opt.skillCheck.has_value()) {
            auto res = SkillCheckEngine::check(*opt.skillCheck, npcTraits,
                                               actorSkill, relationship, rng);
            relationshipDeltaOut = res.relationDelta;
            skillSuccess         = res.success;
            lastCheck_           = res;

            std::string nextId = skillSuccess
                ? opt.nextNodeId
                : (opt.failNodeId.empty() ? opt.nextNodeId : opt.failNodeId);

            // Apply effects
            if (opt.effect) opt.effect(skillSuccess, storyFlags);
            if (!opt.setsFlag.empty())
                storyFlags.set(opt.setsFlag, skillSuccess ? opt.flagValue : StoryValue{false});

            lastChoice_ = opt.text;

            if (nextId.empty() || nextId == "END") { endDialog(); return true; }
            currentNodeId_ = nextId;
            return true;
        }

        // No skill check — normal transition
        if (opt.effect) opt.effect(true, storyFlags);
        if (!opt.setsFlag.empty()) storyFlags.set(opt.setsFlag, opt.flagValue);
        lastChoice_ = opt.text;

        if (opt.nextNodeId.empty() || opt.nextNodeId == "END") { endDialog(); return true; }
        currentNodeId_ = opt.nextNodeId;
        return true;
    }

    // Legacy simple overload (no skill checks)
    bool selectOption(int index) {
        auto* node = currentNode();
        if (!node || index < 0 || index >= static_cast<int>(node->options.size()))
            return false;
        const auto& opt = node->options[index];
        if (opt.effect) {
            StoryFlags dummy; opt.effect(true, dummy);
        }
        lastChoice_ = opt.text;
        if (opt.nextNodeId.empty() || opt.nextNodeId == "END") { endDialog(); return true; }
        currentNodeId_ = opt.nextNodeId;
        return true;
    }

    void endDialog() {
        activeTree_ = std::nullopt;
        currentNodeId_.clear();
    }

    bool isInDialog() const { return activeTree_.has_value(); }
    const std::string& lastChoice() const { return lastChoice_; }
    const std::optional<SkillCheckResult>& lastCheckResult() const { return lastCheck_; }

    void printCurrent(const std::string& speakerName,
                      float reputation, float mood,
                      const StoryFlags& flags = StoryFlags{}) const {
        auto* node = currentNode();
        if (!node) return;
        std::cout << "  " << speakerName << ": \""
                  << node->getText(reputation, flags) << "\"\n";
        auto options = getAvailableOptions(reputation, mood);
        for (const auto& [idx, opt] : options) {
            std::cout << "    [" << idx + 1 << "] " << opt->text;
            if (opt->skillCheck)
                std::cout << "  [" << dialogSkillToString(opt->skillCheck->skill)
                          << " DC" << static_cast<int>(opt->skillCheck->difficulty * 10) << "]";
            std::cout << "\n";
        }
        if (lastCheck_) {
            std::cout << "    >> " << lastCheck_->narrative << "\n";
        }
    }

private:
    std::map<std::string, DialogTree> trees_;
    std::optional<std::string>        activeTree_;
    std::string                       currentNodeId_;
    std::string                       lastChoice_;
    std::optional<SkillCheckResult>   lastCheck_;
};

} // namespace npc
