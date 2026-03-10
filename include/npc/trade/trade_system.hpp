#pragma once

#include "../core/types.hpp"
#include "../personality/personality_traits.hpp"
#include <string>
#include <map>
#include <vector>
#include <deque>
#include <optional>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace npc {

// ─── Trade Config ─────────────────────────────────────────────────────────────
struct TradeConfig {
    float defaultBuyMarkup        = 1.3f;
    float defaultSellMarkdown     = 0.6f;
    int   scarcityThreshold       = 2;
    float scarcityPriceIncrease   = 0.5f;
    float barterFairnessRatio     = 0.8f;
    float maxRelationshipDiscount = 0.3f;
    float outOfStockMul           = 2.0f;
    float lowStockMul             = 1.5f;
    int   lowStockThreshold       = 2;
    float medStockMul             = 1.1f;
    int   medStockThreshold       = 5;
    float highStockMul            = 0.8f;
    int   highStockThreshold      = 20;

    // Demand feedback
    float demandWindowHours       = 24.0f;  // rolling window for velocity tracking
    float demandHighThreshold     = 10.0f;  // sales/window → price rises
    float demandLowThreshold      = 2.0f;   // sales/window → price falls
    float demandHighMul           = 1.25f;
    float demandLowMul            = 0.85f;

    // Bargaining
    int   bargainMaxRoundsBase    = 4;      // modified by patience
    float bargainGreedFloor       = 0.15f;  // max concession ratio from list price
};

// ─── Item ─────────────────────────────────────────────────────────────────────
struct Item {
    ItemId       id;
    std::string  name;
    ItemCategory category;
    float        basePrice = 10.0f;
    float        weight    = 1.0f;
};

// ─── Inventory ────────────────────────────────────────────────────────────────
class Inventory {
public:
    explicit Inventory(float maxWeight = 100.0f, float gold = 50.0f)
        : maxWeight_(maxWeight), gold_(gold) {}

    bool addItem(ItemId id, int quantity = 1) {
        items_[id] += quantity;
        return true;
    }
    bool removeItem(ItemId id, int quantity = 1) {
        auto it = items_.find(id);
        if (it == items_.end() || it->second < quantity) return false;
        it->second -= quantity;
        if (it->second <= 0) items_.erase(it);
        return true;
    }
    bool hasItem(ItemId id, int quantity = 1) const {
        auto it = items_.find(id);
        return it != items_.end() && it->second >= quantity;
    }
    int getQuantity(ItemId id) const {
        auto it = items_.find(id);
        return (it != items_.end()) ? it->second : 0;
    }

    float gold() const { return gold_; }
    void  addGold(float amount) { gold_ += amount; }
    bool  spendGold(float amount) {
        if (gold_ < amount) return false;
        gold_ -= amount;
        return true;
    }
    int totalItems() const {
        int total = 0;
        for (const auto& [id, qty] : items_) total += qty;
        return total;
    }
    const std::map<ItemId, int>& items() const { return items_; }
    float maxWeight() const { return maxWeight_; }

private:
    std::map<ItemId, int> items_;
    float maxWeight_;
    float gold_;
};

// ─── Price History ────────────────────────────────────────────────────────────
struct PriceRecord {
    float timestamp;
    float price;
    int   quantity;
};

// ─── Sale Record (for demand velocity) ───────────────────────────────────────
struct SaleRecord {
    float timestamp;
    int   quantity;
};

// ─── Bargain Session ──────────────────────────────────────────────────────────
struct BargainSession {
    EntityId npcId;
    ItemId   itemId;
    int      quantity;
    bool     playerBuying;    // true = player buys from NPC

    float listPrice;          // NPC public asking/buying price
    float npcTarget;          // NPC's private acceptable limit
    float npcCounter;         // NPC's current counter-offer
    float playerOffer;        // last player offer

    int   roundsElapsed = 0;
    int   maxRounds;
    bool  concluded     = false;
    bool  accepted      = false;
};

struct BargainResult {
    enum class Status { CounterOffer, Accepted, Rejected, AlreadyConcluded };
    Status      status;
    float       npcPrice;     // NPC's counter or final agreed price
    std::string message;
};

// ─── Merchant Route ───────────────────────────────────────────────────────────
struct RouteWaypoint {
    std::string          locationName;
    float                x            = 0.0f;
    float                y            = 0.0f;
    float                stayDuration = 4.0f;  // game hours
    std::vector<ItemId>  buyHere;               // items to stock up on at this stop
    std::vector<ItemId>  sellHere;              // items to offload at this stop
};

struct MerchantRoute {
    std::string                name;
    std::vector<RouteWaypoint> waypoints;
    int                        currentWaypoint  = 0;
    float                      timeAtWaypoint   = 0.0f;  // hours spent at current stop
    float                      travelProgress   = 0.0f;  // 0-1 between waypoints
    float                      travelSpeedHours = 2.0f;  // hours per waypoint leg

    bool isActive() const { return !waypoints.empty(); }

    const RouteWaypoint* current() const {
        if (waypoints.empty()) return nullptr;
        return &waypoints[currentWaypoint % waypoints.size()];
    }
    const RouteWaypoint* next() const {
        if (waypoints.size() < 2) return nullptr;
        return &waypoints[(currentWaypoint + 1) % waypoints.size()];
    }
};

// ─── TradeSystem ──────────────────────────────────────────────────────────────
class TradeSystem {
public:
    Inventory   inventory{200.0f, 100.0f};
    TradeConfig tradeConfig;
    float       buyMarkup;
    float       sellMarkdown;
    float       scarcityMod_    = 1.0f;
    float       relDiscountMod_ = 1.0f;

    TradeSystem() : TradeSystem(TradeConfig{}) {}
    explicit TradeSystem(const TradeConfig& cfg)
        : tradeConfig(cfg),
          buyMarkup(cfg.defaultBuyMarkup),
          sellMarkdown(cfg.defaultSellMarkdown) {}

    // ── Personality wiring ────────────────────────────────────────────────────
    void applyPersonality(float buyMarkupMul, float sellMarkdownMul,
                          float scarcityMul, float relDiscountMul) {
        buyMarkup     *= buyMarkupMul;
        sellMarkdown  *= sellMarkdownMul;
        scarcityMod_   = scarcityMul;
        relDiscountMod_= relDiscountMul;
    }

    // Convenience: wire directly from PersonalityTraits
    void applyPersonality(const PersonalityTraits& p) {
        applyPersonality(
            p.buyMarkupMultiplier(),
            p.sellMarkdownMultiplier(),
            p.scarcityMultiplier(),
            p.relationshipDiscountMultiplier()
        );
        greed_   = p.greed;
        patience_= p.patience;
    }

    // ── Item registry ─────────────────────────────────────────────────────────
    void registerItem(Item item) { itemDB_[item.id] = std::move(item); }

    const Item* getItemInfo(ItemId id) const {
        auto it = itemDB_.find(id);
        return (it != itemDB_.end()) ? &it->second : nullptr;
    }

    // ── Pricing ───────────────────────────────────────────────────────────────
    float getPrice(ItemId id, bool playerBuying) const {
        auto* item = getItemInfo(id);
        if (!item) return 0.0f;

        float price = item->basePrice;

        // Static supply/demand modifier
        auto modIt = priceModifiers_.find(id);
        if (modIt != priceModifiers_.end()) price *= modIt->second;

        // Dynamic demand feedback
        auto demIt = demandModifiers_.find(id);
        if (demIt != demandModifiers_.end()) price *= demIt->second;

        // Scarcity (selling side)
        int stock = inventory.getQuantity(id);
        if (playerBuying && stock <= tradeConfig.scarcityThreshold)
            price *= (1.0f + tradeConfig.scarcityPriceIncrease * scarcityMod_);

        price *= playerBuying ? buyMarkup : sellMarkdown;
        price *= (1.0f - relationshipDiscount_ * relDiscountMod_);

        return std::round(price * 100.0f) / 100.0f;
    }

    // ── Basic trade ───────────────────────────────────────────────────────────
    struct TradeResult {
        bool        success;
        float       price;
        std::string message;
    };

    TradeResult sell(ItemId id, int quantity, Inventory& buyerInv) {
        if (!inventory.hasItem(id, quantity))
            return {false, 0.0f, "Out of stock."};
        float total = getPrice(id, true) * quantity;
        if (buyerInv.gold() < total)
            return {false, total, "Not enough gold."};

        inventory.removeItem(id, quantity);
        buyerInv.addItem(id, quantity);
        buyerInv.spendGold(total);
        inventory.addGold(total);
        recordSaleEvent(id, quantity, total / quantity, currentTime_);
        recordPrice(id, total / quantity, quantity);

        auto* item = getItemInfo(id);
        return {true, total, "Sold " + std::to_string(quantity) + "x " +
                (item ? item->name : "item") + " for " +
                std::to_string(static_cast<int>(total)) + " gold."};
    }

    TradeResult buy(ItemId id, int quantity, Inventory& sellerInv) {
        if (!sellerInv.hasItem(id, quantity))
            return {false, 0.0f, "You don't have that item."};
        float total = getPrice(id, false) * quantity;
        if (inventory.gold() < total)
            return {false, total, "I can't afford that right now."};

        sellerInv.removeItem(id, quantity);
        inventory.addItem(id, quantity);
        sellerInv.addGold(total);
        inventory.spendGold(total);
        recordPrice(id, total / quantity, quantity);

        auto* item = getItemInfo(id);
        return {true, total, "Bought " + std::to_string(quantity) + "x " +
                (item ? item->name : "item") + " for " +
                std::to_string(static_cast<int>(total)) + " gold."};
    }

    bool barter(ItemId offered, int offeredQty,
                ItemId requested, int requestedQty,
                Inventory& other) {
        float offeredValue   = getPrice(offered,   false) * offeredQty;
        float requestedValue = getPrice(requested, true)  * requestedQty;
        if (offeredValue < requestedValue * tradeConfig.barterFairnessRatio) return false;
        if (!other.hasItem(offered, offeredQty))        return false;
        if (!inventory.hasItem(requested, requestedQty)) return false;

        other.removeItem(offered,    offeredQty);
        inventory.addItem(offered,   offeredQty);
        inventory.removeItem(requested, requestedQty);
        other.addItem(requested,     requestedQty);
        return true;
    }

    // ── Bargaining ────────────────────────────────────────────────────────────
    // Start a bargain session. playerBuying=true means player wants to buy from NPC.
    BargainSession initiateBargain(EntityId npcId, ItemId itemId,
                                   int quantity, bool playerBuying) const {
        BargainSession s;
        s.npcId        = npcId;
        s.itemId       = itemId;
        s.quantity     = quantity;
        s.playerBuying = playerBuying;
        s.listPrice    = getPrice(itemId, playerBuying);

        // Greedy NPC has a tighter floor (won't concede much)
        // patience determines how many rounds NPC will entertain
        float maxConcession = tradeConfig.bargainGreedFloor * (1.0f - greed_ * 0.6f + 0.3f);

        if (playerBuying) {
            // Player wants to pay less → NPC target is list * (1 - maxConcession)
            s.npcTarget  = s.listPrice * (1.0f - maxConcession);
        } else {
            // Player wants to sell for more → NPC target is list * (1 + maxConcession)
            s.npcTarget  = s.listPrice * (1.0f + maxConcession);
        }

        s.npcCounter = s.listPrice;
        s.playerOffer= 0.0f;
        s.maxRounds  = tradeConfig.bargainMaxRoundsBase +
                       static_cast<int>(patience_ * 4.0f);  // 4-8 rounds
        return s;
    }

    // Player submits an offer. Returns NPC's response.
    BargainResult playerOffer(BargainSession& s, float offerPerUnit) {
        if (s.concluded)
            return {BargainResult::Status::AlreadyConcluded, s.npcCounter, "Deal already concluded."};

        s.playerOffer = offerPerUnit;
        ++s.roundsElapsed;

        bool playerOfferAcceptable = s.playerBuying
            ? (offerPerUnit >= s.npcTarget)
            : (offerPerUnit <= s.npcTarget);

        // Accept if offer meets the NPC's minimum
        if (playerOfferAcceptable) {
            s.npcCounter = offerPerUnit;
            s.concluded  = true;
            s.accepted   = true;
            return {BargainResult::Status::Accepted, offerPerUnit,
                    "Deal. " + std::to_string(static_cast<int>(offerPerUnit)) + " gold each."};
        }

        // Patience exhausted → final take-it-or-leave-it
        if (s.roundsElapsed >= s.maxRounds) {
            s.concluded = true;
            s.accepted  = false;
            return {BargainResult::Status::Rejected, s.npcCounter,
                    "I won't go any further. That's my final price."};
        }

        // NPC makes concession: moves counter toward player's offer, but not past target
        float midpoint = (s.npcCounter + offerPerUnit) * 0.5f;
        if (s.playerBuying)
            s.npcCounter = std::max(midpoint, s.npcTarget);
        else
            s.npcCounter = std::min(midpoint, s.npcTarget);

        // Greedy NPC narrows concession pace
        float concessionRatio = 1.0f - greed_ * 0.5f;  // 0.5–1.0
        float delta    = (s.listPrice - s.npcCounter) * (1.0f - concessionRatio);
        s.npcCounter  += s.playerBuying ? -delta : delta;

        return {BargainResult::Status::CounterOffer, s.npcCounter,
                "How about " + std::to_string(static_cast<int>(s.npcCounter)) + " gold each?"};
    }

    // Finalize an accepted bargain session (execute the actual trade)
    TradeResult concludeBargain(const BargainSession& s, Inventory& otherInv) {
        if (!s.concluded || !s.accepted)
            return {false, 0.0f, "Bargain not accepted."};

        float total = s.npcCounter * s.quantity;
        if (s.playerBuying) {
            if (!inventory.hasItem(s.itemId, s.quantity))
                return {false, total, "Out of stock."};
            if (otherInv.gold() < total)
                return {false, total, "Not enough gold."};
            inventory.removeItem(s.itemId, s.quantity);
            otherInv.addItem(s.itemId, s.quantity);
            otherInv.spendGold(total);
            inventory.addGold(total);
        } else {
            if (!otherInv.hasItem(s.itemId, s.quantity))
                return {false, total, "Seller lacks the item."};
            if (inventory.gold() < total)
                return {false, total, "I can't afford that."};
            otherInv.removeItem(s.itemId, s.quantity);
            inventory.addItem(s.itemId, s.quantity);
            inventory.spendGold(total);
            otherInv.addGold(total);
        }

        recordSaleEvent(s.itemId, s.quantity, s.npcCounter, currentTime_);
        recordPrice(s.itemId, s.npcCounter, s.quantity);

        auto* item = getItemInfo(s.itemId);
        return {true, total, "Agreed on " + std::to_string(static_cast<int>(total)) + " gold."};
    }

    // ── Price history ─────────────────────────────────────────────────────────
    void recordPrice(ItemId id, float price, int quantity = 1) {
        priceHistory_[id].push_back({currentTime_, price, quantity});
        // Keep last 50 records per item
        auto& h = priceHistory_[id];
        if (h.size() > 50) h.erase(h.begin(), h.begin() + static_cast<int>(h.size()) - 50);
    }

    // Average price over the last N records (0 = all)
    float averagePrice(ItemId id, int lastN = 0) const {
        auto it = priceHistory_.find(id);
        if (it == priceHistory_.end() || it->second.empty()) return 0.0f;
        const auto& h = it->second;
        int start = (lastN > 0 && static_cast<int>(h.size()) > lastN)
                    ? static_cast<int>(h.size()) - lastN : 0;
        float sum = 0.0f; int count = 0;
        for (int i = start; i < static_cast<int>(h.size()); ++i) {
            sum += h[i].price * h[i].quantity;
            count += h[i].quantity;
        }
        return count > 0 ? sum / count : 0.0f;
    }

    const std::vector<PriceRecord>* priceHistory(ItemId id) const {
        auto it = priceHistory_.find(id);
        return (it != priceHistory_.end()) ? &it->second : nullptr;
    }

    // ── Supply/demand feedback ────────────────────────────────────────────────
    void recordSaleEvent(ItemId id, int qty, float /*price*/, float time) {
        saleHistory_[id].push_back({time, qty});
    }

    // Recompute demand modifiers from recent sales velocity
    void updateDemand(float currentTime) {
        currentTime_ = currentTime;
        float window = tradeConfig.demandWindowHours;

        for (auto& [id, records] : saleHistory_) {
            // Evict old records
            records.erase(
                std::remove_if(records.begin(), records.end(),
                    [&](const SaleRecord& r){ return r.timestamp < currentTime - window; }),
                records.end());

            int totalSold = 0;
            for (const auto& r : records) totalSold += r.quantity;

            float& mod = demandModifiers_[id];
            if (static_cast<float>(totalSold) >= tradeConfig.demandHighThreshold)
                mod = tradeConfig.demandHighMul;
            else if (static_cast<float>(totalSold) <= tradeConfig.demandLowThreshold)
                mod = tradeConfig.demandLowMul;
            else
                mod = 1.0f;
        }
    }

    void updatePrices() {
        for (auto& [id, item] : itemDB_) {
            int   stock    = inventory.getQuantity(id);
            float modifier = 1.0f;
            if      (stock == 0)                               modifier = tradeConfig.outOfStockMul;
            else if (stock <= tradeConfig.lowStockThreshold)   modifier = tradeConfig.lowStockMul;
            else if (stock <= tradeConfig.medStockThreshold)   modifier = tradeConfig.medStockMul;
            else if (stock >= tradeConfig.highStockThreshold)  modifier = tradeConfig.highStockMul;
            priceModifiers_[id] = modifier;
        }
    }

    // ── Merchant routes ───────────────────────────────────────────────────────
    void assignRoute(MerchantRoute route) { route_ = std::move(route); }
    const MerchantRoute* route() const { return route_.isActive() ? &route_ : nullptr; }
    MerchantRoute*       route()       { return route_.isActive() ? &route_ : nullptr; }

    // Advance the merchant along their route. Returns true when arriving at a new waypoint.
    bool updateRoute(float dt) {
        if (!route_.isActive()) return false;

        const RouteWaypoint* cur = route_.current();
        if (!cur) return false;

        // Dwelling at current waypoint
        if (route_.timeAtWaypoint < cur->stayDuration) {
            route_.timeAtWaypoint += dt;
            return false;
        }

        // Travelling to next waypoint
        route_.travelProgress += dt / route_.travelSpeedHours;
        if (route_.travelProgress < 1.0f) return false;

        // Arrived at next waypoint
        route_.travelProgress  = 0.0f;
        route_.timeAtWaypoint  = 0.0f;
        route_.currentWaypoint = (route_.currentWaypoint + 1) %
                                  static_cast<int>(route_.waypoints.size());
        return true;
    }

    // List items the merchant should trade at the current waypoint
    std::vector<ItemId> itemsToSellHere() const {
        if (const auto* wp = route_.current()) return wp->sellHere;
        return {};
    }
    std::vector<ItemId> itemsToBuyHere() const {
        if (const auto* wp = route_.current()) return wp->buyHere;
        return {};
    }

    // ── Misc ──────────────────────────────────────────────────────────────────
    void setRelationshipDiscount(float discount) {
        relationshipDiscount_ = std::clamp(discount, 0.0f, tradeConfig.maxRelationshipDiscount);
    }
    void setCurrentTime(float t) { currentTime_ = t; }

    const std::map<ItemId, Item>& itemDB() const { return itemDB_; }

    float demandModifier(ItemId id) const {
        auto it = demandModifiers_.find(id);
        return (it != demandModifiers_.end()) ? it->second : 1.0f;
    }

private:
    std::map<ItemId, Item>                    itemDB_;
    std::map<ItemId, float>                   priceModifiers_;
    std::map<ItemId, float>                   demandModifiers_;
    std::map<ItemId, std::vector<PriceRecord>> priceHistory_;
    std::map<ItemId, std::vector<SaleRecord>>  saleHistory_;

    float relationshipDiscount_ = 0.0f;
    float greed_                = 0.5f;  // cached from PersonalityTraits
    float patience_             = 0.5f;
    float currentTime_          = 0.0f;

    MerchantRoute route_;
};

} // namespace npc
