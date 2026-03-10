# Changelog

All notable changes to Aithena are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Versioning follows [Semantic Versioning](https://semver.org/).

---

## [1.0.0] — 2026-03-10

### Added

**Core AI systems**
- Finite State Machine (FSM) with guarded transitions, priority ordering, blackboard integration, and transition history
- Behavior Tree with composite/decorator/leaf nodes, fluent builder API, `ServiceNode`, `TimeoutDecorator`, `RetryDecorator`, `RandomSelectorNode`, and `debugSnapshot()`
- Utility AI with linear, sigmoid, exponential, and bell-curve response curves
- GOAP (Goal-Oriented Action Planning) with A* over world-state space

**Perception & Memory**
- Sight cone (configurable angle + range), hearing radius, line-of-sight via Bresenham
- Episodic memory with emotional impact scores, importance weights, and three decay stages: Fading / Nearly Forgotten / Forgotten
- Gossip propagation with trust-based reliability degradation and per-hop decay

**Emotion & Needs**
- Seven emotion types (Happy, Sad, Angry, Fearful, Disgusted, Surprised, Neutral) with intensity, duration, and decay
- Seven need types (Hunger, Thirst, Sleep, Social, Fun, Safety, Comfort) with configurable decay rates
- Emotional contagion — nearby NPCs share emotional states scaled by empathy and proximity

**Navigation**
- A* pathfinding with node budget, tie-break weighting, 8-directional movement, partial path fallback, and LRU path cache
- `NavRegions` flood-fill connectivity map for O(1) reachability pre-checks
- Dynamic obstacle invalidation, Catmull-Rom path smoothing
- `WaypointGraph` for sparse navigation over large open worlds
- `PathRequestQueue` for async, budget-limited batched pathfinding
- Steering behaviours: seek, flee, wander, arrival, obstacle avoidance, separation, cohesion, alignment

**Social & Faction**
- Faction system with six stance types (Peace, Alliance, War, Trade, Vassal, Truce), cascade war declarations, and coalition resolution
- Relationship system — directed graph with per-event history, trust channel, time-based decay, and narrative recall
- Social influence chains — hop-by-hop belief/rumour propagation with reliability degradation and charge mutation
- Group behavior with formation system (line, wedge, circle, column) and tactical roles

**World infrastructure**
- Typed event bus with priority ordering, delayed dispatch, event chains, filter predicates, and RAII subscription lifetime
- Shared blackboard with TTL expiry, version counters, and prefix-scoped watcher callbacks
- Two-layer spatial index: `SpatialGrid` (uniform hash-grid) + `QuadTree` (adaptive), with unified `SpatialIndex` façade
- LOD system — three tiers (Active / Background / Dormant), hysteresis, importance scoring, velocity prediction, per-frame CPU budget tracking
- `SimulationManager` orchestrating the full update pipeline
- Zero-dependency JSON serializer and NPC state serializer

**Combat, Trade, Schedule, Dialogue, Quest, Skills**
- Combat: threat assessment, ability system, stamina/mana pools, damage type resistances, flee thresholds
- Trade: supply/demand pricing, personality-based markup, relationship discounts
- Schedule: time-of-day activity planner with need and event overrides
- Dialogue: branching trees, reputation-based text variants, event-bus side effects
- Quest: assignment, progress tracking, completion/failure events
- Skills: six domains, XP, level thresholds, perk unlocks, stat bonuses

**Scripting & Bindings**
- Lua 5.4 scripting bridge — define full NPC FSM behaviour in Lua with no C++ changes
- Pure-C API (`npc_capi.h`) for Unity (P/Invoke), Unreal (native plugin), and Godot (GDExtension)
- WebAssembly build via Emscripten with live browser demo on GitHub Pages

**Tooling**
- CMake 3.16+ build system with optional targets: `npc_lua`, `npc_shared`, `npc_wasm`, `run_benchmarks`
- Performance benchmark suite covering full tick, emotion, FSM, pathfinding, spatial index, relationships, memory, and footprint
- CI matrix: GCC 12, Clang 15, macOS (Apple Clang) — builds, tests, and benchmarks on every push
- ~75 unit tests via zero-dependency test framework

---

[1.0.0]: https://github.com/sa-aris/aithena/releases/tag/v1.0.0
