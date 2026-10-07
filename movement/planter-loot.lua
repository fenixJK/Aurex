return function(p)
    local path={{6,"left"},{5,"backward"}}
    for row=0,6 do
        path[#path+1]={10,row%2==1 and "backward" or "forward"}
        if row<6 then path[#path+1]={2,"right"} end
    end
    path[#path+1]={5,"backward"}; path[#path+1]={6,"left"}
    return path
end
