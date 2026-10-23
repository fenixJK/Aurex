local h=require("helpers")
return function(p)
    if p.action=="row" then
        return {{kind="checkpoint",detector="hive.proximity",timeout=60000,age=2000,attempts=9,
            path={{55.2,"left"}},nudge_path={{1.5,"right"}},use_speed=true}}
    end
    if p.action=="scan" then
        return {h.walk({{9.2*math.abs(p.to-p.from),p.to>p.from and "left" or "right"}})}
    end
    local slots={
        {{4,"right"},{20,"forward","right"}},
        {{13,"forward","right"},{6,"forward"}},
        {{20,"forward"},{4,"backward"}},
        {{13,"forward","left"},{6,"forward"}},
        {{4,"left"},{20,"forward","left"}},
        {{12,"forward","left"},{13,"left"},{10,"forward","left"}}
    }
    assert(p.action=="claim","Unknown hive movement")
    local path=assert(slots[p.slot],"Invalid hive slot")
    local last=path[#path]
    local reverse={forward="backward",backward="forward",left="right",right="left"}
    return {{kind="checkpoint",detector="hive.claim",timeout=25000,age=2000,attempts=2,
        path=path,nudge_path={{1.5,reverse[last[2]],reverse[last[3]]}},use_speed=true}}
end
