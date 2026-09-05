#pragma once
#include "../core/dtype.hpp"
#include "../core/math.hpp"

// Every number the city is built out of, in one place.
//
// The world is measured in metres and laid out on a square grid of CITY_TILE
// cells. Each art kit was authored at its own scale, so each gets a factor
// that brings it into those metres:
//
//   KayKit city builder  road/prop tiles are 2 units across  -> tile / 2
//   Kenney city kits     one building unit is roughly a lot  -> ~10
//   Kenney car kit       a sedan is 2.55 units nose to tail  -> ~1.55 (4.0 m)
//   Kenney nature kit    a default tree is 1.7 units tall    -> ~4 (7 m)
//
// These were measured off the models' own bounding boxes rather than guessed.

#define CITY_CELLS   112                    // grid is CITY_CELLS square
#define CITY_TILE    8.0f                   // metres per cell
#define CITY_EXTENT  (CITY_CELLS * CITY_TILE)

// kit -> world scale
#define KIT_ROAD_SCALE     (CITY_TILE * 0.5f)
#define KIT_PROP_SCALE     (CITY_TILE * 0.5f)   // KayKit street furniture shares the road scale
#define KIT_BUILDING_SCALE 10.0f
#define KIT_VEHICLE_SCALE  1.55f
#define KIT_NATURE_SCALE   4.00f
#define KIT_RAIL_SCALE     4.00f

// Road surface geometry, read off the KayKit road tiles rather than invented:
// the tile is 2 units square, its drivable strip is the middle 1.8, its kerbs
// occupy the outer 0.1 and stand 0.10 units tall with the tarmac at 0.07.
//
// The whole pavement plane is y = 0 and the roads are dropped so their kerb
// tops land exactly on it - which is what makes a kerb read as a kerb instead
// of a kit tile lying on a lawn.
#define ROAD_Y           (-0.10f * KIT_ROAD_SCALE)   // where a road tile's origin goes
#define ROAD_SURFACE_Y   (-0.03f * KIT_ROAD_SCALE)   // the tarmac itself, 12 cm down
#define ROAD_HALF_WIDTH  (CITY_TILE * 0.45f)
#define ROAD_LANE_OFFSET (CITY_TILE * 0.22f)         // lane centre, right of the centreline

// A pavement cell is a whole tile wide, but a whole tile of pavement either
// side of the road is far more than a street has. Only the strip nearest the
// kerb is really footpath; buildings front onto it and take the rest of the
// cell, which brings the carriageway-to-footpath ratio back to something a
// city actually looks like (8 m of road between 4 m paths).
#define SIDEWALK_WIDTH   4.0f
#define LOT_LINE_SHIFT   (CITY_TILE - SIDEWALK_WIDTH)   // how far a facade moves streetward

// how far from a sidewalk cell's centre a prop sits when it belongs at the kerb
#define KERB_INSET       (CITY_TILE * 0.34f)

// a model whose long axis is local +X, laid down along `dir`
static float dir_to_yaw_along(int dir);

// ---- world generation ----
#define BLOCK_MIN        7      // cells; a block never gets smaller than this
#define BLOCK_MAX        14     // cells; anything bigger keeps splitting

// ---- population ----
#define MAX_CARS         220
#define MAX_PEDS         340
#define CAR_SPAWN_RADIUS 210.0f  // cars are kept alive within this of the player
#define PED_SPAWN_RADIUS 130.0f

// ---- draw distance ----
#define VIEW_DISTANCE      420.0f
#define LOD_DISTANCE       190.0f   // buildings swap to their low detail mesh past this
#define PROP_FADE_NEAR      95.0f   // bins, hydrants, flowers
#define PROP_FADE_MID      190.0f   // benches, lamps, trees
#define ANIMATED_DISTANCE  110.0f   // beyond this a pedestrian is not worth a skinned draw

// ---- gameplay ----
#define PLAYER_WALK_SPEED   3.1f
#define PLAYER_RUN_SPEED    7.4f
#define PLAYER_RADIUS       0.42f
#define PLAYER_HEIGHT       1.80f
#define ENTER_CAR_RANGE     2.6f   // clearance beyond the car's own bodywork

#define TRAFFIC_LIGHT_PERIOD 11.0f   // seconds per full north-south / east-west cycle

// cell kinds
#define CELL_GROUND    0
#define CELL_ROAD      1
#define CELL_SIDEWALK  2
#define CELL_LOT       3    // inside a block, buildable
#define CELL_PARK      4
#define CELL_WATER     5

// The river is cut before the streets are, so the road network has to route
// around it and cross it on bridges - which is what makes it read as a river
// the city grew around rather than a blue rectangle dropped on top.
#define WATER_LEVEL      (-2.80f)   // surface height
#define RIVER_BED        (-6.20f)   // the bed the banks retain
#define RIVER_MIN_WIDTH  3          // cells
#define RIVER_MAX_WIDTH  6
#define BRIDGE_SPACING   14         // cells between crossings

// zones, assigned per block and then stamped onto its cells
#define ZONE_DOWNTOWN    0
#define ZONE_COMMERCIAL  1
#define ZONE_RESIDENTIAL 2
#define ZONE_SUBURB      3
#define ZONE_INDUSTRIAL  4
#define ZONE_PARK        5
#define ZONE_COUNT       6

// directions, indexed so that (dir + 2) & 3 is the opposite
#define DIR_PX 0
#define DIR_PZ 1
#define DIR_NX 2
#define DIR_NZ 3
#define DIR_NONE 4

static const int DIR_DX[4] = {  1,  0, -1,  0 };
static const int DIR_DZ[4] = {  0,  1,  0, -1 };

// Kenney and most KayKit models are authored facing -Z (verified from where
// the door and window detail sits in the meshes), so this is the yaw that
// turns a model to look along `dir`.
static float dir_to_yaw(int dir) {
    switch (dir) {
        case DIR_NZ: return 0.0f;
        case DIR_NX: return 1.57079633f;
        case DIR_PZ: return 3.14159265f;
        default:     return 4.71238898f;   // DIR_PX
    }
}

// KayKit's building_A..H are the one exception in the whole art set: their
// ground-floor detail (doors, awnings, the denser cluster of windows) sits on
// +Z, not -Z - checked by counting low-y vertices per face across six of the
// eight models, all lopsided the same way. Half a turn on top of dir_to_yaw
// puts their actual front toward the street instead of their service side.
static float dir_to_yaw_building(int dir) { return dir_to_yaw(dir) + 3.14159265f; }

static vec3 dir_to_vec(int dir) { return v3((float)DIR_DX[dir], 0.0f, (float)DIR_DZ[dir]); }

// under mat4_rotate_y, local +X maps to (cos, -sin), so aligning it with `dir`
// means cos = dx and -sin = dz
static float dir_to_yaw_along(int dir) {
    switch (dir) {
        case DIR_PX: return 0.0f;
        case DIR_PZ: return -1.57079633f;
        case DIR_NX: return 3.14159265f;
        default:     return 1.57079633f;   // DIR_NZ
    }
}

// yaw that points a model authored with its arm along local -X (the KayKit
// street lamp) out over the road in direction `dir`
static float dir_to_yaw_arm(int dir) {
    return atan2f((float)DIR_DZ[dir], -(float)DIR_DX[dir]);
}

// The right hand side of a heading: cross(forward, up) in a right handed world.
// Facing -Z with +Y up, that is +X - which is what puts traffic in the correct
// lane and makes D strafe right instead of left.
static vec3 dir_right(vec3 forward) { return v3(-forward.z, 0.0f, forward.x); }

// Everything in the world faces along its local -Z, so a heading and a facing
// direction are the same thing under these two.
static vec3  forward_from_yaw(float yaw) { return v3(-sinf(yaw), 0.0f, -cosf(yaw)); }
static float yaw_from_forward(vec3 f)    { return atan2f(-f.x, -f.z); }

// Which way traffic may move through a junction right now. The two axes take
// turns; every junction in the city shares the phase, which is what makes a
// green wave possible when you drive straight.
static bool traffic_axis_green(float time, int axis) {
    int phase = (int)(time / (TRAFFIC_LIGHT_PERIOD * 0.5f)) & 1;
    return phase == axis;
}

// grid <-> world. Cell (0,0) is the south-west corner; the grid is centred on
// the origin so the player starts in the middle of downtown.
static float cell_to_world(int c) { return ((float)c - CITY_CELLS * 0.5f + 0.5f) * CITY_TILE; }
static int   world_to_cell(float w) {
    int c = (int)floorf(w / CITY_TILE + CITY_CELLS * 0.5f);
    return c < 0 ? 0 : (c >= CITY_CELLS ? CITY_CELLS - 1 : c);
}
static vec3 cell_centre(int x, int z, float y = 0.0f) {
    return v3(cell_to_world(x), y, cell_to_world(z));
}
