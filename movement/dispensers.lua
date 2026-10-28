local h=require("helpers")
local walk,turn=h.walk,h.turn
local function treat_ledge()
    local steps={{label="Approach ledge",keys={"forward"},ms=200},{label="Jump onto ledge",keys={"forward","jump"},ms=100}}
    for i=1,4 do steps[#steps+1]={label="Finish jump",keys={"forward"},ms=200} end
    return {kind="checkpoint",attempts=0,completion="movement",continuous=true,approach=steps}
end
local function clock_settle()
    return {kind="checkpoint",attempts=0,completion="movement",approach={
    {label="Ladder alignment",keys={},ms=250},{label="Ladder alignment",keys={},ms=250}}}
end
local function royal_jelly(slot)
    local flight=h.flight(slot)
    local function hold(keys,ms) flight:hold(keys,ms) end
    hold({"left"},750)
    for i=1,2 do hold({"left","jump"},50); hold({"left"},50) end
    hold({"left"},2250); hold({"left","forward"},1000); hold({"forward"},1000); hold({},2000)
    local path=flight:finish(); path[#path+1]=walk({{13,"forward"}})
    return path
end
local function treat_approach(slot)
    local ledge=treat_ledge()
    return {walk({{5,"forward"},{9.2 * slot - 4,"right"}}),walk({{67.5,"backward","left"}}),turn("rotate_right",4),walk({{30,"forward"},{20,"forward","right"}}),turn("rotate_right"),walk({{43.5,"forward"},{16,"right"}}),ledge,turn("rotate_left"),walk({{29.25,"forward"},{17,"forward","left"},{3,"forward"}})}
end
local function clock_approach(slot)
    local settle=clock_settle()
    return {walk({{5,"forward"},{9.2 * slot - 4,"right"}}),walk({{44.75,"backward","left"},{28,"left"},{10,"backward"},{6,"left"},{12,"backward","left"}}),settle,walk({{1.5,"right"},{12.2,"forward"},{14,"left"},{1.5,"right","backward"},{15,"backward"},{7.5,"left"}}),settle,walk({{1.75,"right"},{4,"forward"},{22.5,"left"}}),turn("rotate_left"),walk({{40,"forward"},{1,"backward"},{20,"right"},{5,"forward"},{16,"left"},{15,"forward"}})}
end
local function berry_approach(slot,blueberry)
    local path={walk({{5,"forward"},{9.2 * slot - 4,"right"}})}
    if blueberry then
        path[#path+1]=walk({{88.875,"backward","left"},{27,"left"}})
        path[#path+1]=turn("rotate_left")
        path[#path+1]=walk({{30,"forward"},{11.5,"forward","right"},{2,"right"}})
    else
        path[#path+1]=walk({{67.5,"backward","left"}})
        path[#path+1]=turn("rotate_right",4)
        path[#path+1]=walk({{31.5,"forward"},{9,"left"},{9,"backward"},{58.5,"left"},{49.5,"forward"},{20.25,"left"}})
        path[#path+1]=turn("rotate_right",4)
        path[#path+1]=walk({{60.75,"forward"}})
        path[#path+1]=turn("rotate_right")
        path[#path+1]=walk({{9,"backward"},{15.75,"backward","right"},{30,"left"},{36,"forward"},{28,"left"},{5,"right"},{3.5,"backward"},{23.5,"left"},{3,"backward"},{10,"right"},{3,"left"},{8,"backward"}})
    end
    return path
end
local function dispenser_shortcut(name,slot)
    if name == "treats" then
        local flight=h.flight(slot)
        flight:hold({"left"},1810)
        flight:jump(2)
        flight:hold({"left"},1925)
        flight:jump()
        flight:turn("rotate_right",4)
        flight:hold({},1500)
        return flight:finish()
    end
    if name == "blueberries" then
        local flight=h.blue_hq(slot)
        flight:walk({{10,"left"},{8,"right"},{10,"forward"}})
        flight:turn("rotate_right",1)
        flight:walk({{1.6,"forward"}})
        flight:hold({"forward","jump"},300)
        flight:hold({"forward"},50)
        flight:jump()
        flight:hold({"forward"},1000)
        return flight:finish()
    end
    local ramp=h.motion()
    ramp:hold({"right","jump"},100)
    ramp:walk(2,{"right"})
    ramp:walk(1.8,{"right","forward"})
    ramp:walk(30,{"right"})
    ramp:hold({"jump"},300)
    ramp:walk(6,{"right"})
    ramp:hold({},500)
    local entrance=h.motion()
    entrance:hold({"jump"},100)
    entrance:walk(3,{"forward"})
    entrance:hold({},1000)
    entrance:hold({"right","jump"},100)
    entrance:hold({"right"},300)
    entrance:hold({"jump"},50)
    entrance:hold({},1000)
    entrance:walk(7.5,{"backward","right"})
    return {walk({{5,"forward"},{9.2 * slot - 4,"right"}}),ramp:finish(),turn("rotate_right",2),entrance:finish()}
end
local function clock_flight(slot)
    local flight=h.flight(slot)
    flight:hold({"forward","left"},1500)
    flight:jump(2)
    flight:hold({"forward","left"},8000)
    flight:walk({{15,"backward"},{3.5,"right"},{2,"right","backward"},{1,"backward"}})
    local path=flight:finish()
    return path
end
return function(p)
    local slot,name=p.slot,p.name
    if name=="honey" then return {walk({{1,"forward"},{9.2*(7-slot)+10,"left"},{2,"backward","right"},{2,"backward"}})} end
    if name=="honey-return" then return {walk({{2,"forward"},{2,"forward","left"},{1,"backward"},{9.2*(7-slot)+10,"right"}})} end
    if name=="royal-jelly" then return royal_jelly(slot) end
    if name=="clock" then if p.cannon then return clock_flight(slot) else return clock_approach(slot) end end
    if p.cannon then return dispenser_shortcut(name,slot) end
    if name=="treats" then return treat_approach(slot) end
    if name=="blueberries" or name=="strawberries" then return berry_approach(slot,name=="blueberries") end
    error("Unknown dispenser")
end
