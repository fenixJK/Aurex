local h=require("helpers")
local walk,turn=h.walk,h.turn
local function lock_step()
    return {kind="checkpoint",attempts=0,completion="movement",approach={{label="Toggle shift lock",keys={"shift_lock"},ms=50}}}
end
local function shrine(slot)
    local path={walk({{5,"forward"},{9.2 * slot - 4,"right"}})}
    local gate=h.upper_gate(false,false)
    table.remove(gate)
    h.append(path,gate)
    local ledges=h.motion()
    ledges:hold({"forward","jump"},200)
    ledges:hold({"forward"},200)
    ledges:walk(21,{"forward","right"})
    ledges:hold({"jump"},300)
    ledges:walk(3,{"forward"})
    ledges:walk(19.5,{"right"})
    ledges:hold({"jump"},300)
    ledges:walk(3,{"right"})
    path[#path+1]=ledges:finish()
    path[#path+1]=turn("rotate_right",2)
    path[#path+1]=walk({{13,"forward","right"},{10,"right"}})
    local lock=lock_step()
    path[#path+1]=lock
    local lift=h.motion()
    lift:hold({"jump"},2000)
    path[#path+1]=lift:finish()
    path[#path+1]=lock
    local landing=h.motion()
    landing:hold({},1000)
    landing:hold({"jump"},120)
    landing:hold({"jump","right"},130)
    landing:walk(13,{"right"})
    landing:hold({},750)
    path[#path+1]=landing:finish()
    return path
end
return function(p)
    if p.kind=="wind-shrine" then return shrine(p.slot) end
    assert(p.kind=="blender","Unknown item station")
    local path=h.guild(p.slot,p.cannon)
    path[#path+1]=walk({{3,"backward"},{26,"left"},{1,"forward","left"}})
    return path
end
