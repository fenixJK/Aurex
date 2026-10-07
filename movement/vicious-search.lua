return function(p)
    local fields={pepper={20,7,1,10,7},mountaintop={17,5.5,2,8.5,10.5},rose={13,6,2,6.5,12},cactus={26,5.5,1,13,3.7},spider={21,5.7,2,10.5,9.5},clover={22,5.7,2,11,10}}
    local f=assert(fields[p.field],"Unknown Vicious Bee field")
    local steps={{f[4],"right"},{f[5],"forward"}}
    for i=1,f[3] do
        steps[#steps+1]={f[1],"left"}
        steps[#steps+1]={f[2],"backward"}
        steps[#steps+1]={f[1],"right"}
        steps[#steps+1]={f[2],"backward"}
    end
    steps[#steps+1]={f[1],"left"}
    return steps
end
