#pragma once
// Spatial index for fast NPC proximity queries.
// Two implementations under one roof:
//   SpatialGrid  — uniform cell grid, O(1) amortised insert/update, best for
//                  worlds where entities are spread roughly uniformly.
//   QuadTree     — adaptive subdivision, best for sparse / non-uniform worlds.
//   SpatialIndex — façade that wraps SpatialGrid with NPC-friendly ergonomics.
//
// Typical usage:
//   SpatialIndex index(16.f);   // 16-unit cells
//   index.update(npc.id, npc.position);           // call every tick
//   auto nearby = index.nearby(pos, 20.f);        // all within 20 units
//   auto [dist, id] = index.closest(pos, 50.f);   // nearest within 50

#include "../core/types.hpp"
#include "../core/vec2.hpp"

#include <unordered_map>
#include <vector>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <memory>
#include <cassert>

namespace npc {

// ─── Axis-Aligned Bounding Box ────────────────────────────────────────

struct AABB {
    Vec2 min{0, 0};
    Vec2 max{0, 0};

    static AABB fromCircle(Vec2 c, float r) {
        return { {c.x - r, c.y - r}, {c.x + r, c.y + r} };
    }
    static AABB fromPoints(Vec2 a, Vec2 b) {
        return { {std::min(a.x,b.x), std::min(a.y,b.y)},
                 {std::max(a.x,b.x), std::max(a.y,b.y)} };
    }

    bool contains(Vec2 p) const {
        return p.x >= min.x && p.x <= max.x &&
               p.y >= min.y && p.y <= max.y;
    }
    bool intersects(const AABB& o) const {
        return min.x <= o.max.x && max.x >= o.min.x &&
               min.y <= o.max.y && max.y >= o.min.y;
    }
    bool intersectsCircle(Vec2 c, float r) const {
        float cx = std::clamp(c.x, min.x, max.x);
        float cy = std::clamp(c.y, min.y, max.y);
        float dx = c.x - cx, dy = c.y - cy;
        return dx*dx + dy*dy <= r*r;
    }
    Vec2  center() const { return { (min.x+max.x)*0.5f, (min.y+max.y)*0.5f }; }
    Vec2  size()   const { return max - min; }
    float area()   const { auto s=size(); return s.x*s.y; }
};

// ─── Query result entry ───────────────────────────────────────────────

struct SpatialHit {
    EntityId id;
    Vec2     pos;
    float    distSq;   // squared distance to query origin
    float    dist() const { return std::sqrt(distSq); }
    bool operator<(const SpatialHit& o) const { return distSq < o.distSq; }
};

// ═══════════════════════════════════════════════════════════════════════
// SpatialGrid
// Uniform-cell hash grid.  All operations are O(1) amortised except
// range queries which are O(k) in the number of cells visited.
// ═══════════════════════════════════════════════════════════════════════

class SpatialGrid {
public:
    explicit SpatialGrid(float cellSize = 16.f)
        : cellSize_(cellSize), invCell_(1.f / cellSize) {
        assert(cellSize > 0.f);
    }

    // ── Mutation ─────────────────────────────────────────────────────

    void insert(EntityId id, Vec2 pos) {
        auto key = posToKey(pos);
        cells_[key].push_back(id);
        positions_[id] = pos;
    }

    void remove(EntityId id) {
        auto it = positions_.find(id);
        if (it == positions_.end()) return;
        removeFromCell(id, posToKey(it->second));
        positions_.erase(it);
    }

    // Update position (no old pos needed — stored internally)
    void update(EntityId id, Vec2 newPos) {
        auto it = positions_.find(id);
        if (it == positions_.end()) {
            insert(id, newPos); return;
        }
        CellKey oldKey = posToKey(it->second);
        CellKey newKey = posToKey(newPos);
        if (oldKey != newKey) {
            removeFromCell(id, oldKey);
            cells_[newKey].push_back(id);
        }
        it->second = newPos;
    }

    void clear() { cells_.clear(); positions_.clear(); }

    // ── Queries ──────────────────────────────────────────────────────

    std::vector<SpatialHit> queryRadius(Vec2 center, float radius) const {
        std::vector<SpatialHit> out;
        const float r2 = radius * radius;
        forCellsInAABB(AABB::fromCircle(center, radius), [&](EntityId id) {
            auto& pos = positions_.at(id);
            float d2 = pos.distanceSquaredTo(center);
            if (d2 <= r2) out.push_back({id, pos, d2});
        });
        return out;
    }

    std::vector<SpatialHit> queryRect(AABB box) const {
        std::vector<SpatialHit> out;
        Vec2 c = box.center();
        forCellsInAABB(box, [&](EntityId id) {
            auto& pos = positions_.at(id);
            if (box.contains(pos))
                out.push_back({id, pos, pos.distanceSquaredTo(c)});
        });
        return out;
    }

    // N nearest entities within maxDist (sorted closest-first)
    std::vector<SpatialHit> nearest(Vec2 pos, size_t n,
                                     float maxDist = std::numeric_limits<float>::max()) const {
        std::vector<SpatialHit> hits = queryRadius(pos, maxDist);
        if (hits.size() > n) {
            std::partial_sort(hits.begin(), hits.begin() + n, hits.end());
            hits.resize(n);
        } else {
            std::sort(hits.begin(), hits.end());
        }
        return hits;
    }

    // Absolute closest (returns nullopt if none within maxDist)
    std::optional<SpatialHit> closest(Vec2 pos,
                                       float maxDist = std::numeric_limits<float>::max()) const {
        auto hits = nearest(pos, 1, maxDist);
        if (hits.empty()) return std::nullopt;
        return hits.front();
    }

    size_t countRadius(Vec2 center, float radius) const {
        size_t count = 0;
        const float r2 = radius * radius;
        forCellsInAABB(AABB::fromCircle(center, radius), [&](EntityId id) {
            if (positions_.at(id).distanceSquaredTo(center) <= r2) ++count;
        });
        return count;
    }

    bool anyInRadius(Vec2 center, float radius) const {
        const float r2 = radius * radius;
        bool found = false;
        forCellsInAABB(AABB::fromCircle(center, radius), [&](EntityId id) {
            if (!found && positions_.at(id).distanceSquaredTo(center) <= r2)
                found = true;
        });
        return found;
    }

    // ── Diagnostics ──────────────────────────────────────────────────

    size_t entityCount() const { return positions_.size(); }
    size_t cellCount()   const { return cells_.size(); }
    const std::unordered_map<EntityId, Vec2>& positions() const { return positions_; }
    float  cellSize()    const { return cellSize_; }

    // Average entities-per-occupied-cell (load factor indicator)
    float loadFactor() const {
        if (cells_.empty()) return 0.f;
        return static_cast<float>(positions_.size()) /
               static_cast<float>(cells_.size());
    }

    // Rebuild to reduce hash-map fragmentation (call occasionally)
    void rehash() {
        cells_.rehash(cells_.size() * 2);
        positions_.rehash(positions_.size() * 2);
    }

private:
    using CellKey = int64_t;

    CellKey posToKey(Vec2 p) const {
        int32_t cx = static_cast<int32_t>(std::floor(p.x * invCell_));
        int32_t cy = static_cast<int32_t>(std::floor(p.y * invCell_));
        return cellKey(cx, cy);
    }
    static CellKey cellKey(int32_t cx, int32_t cy) {
        return (static_cast<int64_t>(cx) << 32) |
               static_cast<uint64_t>(static_cast<uint32_t>(cy));
    }

    void removeFromCell(EntityId id, CellKey key) {
        auto it = cells_.find(key);
        if (it == cells_.end()) return;
        auto& vec = it->second;
        vec.erase(std::remove(vec.begin(), vec.end(), id), vec.end());
        if (vec.empty()) cells_.erase(it);
    }

    template<typename Fn>
    void forCellsInAABB(const AABB& box, Fn&& fn) const {
        int32_t x0 = static_cast<int32_t>(std::floor(box.min.x * invCell_));
        int32_t y0 = static_cast<int32_t>(std::floor(box.min.y * invCell_));
        int32_t x1 = static_cast<int32_t>(std::floor(box.max.x * invCell_));
        int32_t y1 = static_cast<int32_t>(std::floor(box.max.y * invCell_));
        for (int32_t cx = x0; cx <= x1; ++cx) {
            for (int32_t cy = y0; cy <= y1; ++cy) {
                auto it = cells_.find(cellKey(cx, cy));
                if (it == cells_.end()) continue;
                for (EntityId id : it->second) {
                    if (positions_.count(id)) fn(id);
                }
            }
        }
    }

    float    cellSize_;
    float    invCell_;
    std::unordered_map<CellKey,  std::vector<EntityId>> cells_;
    std::unordered_map<EntityId, Vec2>                  positions_;
};

// ═══════════════════════════════════════════════════════════════════════
// QuadTree
// Adaptive subdivision tree for sparse / non-uniform NPC distributions.
// Supports insert, remove, and range/radius queries.
// ═══════════════════════════════════════════════════════════════════════

class QuadTree {
public:
    static constexpr int MAX_DEPTH    = 8;
    static constexpr int NODE_CAP     = 8;   // entries before splitting

    explicit QuadTree(AABB bounds)
        : root_(std::make_unique<Node>(bounds)) {}

    QuadTree(Vec2 worldMin, Vec2 worldMax)
        : QuadTree(AABB{worldMin, worldMax}) {}

    // ── Mutation ─────────────────────────────────────────────────────

    void insert(EntityId id, Vec2 pos) {
        positions_[id] = pos;
        root_->insert({id, pos}, 0);
    }

    void remove(EntityId id) {
        auto it = positions_.find(id);
        if (it == positions_.end()) return;
        root_->remove(id, it->second);
        positions_.erase(it);
    }

    void update(EntityId id, Vec2 newPos) {
        remove(id);
        insert(id, newPos);
    }

    void clear() {
        AABB bounds = root_->bounds;
        root_ = std::make_unique<Node>(bounds);
        positions_.clear();
    }

    // Rebuild tree (defragments after many removes)
    void rebuild() {
        auto snap = positions_;
        clear();
        for (auto& [id, pos] : snap) insert(id, pos);
    }

    // ── Queries ──────────────────────────────────────────────────────

    std::vector<SpatialHit> queryRadius(Vec2 center, float radius) const {
        std::vector<SpatialHit> out;
        const float r2 = radius * radius;
        root_->query(AABB::fromCircle(center, radius), [&](EntityId id, Vec2 pos) {
            float d2 = pos.distanceSquaredTo(center);
            if (d2 <= r2) out.push_back({id, pos, d2});
        });
        return out;
    }

    std::vector<SpatialHit> queryRect(AABB box) const {
        std::vector<SpatialHit> out;
        Vec2 c = box.center();
        root_->query(box, [&](EntityId id, Vec2 pos) {
            if (box.contains(pos))
                out.push_back({id, pos, pos.distanceSquaredTo(c)});
        });
        return out;
    }

    std::vector<SpatialHit> nearest(Vec2 pos, size_t n,
                                     float maxDist = std::numeric_limits<float>::max()) const {
        std::vector<SpatialHit> hits = queryRadius(pos, maxDist);
        if (hits.size() > n) {
            std::partial_sort(hits.begin(), hits.begin() + n, hits.end());
            hits.resize(n);
        } else {
            std::sort(hits.begin(), hits.end());
        }
        return hits;
    }

    std::optional<SpatialHit> closest(Vec2 pos,
                                       float maxDist = std::numeric_limits<float>::max()) const {
        auto hits = nearest(pos, 1, maxDist);
        return hits.empty() ? std::nullopt : std::optional{hits.front()};
    }

    size_t entityCount() const { return positions_.size(); }
    AABB   bounds()      const { return root_->bounds; }

    // Diagnostics: total node count
    size_t nodeCount() const { return root_->nodeCount(); }

private:
    struct Entry { EntityId id; Vec2 pos; };

    struct Node {
        AABB              bounds;
        std::vector<Entry> entries;
        std::array<std::unique_ptr<Node>, 4> children;
        bool divided = false;

        explicit Node(AABB b) : bounds(b) {}

        void insert(const Entry& e, int depth) {
            if (!bounds.contains(e.pos)) return;
            if (!divided && (static_cast<int>(entries.size()) < NODE_CAP || depth >= MAX_DEPTH)) {
                entries.push_back(e);
                return;
            }
            if (!divided) subdivide();
            for (auto& ch : children)
                if (ch && ch->bounds.contains(e.pos)) { ch->insert(e, depth+1); return; }
            entries.push_back(e); // boundary case: keep at this node
        }

        bool remove(EntityId id, Vec2 pos) {
            // Try this node's entries
            auto it = std::find_if(entries.begin(), entries.end(),
                [id](const Entry& e){ return e.id == id; });
            if (it != entries.end()) { entries.erase(it); return true; }
            if (!divided) return false;
            for (auto& ch : children)
                if (ch && ch->bounds.contains(pos) && ch->remove(id, pos)) return true;
            return false;
        }

        template<typename Fn>
        void query(const AABB& box, Fn&& fn) const {
            if (!bounds.intersects(box)) return;
            for (auto& e : entries)
                if (box.contains(e.pos)) fn(e.id, e.pos);
            if (divided)
                for (auto& ch : children) if (ch) ch->query(box, fn);
        }

        void subdivide() {
            Vec2 c = bounds.center();
            Vec2 mn = bounds.min, mx = bounds.max;
            children[0] = std::make_unique<Node>(AABB{mn,      c});
            children[1] = std::make_unique<Node>(AABB{{c.x,mn.y},{mx.x,c.y}});
            children[2] = std::make_unique<Node>(AABB{{mn.x,c.y},{c.x,mx.y}});
            children[3] = std::make_unique<Node>(AABB{c,       mx});
            divided = true;
            // Re-insert existing entries into children
            auto old = std::move(entries); entries.clear();
            for (auto& e : old) {
                bool placed = false;
                for (auto& ch : children)
                    if (ch->bounds.contains(e.pos)) { ch->entries.push_back(e); placed=true; break; }
                if (!placed) entries.push_back(e);
            }
        }

        size_t nodeCount() const {
            size_t n = 1;
            if (divided) for (auto& ch : children) if (ch) n += ch->nodeCount();
            return n;
        }
    };

    std::unique_ptr<Node>            root_;
    std::unordered_map<EntityId,Vec2> positions_;
};

// ═══════════════════════════════════════════════════════════════════════
// SpatialIndex — NPC-friendly façade over SpatialGrid
// ═══════════════════════════════════════════════════════════════════════

class SpatialIndex {
public:
    // cellSize: tune to ~2× typical NPC interaction radius for best performance
    explicit SpatialIndex(float cellSize = 16.f) : grid_(cellSize) {}

    // ── Position management ──────────────────────────────────────────

    // Register or move an entity. Call every tick for moving NPCs.
    void update(EntityId id, Vec2 newPos)   { grid_.update(id, newPos); }
    void remove(EntityId id)                { grid_.remove(id); }
    void clear()                            { grid_.clear(); }

    // ── Proximity queries ────────────────────────────────────────────

    // All entities within radius (unsorted)
    std::vector<EntityId> nearby(Vec2 center, float radius) const {
        auto hits = grid_.queryRadius(center, radius);
        std::vector<EntityId> out;
        out.reserve(hits.size());
        for (auto& h : hits) out.push_back(h.id);
        return out;
    }

    // All within radius except one entity (common: self-exclusion)
    std::vector<EntityId> nearbyExcept(Vec2 center, float radius,
                                        EntityId exclude) const {
        auto hits = grid_.queryRadius(center, radius);
        std::vector<EntityId> out;
        out.reserve(hits.size());
        for (auto& h : hits) if (h.id != exclude) out.push_back(h.id);
        return out;
    }

    // All within radius except a set (e.g., allies)
    std::vector<EntityId> nearbyExcept(Vec2 center, float radius,
                                        const std::vector<EntityId>& exclude) const {
        auto hits = grid_.queryRadius(center, radius);
        std::vector<EntityId> out;
        out.reserve(hits.size());
        for (auto& h : hits) {
            if (std::find(exclude.begin(), exclude.end(), h.id) == exclude.end())
                out.push_back(h.id);
        }
        return out;
    }

    // All within radius with distances (sorted nearest-first)
    std::vector<SpatialHit> nearbyWithDist(Vec2 center, float radius) const {
        auto hits = grid_.queryRadius(center, radius);
        std::sort(hits.begin(), hits.end());
        return hits;
    }

    // N nearest entities (sorted)
    std::vector<SpatialHit> nearestN(Vec2 pos, size_t n,
                                      float maxDist = std::numeric_limits<float>::max()) const {
        return grid_.nearest(pos, n, maxDist);
    }

    // Single closest entity
    std::optional<SpatialHit> closest(Vec2 pos,
                                       float maxDist = std::numeric_limits<float>::max()) const {
        return grid_.closest(pos, maxDist);
    }

    // Closest entity excluding self
    std::optional<SpatialHit> closestExcept(Vec2 pos, EntityId exclude,
                                              float maxDist = std::numeric_limits<float>::max()) const {
        auto hits = grid_.nearest(pos, 10, maxDist); // get a small batch
        for (auto& h : hits) if (h.id != exclude) return h;
        // If not found in first batch, do full radius search
        auto all = grid_.queryRadius(pos, maxDist);
        std::sort(all.begin(), all.end());
        for (auto& h : all) if (h.id != exclude) return h;
        return std::nullopt;
    }

    // Rect query
    std::vector<SpatialHit> inRect(AABB box) const {
        return grid_.queryRect(box);
    }

    // ── Counting (avoids building result vectors) ─────────────────────

    size_t countNearby(Vec2 center, float radius) const {
        return grid_.countRadius(center, radius);
    }

    bool anyNearby(Vec2 center, float radius) const {
        return grid_.anyInRadius(center, radius);
    }

    // ── Cluster detection (for CIB detector etc.) ────────────────────
    // Returns groups of entities that are within clusterRadius of each other.
    // Uses a simple greedy sweep — O(n * queryRadius) per call.

    struct Cluster {
        std::vector<EntityId> members;
        Vec2                  centroid{0, 0};
    };

    std::vector<Cluster> findClusters(float clusterRadius) const {
        // Build a list of all known positions directly from grid (avoid infinite queryRect)
        std::vector<SpatialHit> all;
        all.reserve(grid_.positions().size());
        for (auto& [id, p] : grid_.positions())
            all.push_back({id, p, 0.f});

        std::vector<bool> visited(all.size(), false);
        std::vector<Cluster> clusters;

        for (size_t i = 0; i < all.size(); ++i) {
            if (visited[i]) continue;
            Cluster c;
            c.members.push_back(all[i].id);
            visited[i] = true;
            float sumX = all[i].pos.x, sumY = all[i].pos.y;

            // BFS: find all neighbours within clusterRadius
            std::vector<size_t> frontier = {i};
            while (!frontier.empty()) {
                size_t cur = frontier.back(); frontier.pop_back();
                auto neighbours = grid_.queryRadius(all[cur].pos, clusterRadius);
                for (auto& nb : neighbours) {
                    // Find its index
                    for (size_t j = 0; j < all.size(); ++j) {
                        if (!visited[j] && all[j].id == nb.id) {
                            visited[j] = true;
                            c.members.push_back(all[j].id);
                            sumX += all[j].pos.x;
                            sumY += all[j].pos.y;
                            frontier.push_back(j);
                        }
                    }
                }
            }
            float n = static_cast<float>(c.members.size());
            c.centroid = { sumX / n, sumY / n };
            clusters.push_back(std::move(c));
        }
        return clusters;
    }

    // ── Diagnostics ──────────────────────────────────────────────────

    size_t size()       const { return grid_.entityCount(); }
    size_t cellCount()  const { return grid_.cellCount(); }
    float  loadFactor() const { return grid_.loadFactor(); }

    // Expose underlying grid for advanced use
    const SpatialGrid& grid() const { return grid_; }
    SpatialGrid&       grid()       { return grid_; }

private:
    SpatialGrid grid_;
};

} // namespace npc
