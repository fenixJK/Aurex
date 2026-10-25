local h=require("helpers")
local walk,turn=h.walk,h.turn
return function(p)
    local field,planting,slot=p.field,p.planting,p.slot
    local f=h.flight(slot)
    if field == "mountaintop" then
        f:hold({},(planting and 2750 or 2800))
        if not  planting then
            f:walk({{6,"forward"},{1,"right"}})
        end
    elseif field == "pinetree" then
        f:hold({"right","backward"},925)
        f:jump(2)
        f:hold({"right","backward"},(planting and 5000 or 4500))
        if not  planting then
            f:hold({"right"},500)
            f:jump(1,{})
            f:turn("rotate_left",4)
            f:hold({},2000)
        else
            f:jump(1,{})
            f:hold({},1000)
            f:turn("rotate_right",3)
            f:hold({"forward","jump"},300)
            f:hold({"forward"},50)
            f:jump(1,{"forward"})
            f:hold({"forward"},2000)
            f:turn("rotate_left",1)
            f:walk({{15,"right"},{15,"forward"},{3,"left"},{3,"backward"}})
        end
    elseif(field == "cactus" or field == "pumpkin") and planting then
        f:hold({"right","backward"},890)
        f:jump(2)
        f:hold({"right","backward"},2500)
        f:hold({"backward"},1100)
        f:jump(1,{})
        f:turn("rotate_left",4)
        f:hold({},600)
        if field == "cactus" then
            f:walk({{20,"forward","right"},{15,"right"},{30,"backward"},{3,"left"}})
        else
            f:walk({{35,"forward"},{30,"right"},{3,"left"},{3,"backward"}})
        end
    elseif field == "cactus" then
        f:aim("rotate_right",3)
        f:launch_moving("forward")
        f:hold({"forward"},967)
        f:jump(2)
        f:hold({},2600)
        f:jump(1,{})
        f:hold({},1500)
        f:turn("rotate_left",3)
    elseif field == "pumpkin" then
        f:hold({"right","backward"},950)
        f:jump(2)
        f:hold({"right","backward"},2700)
        f:hold({"backward"},500)
        f:hold({},600)
        f:jump(1,{})
        f:turn("rotate_left",4)
        f:hold({},1500)
    elseif field == "stump" then
        f:hold({"left"},(planting and 1800 or 1850))
        f:jump(2)
        f:hold({"left"},2750)
        f:turn("rotate_left",2)
        f:hold({"forward","left"},900)
        f:hold({"forward"},(planting and 1500 or 1650))
        f:jump(1,{})
        f:hold({},1000)
    elseif field == "pineapple" and planting then
        f:hold({},500)
        f:steer({"right"},1000,"rotate_right",4)
        f:jump(1,{"right"})
        f:hold({"right"},500)
        f:jump(1,{"right"})
        f:hold({"right"},2900)
        f:hold({"forward","left"},1600)
        f:jump(1,{"forward","left"})
        f:hold({"forward","left"},1000)
        f:walk({{14,"forward","left"},{10,"forward"},{7,"backward","right"}})
    elseif field == "pineapple" then
        f:hold({"left"},1850)
        f:jump(2)
        f:hold({"left"},2750)
        f:hold({"backward"},1150)
        f:jump(1,{})
        f:turn("rotate_right",4)
        f:hold({},2000)
    elseif field == "rose" and planting then
        f:hold({"right"},550)
        f:jump(2)
        f:hold({"right"},2500)
        f:jump(1,{})
        f:turn("rotate_left",4)
        f:hold({},1000)
        f:walk({{17,"forward"},{10,"right"},{8,"forward","right"},{8,"forward"},{7,"backward","left"}})
        f:turn("rotate_left",2)
    elseif field == "rose" then
        f:aim("rotate_right",2)
        f:launch_moving("forward")
        f:hold({"forward"},580)
        f:jump(2)
        f:hold({},3050)
        f:jump(1,{})
        f:hold({},2000)
    elseif field == "bamboo" and planting then
        f:aim("rotate_left",2)
        f:hold({"forward"},800)
        f:jump(2)
        f:hold({"forward"},3000)
        f:hold({"left"},1000)
        f:jump(1,{})
        f:hold({},1000)
        f:walk({{25,"left"},{30,"forward"},{1,"right"},{3,"backward"}})
    elseif field == "blueflower" then
        f:hold({"left"},(planting and 700 or 675))
        f:jump(2)
        f:hold({"left"},(planting and 4450 or 2000))
        if not  planting then
            f:hold({},1250)
        end
        f:jump(1,{})
        if planting then
            f:hold({},1000)
            f:turn("rotate_left",2)
            f:walk({{20,"left"},{20,"forward"},{10,"backward"}})
        else
            f:turn("rotate_left",2)
            f:hold({},1000)
        end
    elseif field == "clover" and planting then
        f:hold({"left","forward"},525)
        f:jump(2)
        f:hold({"left","forward"},1250)
        f:hold({"left"},3000)
        f:jump(1,{})
        f:hold({},700)
        f:walk({{20,"forward","left"},{3,"forward"},{3,"left"},{5,"right"},{10,"right","forward"},{3,"backward"}})
    elseif field == "clover" then
        f:aim("rotate_left",1)
        f:launch_moving("forward")
        f:hold({"forward"},1100)
        f:jump(2)
        f:hold({},3767)
        f:jump(1,{})
        f:turn("rotate_right",1)
        f:hold({},1500)
    elseif field == "spider" then
        if not  planting then
            f:launch_moving("backward")
        end
        f:hold({"backward"},(planting and 1050 or 1075))
        f:jump(2)
        f:hold((planting and {"backward"} or {}),300)
        f:jump(1,{})
        f:turn((planting and "rotate_left" or "rotate_right"),4)
        f:hold({},(planting and 1500 or 2000))
        if planting then
            f:walk({{20,"forward"},{10,"forward","left"},{10,"left"},{9,"backward","right"}})
        end
    elseif field == "strawberry" then
        f:hold({"right","backward"},(planting and 750 or 700))
        f:jump(2)
        f:hold({"right","backward"},(planting and 1000 or 1700))
        if planting then
            f:hold({},800)
        end
        f:jump(1,{})
        f:turn("rotate_right",2)
        f:hold({},2000)
        if planting then
            f:walk({{10,"forward","right"},{15,"right"},{15,"forward"},{3,"backward","left"}})
        end
    elseif field == "bamboo" then
        f:hold({"left"},1250)
        f:jump(2)
        f:hold({"left"},1000)
        f:hold({},1200)
        f:jump(1,{})
        f:turn("rotate_left",2)
        f:hold({},2000)
    end
    return h.scope(f:finish(),field..(planting and ".planting" or ".entry"))
end
