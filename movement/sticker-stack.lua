local h=require("helpers")
return function(p)
    if p.cannon then
        local flight=h.flight(p.slot)
        flight:launch_moving("right")
        flight:hold({"right"},1200)
        flight:jump(2)
        flight:hold({},6000)
        flight:walk({{22,"right"},{7,"left"},{11,"backward"}})
        local path=flight:finish()
        local approach=h.motion()
        for i=1,2 do
            approach:walk(1.5,{"backward"},true)
            approach:walk(i==1 and 1.5 or 9,{"backward"})
        end
        approach:walk(9,{"left"})
        path[#path+1]=approach:finish()
        return path
    end
    local path=require("npc")({name="Honey Bee",slot=p.slot})
    local ledge=h.motion()
    ledge:walk(4.5,{"forward"})
    ledge:walk(8,{"left"})
    for _,distance in ipairs({4,6}) do
        ledge:hold({"jump"},100)
        ledge:walk(distance,{"left"})
    end
    path[#path+1]=ledge:finish()
    path[#path+1]=h.turn("rotate_right",4)
    local podium=h.motion()
    podium:walk(12,{"forward"})
    podium:hold({"jump"},100)
    podium:walk(14,{"forward","left"})
    podium:walk(3,{"forward"})
    path[#path+1]=podium:finish()
    return path
end
