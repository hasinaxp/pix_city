// Headless world generation: builds the city with an empty catalogue (so
// nothing touches OpenGL), prints its statistics, and draws the whole plan to a
// PNG.
//
//   cl /nologo /std:c++14 /EHsc /O2 /Fe:build\dump_map.exe /Fo:build\ tools\dump_map.cpp
//   build\dump_map.exe [seed] [out.png]
//
// The point of it is that a layout change can be judged in a second without
// launching the game and walking to the place that changed. The ASCII grid is
// for reading in a terminal; the PNG is for seeing the shape of the whole city
// at once, which no in-game camera can do.
#define _CRT_SECURE_NO_WARNINGS
// street-by-street generation log; the tool is where this is worth having
#define CITY_MAP_VERBOSE
#include <stdio.h>
#include <stdlib.h>
#include "../src/game/city_map.hpp"
#include "../src/loader/png_writer.hpp"

static city_catalog  cat;
static city_world    world;
static phys_world    physics;

// ---- the map image ----

#define MAP_CELL_PX 8
#define MAP_MARGIN  12
#define MAP_SIZE    (CITY_CELLS * MAP_CELL_PX + MAP_MARGIN * 2)

static unsigned char image[MAP_SIZE * MAP_SIZE * 3];

struct rgb { unsigned char r, g, b; };

static rgb mix(rgb a, rgb b, float t) {
    rgb o;
    o.r = (unsigned char)(a.r + (b.r - a.r) * t);
    o.g = (unsigned char)(a.g + (b.g - a.g) * t);
    o.b = (unsigned char)(a.b + (b.b - a.b) * t);
    return o;
}

// The zone palette is the facade palette the generator actually builds each
// zone out of, so a block on the map is the colour that block is in the world.
static const rgb ZONE_RGB[ZONE_COUNT] = {
    { 104, 118, 131 },   // DOWNTOWN    slate
    { 149, 166, 180 },   // COMMERCIAL  pale blue-grey
    { 189, 164, 128 },   // RESIDENTIAL sandstone
    { 172, 118,  96 },   // SUBURB      brick
    { 128, 122, 112 },   // INDUSTRIAL  concrete
    {  92, 132,  70 }    // PARK
};

static rgb cell_color(const city_cell& c) {
    rgb water    = {  56,  96, 124 };
    rgb road     = {  58,  60,  64 };
    rgb bridge   = { 118, 112, 104 };
    rgb pavement = { 178, 176, 170 };
    rgb park     = {  92, 132,  70 };
    rgb ground   = { 126, 146, 104 };
    rgb white    = { 255, 255, 255 };

    switch (c.kind) {
        case CELL_WATER:    return water;
        case CELL_ROAD:     return (c.flags & CELLF_BRIDGE) ? bridge : road;
        case CELL_SIDEWALK: return pavement;
        case CELL_PARK:     return park;
        case CELL_GROUND:   return ground;
        default: break;
    }

    // A lot. Built lots take the zone's colour outright; the rest are washed
    // out toward white by how deep into the block they sit, so a block reads as
    // a solid frontage fading to an open middle - which is exactly the thing
    // the depth pass exists to control.
    rgb z = ZONE_RGB[c.zone < ZONE_COUNT ? c.zone : 0];
    if (c.flags & CELLF_OCCUPIED) return z;
    float wash = 0.55f + 0.10f * (float)(c.depth > 3 ? 3 : c.depth);
    return mix(z, white, wash);
}

static void put(int px, int py, rgb v) {
    if (px < 0 || py < 0 || px >= MAP_SIZE || py >= MAP_SIZE) return;
    size_t o = ((size_t)py * MAP_SIZE + px) * 3;
    image[o + 0] = v.r; image[o + 1] = v.g; image[o + 2] = v.b;
}

static void draw_map(const char* path) {
    rgb paper = { 236, 234, 228 };
    for (int i = 0; i < MAP_SIZE * MAP_SIZE; i++) {
        image[i * 3 + 0] = paper.r; image[i * 3 + 1] = paper.g; image[i * 3 + 2] = paper.b;
    }

    for (int z = 0; z < CITY_CELLS; z++)
        for (int x = 0; x < CITY_CELLS; x++) {
            const city_cell& c = city_at(world, x, z);
            rgb v = cell_color(c);

            // +Z is north in the world, and a map is drawn with north at the
            // top, so the row order is flipped on the way out
            int px = MAP_MARGIN + x * MAP_CELL_PX;
            int py = MAP_MARGIN + (CITY_CELLS - 1 - z) * MAP_CELL_PX;

            for (int dy = 0; dy < MAP_CELL_PX; dy++)
                for (int dx = 0; dx < MAP_CELL_PX; dx++) {
                    rgb t = v;
                    // a building gets a darker rim so a terrace reads as a row
                    // of buildings rather than as one long block of colour
                    if ((c.flags & CELLF_OCCUPIED) &&
                        (dx == 0 || dy == 0 || dx == MAP_CELL_PX - 1 || dy == MAP_CELL_PX - 1)) {
                        rgb dark = { 0, 0, 0 };
                        t = mix(v, dark, 0.28f);
                    }
                    put(px + dx, py + dy, t);
                }
        }

    // a frame, so the map has an edge of its own rather than bleeding into the
    // page it is looked at on
    rgb ink = { 70, 68, 64 };
    for (int i = MAP_MARGIN - 2; i < MAP_SIZE - MAP_MARGIN + 2; i++) {
        put(i, MAP_MARGIN - 2, ink);
        put(i, MAP_SIZE - MAP_MARGIN + 1, ink);
        put(MAP_MARGIN - 2, i, ink);
        put(MAP_SIZE - MAP_MARGIN + 1, i, ink);
    }

    if (png_write_rgb(path, image, MAP_SIZE, MAP_SIZE))
    printf("wrote %s  (%d x %d, %d m per pixel)\n",
               path, MAP_SIZE, MAP_SIZE, (int)(CITY_TILE / MAP_CELL_PX));
    else
        printf("could not write %s\n", path);
}


// ---- the archipelago, island by island ----
//
// The one report that says whether a generation run actually worked: an
// island with land but no road on it is one the causeway pass failed to
// reach, and every street on it was then pruned as unreachable. That is
// invisible in the picture unless you already know what you are looking at.
static void report_islands(const city_world& w) {
    // Where to stand to look at the things that are hard to find from the
    // air: the parks are a handful of blocks in a city of 139, and a bridge
    // is four cells of a 112 x 112 grid. Printed as PIX_SPAWN arguments so
    // checking one is a copy and paste rather than a walk.
    printf("\nparks (PIX_SPAWN=x,z at the middle of each)\n");
    int parks = 0;
    for (size_t bi = 0; bi < world.block_count; bi++) {
        const city_block& b = world.blocks[bi];
        if (b.zone != ZONE_PARK) continue;
        printf("  PIX_SPAWN=%d,%d   %d x %d cells\n",
               (b.x0 + b.x1) / 2, (b.z0 + b.z1) / 2, b.x1 - b.x0 + 1, b.z1 - b.z0 + 1);
        parks++;
    }
    if (!parks) printf("  (none)\n");

    printf("\nbridges\n");
    int bridges = 0;
    for (int z = 1; z < CITY_CELLS - 1 && bridges < 12; z++)
        for (int x = 1; x < CITY_CELLS - 1 && bridges < 12; x++) {
            const city_cell& c = city_at(world, x, z);
            if (!(c.flags & CELLF_BRIDGE)) continue;
            // only the first cell of each run, so one crossing prints once
            if (city_at(world, x - 1, z).flags & CELLF_BRIDGE) continue;
            if (city_at(world, x, z - 1).flags & CELLF_BRIDGE) continue;
            printf("  PIX_SPAWN=%d,%d\n", x, z);
            bridges++;
        }
    if (!bridges) printf("  (none)\n");

    printf("\nislands\n");
    for (size_t k = 0; k < w.island_count; k++) {
        const city_island& is = w.islands[k];
        int land = 0, road = 0, bridge = 0;
        for (int z = 0; z < CITY_CELLS; z++)
            for (int x = 0; x < CITY_CELLS; x++) {
                const city_cell& c = city_at(w, x, z);
                if (c.kind == CELL_WATER) continue;
                if (city__island_at(w, x, z) != (int)k) continue;
                land++;
                if (c.kind == CELL_ROAD) road++;
                if (c.flags & CELLF_BRIDGE) bridge++;
            }
        printf("  %2zu  centre %5.1f,%5.1f  r %5.1f  %s  land %5d  road %5d  bridge %3d%s\n",
               k, is.cx, is.cz, is.radius, is.plan == ISLAND_GRID ? "grid " : "lanes",
               land, road, bridge, (land > 60 && road == 0) ? "   << UNREACHED" : "");
    }
}

int main(int argc, char** argv) {
    uint32_t seed = argc > 1 ? (uint32_t)strtoul(argv[1], 0, 10) : 20260905u;
    const char* out = argc > 2 ? argv[2] : "city_map.png";

    memset(&cat, 0, sizeof(cat));
    for (int i = 0; i < ONE_COUNT; i++) cat.singles[i] = (idx)-1;
    // A full-size dummy building set so placement takes exactly the branches it
    // does in the game; none of them carries a mesh, so nothing is emitted.
    cat.building_kinds = 8;
    cat.model_count = BUILDING_FACADE_COUNT * 8;
    for (size_t i = 0; i < cat.model_count; i++) {
        cat.models[i].mesh = (idx)-1;
        cat.models[i].bounds_min = v3(-1.0f, 0.0f, -1.0f);
        cat.models[i].bounds_max = v3(1.0f, 2.0f, 1.0f);
        cat.models[i].scale = KIT_ROAD_SCALE;
    }
    cat.sets[SET_BUILDING].first = 0;
    cat.sets[SET_BUILDING].count = (idx)cat.model_count;

    phys_create_world(physics);
    city_generate(world, cat, physics, seed);

    static const char GLYPH[6] = { '.', '#', ':', 'o', ',', '~' };
    int counts[6] = { 0 };
    int built = 0, depth_hist[8] = { 0 };
    for (int z = CITY_CELLS - 1; z >= 0; z--) {
        for (int x = 0; x < CITY_CELLS; x++) {
            const city_cell& c = city_at(world, x, z);
            counts[c.kind < 6 ? c.kind : 0]++;
            char g = GLYPH[c.kind < 6 ? c.kind : 0];
            if (c.flags & CELLF_BRIDGE) g = '=';
            if (c.kind == CELL_LOT) {
                if (c.depth < 8) depth_hist[c.depth]++;
                if (c.flags & CELLF_OCCUPIED) { g = 'B'; built++; }
            }
            putchar(g);
        }
        putchar('\n');
    }

    printf("\nseed %u   %d x %d cells at %.0f m  (%.1f km across)\n",
           seed, CITY_CELLS, CITY_CELLS, CITY_TILE, CITY_EXTENT / 1000.0f);
    printf("ground %d  road %d  pavement %d  lot %d  park %d  water %d\n",
           counts[0], counts[1], counts[2], counts[3], counts[4], counts[5]);
    printf("lots built %d of %d (%.0f%%)\n", built, counts[3],
           counts[3] ? 100.0 * built / counts[3] : 0.0);
    printf("lot depth  ");
    for (int i = 0; i < 8; i++) printf("%d:%d  ", i, depth_hist[i]);
    printf("\nblocks %zu  props %zu  water quads %zu  statics %zu  lamps %zu\n",
           world.block_count, world.prop_count, world.water_count,
           physics.static_count, world.lamp_count);
    printf("road cells %zu  walk cells %zu\n", world.road_cell_count, world.walk_cell_count);

    report_islands(world);

    printf("\nmap colours: slate downtown, pale blue commercial, sandstone residential,\n"
           "             brick suburb, grey industrial, green parks and open ground;\n"
           "             dark grey roads, light grey pavement, blue water, tan bridges.\n"
           "             A solid outlined square is a building; washed out is open lot.\n");
    draw_map(out);
    return 0;
}
