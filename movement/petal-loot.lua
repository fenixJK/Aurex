return function(p)
    local diagonal=math.sqrt(50)
    return {{5,"forward"},{diagonal,"backward","right"},{diagonal,"backward","left"},
        {diagonal,"forward","left"},{diagonal,"forward","right"},{5,"backward"}}
end
