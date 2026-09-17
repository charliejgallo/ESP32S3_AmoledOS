-- HOLA - the smallest thing that is still a script
-- @name Hola
--
-- Every callback is optional: this one only has draw(), and a variable that
-- survives between frames because it lives in the chunk.

local n = 0

function draw()
    n = n + 1
    aos.clear(0x001018)
    aos.disc(aos.W // 2, aos.H // 2, 30 + math.sin(n * 0.05) * 20 // 1, 0x00E5FF)
    aos.text(10, 10, "HOLA DESDE LUA", 0xFFFFFF, 2)
    aos.text(10, aos.H - 14, string.format("CUADRO %d", n), 0x7F8C9F, 1)
end

function touch(x, y, ev)
    if ev == "down" then aos.beep(1200, 25) end
end
