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
//
// These were measured off the models' own bounding boxes rather than guessed.
//
// The nature and rail kits get no factor here, because no single one is right
// for them: inside one folder a tree is 1.7 units tall and a tuft of grass is
// 0.14, so those kits state the size of each model in metres instead and the
// loader derives the scale from the model's own bounds. See city_kit_item.

#define CITY_CELLS   112                    // grid is CITY_CELLS square
#define CITY_TILE    8.0f                   // metres per cell
#define CITY_EXTENT  (CITY_CELLS * CITY_TILE)

// kit -> world scale
#define KIT_ROAD_SCALE     (CITY_TILE * 0.5f)
#define KIT_PROP_SCALE     (CITY_TILE * 0.5f)   // KayKit street furniture shares the road scale
#define KIT_BUILDING_SCALE 10.0f
#define KIT_VEHICLE_SCALE  1.55f

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

// A pavement cell is a whole tile across, but a whole tile of pavement either
// side of the road is nothing like a street: 8 m of footpath flanking 8 m of
// carriageway reads as a plaza with a road down the middle. So a building does
// not sit on its lot cell's centre - it is pushed LOT_LINE_SHIFT toward the
// street, until its facade lands on the pavement cell's centreline and the
// footpath in front of it is SIDEWALK_WIDTH. That is one line in the building
// pass and it is what brings the whole street section back to real
// proportions: 8 m of road between 4 m paths.
//
// The footpath is then divided across its width the way a real one is. The
// outer strip against the kerb is the furniture zone - lamps, signs, bins,
// benches and street trees all stand in it, which is why a real pavement has
// room to walk despite being full of obstacles. Everything from there back to
// the facade is the walking strip, and nothing static is ever put in it.
//
//   facade | 2.5 m walking strip | 1.5 m furniture | kerb | carriageway
#define SIDEWALK_WIDTH   4.0f                              // kerb to facade
#define LOT_LINE_SHIFT   (CITY_TILE - SIDEWALK_WIDTH)      // how far a facade moves streetward
#define KERB_STRIP       1.5f                              // width of the furniture zone
// both measured from a pavement cell's centre, positive toward the road
#define KERB_INSET       (CITY_TILE * 0.5f - KERB_STRIP * 0.5f)
#define WALK_INSET       ((CITY_TILE * 0.5f - SIDEWALK_WIDTH) \
                          + (SIDEWALK_WIDTH - KERB_STRIP) * 0.5f)

// a model whose long axis is local +X, laid down along `dir`
static float dir_to_yaw_along(int dir);

// ---- world generation ----
//
// Block size is what decides whether a city block reads as a block or as a
// field with a fence of buildings round it. Only lot cells that front a street
// can carry a building, so a block wider than about twice the buildable depth
// leaves a dead centre; at 8 m cells, 10 is already 80 m across and about the
// largest that fills convincingly.
#define BLOCK_MIN        5      // cells; a block never gets smaller than this
#define BLOCK_MAX        10     // cells; anything bigger keeps splitting

// How far into a block a lot can still be built on. Depth 0 is the street
// frontage, depth 1 the row behind it; anything deeper is block interior and is
// dressed as courtyard, yard or service ground instead of being left bare.
#define LOT_BUILD_DEPTH  1

// ---- population ----
// Sized so a full spawn radius of road still has gaps in it. Packing more cars
// in than the network can carry does not look busy, it looks jammed - every
// junction saturates and the queues never clear.
#define MAX_CARS         130
// The footpath is a 2.5 m walking strip now, not a whole 8 m tile, so the same
// crowd that used to look busy on it packs it solid and everyone spends their
// time shouldering past everyone else.
#define MAX_PEDS         190
#define CAR_SPAWN_RADIUS 210.0f  // cars are kept alive within this of the player
#define PED_SPAWN_RADIUS 130.0f

// ---- draw distance ----
#define VIEW_DISTANCE      420.0f
#define LOD_DISTANCE       190.0f   // buildings swap to their low detail mesh past this
#define PROP_FADE_NEAR      95.0f   // bins, hydrants, flowers
#define PROP_FADE_MID      190.0f   // benches, lamps, trees
#define ANIMATED_DISTANCE  110.0f   // beyond this a pedestrian is not worth a skinned draw

// ---- gameplay ----
// A brisk walk is about 1.4 m/s and this is well above it, deliberately: a
// third person camera six metres back makes any real-world pace look like
// wading, and the distances here are city blocks rather than rooms.
#define PLAYER_WALK_SPEED   4.0f
#define PLAYER_RUN_SPEED    9.2f
#define PLAYER_RADIUS       0.42f
#define PLAYER_HEIGHT       1.80f
#define ENTER_CAR_RANGE     2.6f   // clearance beyond the car's own bodywork

// A person of PLAYER_HEIGHT clears about a third of their own height standing.
// 7.4 m/s was a 2.5 m hop with a 0.67 s hang time, far more than a jump clip
// covers; 5.2 gives a 0.61 m arc in 0.47 s, close enough to the clip's own
// timing that the take-off, the float and the landing all land where they should.
#define PLAYER_JUMP_SPEED   5.2f
#define PLAYER_AIR_TIME     (2.0f * PLAYER_JUMP_SPEED / 22.0f)   // phys_world gravity is -22 m/s^2

// Where a KayKit streetlight's head sits once the model is at KIT_PROP_SCALE,
// measured off the mesh rather than guessed. The light source goes there, not
// at the base of the post.
#define LAMP_HEIGHT      4.4f
#define LAMP_RADIUS     19.0f      // how far one lamp reaches down the street
#define HEADLIGHT_RANGE 30.0f

// Punctual lights fall off as inverse square, so their colour is an intensity
// in candela-like units, not a 0..1 screen colour. At the 5 m from a lamp head
// down to the pavement under it the falloff has already divided by 25, so a
// colour near 1 arrives as nothing at all - which is exactly what a street of
// lamps lighting none of the road looks like. These are the numbers that put
// roughly a quarter of daylight on the ground directly beneath a lamp.
#define LAMP_INTENSITY      260.0f
#define HEADLIGHT_INTENSITY 420.0f
#define BRAKE_INTENSITY      90.0f

#define TRAFFIC_LIGHT_PERIOD 11.0f   // seconds per full north-south / east-west cycle
// Fraction of each half cycle held all-red after a green ends. Without it the
// two axes hand over on the same frame and whatever is still inside the box
// gets driven into side-on.
#define TRAFFIC_CLEAR_FRAC   0.14f

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
// How deep the channel is matters more than it looks. Cut 2.8 m down with
// vertical retaining walls, an 8 m wide river is a slot you look into edge-on:
// from anywhere but directly above, the far wall hides the surface and what is
// left reads as a concrete storm drain with a stripe of sky in it. Held just
// over a metre below the embankment and made wider, the surface is the thing
// you see, which is the whole point of having a river.
#define WATER_LEVEL      (-1.30f)   // surface height
#define RIVER_BED        (-3.40f)   // the bed the banks retain
#define RIVER_MIN_WIDTH  4          // cells
#define RIVER_MAX_WIDTH  8
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
    float t = fmodf(time, TRAFFIC_LIGHT_PERIOD);
    if (t < 0.0f) t += TRAFFIC_LIGHT_PERIOD;
    float half = TRAFFIC_LIGHT_PERIOD * 0.5f;
    float green = half * (1.0f - TRAFFIC_CLEAR_FRAC);
    return (axis == 0) ? (t < green) : (t >= half && t < half + green);
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
