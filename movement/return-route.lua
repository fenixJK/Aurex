local h=require("helpers")
local ground=require("returns")
return function(p)
    local field,slot=p.field,p.slot
    local blue=field=="pinetree" and p.cannon and p.bees>=25
    local teleporter=p.teleporter and (field=="blueflower" or field=="bamboo")
    p.teleporter=teleporter
    local path={}
    if p.planting then
        if field=="strawberry" then path[#path+1]=h.turn("rotate_left",2)
        elseif field=="cactus" then path[#path+1]=h.turn("rotate_right",4)
        elseif field=="pinetree" then path[#path+1]=h.turn("rotate_right",2) end
    end
    if blue then
        path[#path+1]=h.walk({{30,"forward"},{25,"left"},{2,"forward"},{30,"backward"},{23,"backward","right"},{3,"backward"},{5,"right"},{13,"backward"}})
        local mount=h.motion()
        mount:walk(5,{"backward"},true); mount:hold({"backward","jump"},200)
        mount:walk(3.5,{"right"}); mount:walk(5,{"forward"}); mount:walk(7,{"backward"}); path[#path+1]=mount:finish()
        path[#path+1]={kind="shift_lock",enabled=true}
        path[#path+1]={kind="turn",key="rotate_right",count=4,press=50,release=200}
        local flight={kind="checkpoint",detector="interaction.prompt",timeout=10200,age=2000,attempts=0,completion="launch",continuous=true,approach={}}
        for _,step in ipairs({{{"jump"},200},{{},200},{{"jump"},200},{{},200},{{},3800},{{"jump"},100},{{},500}}) do
            h.actions(flight.approach,step[1],step[2],"Cannon flight")
        end
        path[#path+1]=flight; path[#path+1]={kind="shift_lock",enabled=false}
        path[#path+1]=h.walk({{3.3,"backward"},{35,"right"},{5,"forward"},{4,"backward"}})
    else h.append(path,ground(p)) end
    if slot~=0 then
        local at_three=field~="sunflower" and field~="dandelion" and field~="coconut" and field~="pepper" and field~="rose"
        if blue or teleporter or slot~=3 or not at_three then path[#path+1]=h.walk({{9.2*slot-4,"left"}}) end
        -- Watch the final leg without extending it when recognition is late.
        -- A short reverse search can recover a prompt passed between frames.
        local last=path[#path]
        local leg=table.remove(last.legs)
        if #last.legs==0 then table.remove(path) end
        local reverse={left="right",right="left",forward="backward",backward="forward"}
        path[#path+1]={kind="checkpoint",detector="hive.proximity",timeout=30000,age=2000,attempts=2,
            path={leg},nudge_path={{1.5,assert(reverse[leg[2]])}},use_speed=true}
    end
    return path
end
