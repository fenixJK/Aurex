return function(p)
    if p.enemy=="Tunnel Bear" then return p.loot and {{14,"forward"},{14,"backward"}} or {{.5,"backward"}} end
    if p.enemy=="King Beetle" and not p.loot then return {{2,"right"},{2,"backward"},{2,"left"},{2,"forward"}} end
    if p.loot then
        -- Sweep the interior as well as the edge, then restore the route origin.
        local path={{3,"left"},{3,"backward"}}
        for row=0,4 do
            path[#path+1]={6,row%2==0 and "right" or "left"}
            if row<4 then path[#path+1]={1.5,"forward"} end
        end
        path[#path+1]={3,"backward"}; path[#path+1]={3,"left"}
        return path
    end
    if p.enemy=="Mondo Chick" or p.enemy=="Coconut Crab" or p.enemy=="Vicious Bee" then
        return {{4,"left"},{4,"backward"},{4,"right"},{4,"forward"}}
    end
    return {{1.5,"left"},{1.5,"right"}}
end
