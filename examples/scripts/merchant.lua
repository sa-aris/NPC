-- merchant.lua
-- Merchant NPC behaviour scripted in Lua.
-- States: open_shop → lunch → closed → worried

local OPEN_HOUR     = 7.0    -- shop opens
local LUNCH_HOUR    = 12.5   -- lunch break starts
local REOPEN_HOUR   = 13.5   -- back from lunch
local CLOSE_HOUR    = 19.0   -- closing time

local WORRY_MOOD_THRESHOLD = -0.4  -- mood below this triggers worry state

-- ── open_shop ────────────────────────────────────────────────────────────────

function merchant_enter_open(npc)
    npc:log("Opening shop. Ready for business!")
    npc:addEmotion("Happy", 0.5, 4.0)
    npc:setBB("greeted_today", false)
end

function merchant_update_open(npc, dt)
    local hour = world_hour()

    -- Greet customers once per day
    if not npc:getBB("greeted_today") then
        npc:log("Morning! Fine goods for sale!")
        npc:setBB("greeted_today", true)
    end

    -- Social and fun needs satisfied while trading
    npc:satisfyNeed("Social", 1.5 * dt)
    npc:satisfyNeed("Fun",    0.5 * dt)
    npc:depletNeed("Hunger",  0.8 * dt)

    -- Transitions
    if hour >= LUNCH_HOUR and hour < REOPEN_HOUR then
        npc:setState("lunch")
        return
    end

    if hour >= CLOSE_HOUR then
        npc:setState("closed")
        return
    end

    if npc:getMood() < WORRY_MOOD_THRESHOLD then
        npc:setState("worried")
    end
end

-- ── lunch ────────────────────────────────────────────────────────────────────

function merchant_enter_lunch(npc)
    npc:log("Lunch break. Back soon!")
end

function merchant_update_lunch(npc, dt)
    npc:satisfyNeed("Hunger", 10.0 * dt)
    npc:satisfyNeed("Thirst", 8.0  * dt)
    npc:satisfyNeed("Social", 3.0  * dt)

    local hour = world_hour()
    if hour >= REOPEN_HOUR then
        npc:setState("open_shop")
    end
end

-- ── closed ───────────────────────────────────────────────────────────────────

function merchant_enter_closed(npc)
    npc:log("Closing up for the night. Come back tomorrow.")
    npc:addEmotion("Neutral", 0.3, 2.0)
    npc:rememberEvent("Good trading day", 0.3)
end

function merchant_update_closed(npc, dt)
    npc:satisfyNeed("Sleep",   5.0 * dt)
    npc:satisfyNeed("Comfort", 3.0 * dt)

    local hour = world_hour()
    if hour >= OPEN_HOUR and hour < LUNCH_HOUR then
        npc:setState("open_shop")
    end
end

-- ── worried ──────────────────────────────────────────────────────────────────

function merchant_enter_worried(npc)
    npc:log("Something's wrong… I can't focus on business.")
    npc:addEmotion("Fearful", 0.6, 3.0)
    npc:rememberEvent("Felt unsafe at shop", -0.5)
end

function merchant_update_worried(npc, dt)
    npc:satisfyNeed("Safety", 4.0 * dt)

    -- Recover once mood improves
    if npc:getMood() > 0.0 then
        npc:log("Feeling safer now. Back to business.")
        npc:setState("open_shop")
        return
    end

    local hour = world_hour()
    if hour >= CLOSE_HOUR then
        npc:setState("closed")
    end
end
