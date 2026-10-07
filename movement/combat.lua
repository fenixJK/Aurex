return function(p)
    if p.enemy=="Tunnel Bear" then return p.loot and {{14,"forward"},{14,"backward"}} or {{.5,"backward"}} end
    if p.enemy=="King Beetle" and not p.loot then return {{2,"right"},{2,"backward"},{2,"left"},{2,"forward"}} end
    if p.loot then
        return {{3,"left"},{3,"backward"},{6,"right"},{6,"forward"},{6,"left"},{3,"backward"},{3,"right"}}
    end
    if p.enemy=="Mondo Chick" or p.enemy=="Coconut Crab" or p.enemy=="Vicious Bee" then
        return {{4,"left"},{4,"backward"},{4,"right"},{4,"forward"}}
    end
    return {{1.5,"left"},{1.5,"right"}}
end
