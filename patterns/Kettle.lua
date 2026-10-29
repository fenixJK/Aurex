-- Kettle by Dully176, AHK version 0.1.0 (9/6/2025), converted for Aurex.
-- Pine Tree: upper-left start (Natro distance 7), 180° facing, Shift Lock on, drift correction off.
-- Original settings: medium size (100%), 1 repetition, 10 minutes, 95% bag, Walk return.
-- Each repetition keeps the original two passes and their wall corrections.
-- Width is unused. Speed, gathering limits and camera restoration belong to the host.
local steps = {}
return {
    configure = function(settings)
        steps = {}
        local size = (settings.scale or 100) / 100
        local forward = settings['invert-fb'] and 'backward' or 'forward'
        local back = settings['invert-fb'] and 'forward' or 'backward'
        local left = settings['invert-lr'] and 'right' or 'left'
        local right = settings['invert-lr'] and 'left' or 'right'
        local corner = 0.3
        local diamond_offset, diamond_drift = 1.5, 2
        local honey_offset, honey_drift = 1.5, 2
        local long = 6.25
        local short, half = long / 4, long / 2
        local function pause(ms)
            steps[#steps + 1] = {kind = 'press', key = 'primary', milliseconds = ms}
        end
        local function walk(tiles, direction, second)
            steps[#steps + 1] = {kind = 'walk', direction = direction, second = second, tiles = tiles * size, tool = true}
            pause(20)
        end
        local function sequence(moves)
            for _, move in ipairs(moves) do walk(move[1], move[2], move[3]) end
        end
        local function camera(key, count, settle)
            steps[#steps + 1] = {kind = 'camera', key = key, steps = count}
            pause(settle or 35)
        end
        for repetition = 1, (settings.repetitions or 1) do
            camera('pitch-up', 4, 45)
            for pass = 1, 2 do
                sequence({{half, left}, {short, forward}, {long, right}, {short, forward}, {long, left}})
                camera('rotate-left', 1)
                walk(long, back, left)
                if diamond_offset > 0 then
                    walk(diamond_offset + diamond_drift, back, left)
                    walk(diamond_offset, forward, right)
                end
                sequence({{short, back, right}, {long, forward, right}, {short, back, right}, {half, back, left}})
                camera('rotate-right', 1)
                sequence({{half, back}, {short, right}, {long, forward}, {short, right}})
                if honey_offset > 0 then
                    walk(honey_offset + honey_drift, right)
                    walk(honey_offset, left)
                end
                walk(long, back)
                camera('rotate-left', 2)
                sequence({{long, forward}, {short, right}, {long, back}, {short, right}, {half, forward}})
                camera('rotate-right', 2)
                walk(half, forward, left)
                sequence({{short, forward, right}, {long, back, right}, {short, forward, right}, {long, forward, left}})
                camera('rotate-left', 1)
                sequence({{long, left}, {short + corner, back}, {long, right}, {short + corner, back}, {half, left}})
                camera('rotate-right', 1)
                sequence({{half, back, left}, {short, back, right}, {long, forward, right}, {short, back, right}, {long, back, left}})
                camera('rotate-left', 2)
                sequence({{long, forward, right}, {short, back, right}, {long, back, left}, {short, back, right}})
                walk(half, forward, right)
                camera('rotate-right', 2)
            end
            camera('pitch-down', 4, 45)
        end
        steps[#steps + 1] = {kind = 'cycle_end'}
        return {}
    end,
    next = function(view)
        return steps[view.completed_steps % #steps + 1]
    end
}
