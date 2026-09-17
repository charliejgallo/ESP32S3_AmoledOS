-- @name Pelotas
--
-- The other way of drawing: nothing is cleared and nothing is erased either.
--
-- init() paints the world once -a grid, which is the point: it is DRAWN, not
-- a flat colour- and calls aos.background() to freeze it. From then on the app
-- undoes each frame at the start of the next one, copying those rectangles
-- back from the frozen copy, so draw() only ever has to draw. Erasing by
-- painting over with the background colour, which is what this script did
-- before there was a background, would leave rectangular holes in the grid.
--
-- What it costs is what the balls cover: every primitive marks the box it
-- touches and only those rows go to the panel. The counter at the top right
-- of the screen says how many, out of 224. cubo.lua is the opposite -it calls
-- aos.clear() every frame, so all 224 rows are dirty- and both are right.

local BG   = 0x05050C
local GRID = 0x121A2A
local TOP  = 22
local N    = 4
local balls = {}

local COLOURS = { 0x00E5FF, 0xFF4081, 0xFFD740, 0x69F0AE }

function init()
    aos.clear(BG)
    for y = TOP, aos.H - 1, 16 do aos.rect(0, y, aos.W, 1, GRID) end
    for x = 0, aos.W - 1, 16 do aos.rect(x, TOP, 1, aos.H - TOP, GRID) end
    aos.rect(0, TOP - 1, aos.W, 1, 0x243046)
    aos.text(6, 7, "PELOTAS", 0x3E4A63, 1)

    -- If there is no memory for the copy this returns false; the balls would
    -- then smear, which is honest: without a background there is nothing to
    -- put back.
    aos.background()

    for i = 1, N do
        balls[i] = {
            x  = 20 + i * 30,
            y  = TOP + 24 + i * 14,
            dx = (i % 2 == 0) and 0.9 or -1.1,
            dy = 0.7 + i * 0.15,
            r  = 6 + i,
            c  = COLOURS[i],
        }
    end
end

function tick(dt)
    local step = dt / 16          -- so it moves the same on a slow frame
    for i = 1, N do
        local b = balls[i]
        b.x = b.x + b.dx * step
        b.y = b.y + b.dy * step
        if b.x - b.r < 1 then b.x, b.dx = 1 + b.r, -b.dx end
        if b.x + b.r > aos.W - 2 then b.x, b.dx = aos.W - 2 - b.r, -b.dx end
        if b.y - b.r < TOP + 1 then b.y, b.dy = TOP + 1 + b.r, -b.dy end
        if b.y + b.r > aos.H - 2 then b.y, b.dy = aos.H - 2 - b.r, -b.dy end
    end
end

function draw()
    for i = 1, N do
        local b = balls[i]
        aos.disc(b.x // 1, b.y // 1, b.r, b.c)
    end
end

-- A tap sends them the other way, which is the cheapest way to see that the
-- rows being pushed follow the balls around.
function touch(x, y, ev)
    if ev ~= "down" then return end
    for i = 1, N do
        local b = balls[i]
        b.dx = (x < aos.W / 2) and -math.abs(b.dx) or math.abs(b.dx)
        b.dy = (y < aos.H / 2) and -math.abs(b.dy) or math.abs(b.dy)
    end
    aos.beep(820, 20)
end
