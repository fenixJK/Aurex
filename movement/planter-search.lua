return function()
    local path={}
    for i=1,4 do
        path[#path+1]={3*i,i%2==1 and "forward" or "backward"}
        path[#path+1]={3*i,i%2==1 and "right" or "left"}
    end
    return {{kind="checkpoint",detector="planter.interaction",timeout=45000,attempts=0,use_speed=true,path=path}}
end
