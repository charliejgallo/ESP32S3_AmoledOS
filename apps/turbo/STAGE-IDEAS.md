# Turbo — stages for later

Three stages that were proposed alongside the Halloween road and the mountain
tunnels (v0.4.12) and kept for later. Each is written so it can be built as
it stands: what it looks like, what Blender has to model, what the watch has
to draw, and what it costs. How a stage is added, and the memory rule, are in
[README.md](README.md) ("Adding a stage or a vehicle"): props plus the
vehicles of its traffic up to ~3.7 MB keep the fourth frame buffer.

## Autumn in the country

**The idea.** A daytime drive through farmland in October: rolling hills in
orange, red and gold, the low sun of an autumn afternoon, harvest everywhere.
It pairs with the Halloween night (same season, the opposite light) and is
the calmest, warmest stage, a breather between the night ones.

- **Look.** Sky warm and hazy, a low sun from the front-left, long shadows;
  fog a golden haze. Ground in two tones of stubble and ochre grass; the
  asphalt a little faded; white rumble with red.
- **Props (Blender).** Maples and oaks in autumn colours (three shapes, some
  half bare), a red barn with a silo, a white farmhouse with a porch, round
  hay bales (single and stacked), a wooden fence section, vineyard rows (a
  strip seen at an angle), a scarecrow (reuse Halloween's in daylight), a
  pumpkin stand by the road, a windmill (the old wooden kind), a covered
  wooden bridge the road passes through (a span prop like the overpass).
- **Backdrop.** Rolling hills patched with fields and woods in autumn
  colours, a church steeple, a line of poplars, hot-air balloons.
- **Traffic.** Sedans, the pickup, a tractor (a new slow vehicle: the road's
  hazard), vans.
- **Play.** Two lanes, gentle climbs over hills (blind crests where the
  traffic appears late), a few tight bends by the farms, long straights
  between fields.
- **Engine.** Nothing new. Optional: leaves blowing across the screen
  (a handful of small sprites drifting, cheap).
- **Memory.** Like the coast: ~1.6 MB of props, ~0.9 MB of traffic.

## Neon city at night

**The idea.** A Tokyo-style city after midnight: an elevated expressway
between towers covered in signs, neon in every colour, the road wet and
shining. The flashiest stage, and the third night one (after Snow Pass and
the Halloween road), so it should be spaced from them in the list.

- **Look.** Black-blue sky with a glow over the skyline, fog a magenta haze.
  The asphalt dark with bright lane lines; rumble magenta and cyan (like
  Orbit 9 but city-dirty). Headlights as in Snow Pass.
- **Props (Blender), all emissive where they light up.** Towers with lit
  window grids (two or three), vertical neon sign columns (blank panels,
  glowing, no readable text), a big screen billboard, a lantern-lined
  gateway arch (span), street lamps with a cold white head, a ramen stall,
  vending machines, an overhead sign gantry in green (reuse the city's at
  night), a monorail track crossing over the road (span).
- **Backdrop.** A skyline of lit towers, a red-and-white broadcast tower, a
  bridge with lights, low clouds lit from below.
- **Traffic.** Compacts, taxis (a new vehicle, or the compact in taxi paint:
  the paints table already allows it), vans, the wedge and the rally as
  street racers.
- **Play.** Three lanes, many short bends between buildings, overpasses
  and ramps (hills), dense traffic.
- **Engine.** Wet-road reflections would be the showpiece and the cost: a
  cheap version mirrors the lit props' colours as vertical streaks on the
  asphalt rows below them (a darkened, stretched copy of a few columns). To
  measure before promising; without it the stage still works.
- **Memory.** The heaviest candidate: tall emissive props, ~2 MB of props.
  Keep the traffic to four models (~0.6 MB).

## Volcano in the jungle

**The idea.** A daytime road through a tropical jungle on the slopes of an
active volcano: dense green, stone ruins in the undergrowth, and the lava
getting closer as the stage goes on, ending in a black-rock landscape with
glowing rivers beside the road.

- **Look.** Humid sky, the volcano smoking on the horizon; fog a green-grey
  haze that turns orange-brown near the end. Ground: jungle green, then
  black basalt with orange cracks (a new ground kind, `GR_LAVA`, glowing).
- **Props (Blender).** Jungle trees (a kapok with buttresses, a strangler
  fig), giant ferns and banana plants, a stone temple ruin (steps and a
  collapsed arch), carved stone heads, a rope bridge high above (span),
  smoking vents, basalt spires, a lava fall, warning signs.
- **Backdrop.** The volcano's cone with a plume of smoke and a glow at the
  crater, jungle ridges, a waterfall.
- **Traffic.** Jeeps (a new vehicle, or the pickup), vans, trucks; fewer
  cars than elsewhere.
- **Play.** The stage changes as it goes: the first half twisty jungle on
  two lanes, the second half fast and open over the lava field.
- **Engine.** The lava ground: an animated palette (the glowing colour of
  `GR_LAVA` pulsing by time) is cheap; lava "rivers" beside the road are a
  ground kind on one side, like the sea on the coast.
- **Memory.** ~1.7 MB of props, ~0.9 MB of traffic.
