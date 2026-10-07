local h=require("helpers")
local walk,turn=h.walk,h.turn
local function approach(slot) return {h.ramp(slot)} end
local function normal(slot,cannon)
    if cannon then
        local flight=h.flight(slot)
        flight:hold({},2500)
        flight:walk({{30,"forward"},{2,"backward"},{22,"left"},{12,"right"},{3,"left"},{5,"forward"}})
        flight:turn("rotate_right",2)
        return flight:finish()
    end
    local path=approach(slot)
    local ledge=h.motion()
    ledge:hold({"forward"},200)
    ledge:hold({"forward","jump"},100)
    ledge:hold({"forward"},800)
    h.append(path,{walk({{69,"backward","left"}}),turn("rotate_right",4),walk({{30,"forward"},{20,"forward","right"}}),turn("rotate_right",2),walk({{43.5,"forward"},{16,"right"}}),ledge:finish(),turn("rotate_left",2),walk({{29.25,"forward"},{15,"forward","left"},{8,"left"},{15,"forward","left"},{3.5,"right"},{11,"backward"}}),turn("rotate_left",2)})
    return path
end
local function mega(slot,cannon)
    local path=h.guild(slot,cannon)
    path[#path+1]=walk({{4,"backward"},{3,"left"}})
    return path
end
local function extreme(slot)
    local path=approach(slot)
    local gate=h.upper_gate(false,false)
    table.remove(gate)
    h.append(path,gate)
    local entrance=h.motion()
    entrance:hold({"forward","jump"},200)
    entrance:walk(26,{"forward"})
    entrance:hold({"right","jump"},200)
    entrance:walk(15,{"right"})
    entrance:walk(2,{"forward"})
    path[#path+1]=entrance:finish()
    return path
end
local function night(slot,cannon)
    local path={}
    if cannon then
        local flight=h.flight(slot)
        flight:hold({"left","backward"},1400)
        flight:jump(2)
        flight:hold({"left","backward"},1100)
        flight:hold({"backward"},650)
        flight:jump()
        flight:turn("rotate_right",4)
        flight:hold({},1500)
        flight:walk({{4,"right","forward"},{23,"forward"},{9,"left"}})
        path = flight:finish()
    else
        path = approach(slot)
        h.append(path,{walk({{67.5,"backward","left"}}),turn("rotate_right",4),walk({{31.5,"forward"},{9,"left"},{9,"backward"},{58.5,"left"}}),walk({{49.5,"forward"},{3.375,"left"},{36,"forward"},{54,"right"}}),walk({{54,"backward"},{58.5,"right"},{3,"left"},{57,"forward"},{16,"left"}})})
    end
    path[#path+1]=walk({{3,"forward"},{8,"left"},{3.6,"right"},{41,"forward"}})
    local ledge=h.motion()
    ledge:hold({"jump"},100)
    ledge:walk(8.8,{"forward"})
    h.append(path,{ledge:finish(),turn("rotate_right",2),walk({{25.6,"forward"}})})
    local function hop(legs)
        local motion=h.motion(); motion:hold({"jump"},100)
        for _,leg in ipairs(legs) do motion:walk(leg[1],{leg[2],leg[3]}) end
        local checkpoint=motion:finish(); checkpoint.minimum_duration=1400; path[#path+1]=checkpoint
    end
    hop({{5,"forward"}})
    path[#path+1]=turn("rotate_right",1)
    path[#path+1]=walk({{2,"forward"}})
    hop({{5,"forward"}})
    path[#path+1]=turn("rotate_right",1)
    path[#path+1]=walk({{1.5,"forward","left"},{2,"forward"}})
    hop({{2.5,"forward"},{2.5,"forward","left"}})
    path[#path+1]=walk({{2,"forward"}})
    hop({{5,"forward"}})
    path[#path+1]=walk({{2,"forward"}})
    hop({{2,"forward","right"},{3,"forward"}})
    path[#path+1]=turn("rotate_right",1)
    path[#path+1]=walk({{2,"forward"}})
    hop({{2.5,"forward","left"},{2,"forward"}})
    path[#path+1]=walk({{2,"forward"}})
    hop({{5,"forward"}})
    path[#path+1]=walk({{2,"forward"}})
    hop({{4,"forward"}})
    path[#path+1]=turn("rotate_right",1)
    path[#path+1]=walk({{2,"forward"}})
    hop({{8,"forward"}})
    path[#path+1]=walk({{4,"forward"},{8,"forward","left"},{7,"right"}})
    path[#path+1]=turn("rotate_left",2)
    path[#path+1]=walk({{3,"backward","right"},{10,"right"},{6,"forward","right"}})
    hop({{3,"forward","right"},{1,"right"}})
    return path
end
return function(p)
    if p.kind=="basic" then return normal(p.slot,p.cannon)
elseif p.kind=="mega" then return mega(p.slot,p.cannon)
elseif p.kind=="night" then return night(p.slot,p.cannon)
elseif p.kind=="extreme" then return extreme(p.slot) end
error("Unknown Memory Match")
end
