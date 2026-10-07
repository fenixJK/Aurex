-- Editable gathering pattern. Movement speed and stopping limits are handled by the host.
local steps = {}
return {
    configure = function(settings)
        steps = {}
        local size = (settings.scale or 100) / 100
        local reps = settings.repetitions or 1
        local width = settings.width or 3
        local facingcorner = (settings.facing or 0) % 2
        local fb = settings['invert-fb'] and 'backward' or 'forward'
        local back = settings['invert-fb'] and 'forward' or 'backward'
        local lr = settings['invert-lr'] and 'right' or 'left'
        local side = settings['invert-lr'] and 'left' or 'right'
        local held = {}
        local function down(key) held[key] = true end
        local function up(key) held[key] = nil end
        local function walk(tiles)
            local keys = {}
            for _, key in ipairs({'forward', 'backward', 'left', 'right'}) do
                if held[key] then keys[#keys + 1] = key end
            end
            assert(#keys >= 1 and #keys <= 2, 'Invalid pattern direction')
            steps[#steps + 1] = {kind = 'walk', direction = keys[1], second = keys[2], tiles = tiles, tool = true}
        end
        local function pause(ms)
            steps[#steps + 1] = {kind = 'press', key = 'primary', milliseconds = ms}
        end
        for index = 1, reps do
            down(lr)
            walk(4 * size)
            up(lr)
            down(fb)
            walk(2 * size)
            up(fb)
            down(side)
            walk(8 * size)
            up(side)
            down(fb)
            walk(2 * size)
            up(fb)
            down(lr)
            walk(8 * size)
            up(lr)
            down(side)
            down(back)
            walk(math.sqrt(2.)*8*size)
            up(side)
            up(back)
            down(lr)
            walk(8 * size)
            up(lr)
            down(fb)
            walk(2 * size)
            up(fb)
            down(side)
            walk(8 * size)
            up(side)
            down(fb)
            walk(6.7 * size)
            up(fb)
            down(lr)
            walk(8 * size)
            up(lr)
            down(back)
            walk(2 * size)
            up(back)
            down(side)
            walk(8 * size)
            up(side)
            down(back)
            walk(2 * size)
            up(back)
            down(lr)
            walk(8 * size)
            up(lr)
            down(back)
            walk(2 * size)
            up(back)
            down(side)
            walk(8 * size)
            up(side)
            down(back)
            walk(3 * size)
            up(back)
            down(lr)
            walk(8 * size)
            up(lr)
            down(fb)
            down(side)
            walk(math.sqrt(2.)*4*size)
            up(fb)
            up(side)
        end
        assert(next(held) == nil, 'Pattern retained a direction')
        assert(#steps > 0 and #steps <= 4096, 'Invalid pattern size')
        steps[#steps + 1] = {kind = 'cycle_end'}
        return {}
    end,
    next = function(view)
        return steps[view.completed_steps % #steps + 1]
    end
}
