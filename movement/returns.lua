local h=require("helpers")
local walk,turn=h.walk,h.turn
local function transport_flight(ms)
    local plan={kind="checkpoint",detector="interaction.prompt",timeout=ms+5000,age=2000,attempts=0,completion="launch",continuous=true,approach={}}
    h.actions(plan.approach,{},ms,"Transport flight")
    return plan
end
local function pineapple_descent()
    local hop=h.motion()
    hop:hold({"forward","jump"},200)
    return {walk({{12,"forward"},{8,"right"}}),turn("rotate_right",2),hop:finish(),walk({{30,"forward"},{3,"backward"}}),turn("rotate_left",2),walk({{30,"forward"},{4.5,"backward"},{40.5,"right"},{40.5,"forward"},{34.5,"right"},{27,"forward"}})}
end
return function(p)
    local field,hive_slot,access,teleporter=p.field,p.slot,p,p.teleporter
    local function append_descent(path) h.append(path,pineapple_descent()) end
    if teleporter and(field == "blueflower" or field == "bamboo") then
        local path=(field == "blueflower" and {walk({{18,"right"},{8,"left"},{65,"forward"},{10,"right"},{4,"backward"},{3,"left"}})} or {walk({{15,"left"},{25,"forward"},{7,"right"},{5,"backward"},{50,"right"},{8,"left"},{45,"forward"},{10,"right"},{4,"backward"},{3,"left"}})})
        path[#path+1]=turn("rotate_right",2)
        path[#path+1]=transport_flight(1500)
        path[#path+1]=walk({{5,"right"},{8,"backward"},{18.5,"left"},{4,"backward"},{10,"left"},{10,"right"},{5,"forward"},{4,"backward"}})
        return path
    end
    if field == "sunflower" then
        return {turn("rotate_left"),walk({{13.5,"right"},{45,"forward"},{2.25,"backward"},{25,"forward","left"},{13.5,"forward"},{1.5,"backward"},{10,"right"},{2.7,"backward"}})}
    end
    if field == "dandelion" then
        return {turn("rotate_right"),walk({{13.5,"forward"},{42,"right"},{28,"forward"},{1.5,"backward"},{35,"right"},{2.7,"backward"}})}
    end
    if field == "mushroom" then
        local approach={{11.5,"left"},{72,"forward"}}
        if hive_slot ~= 3 then
            approach[#approach+1]={1.5,"backward"}
            approach[#approach+1]={35,"right"}
        end
        approach[#approach+1]={2.7,"backward"}
        return {walk({{13.5,"forward"},{27,"left"}}),turn("rotate_left",4),walk(approach)}
    end
    if field == "clover" then
        local approach={{18,"forward"},{36,"right"},{4.5,"backward"},{50.5,"right"},{36,"forward"}}
        if hive_slot ~= 3 then
            approach[#approach+1]={1.5,"backward"}
            approach[#approach+1]={35,"right"}
        end
        approach[#approach+1]={2.7,"backward"}
        return {walk(approach)}
    end
    if field == "blueflower" or field == "bamboo" then
        local path={}
        local approach={}
        if field == "blueflower" then
            path = {turn("rotate_right",2)}
            approach = {{13.5,"forward"},{4.5,"backward"},{46,"right"},{40.5,"forward"},{33.5,"right"},{27,"forward"}}
        else
            path = {walk({{16,"left"},{5,"right"}}),turn("rotate_right",2)}
            approach = {{75,"right"},{64,"forward"},{5.5,"forward","right"},{36,"forward"}}
        end
        if hive_slot ~= 3 then
            approach[#approach+1]={1.5,"backward"}
            approach[#approach+1]={35,"right"}
        end
        approach[#approach+1]={2.7,"backward"}
        path[#path+1]=walk(approach)
        return path
    end
    if field == "spider" or field == "strawberry" then
        local approach={}
        local path={}
        if field == "spider" then
            path = {walk({{22.5,"forward"},{27,"left"}}),turn("rotate_left",4)}
            approach = {{64,"forward"},{5.5,"forward","right"},{36,"forward"}}
        else
            path = {turn("rotate_left")}
            approach = {{12,"backward"},{15,"backward","left"},{18,"left"},{15,"forward"},{6,"left"},{95,"forward"}}
        end
        if hive_slot ~= 3 then
            approach[#approach+1]={1.5,"backward"}
            approach[#approach+1]={35,"right"}
        end
        approach[#approach+1]={2.7,"backward"}
        path[#path+1]=walk(approach)
        return path
    end
    if field == "coconut" then
        local descent=h.motion()
        descent:walk(22.5,{"backward"})
        descent:hold({"backward","jump"},50)
        descent:walk(31.5,{"backward"})
        return {walk({{20.25,"right"}}),descent:finish(),walk({{33.75,"left"},{13.5,"forward"},{1.5,"backward"},{10,"right"},{2.7,"backward"}})}
    end
    if field == "pepper" then
        return {walk({{42,"right"}}),turn("rotate_left",4),walk({{45,"forward"},{50,"left"},{49,"forward"}}),turn("rotate_right",2),walk({{13.5,"forward"},{1.5,"backward"},{10,"right"},{2.7,"backward"}})}
    end
    if field == "rose" then
        return {walk({{12,"forward"},{20,"left"},{8,"right"}}),turn("rotate_left",2),walk({{35,"left"},{41,"forward"},{9,"left"},{28,"forward"},{8,"left"},{6,"forward","left"},{6,"forward"},{1.5,"backward"},{35,"right"},{2.7,"backward"}})}
    end
    if field == "cactus" or field == "pumpkin" or field == "pinetree" or field == "pineapple" or field == "stump" or field == "mountaintop" then
        local path={}
        if field == "cactus" then
            path = {walk({{8,"backward"},{10,"left","backward"},{14.5,"backward"},{28,"left"},{36,"forward"},{3,"right"},{4,"forward"},{4,"left"},{27,"forward"},{2.75,"forward","left"},{90,"forward"}})}
        end
        if field == "pumpkin" then
            local ledge=h.motion()
            ledge:hold({"backward","left"},2000)
            ledge:hold({"backward","left","jump"},200)
            ledge:hold({"backward","left"},2000)
            path = {walk({{9,"right"}}),turn("rotate_left",4),walk({{9,"backward"}}),ledge:finish(),walk({{36,"forward"},{4.5,"forward","right"},{4.5,"forward","left"},{27,"forward"},{3,"forward","left"},{85.5,"forward"}})}
        end
        if field == "pinetree" then
            local ledge=h.motion()
            ledge:walk(6,{"forward"})
            ledge:hold({"forward","jump"},200)
            ledge:walk(108,{"forward"})
            path = {walk({{31,"forward"},{75,"right"}}),turn("rotate_left",4),walk({{20,"forward"},{3,"forward","left"},{18,"forward"},{6,"forward","right"},{10,"right"},{2,"left"}}),ledge:finish()}
        end
        if field == "pineapple" or field == "stump" then
            if field == "pineapple" then
                path = {walk({{18,"forward"},{31.5,"right"},{4,"left"},{10,"backward"},{4,"right"}}),turn("rotate_left",4),walk({{60,"forward"},{5.5,"backward"},{10,"right"}})}
            else
                path = {walk({{40.5,"right"}}),turn("rotate_right",2),walk({{22.5,"right"},{22.5,"backward"},{13,"right"},{40.5,"forward"},{5.5,"backward"},{10,"right"}})}
            end
            if access.cannon and access.bees >= 12 then
                local mount=h.motion()
                mount:hold({"jump"},50)
                mount:walk(4,{"right"})
                mount:hold({},1100)
                path[#path+1]=mount:finish()
                path[#path+1]=transport_flight(3000)
                path[#path+1]=walk({{34,"forward"}})
            else
                append_descent(path)
            end
        end
        if field == "mountaintop" then
            local approach={}
            for i=1,15 do
                approach[#approach+1]={3,"backward"}
                approach[#approach+1]={1,"left"}
            end
            path = {walk(approach),walk({{36,"forward","right"},{100,"forward"},{32,"right"},{37,"forward"}})}
        end
        local home={}
        if hive_slot ~= 3 then
            home = {{1.5,"backward"},{35,"right"}}
        end
        home[#home+1]={2.7,"backward"}
        path[#path+1]=walk(home)
        return path
    end
    error("No walking return recipe for this field")
end
