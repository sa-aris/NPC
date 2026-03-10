#pragma once

#include <any>
#include <string>
#include <unordered_map>
#include <optional>
#include <functional>
#include <vector>

namespace npc {

class Blackboard {
public:
    // ── Typed set / get ──────────────────────────────────────────────

    template<typename T>
    void set(const std::string& key, const T& value) {
        data_[key] = value;
    }

    template<typename T>
    std::optional<T> get(const std::string& key) const {
        auto it = data_.find(key);
        if (it == data_.end()) return std::nullopt;
        try { return std::any_cast<T>(it->second); }
        catch (const std::bad_any_cast&) { return std::nullopt; }
    }

    template<typename T>
    T getOr(const std::string& key, const T& defaultValue) const {
        auto val = get<T>(key);
        return val.value_or(defaultValue);
    }

    // ── Raw any access (for type-agnostic copies) ────────────────────

    void setAny(const std::string& key, std::any value) {
        data_[key] = std::move(value);
    }

    const std::any* getAny(const std::string& key) const {
        auto it = data_.find(key);
        return it != data_.end() ? &it->second : nullptr;
    }

    // ── Existence / removal ──────────────────────────────────────────

    bool has(const std::string& key) const {
        return data_.find(key) != data_.end();
    }

    void remove(const std::string& key) { data_.erase(key); }

    void clear() { data_.clear(); }

    size_t size() const { return data_.size(); }

    // ── Iteration ────────────────────────────────────────────────────

    void forEach(std::function<void(const std::string&, const std::any&)> fn) const {
        for (auto& [k, v] : data_) fn(k, v);
    }

    std::vector<std::string> keys() const {
        std::vector<std::string> out;
        out.reserve(data_.size());
        for (auto& [k, v] : data_) out.push_back(k);
        return out;
    }

    // ── Snapshot / restore ───────────────────────────────────────────

    using Snapshot = std::unordered_map<std::string, std::any>;

    Snapshot snapshot() const { return data_; }

    void restore(Snapshot snap) { data_ = std::move(snap); }

    // Merge another snapshot into this BB (overwrites on conflict)
    void merge(const Snapshot& snap) {
        for (auto& [k, v] : snap) data_[k] = v;
    }

private:
    std::unordered_map<std::string, std::any> data_;
};

} // namespace npc
