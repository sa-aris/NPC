#pragma once
// economy_system.hpp — settlement-level production, consumption, price dynamics
// Part of Aithena (C++17, header-only)
//
// Complements TradeSystem: TradeSystem handles one merchant's inventory and
// haggling; EconomySystem simulates the macro loop that FEEDS those markets —
// producers turn inputs into goods, populations consume them, prices respond
// to stock levels, and caravans arbitrage price gaps between settlements.

#include "../core/types.hpp"
#include "../trade/trade_system.hpp"   // reuses Item / ItemId
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <functional>
#include <sstream>
#include <cmath>

namespace npc {

// ─── Production recipe ────────────────────────────────────────────────────────

struct ProductionRecipe {
    std::string                          id;          // "bake_bread"
    ItemId                               output       = 0;
    int                                  outputQty    = 1;
    std::vector<std::pair<ItemId, int>>  inputs;      // consumed per batch
    float                                laborHours   = 1.0f; // hours per batch
    std::string                          profession;  // e.g. "Baker" (informational)
};

// ─── Producer — an NPC working a recipe in a settlement ──────────────────────

struct Producer {
    std::string npcId;
    std::string recipeId;
    std::string settlement;
    float       efficiency  = 1.0f;  // skill multiplier on labor speed
    float       hoursWorked = 0.0f;  // accumulator toward next batch
    int         batchesDone = 0;
};

// ─── Caravan — goods in transit between settlements ─────────────────────────

struct Caravan {
    uint32_t    id          = 0;
    std::string from;
    std::string to;
    ItemId      item        = 0;
    int         quantity    = 0;
    double      departTime  = 0.0;
    float       travelHours = 8.0f;
    bool        arrived     = false;

    float progress(double now) const {
        if (arrived) return 1.0f;
        if (travelHours <= 0.0f) return 1.0f;
        return std::clamp(static_cast<float>((now - departTime) / travelHours),
                          0.0f, 1.0f);
    }
};

// ─── Settlement — stockpile + population ─────────────────────────────────────

struct SettlementEconomy {
    std::string            name;
    int                    population = 10;
    std::map<ItemId, int>  stockpile;
    std::map<ItemId, int>  targetStock;   // desired stock level per item
    float                  treasury   = 500.0f;

    int stockOf(ItemId id) const {
        auto it = stockpile.find(id);
        return it != stockpile.end() ? it->second : 0;
    }
    void addStock(ItemId id, int qty) {
        stockpile[id] += qty;
        if (stockpile[id] < 0) stockpile[id] = 0;
    }
    bool takeStock(ItemId id, int qty) {
        auto it = stockpile.find(id);
        if (it == stockpile.end() || it->second < qty) return false;
        it->second -= qty;
        return true;
    }
};

// ─── Economy events (for logging / demo output) ──────────────────────────────

struct EconomyEvent {
    enum class Type { Produced, Consumed, Shortage, CaravanDispatched, CaravanArrived };
    Type        type;
    std::string settlement;   // or destination for caravans
    ItemId      item   = 0;
    int         qty    = 0;
    double      simTime = 0.0;
    std::string note;
};

// ─── EconomySystem ────────────────────────────────────────────────────────────

class EconomySystem {
public:
    struct Config {
        float priceElasticity      = 0.5f;   // exponent of (target/stock) price curve
        float minPriceMul          = 0.25f;  // clamp on price multiplier
        float maxPriceMul          = 4.0f;
        float caravanPriceGap      = 1.6f;   // dispatch when price ratio exceeds this
        int   caravanMinLoad       = 5;      // don't send caravans for less
        float caravanTravelHours   = 8.0f;
        float consumptionInterval  = 24.0f;  // population eats once per interval
        int   maxEventLog          = 128;
    };

    Config config;

    // ── Item registry (shared with TradeSystem's Item struct) ─────────────────
    void registerItem(Item item) { itemDB_[item.id] = std::move(item); }

    const Item* getItemInfo(ItemId id) const {
        auto it = itemDB_.find(id);
        return it != itemDB_.end() ? &it->second : nullptr;
    }

    // Per-capita daily consumption for an item (0 = not consumed)
    void setConsumptionRate(ItemId id, float perCapitaPerDay) {
        consumptionRates_[id] = perCapitaPerDay;
    }

    // ── Settlements ───────────────────────────────────────────────────────────
    SettlementEconomy& addSettlement(const std::string& name, int population = 10) {
        auto& s = settlements_[name];
        s.name = name;
        s.population = population;
        return s;
    }

    SettlementEconomy* settlement(const std::string& name) {
        auto it = settlements_.find(name);
        return it != settlements_.end() ? &it->second : nullptr;
    }
    const SettlementEconomy* settlement(const std::string& name) const {
        auto it = settlements_.find(name);
        return it != settlements_.end() ? &it->second : nullptr;
    }

    // ── Recipes & producers ───────────────────────────────────────────────────
    void registerRecipe(ProductionRecipe r) { recipes_[r.id] = std::move(r); }

    const ProductionRecipe* getRecipe(const std::string& id) const {
        auto it = recipes_.find(id);
        return it != recipes_.end() ? &it->second : nullptr;
    }

    bool assignProducer(const std::string& npcId, const std::string& recipeId,
                        const std::string& settlementName, float efficiency = 1.0f) {
        if (!getRecipe(recipeId) || !settlement(settlementName)) return false;
        producers_.push_back({npcId, recipeId, settlementName, efficiency, 0.0f, 0});
        return true;
    }

    void removeProducer(const std::string& npcId) {
        producers_.erase(std::remove_if(producers_.begin(), producers_.end(),
                         [&](const Producer& p){ return p.npcId == npcId; }),
                         producers_.end());
    }

    // ── Pricing ───────────────────────────────────────────────────────────────
    // Local price = basePrice * (target / stock)^elasticity, clamped.
    float localPrice(const std::string& settlementName, ItemId id) const {
        const auto* item = getItemInfo(id);
        const auto* s    = settlement(settlementName);
        if (!item || !s) return 0.0f;

        int stock  = s->stockOf(id);
        auto tIt   = s->targetStock.find(id);
        int target = tIt != s->targetStock.end() ? tIt->second
                                                 : std::max(1, s->population);

        float ratio = static_cast<float>(target) /
                      static_cast<float>(std::max(1, stock));
        float mul   = std::pow(ratio, config.priceElasticity);
        mul = std::clamp(mul, config.minPriceMul, config.maxPriceMul);
        return item->basePrice * mul;
    }

    // Items whose stock is below half their target (quest generator fodder)
    std::vector<ItemId> scarceItems(const std::string& settlementName) const {
        std::vector<ItemId> out;
        const auto* s = settlement(settlementName);
        if (!s) return out;
        for (const auto& [id, item] : itemDB_) {
            auto tIt   = s->targetStock.find(id);
            int target = tIt != s->targetStock.end() ? tIt->second
                                                     : std::max(1, s->population);
            if (s->stockOf(id) * 2 < target) out.push_back(id);
        }
        return out;
    }

    // ── Tick ──────────────────────────────────────────────────────────────────
    void update(double simTime, float elapsedHours) {
        if (elapsedHours <= 0.0f) return;

        runProduction(simTime, elapsedHours);
        runConsumption(simTime, elapsedHours);
        runCaravans(simTime);
        dispatchCaravans(simTime);
    }

    // ── Caravans ──────────────────────────────────────────────────────────────
    // Manual dispatch (auto-dispatch also runs inside update()).
    uint32_t sendCaravan(const std::string& from, const std::string& to,
                         ItemId item, int qty, double simTime) {
        auto* src = settlement(from);
        if (!src || !settlement(to) || qty <= 0) return 0;
        if (src->stockOf(item) < qty) return 0;

        src->addStock(item, -qty);
        Caravan c;
        c.id          = nextCaravanId_++;
        c.from        = from;
        c.to          = to;
        c.item        = item;
        c.quantity    = qty;
        c.departTime  = simTime;
        c.travelHours = config.caravanTravelHours;
        caravans_.push_back(c);
        logEvent({EconomyEvent::Type::CaravanDispatched, to, item, qty, simTime,
                  from + " → " + to});
        return c.id;
    }

    const std::vector<Caravan>& caravans() const { return caravans_; }

    std::vector<const Caravan*> caravansInTransit() const {
        std::vector<const Caravan*> out;
        for (const auto& c : caravans_) if (!c.arrived) out.push_back(&c);
        return out;
    }

    // ── Introspection ─────────────────────────────────────────────────────────
    const std::vector<Producer>&                     producers()   const { return producers_; }
    const std::map<std::string, SettlementEconomy>&  settlements() const { return settlements_; }

    // ── Restore (save/load support) ───────────────────────────────────────────
    // Items, recipes and consumption rates are code-defined static data:
    // re-register them before restoring the dynamic state below.
    std::vector<Producer>& producersMutable() { return producers_; }

    void restoreCaravan(Caravan c) {
        nextCaravanId_ = std::max(nextCaravanId_, c.id + 1);
        caravans_.push_back(std::move(c));
    }

    void clearDynamicState() {
        settlements_.clear();
        producers_.clear();
        caravans_.clear();
        events_.clear();
        consumptionClock_ = 0.0f;
        nextCaravanId_    = 1;
    }

    // Drain accumulated events (Produced / Shortage / Caravan…)
    std::vector<EconomyEvent> drainEvents() {
        std::vector<EconomyEvent> out;
        out.swap(events_);
        return out;
    }

    std::string debugString() const {
        std::ostringstream ss;
        ss << "EconomySystem: " << settlements_.size() << " settlements, "
           << producers_.size() << " producers, "
           << caravansInTransit().size() << " caravans in transit\n";
        for (const auto& [name, s] : settlements_) {
            ss << "  [" << name << "] pop=" << s.population
               << " treasury=" << static_cast<int>(s.treasury) << "g\n";
            for (const auto& [id, qty] : s.stockpile) {
                const auto* item = getItemInfo(id);
                ss << "    " << (item ? item->name : std::to_string(id))
                   << ": " << qty
                   << " (price " << static_cast<int>(localPrice(name, id)) << "g)\n";
            }
        }
        return ss.str();
    }

private:
    void runProduction(double simTime, float elapsedHours) {
        for (auto& p : producers_) {
            const auto* recipe = getRecipe(p.recipeId);
            auto*       s      = settlement(p.settlement);
            if (!recipe || !s || recipe->laborHours <= 0.0f) continue;

            p.hoursWorked += elapsedHours * p.efficiency;
            while (p.hoursWorked >= recipe->laborHours) {
                // Check inputs
                bool haveInputs = true;
                for (const auto& [inId, inQty] : recipe->inputs)
                    if (s->stockOf(inId) < inQty) { haveInputs = false; break; }

                if (!haveInputs) {
                    // Stall at the threshold — resume when inputs arrive
                    p.hoursWorked = recipe->laborHours;
                    break;
                }

                for (const auto& [inId, inQty] : recipe->inputs)
                    s->addStock(inId, -inQty);
                s->addStock(recipe->output, recipe->outputQty);
                p.hoursWorked -= recipe->laborHours;
                ++p.batchesDone;
                logEvent({EconomyEvent::Type::Produced, s->name, recipe->output,
                          recipe->outputQty, simTime, p.npcId});
            }
        }
    }

    void runConsumption(double simTime, float elapsedHours) {
        consumptionClock_ += elapsedHours;
        while (consumptionClock_ >= config.consumptionInterval) {
            consumptionClock_ -= config.consumptionInterval;
            float days = config.consumptionInterval / 24.0f;

            for (auto& [name, s] : settlements_) {
                for (const auto& [itemId, perCapita] : consumptionRates_) {
                    int needed = static_cast<int>(
                        std::ceil(perCapita * s.population * days));
                    if (needed <= 0) continue;

                    int available = s.stockOf(itemId);
                    int eaten     = std::min(needed, available);
                    if (eaten > 0) {
                        s.addStock(itemId, -eaten);
                        logEvent({EconomyEvent::Type::Consumed, name, itemId,
                                  eaten, simTime, {}});
                    }
                    if (eaten < needed) {
                        logEvent({EconomyEvent::Type::Shortage, name, itemId,
                                  needed - eaten, simTime, {}});
                    }
                }
            }
        }
    }

    void runCaravans(double simTime) {
        for (auto& c : caravans_) {
            if (c.arrived || c.progress(simTime) < 1.0f) continue;
            c.arrived = true;
            if (auto* dst = settlement(c.to))
                dst->addStock(c.item, c.quantity);
            logEvent({EconomyEvent::Type::CaravanArrived, c.to, c.item,
                      c.quantity, simTime, c.from + " → " + c.to});
        }
        // Trim delivered caravans (keep a short tail for queries)
        if (caravans_.size() > 64) {
            caravans_.erase(std::remove_if(caravans_.begin(), caravans_.end(),
                            [](const Caravan& c){ return c.arrived; }),
                            caravans_.end());
        }
    }

    // Auto-arbitrage: when an item is much pricier elsewhere, ship surplus there
    void dispatchCaravans(double simTime) {
        for (const auto& [itemId, item] : itemDB_) {
            for (auto& [fromName, from] : settlements_) {
                int surplus = from.stockOf(itemId);
                auto tIt    = from.targetStock.find(itemId);
                int target  = tIt != from.targetStock.end() ? tIt->second
                                                            : std::max(1, from.population);
                surplus -= target; // only ship what's above local needs
                if (surplus < config.caravanMinLoad) continue;

                float here = localPrice(fromName, itemId);
                if (here <= 0.0f) continue;

                for (auto& [toName, to] : settlements_) {
                    if (toName == fromName) continue;
                    if (localPrice(toName, itemId) / here < config.caravanPriceGap)
                        continue;
                    if (caravanEnRoute(fromName, toName, itemId)) continue;

                    int load = std::min(surplus, config.caravanMinLoad * 2);
                    sendCaravan(fromName, toName, itemId, load, simTime);
                    surplus -= load;
                    if (surplus < config.caravanMinLoad) break;
                }
            }
        }
    }

    bool caravanEnRoute(const std::string& from, const std::string& to,
                        ItemId item) const {
        for (const auto& c : caravans_)
            if (!c.arrived && c.from == from && c.to == to && c.item == item)
                return true;
        return false;
    }

    void logEvent(EconomyEvent ev) {
        events_.push_back(std::move(ev));
        if (static_cast<int>(events_.size()) > config.maxEventLog)
            events_.erase(events_.begin());
    }

    std::map<ItemId, Item>                  itemDB_;
    std::map<ItemId, float>                 consumptionRates_;
    std::map<std::string, SettlementEconomy> settlements_;
    std::map<std::string, ProductionRecipe> recipes_;
    std::vector<Producer>                   producers_;
    std::vector<Caravan>                    caravans_;
    std::vector<EconomyEvent>               events_;
    float                                   consumptionClock_ = 0.0f;
    uint32_t                                nextCaravanId_    = 1;
};

} // namespace npc
