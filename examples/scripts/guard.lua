-- guard.lua
-- Guard NPC behaviour scripted in Lua.
-- States: patrol → alert → combat → flee → recover

local FLEE_HP_THRESHOLD    = 0.25   -- flee below 25% HP
local RECOVER_HP_THRESHOLD = 0.60   -- return to patrol above 60% HP
local ALERT_MOOD_THRESHOLD = -0.30  -- mood below this triggers alert
local PATROL_SOCIAL_GAIN   = 0.5    -- social need gain per second on patrol

-- ── patrol ──────────────────────────────────────────────────────────────────

function guard_enter_patrol(npc)
    npc:log("Starting patrol route.")
    npc:addEmotion("Neutral", 0.4, 5.0)
end

function guard_update_patrol(npc, dt)
    -- Patrol boosts social need slightly (seeing people)
    npc:satisfyNeed("Social", PATROL_SOCIAL_GAIN * dt)

    -- Deteriorating mood → move to alert
    if npc:getMood() < ALERT_MOOD_THRESHOLD then
        npc:setState("alert")
    end

    -- Low HP from a prior fight → recover first
    if npc:getHealthPercent() < RECOVER_HP_THRESHOLD then
        npc:setState("recover")
    end
end

-- ── alert ───────────────────────────────────────────────────────────────────

function guard_enter_alert(npc)
    npc:log("Something feels wrong — staying sharp.")
    npc:addEmotion("Fearful", 0.5, 3.0)
    npc:depletNeed("Safety", 15.0)
end

function guard_update_alert(npc, dt)
    local mood = npc:getMood()
    local hp   = npc:getHealthPercent()

    if hp < FLEE_HP_THRESHOLD then
        npc:setState("flee")
        return
    end

    -- Under sustained threat → escalate to combat stance
    if mood < -0.55 then
        npc:setState("combat")
        return
    end

    -- Mood recovered → return to patrol
    if mood > 0.1 then
        npc:setState("patrol")
    end
end

-- ── combat ──────────────────────────────────────────────────────────────────

function guard_enter_combat(npc)
    npc:log("Enemy spotted! Drawing sword!")
    npc:addEmotion("Angry", 0.9, 4.0)
    npc:depletNeed("Safety", 30.0)
    npc:rememberEvent("Entered combat", -0.6)
end

function guard_update_combat(npc, dt)
    local hp   = npc:getHealthPercent()
    local mood = npc:getMood()

    if hp <= FLEE_HP_THRESHOLD then
        npc:setState("flee")
        return
    end

    -- Anger sustains combat readiness
    npc:addEmotion("Angry", 0.1 * dt, 0.5)

    -- Safety need depletes during combat
    npc:depletNeed("Safety", 5.0 * dt)

    -- If full HP and mood has recovered (threat passed) → return to patrol
    if hp >= 0.95 and mood > -0.2 then
        npc:setState("patrol")
    end
end

function guard_exit_combat(npc)
    npc:rememberEvent("Survived combat", 0.4)
    npc:log("Combat over.")
end

-- ── flee ─────────────────────────────────────────────────────────────────────

function guard_enter_flee(npc)
    npc:log("Retreating!")
    npc:addEmotion("Fearful", 0.95, 5.0)
end

function guard_update_flee(npc, dt)
    local hp = npc:getHealthPercent()

    -- Slowly recover HP while fleeing (adrenaline fades)
    npc:heal(2.0 * dt)
    npc:satisfyNeed("Safety", 3.0 * dt)

    if hp >= RECOVER_HP_THRESHOLD then
        npc:setState("recover")
    end
end

-- ── recover ──────────────────────────────────────────────────────────────────

function guard_enter_recover(npc)
    npc:log("Catching breath...")
    npc:addEmotion("Sad", 0.4, 3.0)
end

function guard_update_recover(npc, dt)
    npc:heal(5.0 * dt)
    npc:satisfyNeed("Safety", 8.0 * dt)
    npc:satisfyNeed("Comfort", 4.0 * dt)

    if npc:getHealthPercent() >= 0.90 and npc:getMood() > -0.1 then
        npc:log("Ready to patrol again.")
        npc:setState("patrol")
    end
end
