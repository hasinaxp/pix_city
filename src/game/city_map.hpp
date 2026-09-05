#pragma once
#include <string.h>
#include "../core/random.hpp"
#include "../core/physics.hpp"
#include "city_config.hpp"
#include "city_assets.hpp"

// Procedural city generation.
//
// The layout comes out of recursive subdivision rather than a fixed lattice:
// the map is repeatedly cut in two by a street until no piece is larger than a
// block, which gives streets that run for a long way, blocks of genuinely
// different sizes, and the occasional short stub - the shape of a real street
// plan instead of graph paper.
//
// Everything after that is derived, never random-placed:
//
//   sidewalk   any cell touching a road
//   frontage   a lot cell knows which way the nearest street is, so a building
//              put there faces it and its facade lands on the lot line
//   kerbside   lamps, hydrants, bins and benches attach to the kerb edge of a
//              pavement cell, with the lamp's arm turned out over the road
//   set-back   suburban houses sit back from the street with a drive and a
//              path connecting them to it, and fences on the lot boundary
//
// Output is one flat array of placements, bucketed by cell so a frame only
// walks the cells it can actually see.

#define CITY_MAX_PROPS   90000
#define CITY_MAX_BLOCKS  512
#define CITY_MAX_LAMPS   8000

struct city_cell {
    uint8_t kind;       // CELL_*
    uint8_t zone;       // ZONE_*
    uint8_t road_mask;  // bit per DIR of neighbouring road cells
    uint8_t facing;     // DIR_* toward the street this lot fronts, else DIR_NONE
    uint8_t kerb;       // pavement cells: DIR_* toward the road
    uint8_t flags;
    // Lot cells only: how many cells back from the street frontage this is.
    // 0 is the frontage itself, and everything past the zone's build depth is
    // block interior - courtyard, garden or service yard, never a facade
    // stranded in the middle of a field. 0xFF for anything that is not a lot.
    uint8_t depth;
};

#define CELLF_OCCUPIED 0x01   // a building footprint covers this cell
#define CELLF_JUNCTION 0x02   // road cell with three or four connections
#define CELLF_CROSSING 0x04   // road cell carrying a pedestrian crossing
#define CELLF_YARD     0x08   // lot cell kept clear as a garden
#define CELLF_BRIDGE   0x10   // road cell carried over water on a deck

// One placed piece of scenery. Kept as loose transform components rather than a
// matrix: at ninety thousand of them the memory saved is worth more than the
// handful of sin/cos rebuilt for the few thousand that survive culling.
struct city_prop {
    vec3  position;
    float yaw;
    float scale;
    float scale_y;      // 0 means "same as scale"
    idx   mesh;
    idx   lod_mesh;     // drawn instead past LOD_DISTANCE; (idx)-1 for none
    idx   material;
    float radius;       // bounding sphere, world units
    float fade;         // never drawn beyond this
};

struct city_block {
    int     x0, z0, x1, z1;    // inclusive cell bounds, road lines excluded
    uint8_t zone;
};

struct city_world {
    city_cell cells[CITY_CELLS * CITY_CELLS];

    // one instanced quad per water cell, drawn in the transparent pass
    city_prop water[CITY_CELLS * CITY_CELLS];
    size_t    water_count;

    city_prop props[CITY_MAX_PROPS];
    size_t    prop_count;
    // props sorted by cell, with per-cell ranges, so culling is a window scan
    uint32_t  cell_prop_start[CITY_CELLS * CITY_CELLS + 1];

    city_block blocks[CITY_MAX_BLOCKS];
    size_t     block_count;

    // Every street lamp head in the city. Stored apart from the props because
    // these are not scenery - after dusk each one is a light the renderer has to
    // be handed, and a frame only wants the handful near the camera.
    vec3     lamps[CITY_MAX_LAMPS];
    size_t   lamp_count;

    // road cells, listed so traffic can pick a spawn point without scanning
    uint32_t road_cells[CITY_CELLS * CITY_CELLS];
    size_t   road_cell_count;
    uint32_t walk_cells[CITY_CELLS * CITY_CELLS];
    size_t   walk_cell_count;

    uint32_t seed;
};

static void city_generate(city_world& world, const city_catalog& cat, phys_world& physics,
                          uint32_t seed);

static city_cell& city_at(city_world& w, int x, int z);
static const city_cell& city_at(const city_world& w, int x, int z);
static bool city_in_bounds(int x, int z);
static bool city_is_road(const city_world& w, int x, int z);
static bool city_is_walkable(const city_world& w, int x, int z);
static bool city_is_water(const city_world& w, int x, int z);
// picks the road tile and rotation that matches a cell's connections
static idx  city_road_mesh(const city_catalog& cat, uint8_t mask, bool crossing, float* out_yaw);

// ---------------- implementation ----------------

static bool city_in_bounds(int x, int z) {
    return x >= 0 && z >= 0 && x < CITY_CELLS && z < CITY_CELLS;
}

static city_cell& city_at(city_world& w, int x, int z) {
    static city_cell dummy;
    if (!city_in_bounds(x, z)) { dummy = city_cell(); dummy.facing = DIR_NONE; return dummy; }
    return w.cells[(size_t)z * CITY_CELLS + x];
}

static const city_cell& city_at(const city_world& w, int x, int z) {
    return city_at(const_cast<city_world&>(w), x, z);
}

static bool city_is_road(const city_world& w, int x, int z) {
    return city_in_bounds(x, z) && city_at(w, x, z).kind == CELL_ROAD;
}

static bool city_is_walkable(const city_world& w, int x, int z) {
    if (!city_in_bounds(x, z)) return false;
    uint8_t k = city_at(w, x, z).kind;
    return k == CELL_SIDEWALK || k == CELL_PARK;
}

static bool city_is_water(const city_world& w, int x, int z) {
    return city_in_bounds(x, z) && city_at(w, x, z).kind == CELL_WATER;
}

// Where a person actually stands in a cell. On a pavement that is not the cell
// centre any more: the building in front of it takes the back half of the cell
// and the kerb strip takes the outer edge, so the free ground is the walking
// strip between them. Anything that puts a character on a pavement - spawning
// one, walking one, dropping the player in - has to use this, or it starts
// inside a wall and gets thrown out by the solver.
static vec3 city_stand_point(const city_world& w, int x, int z) {
    const city_cell& c = city_at(w, x, z);
    vec3 centre = cell_centre(x, z);
    if (c.kind == CELL_SIDEWALK && c.kerb != DIR_NONE)
        return v3add(centre, v3scale(dir_to_vec(c.kerb), WALK_INSET));
    return centre;
}

// ---- road tiles ----

// A road tile's connection mask rotates right by one bit per 90 degrees of yaw,
// because mat4_rotate_y sends +X to -Z and the direction bits are numbered in
// that same order. So matching a cell is a search over four rotations.
static uint8_t city__ror4(uint8_t m, int t) {
    return (uint8_t)(((m >> t) | (m << (4 - t))) & 0xF);
}

static idx city_road_mesh(const city_catalog& cat, uint8_t mask, bool crossing, float* out_yaw) {
    // the connections each tile is authored with, measured from the meshes
    const uint8_t STRAIGHT = 0x0A;  // +Z and -Z
    const uint8_t CORNER   = 0x03;  // +X and +Z
    const uint8_t TEE      = 0x0B;  // +X, +Z and -Z
    const uint8_t CROSS    = 0x0F;

    int bits = (mask & 1) + ((mask >> 1) & 1) + ((mask >> 2) & 1) + ((mask >> 3) & 1);

    for (int turn = 0; turn < 4; turn++) {
        *out_yaw = (float)turn * 1.57079633f;
        if (bits == 4 && city__ror4(CROSS, turn) == mask)    return cat.singles[ONE_ROAD_JUNCTION];
        if (bits == 3 && city__ror4(TEE, turn) == mask)      return cat.singles[ONE_ROAD_TSPLIT];
        if (bits == 2 && city__ror4(STRAIGHT, turn) == mask)
            return cat.singles[crossing ? ONE_ROAD_CROSSING : ONE_ROAD_STRAIGHT];
        if (bits == 2 && city__ror4(CORNER, turn) == mask)   return cat.singles[ONE_ROAD_CORNER];
    }

    // a stub or an orphan: lay a straight piece along whatever it does connect to
    *out_yaw = (mask & 0x05) ? 1.57079633f : 0.0f;
    return cat.singles[ONE_ROAD_STRAIGHT];
}

// ---- prop emission ----

static void city__prop(city_world& w, const city_model* m, vec3 position, float yaw,
                       float scale, float fade, idx lod_mesh = (idx)-1, float scale_y = 0.0f) {
    if (!m || m->mesh == (idx)-1 || w.prop_count >= CITY_MAX_PROPS) return;
    city_prop& p = w.props[w.prop_count++];
    p.position = position;
    p.yaw = yaw;
    p.scale = scale;
    p.scale_y = scale_y;
    p.mesh = m->mesh;
    p.lod_mesh = lod_mesh;
    p.material = m->material;
    p.fade = fade;

    // bounding sphere from the model's own extents, so culling is exact enough
    // that a skyscraper never pops while a bin never survives too long
    vec3 e = v3sub(m->bounds_max, m->bounds_min);
    float sy = scale_y > 0.0f ? scale_y : scale;
    float rx = e.x * 0.5f * scale, rz = e.z * 0.5f * scale, ry = e.y * 0.5f * sy;
    p.radius = sqrtf(rx * rx + ry * ry + rz * rz);
}

// a mesh chosen by hand rather than through the catalogue tables
static void city__prop_mesh(city_world& w, idx mesh, idx material, vec3 position, float yaw,
                            float scale, float scale_y, float radius, float fade) {
    if (mesh == (idx)-1 || w.prop_count >= CITY_MAX_PROPS) return;
    city_prop& p = w.props[w.prop_count++];
    p.position = position;
    p.yaw = yaw;
    p.scale = scale;
    p.scale_y = scale_y;
    p.mesh = mesh;
    p.lod_mesh = (idx)-1;
    p.material = material;
    p.radius = radius;
    p.fade = fade;
}

// ---- parked vehicles ----
//
// A city with cars only where the traffic AI has put them reads as a race
// track: every vehicle in sight is moving, and the kerbs and yards are bare.
// Most of the cars in a real city are standing still, and they are what makes a
// street look inhabited rather than staged.
//
// These are scenery, not traffic - they cost one instanced draw each rather
// than a physics body and a driver, which is what makes it affordable to put
// hundreds of them in. A vehicle is several meshes over one chassis frame, so
// each part's own offset is rotated into the world here and emitted as its own
// prop; the parts are rigid relative to each other, so nothing is lost.
static vec3 city__rot_y(vec3 v, float yaw) {
    float c = cosf(yaw), s = sinf(yaw);
    return v3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
}

static void city__parked_car(city_world& w, const city_catalog& cat, phys_world& phys,
                             vec3 at, float yaw, rng& r) {
    if (!cat.vehicle_count) return;
    const city_vehicle_model& vm = cat.vehicles[rng_u32(r) % cat.vehicle_count];
    if (!vm.valid || vm.body_mesh == (idx)-1) return;

    float chassis_yaw = yaw + vm.model_yaw;
    at.y = ROAD_SURFACE_Y;
    float radius = vm.half_length + vm.half_width;

    vec3 body = v3add(at, city__rot_y(vm.body_offset, chassis_yaw));
    city__prop_mesh(w, vm.body_mesh, vm.material, body, chassis_yaw,
                    vm.scale, vm.scale, radius, PROP_FADE_MID);
    for (int i = 0; i < vm.part_count; i++) {
        vec3 q = v3add(at, city__rot_y(vm.part_offset[i], chassis_yaw));
        city__prop_mesh(w, vm.part_mesh[i], vm.material, q, chassis_yaw,
                        vm.scale, vm.scale, radius, PROP_FADE_MID);
    }
    if (vm.wheel_mesh != (idx)-1)
        for (int i = 0; i < 4; i++) {
            vec3 q = v3add(at, city__rot_y(vm.wheel_pivot[i], chassis_yaw));
            city__prop_mesh(w, vm.wheel_mesh, vm.material, q, chassis_yaw,
                            vm.scale, vm.scale, vm.wheel_radius * 2.0f, PROP_FADE_NEAR);
        }

    phys_add_static_prop(phys, at, chassis_yaw, v2(vm.half_width, vm.half_length), vm.height);
}

// ---- pass 0: the river ----

// Carves a meandering channel across the map. The centreline is a sum of two
// sines rather than a random walk so it stays smooth - a river that wobbles
// per-cell looks like noise, and the banks come out ragged.
static void city__carve_river(city_world& w, rng& r) {
    bool along_x = rng_chance(r, 0.5f);

    float phase_a = rng_range(r, 0.0f, 6.28f);
    float phase_b = rng_range(r, 0.0f, 6.28f);
    float amp_a   = rng_range(r, 6.0f, 13.0f);
    float amp_b   = rng_range(r, 2.5f, 6.0f);
    float centre  = CITY_CELLS * 0.5f + rng_range(r, -10.0f, 10.0f);
    float width   = rng_range(r, (float)RIVER_MIN_WIDTH, (float)RIVER_MAX_WIDTH);

    for (int t = 0; t < CITY_CELLS; t++) {
        float u = (float)t;
        float mid = centre + sinf(u * 0.055f + phase_a) * amp_a
                           + sinf(u * 0.131f + phase_b) * amp_b;
        float half = width * 0.5f + sinf(u * 0.083f) * 0.6f;

        int lo = (int)floorf(mid - half), hi = (int)ceilf(mid + half);
        for (int c = lo; c <= hi; c++) {
            int x = along_x ? t : c;
            int z = along_x ? c : t;
            if (!city_in_bounds(x, z)) continue;
            city_at(w, x, z).kind = CELL_WATER;
        }
    }
}

// A road that runs into the river becomes a bridge - but only if the same road
// picks up again on the far bank. A street that merely runs *along* the water,
// or dead-ends at it, leaves the river alone.
//
// The crossings are decided from a snapshot and applied afterwards. Converting
// cells while scanning would let each new bridge cell qualify its neighbour as
// road-adjacent, and the conversion would eat its way along the channel until
// there was no river left.
#define BRIDGE_MAX_SPAN 9

static void city__build_bridges(city_world& w, const city_catalog& cat, phys_world& phys) {
    static bool deck[CITY_CELLS * CITY_CELLS];
    memset(deck, 0, sizeof(deck));

    for (int z = 0; z < CITY_CELLS; z++)
        for (int x = 0; x < CITY_CELLS; x++) {
            if (city_at(w, x, z).kind != CELL_ROAD) continue;

            for (int d = 0; d < 4; d++) {
                if (!city_is_water(w, x + DIR_DX[d], z + DIR_DZ[d])) continue;

                // walk straight out over the water looking for the far bank
                int span = 1;
                while (span <= BRIDGE_MAX_SPAN
                       && city_is_water(w, x + DIR_DX[d] * span, z + DIR_DZ[d] * span)) span++;
                if (span > BRIDGE_MAX_SPAN) continue;              // too wide to bridge

                int fx = x + DIR_DX[d] * span, fz = z + DIR_DZ[d] * span;
                if (!city_is_road(w, fx, fz)) continue;            // the road does not resume

                for (int k = 1; k < span; k++) {
                    int cx = x + DIR_DX[d] * k, cz = z + DIR_DZ[d] * k;
                    deck[(size_t)cz * CITY_CELLS + cx] = true;
                }
            }
        }

    const city_model* deck_model = city_get(cat, cat.singles[ONE_BRIDGE]);
    const city_model* pier_model = city_get(cat, cat.singles[ONE_PIER]);

    for (int z = 0; z < CITY_CELLS; z++)
        for (int x = 0; x < CITY_CELLS; x++) {
            if (!deck[(size_t)z * CITY_CELLS + x]) continue;
            city_cell& c = city_at(w, x, z);
            c.kind = CELL_ROAD;
            c.flags |= CELLF_BRIDGE;

            vec3 p = cell_centre(x, z);
            // the deck fills the gap between the water and the road surface
            city__prop_mesh(w, deck_model ? deck_model->mesh : (idx)-1, cat.mat_concrete,
                            v3(p.x, WATER_LEVEL + 0.4f, p.z), 0.0f,
                            CITY_TILE, ROAD_Y - (WATER_LEVEL + 0.4f), CITY_TILE, VIEW_DISTANCE);
            if (pier_model && ((x + z) & 1) == 0)
                city__prop_mesh(w, pier_model->mesh, cat.mat_dirt,
                                v3(p.x, RIVER_BED, p.z), 0.0f,
                                CITY_TILE * 0.3f, WATER_LEVEL + 0.4f - RIVER_BED,
                                CITY_TILE, PROP_FADE_MID);
        }
}

// ---- pass 1: streets ----

// Cuts a rectangle in two with a street and recurses, until every piece is a
// block. Preferring the long axis keeps blocks roughly square; the cut position
// is random within the range that leaves both halves legal, which is what makes
// the grid irregular without ever producing a slice too thin to build on.
static void city__subdivide(city_world& w, rng& r, int x0, int z0, int x1, int z1, int depth) {
    int width = x1 - x0 + 1, height = z1 - z0 + 1;
    bool can_x = width  >= BLOCK_MIN * 2 + 1;
    bool can_z = height >= BLOCK_MIN * 2 + 1;
    bool must  = width > BLOCK_MAX || height > BLOCK_MAX;

    if (!must || (!can_x && !can_z) || depth > 20) {
        if (w.block_count < CITY_MAX_BLOCKS) {
            city_block& b = w.blocks[w.block_count++];
            b.x0 = x0; b.z0 = z0; b.x1 = x1; b.z1 = z1;
            b.zone = ZONE_RESIDENTIAL;
        }
        return;
    }

    bool split_x = can_x && (!can_z || width >= height);
    if (split_x) {
        int cut = rng_int(r, x0 + BLOCK_MIN, x1 - BLOCK_MIN);
        for (int z = z0; z <= z1; z++)
            if (city_at(w, cut, z).kind != CELL_WATER) city_at(w, cut, z).kind = CELL_ROAD;
        city__subdivide(w, r, x0, z0, cut - 1, z1, depth + 1);
        city__subdivide(w, r, cut + 1, z0, x1, z1, depth + 1);
    } else {
        int cut = rng_int(r, z0 + BLOCK_MIN, z1 - BLOCK_MIN);
        for (int x = x0; x <= x1; x++)
            if (city_at(w, x, cut).kind != CELL_WATER) city_at(w, x, cut).kind = CELL_ROAD;
        city__subdivide(w, r, x0, z0, x1, cut - 1, depth + 1);
        city__subdivide(w, r, x0, cut + 1, x1, z1, depth + 1);
    }
}

static void city__zone_blocks(city_world& w, rng& r) {
    float centre = CITY_CELLS * 0.5f;
    for (size_t i = 0; i < w.block_count; i++) {
        city_block& b = w.blocks[i];
        float cx = (b.x0 + b.x1) * 0.5f - centre;
        float cz = (b.z0 + b.z1) * 0.5f - centre;
        float dist = sqrtf(cx * cx + cz * cz) / centre;   // 0 at the middle, ~1 at the edge

        if      (dist < 0.15f) b.zone = ZONE_DOWNTOWN;
        else if (dist < 0.34f) b.zone = ZONE_COMMERCIAL;
        else if (dist < 0.58f) b.zone = ZONE_RESIDENTIAL;
        else                   b.zone = ZONE_SUBURB;

        // Parks and industry are placed against the gradient rather than at
        // random: greenery is what the residential ring wants, and industry
        // belongs out where the land is cheap.
        float roll = rng_float(r);
        if (dist > 0.18f && roll < 0.11f)                    b.zone = ZONE_PARK;
        else if (dist > 0.62f && roll > 0.11f && roll < 0.26f) b.zone = ZONE_INDUSTRIAL;

        for (int z = b.z0; z <= b.z1; z++)
            for (int x = b.x0; x <= b.x1; x++) {
                city_cell& c = city_at(w, x, z);
                if (c.kind == CELL_WATER) continue;   // the river predates the block
                c.zone = b.zone;
                if (c.kind != CELL_ROAD) c.kind = (b.zone == ZONE_PARK) ? CELL_PARK : CELL_LOT;
            }
    }
}

// ---- pass 2: sidewalks, frontage, junctions ----

static void city__derive_topology(city_world& w) {
    for (int z = 0; z < CITY_CELLS; z++)
        for (int x = 0; x < CITY_CELLS; x++) {
            city_cell& c = city_at(w, x, z);
            c.facing = DIR_NONE;
            c.kerb = DIR_NONE;
            c.depth = 0xFF;
            if (c.kind == CELL_WATER) continue;

            // land touching the river becomes embankment rather than a plot;
            // nobody builds a tower with its foundations in the water
            for (int d = 0; d < 4; d++)
                if (city_is_water(w, x + DIR_DX[d], z + DIR_DZ[d]) && c.kind == CELL_LOT)
                    c.kind = CELL_PARK;

            uint8_t mask = 0;
            for (int d = 0; d < 4; d++)
                if (city_is_road(w, x + DIR_DX[d], z + DIR_DZ[d])) mask |= (uint8_t)(1 << d);
            c.road_mask = mask;

            if (c.kind == CELL_ROAD) {
                int bits = (mask & 1) + ((mask >> 1) & 1) + ((mask >> 2) & 1) + ((mask >> 3) & 1);
                if (bits >= 3) c.flags |= CELLF_JUNCTION;
                continue;
            }
            if (!mask) continue;

            // touching a road at all makes this pavement. Which way its kerb
            // faces is settled by the frontage pass below, because a corner
            // pavement touches two streets and the answer has to be the same
            // one the building in front of it chose.
            c.kind = CELL_SIDEWALK;
        }

    // A crossing goes on the road cells immediately either side of a junction,
    // which is where people would actually step off the kerb.
    for (int z = 0; z < CITY_CELLS; z++)
        for (int x = 0; x < CITY_CELLS; x++) {
            city_cell& c = city_at(w, x, z);
            if (c.kind != CELL_ROAD || (c.flags & CELLF_JUNCTION)) continue;
            for (int d = 0; d < 4; d++) {
                const city_cell& n = city_at(w, x + DIR_DX[d], z + DIR_DZ[d]);
                if (n.kind == CELL_ROAD && (n.flags & CELLF_JUNCTION)) {
                    c.flags |= CELLF_CROSSING;
                    break;
                }
            }
        }

    // Frontage.
    //
    // A lot fronts a street when the pavement next to it backs onto one. Corner
    // lots see two, and the tie is broken by a hash of the cell so neighbouring
    // corners do not all turn the same way.
    //
    // Two things are settled here at once, and they have to be settled together.
    // A building is pushed forward into the pavement cell it fronts, so that
    // pavement cell's kerb *is* the direction the building faces - if the two
    // were derived separately they disagree on corners, and the street
    // furniture ends up planted along the building line with the whole footpath
    // empty behind it. And a pavement cell can only take one building, or two
    // corner lots both push into it and their walls intersect.
    static uint32_t queue[CITY_CELLS * CITY_CELLS];
    size_t head = 0, tail = 0;

    for (int z = 0; z < CITY_CELLS; z++)
        for (int x = 0; x < CITY_CELLS; x++) {
            city_cell& c = city_at(w, x, z);
            if (c.kind != CELL_LOT) continue;
            uint32_t start = hash2(x, z, 0x51A7u) & 3;
            for (int i = 0; i < 4; i++) {
                int d = (int)((start + i) & 3);
                int sx = x + DIR_DX[d], sz = z + DIR_DZ[d];
                if (!city_in_bounds(sx, sz)) continue;
                city_cell& pave = city_at(w, sx, sz);
                if (pave.kind != CELL_SIDEWALK) continue;
                if (pave.kerb != DIR_NONE) continue;              // already spoken for
                if (!city_is_road(w, sx + DIR_DX[d], sz + DIR_DZ[d])) continue;
                pave.kerb = (uint8_t)d;
                c.facing = (uint8_t)d;
                c.depth = 0;
                queue[tail++] = (uint32_t)((size_t)z * CITY_CELLS + x);
                break;
            }
        }

    // Pavement with nothing fronting it - the outside of a corner, a stretch
    // beside a park - still needs a kerb for its lamps to line up against, and
    // there its own road adjacency is the whole answer.
    for (int z = 0; z < CITY_CELLS; z++)
        for (int x = 0; x < CITY_CELLS; x++) {
            city_cell& c = city_at(w, x, z);
            if (c.kind != CELL_SIDEWALK || c.kerb != DIR_NONE) continue;
            for (int d = 0; d < 4; d++)
                if (c.road_mask & (1 << d)) { c.kerb = (uint8_t)d; break; }
        }

    // How far back from the street every other lot cell sits, and which street
    // it belongs to - a breadth-first flood out of the frontage cells. A second
    // row inherits the facing of the row in front of it, so a whole block turns
    // the same way instead of each cell picking its own nearest pavement.
    while (head < tail) {
        uint32_t id = queue[head++];
        int x = (int)(id % CITY_CELLS), z = (int)(id / CITY_CELLS);
        const city_cell& from = city_at(w, x, z);
        uint8_t next_depth = (uint8_t)(from.depth + 1);
        if (next_depth == 0xFF) continue;

        for (int d = 0; d < 4; d++) {
            int nx = x + DIR_DX[d], nz = z + DIR_DZ[d];
            if (!city_in_bounds(nx, nz)) continue;
            city_cell& n = city_at(w, nx, nz);
            if (n.kind != CELL_LOT || n.depth != 0xFF) continue;
            n.depth = next_depth;
            n.facing = from.facing;
            queue[tail++] = (uint32_t)((size_t)nz * CITY_CELLS + nx);
        }
    }
}

// ---- pass 3: buildings ----
//
// Every building in the city, downtown tower to suburban house, is one of
// KayKit's 8 building_A..H models: each is modelled to exactly fill one road
// tile, so placing one is just dropping it on a lot cell's centre facing the
// street - no footprint fitting, no per-model scale search.
//
// What varies by zone is three things: which of the 8 shapes a lot reaches for,
// how deep into the block the built frontage runs, and which facade colour the
// street is built in. That last one is the difference between a street and a
// row of samples: a real street is mostly one or two materials with the odd
// outlier, so the facade is picked per *block* with a small chance of a
// building breaking rank, not per building.

// building_A..H, in the load order city_assets.hpp lists them, grouped by
// their own authored height rather than stretched to fake one: KayKit's
// proportions are baked into the mesh (window rows, door height, cornice
// lines), and pushing a single model's Y axis by any real amount just smears
// those details into the wrong shape. Real height variety by zone comes from
// which of the 8 models a lot reaches for, not from distorting whichever one
// it got. Measured heights: A/B ~6.6m, E/F ~9.4m, C/D/G/H ~11.9-12.2m.
static const int BUILDING_SHORT[]  = { 0, 1 };
static const int BUILDING_MEDIUM[] = { 4, 5 };
static const int BUILDING_TALL[]   = { 2, 3, 6, 7 };

static int city__pick_building_kind(uint8_t zone, rng& r) {
    const int* pool = BUILDING_SHORT;
    int count = 2;
    switch (zone) {
        case ZONE_DOWNTOWN:
            if (rng_chance(r, 0.75f)) { pool = BUILDING_TALL; count = 4; }
            else                      { pool = BUILDING_MEDIUM; count = 2; }
            break;
        case ZONE_COMMERCIAL:
            if (rng_chance(r, 0.5f)) { pool = BUILDING_TALL; count = 4; }
            else                     { pool = BUILDING_MEDIUM; count = 2; }
            break;
        case ZONE_RESIDENTIAL:
            if (rng_chance(r, 0.4f)) { pool = BUILDING_MEDIUM; count = 2; }
            break;                            // else stays BUILDING_SHORT
        default:
            break;                            // SUBURB and INDUSTRIAL: BUILDING_SHORT
    }
    return pool[rng_int(r, 0, count - 1)];
}

// BUILDING_FACADES is ordered commercial-first (slate, pale blue-grey) then
// residential (sandstone, brick, terracotta), so a zone is a window into it
// rather than a table of its own.
static int city__pick_facade(uint8_t zone, rng& r) {
    switch (zone) {
        case ZONE_DOWNTOWN:   return rng_int(r, 0, 1);
        case ZONE_COMMERCIAL: return rng_chance(r, 0.7f) ? rng_int(r, 0, 1) : 2;
        case ZONE_INDUSTRIAL: return rng_chance(r, 0.6f) ? 1 : 3;
        default:              return rng_int(r, 2, 4);
    }
}

static float city__lot_density(uint8_t zone) {
    switch (zone) {
        case ZONE_DOWNTOWN:    return 0.95f;
        case ZONE_COMMERCIAL:  return 0.86f;
        case ZONE_RESIDENTIAL: return 0.72f;
        case ZONE_INDUSTRIAL:  return 0.40f;
        default:               return 0.55f;   // ZONE_SUBURB: room to breathe
    }
}

// How many rows back from the street a zone builds. Downtown blocks are solid
// to the party wall; a suburb is one row of houses with gardens behind them.
static uint8_t city__build_depth(uint8_t zone) {
    switch (zone) {
        case ZONE_DOWNTOWN:   return 2;
        case ZONE_COMMERCIAL: return 1;
        default:              return 0;
    }
}

static void city__place_buildings(city_world& w, const city_catalog& cat, phys_world& phys) {
    for (size_t bi = 0; bi < w.block_count; bi++) {
        const city_block& b = w.blocks[bi];
        if (b.zone == ZONE_PARK) continue;
        float density = city__lot_density(b.zone);
        uint8_t max_depth = city__build_depth(b.zone);

        // one facade for the whole block, so a street reads as having been
        // built at one time out of one material
        rng br = rng_at(b.x0, b.z0, w.seed ^ 0xFACAu);
        int block_facade = city__pick_facade(b.zone, br);

        for (int z = b.z0; z <= b.z1; z++)
            for (int x = b.x0; x <= b.x1; x++) {
                city_cell& c = city_at(w, x, z);
                if (c.kind != CELL_LOT || c.facing == DIR_NONE) continue;
                if (c.flags & CELLF_OCCUPIED) continue;
                if (c.depth > max_depth) continue;        // block interior, not a frontage

                rng r = rng_at(x, z, w.seed ^ 0xB0D1u);
                // the back rows are always sparser than the street front
                float lot_density = density * (c.depth == 0 ? 1.0f : 0.62f);
                if (!rng_chance(r, lot_density)) continue;   // a gap: yard, scrub, empty lot

                // most of the block in its own colour, the odd infill in another
                int facade = rng_chance(r, 0.82f) ? block_facade : city__pick_facade(b.zone, r);
                const city_model* m = city_building(cat, facade, city__pick_building_kind(b.zone, r));
                if (!m) continue;
                c.flags |= CELLF_OCCUPIED;

                // Pushed streetward onto the pavement cell so its facade lands
                // on the building line rather than a whole tile behind it. Every
                // row of the block moves by the same amount, so the rows stay
                // shoulder to shoulder and only the courtyard behind them grows.
                vec3 pos = v3add(cell_centre(x, z),
                                 v3scale(dir_to_vec(c.facing), LOT_LINE_SHIFT));
                float yaw = dir_to_yaw_building(c.facing);
                // a few percent of natural variance, not a fake extra storey
                float sy = rng_range(r, 0.94f, 1.10f);

                city__prop(w, m, pos, yaw, m->scale, VIEW_DISTANCE, (idx)-1, m->scale * sy);

                vec3 ext = v3sub(m->bounds_max, m->bounds_min);
                phys_add_static_prop(phys, pos, yaw,
                                     v2(ext.x * 0.5f * m->scale, ext.z * 0.5f * m->scale),
                                     ext.y * m->scale * sy);
            }
    }
}

// ---- pass 4: street furniture ----
//
// Everything here goes in the kerb strip - the outer KERB_STRIP metres of the
// pavement cell, against the road. That is where a real street puts its lamps,
// signs, bins, benches and trees, and it is what leaves the rest of the
// footpath clear to walk down. Nothing static is ever placed in the walking
// strip; a street tree planted in the middle of the pavement is the single
// most obviously wrong thing a generated street can do.

static void city__place_street_furniture(city_world& w, const city_catalog& cat, phys_world& phys) {
    for (int z = 0; z < CITY_CELLS; z++)
        for (int x = 0; x < CITY_CELLS; x++) {
            const city_cell& c = city_at(w, x, z);
            if (c.kind != CELL_SIDEWALK || c.kerb == DIR_NONE) continue;

            int kerb = c.kerb;
            vec3 base = cell_centre(x, z);
            vec3 out  = dir_to_vec(kerb);                    // toward the road
            vec3 kerb_pos = v3add(base, v3scale(out, KERB_INSET));
            rng r = rng_at(x, z, w.seed ^ 0x5CA1u);

            // Lamps go on a fixed stride along the run rather than at random,
            // because street lighting that is not evenly spaced reads as wrong
            // immediately. The stride is measured along the road, so a corner
            // never gets two lamps on top of each other.
            int along_axis = (kerb == DIR_PX || kerb == DIR_NX) ? z : x;
            bool junction_corner = false;
            for (int d = 0; d < 4; d++) {
                const city_cell& n = city_at(w, x + DIR_DX[d], z + DIR_DZ[d]);
                if (n.kind == CELL_ROAD && (n.flags & CELLF_JUNCTION)) junction_corner = true;
            }

            // One thing per cell in the kerb strip: two would either intersect
            // or crowd the one lane of pavement they share. Lamps and signals
            // come first because they are the ones that have to line up.
            bool taken = false;

            if (along_axis % 4 == 0 && !junction_corner) {
                const city_model* lamp = city_get(cat, cat.singles[ONE_STREETLIGHT]);
                city__prop(w, lamp, kerb_pos, dir_to_yaw_arm(kerb), KIT_PROP_SCALE, PROP_FADE_MID);
                phys_add_static_prop(phys, kerb_pos, 0.0f, v2(0.16f, 0.16f), 3.6f);
                // The lamp head is on an arm reaching out over the carriageway,
                // so the light does not hang above the post - it hangs where the
                // arm puts it, which is what makes a lit street read as lit
                // rather than as a row of glowing sticks.
                if (w.lamp_count < CITY_MAX_LAMPS)
                    w.lamps[w.lamp_count++] =
                        v3add(kerb_pos, v3(out.x * 1.1f, LAMP_HEIGHT, out.z * 1.1f));
                taken = true;
            }

            // traffic signals face the traffic that has to stop for them
            if (!taken && junction_corner && rng_chance(r, 0.55f)) {
                const city_model* light = city_get(cat, cat.singles[ONE_TRAFFICLIGHT]);
                city__prop(w, light, kerb_pos, dir_to_yaw((kerb + 2) & 3), KIT_PROP_SCALE, PROP_FADE_MID);
                phys_add_static_prop(phys, kerb_pos, 0.0f, v2(0.22f, 0.22f), 2.8f);
                taken = true;
            }

            // hydrants space themselves out along the kerb the same way
            if (!taken && !junction_corner && along_axis % 9 == 3) {
                const city_model* hyd = city_get(cat, cat.singles[ONE_HYDRANT]);
                city__prop(w, hyd, kerb_pos, rng_range(r, 0.0f, 6.28f), KIT_PROP_SCALE, PROP_FADE_NEAR);
                taken = true;
            }

            // Street trees. Planted in the kerb strip beside the lamps, on the
            // stride between them, and only where a street tree belongs: a bare
            // downtown pavement is bare on purpose.
            bool leafy = c.zone == ZONE_SUBURB || c.zone == ZONE_RESIDENTIAL || c.zone == ZONE_PARK;
            if (!taken && leafy && !junction_corner && (along_axis % 4 == 2)
                && rng_chance(r, 0.75f)) {
                const city_model* tree = city_pick(cat, SET_TREE_STREET, rng_u32(r));
                if (tree) {
                    float s = tree->scale * rng_range(r, 0.85f, 1.15f);
                    city__prop(w, tree, kerb_pos, rng_range(r, 0.0f, 6.28f), s, PROP_FADE_MID);
                    // Only the trunk is a collider. The canopy is well above
                    // head height and is meant to overhang the footpath.
                    phys_add_static_prop(phys, kerb_pos, 0.0f, v2(0.30f, 0.30f), 3.0f);
                    taken = true;
                }
            }

            // A bin or a bench belongs where people walk past a door, so it
            // goes outside an occupied lot - still in the kerb strip, facing
            // the street, never in the middle of the path.
            int inward = (kerb + 2) & 3;
            const city_cell& behind = city_at(w, x + DIR_DX[inward], z + DIR_DZ[inward]);
            if (!taken && (behind.flags & CELLF_OCCUPIED)) {
                if (rng_chance(r, 0.20f)) {
                    const city_model* bin = city_get(cat, cat.singles[ONE_BIN]);
                    city__prop(w, bin, kerb_pos, rng_range(r, 0.0f, 6.28f),
                               KIT_PROP_SCALE, PROP_FADE_NEAR);
                } else if (rng_chance(r, 0.12f)) {
                    const city_model* bench = city_get(cat, cat.singles[ONE_BENCH]);
                    // seat turned to look out over the street
                    city__prop(w, bench, kerb_pos, dir_to_yaw(kerb), KIT_PROP_SCALE, PROP_FADE_MID);
                    phys_add_static_prop(phys, kerb_pos, dir_to_yaw(kerb), v2(0.8f, 0.3f), 0.9f);
                }
            }
        }
}

// ---- pass 5: what fills the rest of a lot ----
//
// Three different jobs, told apart by where in the block a cell sits:
//
//   park       canopy, shrub layer and ground cover, laid on top of each other
//   frontage   the strip of an unbuilt lot between the house line and the
//              street: drive, path, fence, a tree
//   interior   everything past the zone's build depth. This is the half of a
//              block that used to come out as bare ground, and it is what made
//              a block read as a field with buildings round the edge. A real
//              block interior is a courtyard, a car park or a run of back
//              gardens, so that is what goes in it - by zone, not at random.

// Ground cover that belongs on soil: never dropped on a cell whose ground is
// going to be poured concrete, because a tuft of grass growing out of a car
// park is the sort of thing that reads as wrong without being nameable.
static bool city__is_soft_ground(const city_cell& c) {
    if (c.kind == CELL_PARK || c.kind == CELL_GROUND) return true;
    return c.kind == CELL_LOT && (c.zone == ZONE_SUBURB || c.zone == ZONE_RESIDENTIAL);
}

static void city__dress_park(city_world& w, const city_catalog& cat, phys_world& phys,
                             int x, int z, rng& r) {
    vec3 centre = cell_centre(x, z);
    if (rng_chance(r, 0.42f)) {
        const city_model* t = city_pick(cat, SET_TREE_PARK, rng_u32(r));
        vec3 p = v3add(centre, v3(rng_range(r, -2.5f, 2.5f), 0.0f, rng_range(r, -2.5f, 2.5f)));
        float s = t ? t->scale * rng_range(r, 0.85f, 1.2f) : 1.0f;
        city__prop(w, t, p, rng_range(r, 0.0f, 6.28f), s, VIEW_DISTANCE * 0.6f);
        phys_add_static_prop(phys, p, 0.0f, v2(0.4f, 0.4f), 5.0f);
    }
    for (int k = 0; k < 4; k++) {
        if (!rng_chance(r, 0.55f)) continue;
        const city_model* sh = city_pick(cat, SET_SHRUB, rng_u32(r));
        vec3 p = v3add(centre, v3(rng_range(r, -3.5f, 3.5f), 0.0f, rng_range(r, -3.5f, 3.5f)));
        city__prop(w, sh, p, rng_range(r, 0.0f, 6.28f),
                   sh ? sh->scale * rng_range(r, 0.8f, 1.25f) : 1.0f, PROP_FADE_NEAR);
    }
    if (rng_chance(r, 0.16f)) {
        const city_model* rk = city_pick(cat, SET_ROCK, rng_u32(r));
        vec3 p = v3add(centre, v3(rng_range(r, -2.0f, 2.0f), 0.0f, rng_range(r, -2.0f, 2.0f)));
        city__prop(w, rk, p, rng_range(r, 0.0f, 6.28f), rk ? rk->scale : 1.0f, PROP_FADE_MID);
    }
    if (rng_chance(r, 0.045f)) {
        const city_model* f = city_pick(cat, SET_PARK_FEATURE, rng_u32(r));
        city__prop(w, f, centre, rng_range(r, 0.0f, 6.28f), f ? f->scale : 1.0f, PROP_FADE_MID);
        phys_add_static_prop(phys, centre, 0.0f, v2(0.8f, 0.8f), 2.0f);
    }
}

static void city__dress_frontage(city_world& w, const city_catalog& cat, phys_world& phys,
                                 int x, int z, city_cell& c, rng& r) {
    c.flags |= CELLF_YARD;
    int f = c.facing;
    vec3 base = cell_centre(x, z);

    // A run of paving from the house line out to the footpath, which is a whole
    // LOT_LINE_SHIFT further out than the lot cell's own edge now.
    bool paved_run = rng_chance(r, 0.5f);
    const city_model* paving = city_get(cat, cat.singles[paved_run ? ONE_DRIVEWAY : ONE_PATH]);
    if (paving) {
        for (int step = 0; step < 3; step++) {
            float along = LOT_LINE_SHIFT + CITY_TILE * (0.2f - 0.32f * (float)step);
            vec3 q = v3add(base, v3scale(dir_to_vec(f), along));
            city__prop(w, paving, q, dir_to_yaw(f), paving->scale, PROP_FADE_MID);
        }
    }

    // fence along the two lot boundaries that run to the street
    const city_model* fence = city_get(cat, cat.singles[ONE_FENCE]);
    if (fence) {
        int side = (f + 1) & 3;
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            if (!rng_chance(r, 0.55f)) continue;
            vec3 q = v3add(base, v3scale(dir_to_vec(side), sgn * CITY_TILE * 0.5f));
            city__prop(w, fence, q, dir_to_yaw_along(side), fence->scale, PROP_FADE_MID);
            phys_add_static_prop(phys, q, dir_to_yaw_along(side),
                                 v2(CITY_TILE * 0.5f, 0.2f), 1.2f);
        }
    }

    // somebody's car on the drive, nose in toward the house
    if (paved_run && rng_chance(r, 0.45f))
        city__parked_car(w, cat, phys,
                         v3add(base, v3scale(dir_to_vec(f), LOT_LINE_SHIFT - CITY_TILE * 0.1f)),
                         dir_to_yaw((f + 2) & 3), r);

    if (rng_chance(r, 0.5f)) {
        const city_model* t = city_pick(cat, SET_TREE_STREET, rng_u32(r));
        vec3 p = v3add(base, v3scale(dir_to_vec((f + 1) & 3), rng_range(r, -2.5f, 2.5f)));
        city__prop(w, t, p, rng_range(r, 0.0f, 6.28f),
                   t ? t->scale * rng_range(r, 0.85f, 1.1f) : 1.0f, PROP_FADE_MID);
        phys_add_static_prop(phys, p, 0.0f, v2(0.3f, 0.3f), 3.0f);
    }
    for (int k = 0; k < 2; k++) {
        if (!rng_chance(r, 0.45f)) continue;
        const city_model* sh = city_pick(cat, SET_SHRUB, rng_u32(r));
        vec3 p = v3add(base, v3(rng_range(r, -3.0f, 3.0f), 0.0f, rng_range(r, -3.0f, 3.0f)));
        city__prop(w, sh, p, rng_range(r, 0.0f, 6.28f),
                   sh ? sh->scale * rng_range(r, 0.8f, 1.2f) : 1.0f, PROP_FADE_NEAR);
    }
}

// The middle of a block. What belongs here is entirely a question of what the
// block is: a downtown courtyard is service ground with a skip and a planter, a
// suburb is back gardens, an industrial block is a yard full of crates.
static void city__dress_interior(city_world& w, const city_catalog& cat, phys_world& phys,
                                 int x, int z, const city_cell& c, rng& r) {
    vec3 centre = cell_centre(x, z);

    if (c.zone == ZONE_INDUSTRIAL) {
        if (rng_chance(r, 0.40f)) {
            const city_model* m = city_pick(cat, SET_INDUSTRIAL, rng_u32(r));
            city__prop(w, m, centre, rng_range(r, 0.0f, 6.28f),
                       m ? m->scale : 1.0f, PROP_FADE_MID);
            phys_add_static_prop(phys, centre, 0.0f, v2(1.5f, 1.5f), 3.0f);
        } else if (rng_chance(r, 0.35f)) {
            // something standing in the yard, squared up to the fence line
            city__parked_car(w, cat, phys, centre,
                             dir_to_yaw(rng_int(r, 0, 3)), r);
        }
        return;
    }

    if (c.zone == ZONE_DOWNTOWN || c.zone == ZONE_COMMERCIAL) {
        // The middle of a commercial block is a car park. That is what is
        // actually behind a row of shops, and it is also the answer to the one
        // thing that made these blocks read as unfinished: an 8 m square of
        // bare concrete with a wheelie bin on it. Two bays per cell, squared up
        // to the block rather than scattered, because a car park is the most
        // rigidly aligned thing in a city.
        int aisle = (c.facing == DIR_NONE) ? 0 : ((c.facing + 1) & 3);
        float bay_yaw = dir_to_yaw(aisle);
        vec3 across = dir_to_vec(aisle);
        vec3 along  = dir_to_vec((aisle + 1) & 3);

        for (int bay = -1; bay <= 1; bay += 2) {
            if (!rng_chance(r, 0.62f)) continue;
            vec3 q = v3add(centre, v3scale(along, (float)bay * 1.5f));
            city__parked_car(w, cat, phys, q, bay_yaw, r);
        }
        // and the service end of the yard
        if (rng_chance(r, 0.22f)) {
            const city_model* m = city_get(cat, cat.singles[ONE_DUMPSTER]);
            vec3 q = v3add(centre, v3scale(across, 3.0f));
            city__prop(w, m, q, bay_yaw, KIT_PROP_SCALE, PROP_FADE_NEAR);
            phys_add_static_prop(phys, q, bay_yaw, v2(1.2f, 0.8f), 1.3f);
        }
        return;
    }

    // back gardens: a tree, hedging and ground cover, and a fence line between
    // one garden and the next
    if (rng_chance(r, 0.38f)) {
        const city_model* t = city_pick(cat, SET_TREE_PARK, rng_u32(r));
        vec3 p = v3add(centre, v3(rng_range(r, -2.2f, 2.2f), 0.0f, rng_range(r, -2.2f, 2.2f)));
        city__prop(w, t, p, rng_range(r, 0.0f, 6.28f),
                   t ? t->scale * rng_range(r, 0.75f, 1.0f) : 1.0f, PROP_FADE_MID);
        phys_add_static_prop(phys, p, 0.0f, v2(0.35f, 0.35f), 4.0f);
    }
    for (int k = 0; k < 3; k++) {
        if (!rng_chance(r, 0.42f)) continue;
        const city_model* sh = city_pick(cat, SET_SHRUB, rng_u32(r));
        vec3 p = v3add(centre, v3(rng_range(r, -3.2f, 3.2f), 0.0f, rng_range(r, -3.2f, 3.2f)));
        city__prop(w, sh, p, rng_range(r, 0.0f, 6.28f),
                   sh ? sh->scale * rng_range(r, 0.8f, 1.25f) : 1.0f, PROP_FADE_NEAR);
    }
    const city_model* fence = city_get(cat, cat.singles[ONE_FENCE]);
    if (fence && rng_chance(r, 0.35f)) {
        int side = (c.facing == DIR_NONE ? 0 : ((c.facing + 1) & 3));
        vec3 q = v3add(centre, v3scale(dir_to_vec(side), CITY_TILE * 0.5f));
        city__prop(w, fence, q, dir_to_yaw_along(side), fence->scale, PROP_FADE_MID);
        phys_add_static_prop(phys, q, dir_to_yaw_along(side), v2(CITY_TILE * 0.5f, 0.2f), 1.2f);
    }
}

static void city__dress_lots(city_world& w, const city_catalog& cat, phys_world& phys) {
    for (int z = 0; z < CITY_CELLS; z++)
        for (int x = 0; x < CITY_CELLS; x++) {
            city_cell& c = city_at(w, x, z);
            rng r = rng_at(x, z, w.seed ^ 0x77E3u);

            if (c.kind == CELL_PARK)  { city__dress_park(w, cat, phys, x, z, r); continue; }
            if (c.kind != CELL_LOT)   continue;
            if (c.flags & CELLF_OCCUPIED) continue;

            uint8_t built = city__build_depth(c.zone);
            if (c.depth <= built && c.facing != DIR_NONE
                && (c.zone == ZONE_SUBURB || c.zone == ZONE_RESIDENTIAL)) {
                city__dress_frontage(w, cat, phys, x, z, c, r);
                continue;
            }
            city__dress_interior(w, cat, phys, x, z, c, r);
        }
}

// ---- pass 6: ground and roads ----

static void city__place_ground(city_world& w, const city_catalog& cat) {
    const city_model* quad = city_get(cat, cat.singles[ONE_QUAD]);
    const city_model* slab = city_get(cat, cat.singles[ONE_SLAB]);
    const city_model* wtile = city_get(cat, cat.singles[ONE_WATER_TILE]);
    if (!quad) return;

    for (int z = 0; z < CITY_CELLS; z++)
        for (int x = 0; x < CITY_CELLS; x++) {
            const city_cell& c = city_at(w, x, z);
            vec3 p = cell_centre(x, z);

            if (c.kind == CELL_WATER) {
                // riverbed, and the surface itself into the transparent list
                city__prop_mesh(w, quad->mesh, cat.mat_riverbed,
                                v3(p.x, RIVER_BED, p.z), 0.0f,
                                CITY_TILE, CITY_TILE, CITY_TILE * 0.72f, VIEW_DISTANCE);
                if (wtile && w.water_count < CITY_CELLS * CITY_CELLS) {
                    city_prop& s = w.water[w.water_count++];
                    s.position = v3(p.x, WATER_LEVEL, p.z);
                    s.yaw = 0.0f;
                    s.scale = CITY_TILE;
                    s.scale_y = CITY_TILE;
                    s.mesh = wtile->mesh;
                    s.lod_mesh = (idx)-1;
                    s.material = cat.mat_water;
                    // the wave shader lifts vertices, so the cull sphere has to
                    // allow for a crest standing above the flat tile
                    s.radius = CITY_TILE * 0.8f;
                    s.fade = VIEW_DISTANCE;
                }
                continue;
            }

            if (c.kind == CELL_ROAD) {
                float yaw;
                idx mesh = city_road_mesh(cat, c.road_mask, (c.flags & CELLF_CROSSING) != 0, &yaw);
                city__prop_mesh(w, mesh, cat.mat_city, v3(p.x, ROAD_Y, p.z), yaw,
                                KIT_ROAD_SCALE, KIT_ROAD_SCALE, CITY_TILE * 0.75f, VIEW_DISTANCE);
                continue;
            }

            // Land against the river needs something holding it up, or the
            // ground reads as a sheet floating over a hole. This block's top is
            // flush with the street, so all that shows is the wall face.
            bool bankside = false;
            for (int d = 0; d < 4; d++)
                if (city_is_water(w, x + DIR_DX[d], z + DIR_DZ[d])) bankside = true;
            if (bankside && slab)
                city__prop_mesh(w, slab->mesh, cat.mat_stone,
                                v3(p.x, RIVER_BED - 0.5f, p.z), 0.0f,
                                CITY_TILE, -(RIVER_BED - 0.5f), CITY_TILE, VIEW_DISTANCE);

            // What the ground is made of, and it matters more than a texture
            // choice: pavement and lot were both plain concrete, so a block
            // interior and the footpath in front of it were one unbroken sheet
            // of the same grey and the whole street read as a plaza. Three
            // surfaces tell the three apart - pavement is concrete, a hard lot
            // is the darker asphalt of a yard or a car park, and a soft lot is
            // turf.
            //
            // Grass is handed one of a few shades of the same green, chosen by
            // a hash of the cell. One flat colour over a whole park or verge is
            // what makes ground read as a billiard table rather than as turf,
            // and it costs three extra materials to fix.
            idx material;
            if (city__is_soft_ground(c))
                material = cat.mat_grass_shades[hash2(x, z, 0x6A55u) % CITY_GRASS_SHADES];
            else if (c.kind == CELL_LOT)
                material = cat.mat_asphalt;
            else
                material = cat.mat_concrete;

            city__prop_mesh(w, quad->mesh, material, p, 0.0f,
                            CITY_TILE, CITY_TILE, CITY_TILE * 0.72f, VIEW_DISTANCE);
        }
}

// ---- pass 6b: the freight line ----
//
// A city has an edge, and what is usually at it is infrastructure rather than
// more city. The belt outside the ring road is the only land in the map that
// belongs to nothing, so the railway runs down it: a straight line the whole
// length of the map, with rolling stock standing on it.
//
// It is also what makes the industrial zoning mean something. Industry was
// being scattered out at the edge because land is cheap there; with a line to
// load from, it is out there because that is where the trains are.
static void city__lay_railway(city_world& w, const city_catalog& cat, phys_world& phys) {
    if (!cat.sets[SET_RAILROAD].count) return;
    const city_model* track = city_pick(cat, SET_RAILROAD, 0);
    if (!track || track->mesh == (idx)-1) return;

    // the middle of the two-cell belt on the far side of the ring road
    const int line_z = 1;
    rng r = rng_at(line_z, 0, w.seed ^ 0x7A11u);

    for (int x = 0; x < CITY_CELLS; x++) {
        const city_cell& c = city_at(w, x, line_z);
        if (c.kind == CELL_WATER) continue;          // the river gets there first
        vec3 p = cell_centre(x, line_z, 0.02f);
        // the tile is authored running along its own +Z, so it is turned to lie
        // along the line
        city__prop_mesh(w, track->mesh, track->material, p, 1.57079633f,
                        track->scale, track->scale, CITY_TILE, VIEW_DISTANCE);
    }

    if (!cat.sets[SET_TRAIN].count) return;

    // One train, standing. Where it stands is decided from the seed so it is
    // somewhere different in every city, and it is laid out the way a train is:
    // a locomotive and then whatever it is pulling, nose to tail.
    int head = rng_int(r, 12, CITY_CELLS - 30);
    int cars = rng_int(r, 5, 9);
    for (int i = 0; i < cars; i++) {
        int x = head + i;
        if (!city_in_bounds(x, line_z) || city_at(w, x, line_z).kind == CELL_WATER) break;
        // the locomotive leads; everything behind it is picked at random
        const city_model* m = (i == 0) ? city_pick(cat, SET_TRAIN, 0)
                                       : city_pick(cat, SET_TRAIN, rng_u32(r) | 1u);
        if (!m) continue;
        vec3 p = cell_centre(x, line_z, 0.12f);
        city__prop(w, m, p, 1.57079633f, m->scale, VIEW_DISTANCE);
        phys_add_static_prop(phys, p, 1.57079633f, v2(CITY_TILE * 0.45f, 1.6f), 4.0f);
    }
}

// ---- pass 6c: the waterfront ----
//
// Walked from the *water* side rather than the land side, and that is the whole
// point: every water cell asks each of its four neighbours whether it is water
// too, and wherever the answer is no there is a bank, so that edge gets a
// barrier. Coming at it from the land side means a corner cell with water on
// two sides only ever fences one of them, and a road that runs to the
// waterline gets nothing at all - which leaves gaps you can walk, and drive,
// straight through into the river.
//
// The barrier is a collider first and a railing second. The collider is what
// makes the water unreachable and it goes on every edge without exception; the
// railing is scenery and is skipped where a railing would be wrong, which is
// nowhere so far but will be the moment there is a slipway or a beach.
#define BANK_WALL_HEIGHT 1.6f
#define BANK_WALL_DEPTH  0.35f

static void city__fence_water(city_world& w, const city_catalog& cat, phys_world& phys) {
    for (int z = 0; z < CITY_CELLS; z++)
        for (int x = 0; x < CITY_CELLS; x++) {
            if (!city_is_water(w, x, z)) continue;
            rng r = rng_at(x, z, w.seed ^ 0x0EA1u);

            for (int d = 0; d < 4; d++) {
                int nx = x + DIR_DX[d], nz = z + DIR_DZ[d];
                // The map edge needs no wall: there is nothing out there to
                // walk in from, and the river is allowed to run off the map.
                if (!city_in_bounds(nx, nz)) continue;
                if (city_is_water(w, nx, nz)) continue;

                const city_cell& land = city_at(w, nx, nz);
                bool decked = (land.kind == CELL_ROAD);
                float top = decked ? ROAD_Y : 0.0f;

                // The shared edge, half a tile out from the water cell's centre,
                // running perpendicular to the direction it was found in.
                vec3 edge = v3add(cell_centre(x, z), v3scale(dir_to_vec(d), CITY_TILE * 0.5f));
                float along_yaw = dir_to_yaw_along((d + 1) & 3);

                // Sunk below the surface it stands on so nothing can be nudged
                // underneath it, and tall enough that the solver treats it as a
                // wall rather than as a kerb to step over.
                vec3 half = v3scale(dir_to_vec((d + 1) & 3), CITY_TILE * 0.5f);
                vec3 thick = v3scale(dir_to_vec(d), BANK_WALL_DEPTH * 0.5f);
                vec3 lo = v3(edge.x - fabsf(half.x) - fabsf(thick.x), top - 1.0f,
                             edge.z - fabsf(half.z) - fabsf(thick.z));
                vec3 hi = v3(edge.x + fabsf(half.x) + fabsf(thick.x), top + BANK_WALL_HEIGHT,
                             edge.z + fabsf(half.z) + fabsf(thick.z));
                phys_add_static_box(phys, lo, hi);

                // Nothing is drawn for it. The bank already carries a stone
                // retaining wall from the ground pass, which is what an
                // embankment looks like; a garden fence on top of it turned the
                // whole waterfront into a brick maze, and it was covering the
                // one piece of geometry that was already saying "edge" clearly.
                // something moored against the bank now and then
                if (!decked && rng_chance(r, 0.10f)) {
                    const city_model* boat = city_pick(cat, SET_PARK_FEATURE, 10);  // the canoe
                    vec3 q = v3add(cell_centre(x, z, WATER_LEVEL - 0.05f),
                                   v3scale(dir_to_vec(d), CITY_TILE * 0.28f));
                    city__prop(w, boat, q, along_yaw, boat ? boat->scale : 1.0f, PROP_FADE_MID);
                }
            }
        }
}

// ---- pass 7: bucket props by cell ----

static void city__bucket_props(city_world& w) {
    static uint32_t cursor[CITY_CELLS * CITY_CELLS + 1];
    static city_prop scratch[CITY_MAX_PROPS];
    const size_t cells = CITY_CELLS * CITY_CELLS;

    memset(w.cell_prop_start, 0, sizeof(w.cell_prop_start));
    for (size_t i = 0; i < w.prop_count; i++) {
        int cx = world_to_cell(w.props[i].position.x);
        int cz = world_to_cell(w.props[i].position.z);
        w.cell_prop_start[(size_t)cz * CITY_CELLS + cx + 1]++;
    }
    for (size_t i = 0; i < cells; i++) w.cell_prop_start[i + 1] += w.cell_prop_start[i];

    memcpy(cursor, w.cell_prop_start, (cells + 1) * sizeof(uint32_t));
    for (size_t i = 0; i < w.prop_count; i++) {
        int cx = world_to_cell(w.props[i].position.x);
        int cz = world_to_cell(w.props[i].position.z);
        scratch[cursor[(size_t)cz * CITY_CELLS + cx]++] = w.props[i];
    }
    memcpy(w.props, scratch, w.prop_count * sizeof(city_prop));
}

static void city__index_navigation(city_world& w) {
    w.road_cell_count = 0;
    w.walk_cell_count = 0;
    for (int z = 0; z < CITY_CELLS; z++)
        for (int x = 0; x < CITY_CELLS; x++) {
            const city_cell& c = city_at(w, x, z);
            uint32_t id = (uint32_t)((size_t)z * CITY_CELLS + x);
            if (c.kind == CELL_ROAD)     w.road_cells[w.road_cell_count++] = id;
            if (c.kind == CELL_SIDEWALK) w.walk_cells[w.walk_cell_count++] = id;
        }
}

static void city_generate(city_world& w, const city_catalog& cat, phys_world& phys, uint32_t seed) {
    memset(&w, 0, sizeof(w));
    w.seed = seed;
    rng r = rng_seed(seed);

    for (size_t i = 0; i < CITY_CELLS * CITY_CELLS; i++) {
        w.cells[i].kind = CELL_GROUND;
        w.cells[i].zone = ZONE_SUBURB;
        w.cells[i].facing = DIR_NONE;
        w.cells[i].kerb = DIR_NONE;
    }

    // a ring road two cells in from the edge, so the city has an outside
    const int LO = 2, HI = CITY_CELLS - 3;
    for (int i = LO; i <= HI; i++) {
        city_at(w, i, LO).kind = CELL_ROAD;
        city_at(w, i, HI).kind = CELL_ROAD;
        city_at(w, LO, i).kind = CELL_ROAD;
        city_at(w, HI, i).kind = CELL_ROAD;
    }

    // The river goes in first so the ring road, the subdivision and every
    // block boundary have to accommodate it.
    city__carve_river(w, r);
    for (int i = LO; i <= HI; i++) {
        if (city_at(w, i, LO).kind == CELL_WATER) city_at(w, i, LO).kind = CELL_ROAD;
        if (city_at(w, i, HI).kind == CELL_WATER) city_at(w, i, HI).kind = CELL_ROAD;
        if (city_at(w, LO, i).kind == CELL_WATER) city_at(w, LO, i).kind = CELL_ROAD;
        if (city_at(w, HI, i).kind == CELL_WATER) city_at(w, HI, i).kind = CELL_ROAD;
    }
    city__subdivide(w, r, LO + 1, LO + 1, HI - 1, HI - 1, 0);

    city__zone_blocks(w, r);
    city__build_bridges(w, cat, phys);
    city__derive_topology(w);
    city__place_buildings(w, cat, phys);
    city__place_street_furniture(w, cat, phys);
    city__dress_lots(w, cat, phys);
    city__lay_railway(w, cat, phys);
    city__fence_water(w, cat, phys);
    city__place_ground(w, cat);
    city__bucket_props(w);
    city__index_navigation(w);
    phys_build_statics(phys);
}
