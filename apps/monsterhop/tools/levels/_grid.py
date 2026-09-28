"""A painter for level maps: rectangles of tiles and floors instead of long
ASCII rows (the zones 5 and 6 are drawn with it). y = 0 is the near row,
as in the game; map2() gets the rows far first."""


class Grid:
    def __init__(self, w, h, tile='g', floor=0):
        self.w, self.h = w, h
        self.t = [[tile] * w for _ in range(h)]
        self.f = [[floor] * w for _ in range(h)]

    def rect(self, x0, y0, x1, y1, tile=None, floor=None):
        """x0..x1, y0..y1 inclusive"""
        for y in range(max(0, y0), min(self.h - 1, y1) + 1):
            for x in range(max(0, x0), min(self.w - 1, x1) + 1):
                if tile is not None:
                    self.t[y][x] = tile
                if floor is not None:
                    self.f[y][x] = floor

    def row(self, y, tile=None, floor=None):
        self.rect(0, y, self.w - 1, y, tile, floor)

    def cells(self, pts, tile=None, floor=None):
        for (x, y) in pts:
            self.rect(x, y, x, y, tile, floor)

    def apply(self, lv):
        tiles = '\n'.join(''.join(self.t[y]) for y in range(self.h - 1, -1, -1))
        floors = '\n'.join(''.join(str(v) for v in self.f[y]) for y in range(self.h - 1, -1, -1))
        lv.map2(tiles, floors)
