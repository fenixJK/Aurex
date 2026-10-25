-- Routes use tiles for walking and milliseconds for airborne movement.
local M = {}
function M.walk(legs) return {kind="walk", legs=legs} end
function M.turn(key, count) return {kind="turn", key=key, count=count or 2} end
function M.observe(detector) return {kind="checkpoint", detector=detector, timeout=5000, age=2000, attempts=0} end
function M.append(path, steps) for _, step in ipairs(steps) do path[#path+1]=step end end
function M.scope(path,location,first)
    for i=first or #path,#path do path[i].scope=location end
    return path
end
function M.ramp(slot) return M.walk({{5,"forward"},{9.2*slot-4,"right"}}) end
function M.motion()
    local m={kind="motion", steps={}}
    function m:hold(keys, ms) self.steps[#self.steps+1]={keys=keys, ms=ms} end
    function m:walk(tiles, keys, jumping) self.steps[#self.steps+1]={tiles,keys[1],keys[2],jumping} end
    function m:finish() return self end
    return m
end
function M.actions(target, keys, ms, label)
    while ms>0 do
        local slice=math.min(ms,250)
        target[#target+1]={keys=keys,ms=slice,label=label}
        ms=ms-slice
    end
end
function M.cannon(slot)
    local mount=M.motion()
    mount:hold({"right","jump"},100); mount:walk(2,{"right"}); mount:walk(1.5,{"forward","right"})
    local step,attempts=1.5,9
    -- Bound the approach to the distance the reverse search can recover.
    local align={kind="checkpoint",detector="interaction.prompt",timeout=30000,age=250,attempts=attempts,
        path={{step*attempts,"right"}},nudge_path={{step,"left"}},use_speed=true}
    return {M.ramp(slot),mount:finish(),align}
end
function M.flight(slot)
    local path=M.cannon(slot)
    local launch=table.remove(path)
    launch.detector="transport.red-cannon"; launch.continuous=true; launch.completion="launch"
    launch.approach={}; launch.launch_prepare={}
    local f={path=path,held={},current=launch}
    function f:flush()
        assert(not self.double_jump,"Double jump requires a timed flight leg")
        if #self.current.approach>0 then self.path[#self.path+1]=self.current end
        self.current={kind="checkpoint",continuous=true,attempts=0,completion="movement",timeout=2000,approach={}}
        self.held={}
    end
    function f:aim(key,count)
        self.current.launch_prepare[#self.current.launch_prepare+1]=M.turn(key,count)
    end
    function f:launch_moving(key) self.current.launch_movement={key} end
    function f:hold(keys,ms)
        self.current.timeout=self.current.timeout+ms
        local actions=self.current.approach
        if self.double_jump then
            -- Space the taps within this leg, preserving its steering and duration.
            local gap=50
            assert(ms>gap,"Flight leg is too short for a double jump")
            actions[#actions+1]={keys=keys,ms=gap,label="Cannon flight",tap="jump"}
            ms=ms-gap
        end
        local first=#actions+1
        M.actions(actions,keys,ms,"Cannon flight")
        if self.double_jump then actions[first].tap="jump"; self.double_jump=nil end
        self.held=keys
    end
    -- Double jumps run inside the following timed leg; single taps stay at its boundary.
    function f:jump(count,keys)
        assert(not self.double_jump and (count==nil or count==1 or count==2),"Invalid flight jump sequence")
        if count==2 then
            assert(not keys,"Double jump uses the following flight leg's steering")
            self.double_jump=true
            return
        end
        self:hold(keys or self.held,1)
        local action=self.current.approach[#self.current.approach]
        action.tap="jump"
    end
    function f:steer(keys,ms,key,count)
        assert(ms>0 and not self.double_jump)
        local first=#self.current.approach+1
        self:hold(keys,ms); self.current.approach[first].tap=key; self.current.approach[first].tap_count=count
    end
    function f:turn(key,count)
        self:flush(); self.path[#self.path+1]=M.turn(key,count)
    end
    function f:walk(legs) self:flush(); self.path[#self.path+1]=M.walk(legs) end
    function f:finish() self:flush(); return self.path end
    return f
end
function M.upper_field()
    return M.walk({{31,"forward"},{7.8,"left"},{10,"backward"},{5,"right"},{1.5,"forward"},{60,"left"}})
end
function M.blue_hq(slot)
    local f=M.flight(slot)
    f:hold({"left"},700); f:jump(2); f:hold({"left"},4450); f:jump(1,{}); f:hold({},1000); f:turn("rotate_left",2)
    return f
end
function M.guild(slot,cannon)
    local path
    if cannon then
        local f=M.flight(slot)
        f:hold({"right","backward"},925); f:jump(2); f:hold({"right","backward"},2850)
        f:hold({"right"},1450); f:jump(1,{}); f:hold({},600)
        f:walk({{10,"forward","left"},{10,"left","forward"}}); path=f:finish()
    else
        path={M.ramp(slot),M.walk({{67.5,"backward","left"}}),M.turn("rotate_right",4),M.upper_field(),
            M.walk({{3.75,"right"},{38,"forward"}}),M.turn("rotate_left",4),M.walk({{14,"right"},{15,"forward","left"}})}
    end
    M.append(path,{M.walk({{1,"backward"},{25,"right"}}),M.turn("rotate_right",2),M.walk({{15,"forward"},{1,"forward","right"},{7,"forward"}})})
    return path
end
function M.upper_gate(pepper,planting)
    local ramp=M.motion()
    ramp:hold({"jump","right"},100); ramp:walk(2,{"right"}); ramp:walk(1.8,{"right","forward"})
    ramp:walk(30,{"right"}); ramp:hold({"jump"},300)
    local hop=M.motion(); hop:hold({"jump"},300)
    local climb=M.motion()
    climb:walk(11,{"forward","right"}); climb:hold({"forward","jump"},200)
    climb:hold({"forward"},1100); climb:hold({"forward","jump"},200); climb:walk(18,{"forward"})
    if pepper then
        climb:hold({"forward","jump"},200); climb:walk(20,{"forward"}); climb:walk(9,{"forward","right"})
        climb:hold({"forward","right","jump"},300); climb:walk(1,{"forward","right"}); climb:walk(33,{"right"})
        climb:hold({"right","jump"},300); climb:walk(planting and 6 or 4,{"right"})
        if planting then
            for i=1,2 do climb:hold({"right","rotate_right"},50); climb:hold({"right"},50) end
            climb:walk(30,{"right","forward"})
        end
    end
    local path={ramp:finish(),M.walk({{4,"right"},{5,"forward"},{3,"right"}}),hop:finish(),
        M.walk({{(pepper or not planting) and 6 or 5,"forward"},{2,"left","forward"},{8,"forward"}}),climb:finish()}
    if pepper and planting then path[#path+1]=M.walk({{10,"forward"},{10,"backward","left"},{4,"backward"}})
    elseif pepper then M.append(path,{M.turn("rotate_right",2),M.walk({{9,"forward"},{1.5,"right"}})})
    else path[#path+1]=M.walk({{planting and 7 or 13.5,"left"}}) end
    return path
end
return M
