// run_tests.cpp — comprehensive test suite for NPC behavior system
// Compile: g++ -std=c++17 -I../include -O2 -o run_tests run_tests.cpp
// Run:     ./run_tests          (dot output)
//          ./run_tests -v       (verbose)

#include "test_runner.hpp"

// Systems under test
#include "npc/social/faction_system.hpp"
#include "npc/social/relationship_system.hpp"
#include "npc/world/spatial_index.hpp"
#include "npc/navigation/pathfinding.hpp"
#include "npc/ai/shared_blackboard.hpp"
#include "npc/world/lod_system.hpp"
#include "npc/serialization/json.hpp"
#include "npc/event/event_system.hpp"
#include "npc/ai/blackboard.hpp"

#include <vector>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// JSON Parser / Writer
// ─────────────────────────────────────────────────────────────────────────────

TEST("JSON: parse null") {
    auto v = npc::json::parse("null");
    ASSERT_TRUE(std::holds_alternative<std::nullptr_t>(v));
}

TEST("JSON: parse bool true") {
    auto v = npc::json::parse("true");
    ASSERT_TRUE(std::get<bool>(v) == true);
}

TEST("JSON: parse bool false") {
    auto v = npc::json::parse("false");
    ASSERT_TRUE(std::get<bool>(v) == false);
}

TEST("JSON: parse integer") {
    auto v = npc::json::parse("42");
    ASSERT_EQ(std::get<int64_t>(v), int64_t(42));
}

TEST("JSON: parse negative integer") {
    auto v = npc::json::parse("-17");
    ASSERT_EQ(std::get<int64_t>(v), int64_t(-17));
}

TEST("JSON: parse double") {
    auto v = npc::json::parse("3.14");
    ASSERT_NEAR(std::get<double>(v), 3.14, 1e-9);
}

TEST("JSON: parse string") {
    auto v = npc::json::parse("\"hello world\"");
    ASSERT_EQ(std::get<std::string>(v), "hello world");
}

TEST("JSON: parse string with escape") {
    auto v = npc::json::parse("\"tab\\there\"");
    ASSERT_EQ(std::get<std::string>(v), "tab\there");
}

TEST("JSON: parse empty array") {
    auto v = npc::json::parse("[]");
    ASSERT_TRUE(std::holds_alternative<npc::json::JsonArray>(v));
    ASSERT_EQ(std::get<npc::json::JsonArray>(v).size(), std::size_t(0));
}

TEST("JSON: parse array of ints") {
    auto v = npc::json::parse("[1,2,3]");
    auto& arr = std::get<npc::json::JsonArray>(v);
    ASSERT_EQ(arr.size(), std::size_t(3));
    ASSERT_EQ(std::get<int64_t>(arr[0]), int64_t(1));
    ASSERT_EQ(std::get<int64_t>(arr[2]), int64_t(3));
}

TEST("JSON: parse empty object") {
    auto v = npc::json::parse("{}");
    ASSERT_TRUE(std::holds_alternative<npc::json::JsonObject>(v));
}

TEST("JSON: parse nested object") {
    auto v = npc::json::parse("{\"name\":\"Aria\",\"level\":5}");
    auto& obj = std::get<npc::json::JsonObject>(v);
    ASSERT_EQ(std::get<std::string>(obj.at("name")), "Aria");
    ASSERT_EQ(std::get<int64_t>(obj.at("level")), int64_t(5));
}

TEST("JSON: roundtrip") {
    std::string original = "{\"a\":1,\"b\":[2,3],\"c\":null}";
    auto v = npc::json::parse(original);
    std::string out = npc::json::toString(v);
    auto v2 = npc::json::parse(out);
    ASSERT_EQ(npc::json::toString(v2), out);
}

TEST("JSON: toString pretty smoke") {
    auto v = npc::json::parse("[1,[2,3],{\"x\":true}]");
    std::string pretty = npc::json::toString(v, true);
    ASSERT_FALSE(pretty.empty());
    ASSERT_TRUE(pretty.find('\n') != std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Blackboard
// ─────────────────────────────────────────────────────────────────────────────

TEST("Blackboard: set and get typed") {
    npc::Blackboard bb;
    bb.set<int>("health", 100);
    ASSERT_EQ(bb.get<int>("health"), 100);
}

TEST("Blackboard: get missing returns default") {
    npc::Blackboard bb;
    ASSERT_EQ(bb.get<int>("missing"), 0);
}

TEST("Blackboard: overwrite value") {
    npc::Blackboard bb;
    bb.set<float>("x", 1.0f);
    bb.set<float>("x", 2.0f);
    ASSERT_NEAR(bb.get<float>("x"), 2.0f, 1e-6f);
}

TEST("Blackboard: has() works") {
    npc::Blackboard bb;
    ASSERT_FALSE(bb.has("key"));
    bb.set<std::string>("key", "val");
    ASSERT_TRUE(bb.has("key"));
}

TEST("Blackboard: erase()") {
    npc::Blackboard bb;
    bb.set<int>("n", 42);
    bb.erase("n");
    ASSERT_FALSE(bb.has("n"));
}

TEST("Blackboard: snapshot and restore") {
    npc::Blackboard bb;
    bb.set<int>("a", 1);
    bb.set<int>("b", 2);
    auto snap = bb.snapshot();
    bb.set<int>("a", 99);
    bb.restore(snap);
    ASSERT_EQ(bb.get<int>("a"), 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// SharedBlackboard
// ─────────────────────────────────────────────────────────────────────────────

TEST("SharedBlackboard: set and get") {
    npc::SharedBlackboard sbb;
    sbb.set<int>("counter", 5, 0.0);
    ASSERT_EQ(sbb.get<int>("counter"), 5);
}

TEST("SharedBlackboard: TTL expiry") {
    npc::SharedBlackboard sbb;
    sbb.set<int>("temp", 99, 0.0, 1.0); // expires in 1 second
    ASSERT_EQ(sbb.get<int>("temp"), 99);
    sbb.pruneExpired(2.0); // advance time past TTL
    ASSERT_FALSE(sbb.has("temp"));
}

TEST("SharedBlackboard: setIfAbsent does not overwrite") {
    npc::SharedBlackboard sbb;
    sbb.set<int>("x", 10, 0.0);
    sbb.setIfAbsent<int>("x", 20, 0.0);
    ASSERT_EQ(sbb.get<int>("x"), 10);
}

TEST("SharedBlackboard: versioning increments") {
    npc::SharedBlackboard sbb;
    sbb.set<int>("v", 1, 0.0);
    uint64_t v1 = sbb.version("v");
    sbb.set<int>("v", 2, 0.0);
    uint64_t v2 = sbb.version("v");
    ASSERT_GT(v2, v1);
}

TEST("SharedBlackboard: watcher fires on set") {
    npc::SharedBlackboard sbb;
    int fired = 0;
    auto id = sbb.watch("test/", [&](const std::string&, const npc::BBEntry&){
        ++fired;
    });
    sbb.set<int>("test/value", 42, 0.0);
    ASSERT_EQ(fired, 1);
    sbb.unwatch(id);
}

// ─────────────────────────────────────────────────────────────────────────────
// EventBus
// ─────────────────────────────────────────────────────────────────────────────

struct TestEvent { int value; };
struct OtherEvent { std::string msg; };

TEST("EventBus: basic subscribe and publish") {
    npc::EventBus bus;
    int received = 0;
    bus.subscribe<TestEvent>([&](const npc::EventRecord& r){
        received = r.as<TestEvent>().value;
    });
    bus.publish(TestEvent{42});
    ASSERT_EQ(received, 42);
}

TEST("EventBus: multiple subscribers") {
    npc::EventBus bus;
    int count = 0;
    bus.subscribe<TestEvent>([&](const npc::EventRecord&){ ++count; });
    bus.subscribe<TestEvent>([&](const npc::EventRecord&){ ++count; });
    bus.publish(TestEvent{1});
    ASSERT_EQ(count, 2);
}

TEST("EventBus: different event types don't cross") {
    npc::EventBus bus;
    int testCount = 0, otherCount = 0;
    bus.subscribe<TestEvent>([&](const npc::EventRecord&){ ++testCount; });
    bus.subscribe<OtherEvent>([&](const npc::EventRecord&){ ++otherCount; });
    bus.publish(TestEvent{1});
    ASSERT_EQ(testCount, 1);
    ASSERT_EQ(otherCount, 0);
}

TEST("EventBus: ScopedSubscription auto-unsubscribes") {
    npc::EventBus bus;
    int count = 0;
    {
        auto sub = bus.subscribeScoped<TestEvent>([&](const npc::EventRecord&){
            ++count;
        });
        bus.publish(TestEvent{1});
        ASSERT_EQ(count, 1);
    } // sub destroyed here
    bus.publish(TestEvent{2});
    ASSERT_EQ(count, 1); // should not have incremented
}

TEST("EventBus: delayed event fires after update") {
    npc::EventBus bus;
    int received = 0;
    bus.subscribe<TestEvent>([&](const npc::EventRecord& r){
        received = r.as<TestEvent>().value;
    });
    bus.publishDelayed(TestEvent{99}, 1.0, 5.0); // delay 1s, published at t=5
    bus.update(5.5); // t=5.5, delay triggered
    ASSERT_EQ(received, 99);
}

TEST("EventBus: delayed event does not fire before time") {
    npc::EventBus bus;
    int received = 0;
    bus.subscribe<TestEvent>([&](const npc::EventRecord& r){
        received = r.as<TestEvent>().value;
    });
    bus.publishDelayed(TestEvent{77}, 10.0, 0.0); // delay 10s
    bus.update(5.0); // before deadline
    ASSERT_EQ(received, 0);
}

TEST("EventBus: history records events") {
    npc::EventBus bus;
    bus.publish(TestEvent{1});
    bus.publish(TestEvent{2});
    auto hist = bus.getHistory<TestEvent>();
    ASSERT_GE(hist.size(), std::size_t(2));
}

TEST("EventBus: priority ordering") {
    npc::EventBus bus;
    std::vector<int> order;
    // Lower priority value = fires first
    bus.subscribe<TestEvent>([&](const npc::EventRecord&){ order.push_back(2); },
                             npc::EventPriority::Normal);
    bus.subscribe<TestEvent>([&](const npc::EventRecord&){ order.push_back(1); },
                             npc::EventPriority::High);
    bus.subscribe<TestEvent>([&](const npc::EventRecord&){ order.push_back(3); },
                             npc::EventPriority::Low);
    bus.publish(TestEvent{0});
    ASSERT_EQ(order.size(), std::size_t(3));
    ASSERT_EQ(order[0], 1);
    ASSERT_EQ(order[1], 2);
    ASSERT_EQ(order[2], 3);
}

// ─────────────────────────────────────────────────────────────────────────────
// SpatialGrid / SpatialIndex
// ─────────────────────────────────────────────────────────────────────────────

TEST("SpatialGrid: insert and queryRadius") {
    npc::SpatialGrid grid(10.0f);
    grid.insert(1, {0, 0});
    grid.insert(2, {5, 0});
    grid.insert(3, {50, 50});
    auto hits = grid.queryRadius({0,0}, 8.0f);
    // Should find 1 and 2, not 3
    ASSERT_EQ(hits.size(), std::size_t(2));
}

TEST("SpatialGrid: update moves entity") {
    npc::SpatialGrid grid(10.0f);
    grid.insert(1, {0, 0});
    grid.update(1, {100, 100});
    auto hits = grid.queryRadius({0,0}, 5.0f);
    ASSERT_EQ(hits.size(), std::size_t(0));
    auto hits2 = grid.queryRadius({100,100}, 5.0f);
    ASSERT_EQ(hits2.size(), std::size_t(1));
}

TEST("SpatialGrid: remove clears entity") {
    npc::SpatialGrid grid(10.0f);
    grid.insert(1, {0,0});
    grid.remove(1);
    auto hits = grid.queryRadius({0,0}, 5.0f);
    ASSERT_EQ(hits.size(), std::size_t(0));
}

TEST("SpatialGrid: nearest returns closest") {
    npc::SpatialGrid grid(10.0f);
    grid.insert(1, {0, 0});
    grid.insert(2, {3, 0});
    grid.insert(3, {10, 0});
    auto hits = grid.nearest({0,0}, 1, 20.0f);
    ASSERT_EQ(hits.size(), std::size_t(1));
    ASSERT_EQ(hits[0].id, 1u);
}

TEST("SpatialIndex: nearby excludes self") {
    npc::SpatialIndex si(10.0f);
    si.insert(1, {0,0});
    si.insert(2, {1,0});
    auto hits = si.nearbyExcept(1u, {0,0}, 5.0f);
    ASSERT_EQ(hits.size(), std::size_t(1));
    ASSERT_EQ(hits[0].id, 2u);
}

TEST("SpatialIndex: findClusters groups close entities") {
    npc::SpatialIndex si(5.0f);
    // Cluster A
    si.insert(1, {0,0});
    si.insert(2, {1,0});
    si.insert(3, {0,1});
    // Cluster B far away
    si.insert(4, {100,100});
    si.insert(5, {101,100});
    auto clusters = si.findClusters(5.0f);
    ASSERT_GE(clusters.size(), std::size_t(2));
}

// ─────────────────────────────────────────────────────────────────────────────
// Pathfinding
// ─────────────────────────────────────────────────────────────────────────────

TEST("Pathfinding: simple straight path") {
    npc::Pathfinder pf;
    int W = 10, H = 10;
    std::vector<bool> walkable(W*H, true);
    pf.init(W, H, [&](int x, int y){ return walkable[y*W+x]; });
    pf.buildRegions();
    auto result = pf.query({0,0}, {5,0});
    ASSERT_TRUE(result.complete);
    ASSERT_FALSE(result.waypoints.empty());
}

TEST("Pathfinding: blocked goal returns partial or failure") {
    npc::Pathfinder pf;
    int W = 5, H = 5;
    std::vector<bool> walkable(W*H, true);
    // Block column 2 completely — wall cuts map
    for (int y = 0; y < H; ++y) walkable[y*W+2] = false;
    pf.init(W, H, [&](int x, int y){ return walkable[y*W+x]; });
    pf.buildRegions();
    // Left side to right side — not reachable
    bool reachable = pf.isReachable({0,0}, {4,0});
    ASSERT_FALSE(reachable);
}

TEST("Pathfinding: PathCache stores and retrieves") {
    npc::PathCache cache;
    npc::PathResult r;
    r.complete = true;
    r.waypoints = {{0,0},{1,0},{2,0}};
    r.cost = 2.0f;
    cache.put(1, 2, r);
    auto* got = cache.get(1, 2);
    ASSERT_TRUE(got != nullptr);
    ASSERT_EQ(got->waypoints.size(), std::size_t(3));
}

TEST("Pathfinding: PathCache miss returns nullptr") {
    npc::PathCache cache;
    auto* got = cache.get(99, 100);
    ASSERT_TRUE(got == nullptr);
}

TEST("Pathfinding: obstacle invalidates cache") {
    npc::Pathfinder pf;
    int W = 10, H = 10;
    std::vector<bool> walkable(W*H, true);
    pf.init(W, H, [&](int x, int y){ return walkable[y*W+x]; });
    pf.buildRegions();
    auto r1 = pf.query({0,0}, {9,0});
    ASSERT_TRUE(r1.fromCache == false);
    auto r2 = pf.query({0,0}, {9,0}); // second time: from cache
    ASSERT_TRUE(r2.fromCache == true);
    pf.addObstacle({5,0}); // invalidate
    auto r3 = pf.query({0,0}, {9,0});
    ASSERT_TRUE(r3.fromCache == false); // cache was busted
}

TEST("Pathfinding: NavRegions reachability") {
    npc::NavRegions nr;
    int W = 6, H = 6;
    std::vector<bool> walkable(W*H, true);
    // Vertical wall at x=3
    for (int y = 0; y < H; ++y) walkable[y*W+3] = false;
    nr.rebuild(W, H, [&](int x, int y){ return walkable[y*W+x]; });
    ASSERT_FALSE(nr.isReachable({0,0}, {5,0}));
    ASSERT_TRUE(nr.isReachable({0,0}, {2,0}));
    ASSERT_TRUE(nr.isReachable({4,0}, {5,0}));
}

// ─────────────────────────────────────────────────────────────────────────────
// FactionSystem
// ─────────────────────────────────────────────────────────────────────────────

TEST("FactionSystem: create and query factions") {
    npc::FactionSystem fs;
    fs.createFaction("rebels", "Rebel Alliance");
    fs.createFaction("empire", "Galactic Empire");
    ASSERT_TRUE(fs.factionExists("rebels"));
    ASSERT_TRUE(fs.factionExists("empire"));
    ASSERT_EQ(fs.getFactionName("rebels"), "Rebel Alliance");
}

TEST("FactionSystem: default stance is Peace") {
    npc::FactionSystem fs;
    fs.createFaction("a", "A");
    fs.createFaction("b", "B");
    ASSERT_EQ(fs.getStance("a","b"), npc::FactionStance::Peace);
}

TEST("FactionSystem: declareWar sets stance") {
    npc::FactionSystem fs;
    fs.createFaction("a", "A");
    fs.createFaction("b", "B");
    fs.declareWar("a", "b", "resources", 0.0);
    ASSERT_EQ(fs.getStance("a","b"), npc::FactionStance::War);
    ASSERT_EQ(fs.getStance("b","a"), npc::FactionStance::War);
}

TEST("FactionSystem: war is symmetric") {
    npc::FactionSystem fs;
    fs.createFaction("x","X"); fs.createFaction("y","Y");
    fs.declareWar("x","y","test", 0.0);
    ASSERT_TRUE(fs.areHostile("x","y"));
    ASSERT_TRUE(fs.areHostile("y","x"));
}

TEST("FactionSystem: formAlliance sets stance") {
    npc::FactionSystem fs;
    fs.createFaction("a","A"); fs.createFaction("b","B");
    fs.formAlliance("a","b",0.0);
    ASSERT_EQ(fs.getStance("a","b"), npc::FactionStance::Alliance);
    ASSERT_TRUE(fs.areAllied("a","b"));
}

TEST("FactionSystem: declarePeace creates truce") {
    npc::FactionSystem fs;
    fs.createFaction("a","A"); fs.createFaction("b","B");
    fs.declareWar("a","b","greed",0.0);
    fs.declarePeace("a","b",0.0, 100.0); // truce for 100 sim hours
    ASSERT_EQ(fs.getStance("a","b"), npc::FactionStance::Truce);
}

TEST("FactionSystem: truce expires to Peace") {
    npc::FactionSystem fs;
    fs.createFaction("a","A"); fs.createFaction("b","B");
    fs.declareWar("a","b","test",0.0);
    fs.declarePeace("a","b",0.0, 50.0); // expires at t=50
    fs.update(60.0, nullptr); // past expiry
    ASSERT_EQ(fs.getStance("a","b"), npc::FactionStance::Peace);
}

TEST("FactionSystem: alliance cascade on war") {
    npc::FactionSystem fs;
    fs.createFaction("a","A");
    fs.createFaction("b","B");
    fs.createFaction("ally_of_b","AllyB");
    fs.formAlliance("b","ally_of_b",0.0);
    fs.declareWar("a","b","conquest",0.0, true); // cascade=true
    // ally_of_b should also be at war with a
    ASSERT_EQ(fs.getStance("a","ally_of_b"), npc::FactionStance::War);
}

TEST("FactionSystem: vassal joins overlord war") {
    npc::FactionSystem fs;
    fs.createFaction("lord","Lord");
    fs.createFaction("vassal","Vassal");
    fs.createFaction("enemy","Enemy");
    fs.formVassal("vassal","lord",0.0);
    fs.declareWar("lord","enemy","expansion",0.0, true);
    // vassal auto-joins
    ASSERT_EQ(fs.getStance("vassal","enemy"), npc::FactionStance::War);
}

TEST("FactionSystem: wouldDefend transitive check") {
    npc::FactionSystem fs;
    fs.createFaction("a","A");
    fs.createFaction("b","B");
    fs.createFaction("c","C");
    fs.formAlliance("b","c",0.0);
    ASSERT_TRUE(fs.wouldDefend("b","c")); // direct ally
    ASSERT_FALSE(fs.wouldDefend("a","c")); // no relationship
}

TEST("FactionSystem: resolveCoalition both sides") {
    npc::FactionSystem fs;
    fs.createFaction("attacker","Attacker");
    fs.createFaction("target","Target");
    fs.createFaction("target_ally","TargetAlly");
    fs.formAlliance("target","target_ally",0.0);
    auto coalition = fs.resolveCoalition("attacker","target");
    ASSERT_EQ(coalition.aggressor, "attacker");
    ASSERT_EQ(coalition.defender, "target");
    // target_ally should be on defender side
    ASSERT_TRUE(std::find(coalition.defenderSide.begin(),
                          coalition.defenderSide.end(),
                          "target_ally") != coalition.defenderSide.end());
}

TEST("FactionSystem: addMember and getMemberFaction") {
    npc::FactionSystem fs;
    fs.createFaction("guild","Guild");
    fs.addMember("npc_001","guild");
    ASSERT_EQ(fs.getFactionOf("npc_001"), "guild");
}

TEST("FactionSystem: diplomatic history records events") {
    npc::FactionSystem fs;
    fs.createFaction("a","A"); fs.createFaction("b","B");
    fs.declareWar("a","b","land",1.0);
    fs.declarePeace("a","b",10.0, 50.0);
    auto summary = fs.diplomaticSummary("a","b");
    ASSERT_FALSE(summary.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// RelationshipSystem
// ─────────────────────────────────────────────────────────────────────────────

TEST("RelationshipSystem: default value is neutral") {
    npc::RelationshipSystem rs;
    ASSERT_NEAR(rs.getValue("aria","bard"), 0.0f, 1.0f);
}

TEST("RelationshipSystem: recordEvent adjusts value") {
    npc::RelationshipSystem rs;
    rs.recordEvent("aria","bard", npc::RelationshipEventType::Saved, 0.0);
    float v = rs.getValue("aria","bard"); // bard's view of aria
    ASSERT_GT(v, 10.0f);
}

TEST("RelationshipSystem: betrayal reduces value") {
    npc::RelationshipSystem rs;
    rs.recordEvent("aria","bard", npc::RelationshipEventType::Betrayed, 0.0);
    float v = rs.getValue("aria","bard");
    ASSERT_LT(v, -20.0f);
}

TEST("RelationshipSystem: trust decreases on lie") {
    npc::RelationshipSystem rs;
    rs.recordEvent("aria","bard", npc::RelationshipEventType::Lied, 0.0);
    float trust = rs.getTrust("aria","bard");
    ASSERT_LT(trust, 50.0f);
}

TEST("RelationshipSystem: mutual event affects both") {
    npc::RelationshipSystem rs;
    rs.recordMutualEvent("a","b", npc::RelationshipEventType::Traded, 0.0);
    ASSERT_GT(rs.getValue("a","b"), 0.0f);
    ASSERT_GT(rs.getValue("b","a"), 0.0f);
}

TEST("RelationshipSystem: decay moves toward neutral") {
    npc::RelationshipSystem rs;
    rs.setValue("a","b", 50.0f);
    rs.update(0.0, 100.0f); // 100 hours
    float after = rs.getValue("a","b");
    ASSERT_LT(after, 50.0f); // decayed toward 0
    ASSERT_GT(after, 0.0f);  // but not past 0
}

TEST("RelationshipSystem: remembers past event") {
    npc::RelationshipSystem rs;
    rs.recordEvent("hero","npc", npc::RelationshipEventType::Saved, 10.0);
    ASSERT_TRUE(rs.remembers("npc","hero", npc::RelationshipEventType::Saved));
}

TEST("RelationshipSystem: does not remember event before since") {
    npc::RelationshipSystem rs;
    rs.recordEvent("hero","npc", npc::RelationshipEventType::Saved, 5.0);
    ASSERT_FALSE(rs.remembers("npc","hero", npc::RelationshipEventType::Saved, 10.0));
}

TEST("RelationshipSystem: narrative non-empty after events") {
    npc::RelationshipSystem rs;
    rs.recordEvent("hero","npc", npc::RelationshipEventType::Saved, 0.0);
    rs.recordEvent("hero","npc", npc::RelationshipEventType::Gifted, 5.0);
    std::string n = rs.narrative("hero","npc", 10.0);
    ASSERT_FALSE(n.empty());
    ASSERT_TRUE(n.find("hero") != std::string::npos);
}

TEST("RelationshipSystem: recallSentence returns relevant event") {
    npc::RelationshipSystem rs;
    rs.recordEvent("hero","npc", npc::RelationshipEventType::Saved, 24.0);
    auto s = rs.recallSentence("npc","hero", npc::RelationshipEventType::Saved, 48.0);
    ASSERT_TRUE(s.has_value());
    ASSERT_TRUE(s->find("saved") != std::string::npos);
}

TEST("RelationshipSystem: areHostile when both negative") {
    npc::RelationshipSystem rs;
    rs.setValue("a","b", -50.0f);
    rs.setValue("b","a", -50.0f);
    ASSERT_TRUE(rs.areHostile("a","b"));
}

TEST("RelationshipSystem: areFriendly when both positive") {
    npc::RelationshipSystem rs;
    rs.setValue("a","b", 30.0f);
    rs.setValue("b","a", 30.0f);
    ASSERT_TRUE(rs.areFriendly("a","b"));
}

TEST("RelationshipSystem: topFriends sorted descending") {
    npc::RelationshipSystem rs;
    rs.setValue("a","b", 80.0f);
    rs.setValue("a","c", 20.0f);
    rs.setValue("a","d", 60.0f);
    auto friends = rs.topFriends("a", 3);
    ASSERT_EQ(friends.size(), std::size_t(3));
    ASSERT_GE(friends[0].second, friends[1].second);
    ASSERT_GE(friends[1].second, friends[2].second);
}

TEST("RelationshipSystem: removeNPC cleans all pairs") {
    npc::RelationshipSystem rs;
    rs.recordEvent("a","b", npc::RelationshipEventType::Helped, 0.0);
    rs.recordEvent("a","c", npc::RelationshipEventType::Helped, 0.0);
    rs.removeNPC("a");
    ASSERT_EQ(rs.pairCount(), std::size_t(0));
}

// ─────────────────────────────────────────────────────────────────────────────
// LOD System
// ─────────────────────────────────────────────────────────────────────────────

TEST("LODSystem: new NPC starts Active when close") {
    npc::LODSystem lod;
    npc::LODConfig cfg;
    cfg.activeRadius     = 50.0f;
    cfg.backgroundRadius = 150.0f;
    lod.setConfig(cfg);
    lod.track(1, {0,0});

    std::vector<std::pair<uint32_t, npc::Vec2>> npcs = {{1, {10.0f, 0.0f}}};
    lod.update(npcs, {0,0}, 0.0, 0.016f);
    ASSERT_EQ(lod.getTier(1), npc::LODTier::Active);
}

TEST("LODSystem: distant NPC is Dormant") {
    npc::LODSystem lod;
    npc::LODConfig cfg;
    cfg.activeRadius     = 50.0f;
    cfg.backgroundRadius = 100.0f;
    lod.setConfig(cfg);
    lod.track(1, {0,0});

    std::vector<std::pair<uint32_t, npc::Vec2>> npcs = {{1, {300.0f, 0.0f}}};
    // Need enough frames to pass hysteresis dwell
    for (int i = 0; i < 200; ++i)
        lod.update(npcs, {0,0}, static_cast<double>(i)*0.016, 0.016f);
    ASSERT_EQ(lod.getTier(1), npc::LODTier::Dormant);
}

TEST("LODSystem: pin prevents demotion") {
    npc::LODSystem lod;
    npc::LODConfig cfg;
    cfg.activeRadius     = 50.0f;
    cfg.backgroundRadius = 100.0f;
    lod.setConfig(cfg);
    lod.track(1, {0,0});
    lod.pin(1);

    std::vector<std::pair<uint32_t, npc::Vec2>> npcs = {{1, {500.0f, 0.0f}}};
    for (int i = 0; i < 200; ++i)
        lod.update(npcs, {0,0}, static_cast<double>(i)*0.016, 0.016f);
    // Pinned — should remain Active
    ASSERT_EQ(lod.getTier(1), npc::LODTier::Active);
}

TEST("LODSystem: toTickThisFrame returns active NPCs") {
    npc::LODSystem lod;
    lod.track(1, {0,0});
    lod.track(2, {5,0});
    std::vector<std::pair<uint32_t, npc::Vec2>> npcs = {{1,{0,0}},{2,{5,0}}};
    lod.update(npcs, {0,0}, 0.0, 0.016f);
    auto active = lod.toTickThisFrame(npc::LODTier::Active);
    ASSERT_GE(active.size(), std::size_t(1));
}

// ─────────────────────────────────────────────────────────────────────────────
// RelationshipData decay edge cases
// ─────────────────────────────────────────────────────────────────────────────

TEST("RelationshipData: decayedWeight at t=0 is 1") {
    npc::RelationshipEvent ev;
    ev.simTime = 100.0;
    ASSERT_NEAR(ev.decayedWeight(100.0, 168.0f), 1.0f, 1e-5f);
}

TEST("RelationshipData: decayedWeight at half life is 0.5") {
    npc::RelationshipEvent ev;
    ev.simTime = 0.0;
    ASSERT_NEAR(ev.decayedWeight(168.0, 168.0f), 0.5f, 1e-4f);
}

TEST("RelationshipData: history capped at MAX_HISTORY") {
    npc::RelationshipData d;
    for (int i = 0; i < 100; ++i) {
        npc::RelationshipEvent ev;
        ev.simTime = static_cast<double>(i);
        d.addEvent(ev);
    }
    ASSERT_EQ(d.history.size(), npc::RelationshipData::MAX_HISTORY);
}

TEST("RelationshipData: worstEvent finds minimum delta") {
    npc::RelationshipData d;
    npc::RelationshipEvent good; good.delta = 20.0f; d.addEvent(good);
    npc::RelationshipEvent bad;  bad.delta  = -50.0f; d.addEvent(bad);
    npc::RelationshipEvent mid;  mid.delta  = 5.0f;  d.addEvent(mid);
    auto* worst = d.worstEvent();
    ASSERT_TRUE(worst != nullptr);
    ASSERT_NEAR(worst->delta, -50.0f, 1e-5f);
}

TEST("RelationshipData: bestEvent finds maximum delta") {
    npc::RelationshipData d;
    npc::RelationshipEvent e1; e1.delta = 10.0f; d.addEvent(e1);
    npc::RelationshipEvent e2; e2.delta = 35.0f; d.addEvent(e2);
    auto* best = d.bestEvent();
    ASSERT_TRUE(best != nullptr);
    ASSERT_NEAR(best->delta, 35.0f, 1e-5f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Integration smoke test
// ─────────────────────────────────────────────────────────────────────────────

TEST("Integration: faction war triggers NPC relationship hostility") {
    // Scenario: two factions at war → members should be hostile
    npc::FactionSystem  fs;
    npc::RelationshipSystem rs;

    fs.createFaction("knights","Knights");
    fs.createFaction("bandits","Bandits");
    fs.addMember("sir_roland","knights");
    fs.addMember("rogue_mira","bandits");

    // Declare war
    fs.declareWar("knights","bandits","territorial",0.0);

    // Simulate: each NPC inherits faction hostility by recording an event
    if (fs.areHostile("knights","bandits")) {
        rs.recordEvent("rogue_mira","sir_roland",
                       npc::RelationshipEventType::Attacked, 0.0);
        rs.recordEvent("sir_roland","rogue_mira",
                       npc::RelationshipEventType::Attacked, 0.0);
    }

    ASSERT_TRUE(rs.areHostile("sir_roland","rogue_mira"));
    ASSERT_TRUE(rs.areHostile("rogue_mira","sir_roland"));
}

TEST("Integration: saved event remembered in dialogue") {
    npc::RelationshipSystem rs;
    // Hero saves NPC at time=10
    rs.recordEvent("hero","merchant", npc::RelationshipEventType::Saved, 10.0);
    // At time=34 (1 day later), NPC recalls "hero saved me"
    auto recall = rs.recallSentence("merchant","hero",
                                    npc::RelationshipEventType::Saved, 34.0);
    ASSERT_TRUE(recall.has_value());
    ASSERT_TRUE(recall->find("day") != std::string::npos ||
                recall->find("ago") != std::string::npos);
}

TEST("Integration: blackboard shared world state") {
    npc::WorldBlackboard wb;
    wb.setTime(12.5, 5, 0.0);
    wb.setFactionAlert("undead", 0.9f, 0.0);
    wb.setItemPrice("iron_sword", 45.0f, 0.0);

    auto view = wb.viewOf("faction/", 0.0);
    ASSERT_TRUE(view.has("faction/undead/alert"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Entry point
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    bool verbose = (argc > 1 && std::string(argv[1]) == "-v");
    return npc::test::run_all(verbose);
}
