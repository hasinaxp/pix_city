#pragma once
#include "../core/physics.hpp"
#include "../core/random.hpp"
#include "city_assets.hpp"
#include "city_map.hpp"
#include "city_player.hpp"

// Walking through a front door.
//
// A house's interior is not a place on the map - there would be nowhere to
// put hundreds of them. Instead every house shares one room, built once at
// a fixed spot far outside the city (see INTERIOR_ORIGIN) that the player is
// teleported to and from. What makes each house feel like its own home
// rather than the same room revisited is that the *furniture* is not shared:
// city_house_layout regenerates it from the house's id every time, the same
// way the city itself regenerates from a seed - so house #4127 has the same
// bed in the same corner on every visit, and no two neighbours match.
//
// The room's walls are real static colliders in the main phys_world, added
// once at startup alongside the city's own. That is what lets it reuse every
// piece of the on-foot player code unchanged: walking, the camera's own
// wall-avoidance raycast, the ground clamp - none of it knows or cares that
// its coordinates happen to be nowhere near the city.

// Placed well clear of the generated city (which spans roughly +-450 m) but
// not so far that float precision or the static grid's cell count suffers.
#define INTERIOR_ORIGIN v3(1500.0f, 0.0f, 1500.0f)

#define ROOM_HALF_W   4.0f     // 8 m wide
#define ROOM_HALF_D   3.5f     // 7 m deep
#define ROOM_HEIGHT   2.6f
#define ROOM_WALL_T   0.2f
#define ROOM_DOOR_HALF 1.0f    // half width of the doorway gap, in the +Z wall

#define HOUSE_ENTER_RANGE 3.0f
#define HOUSE_EXIT_RANGE  2.4f

struct city_house_system {
    bool     indoors;
    int      house_index;     // into city_world::houses, while indoors
    uint32_t house_id;

    // where to put the player back, and facing which way, on the way out
    vec3     outdoor_pos;
    float    outdoor_yaw;

    float    enter_cooldown;
    int      near_house;      // nearest door in range, while outdoors; -1 otherwise
};

static void city_house_add_colliders(phys_world& phys);
static void city_house_update(city_house_system& hs, const city_world& world, city_player& p,
                              phys_world& phys, const pix_window& window, float dt);
static void city_house_draw(const city_house_system& hs, const city_catalog& cat,
                            pix_renderer& renderer);

// ---------------- implementation ----------------

// One static box, shared by the render push and the collider it stands in
// for - the two must agree exactly or the walls would be seen through in
// places they still block, or block a doorway that reads as open.
struct city_house__box { vec3 centre; vec3 size; };

static void city_house__wall_boxes(city_house__box* out, int* count) {
    int n = 0;
    float fw = ROOM_HALF_W * 2.0f + ROOM_WALL_T * 2.0f;
    float fd = ROOM_HALF_D * 2.0f + ROOM_WALL_T * 2.0f;

    // south (back) wall - solid
    out[n].centre = v3(0.0f, 0.0f, -ROOM_HALF_D - ROOM_WALL_T * 0.5f);
    out[n].size   = v3(fw, ROOM_HEIGHT, ROOM_WALL_T);
    n++;
    // west and east - solid
    out[n].centre = v3(-ROOM_HALF_W - ROOM_WALL_T * 0.5f, 0.0f, 0.0f);
    out[n].size   = v3(ROOM_WALL_T, ROOM_HEIGHT, fd);
    n++;
    out[n].centre = v3(ROOM_HALF_W + ROOM_WALL_T * 0.5f, 0.0f, 0.0f);
    out[n].size   = v3(ROOM_WALL_T, ROOM_HEIGHT, fd);
    n++;
    // north wall - split either side of the doorway
    float seg = ROOM_HALF_W - ROOM_DOOR_HALF;
    if (seg > 0.1f) {
        float cx = ROOM_DOOR_HALF + seg * 0.5f;
        out[n].centre = v3(-cx, 0.0f, ROOM_HALF_D + ROOM_WALL_T * 0.5f);
        out[n].size   = v3(seg, ROOM_HEIGHT, ROOM_WALL_T);
        n++;
        out[n].centre = v3(cx, 0.0f, ROOM_HALF_D + ROOM_WALL_T * 0.5f);
        out[n].size   = v3(seg, ROOM_HEIGHT, ROOM_WALL_T);
        n++;
    }
    *count = n;
}

// Called once at startup, after city_generate has built the outdoor statics
// and its own call to phys_build_statics. Adding these and rebuilding again
// is cheap - it is a one-off cost paid at load time, not per frame - and
// simpler than threading a second entry point through world generation for
// five extra boxes.
static void city_house_add_colliders(phys_world& phys) {
    city_house__box boxes[8];
    int count = 0;
    city_house__wall_boxes(boxes, &count);
    for (int i = 0; i < count; i++) {
        vec3 c = v3add(INTERIOR_ORIGIN, boxes[i].centre);
        phys_add_static_prop(phys, c, 0.0f,
                             v2(boxes[i].size.x * 0.5f, boxes[i].size.z * 0.5f),
                             boxes[i].size.y);
    }
    phys_build_statics(phys);
}

static const city_model* city_house__m(const city_catalog& cat, int one) {
    return city_get(cat, cat.singles[one]);
}

static void city_house__push(pix_renderer& renderer, const city_catalog& cat, int one,
                             vec3 local_pos, float yaw) {
    const city_model* m = city_house__m(cat, one);
    if (!m) return;
    vec3 pos = v3add(INTERIOR_ORIGIN, local_pos);
    push_instance(renderer, m->mesh, m->material, mat4_trs_y(pos, yaw, m->scale));
}

static void city_house__push_box(pix_renderer& renderer, const city_catalog& cat, int one,
                                 vec3 local_centre, vec3 size) {
    const city_model* m = city_house__m(cat, one);
    if (!m) return;
    vec3 pos = v3add(INTERIOR_ORIGIN, v3(local_centre.x, local_centre.y - size.y * 0.5f,
                                         local_centre.z));
    mat4 t = mat4_mul(mat4_translate(pos.x, pos.y, pos.z), mat4_scale(size.x, size.y, size.z));
    push_instance(renderer, m->mesh, m->material, t);
}

// Every house's furniture, laid out fresh from its id: two back corners for
// the bed and the kitchen (which one gets which side is the first random
// choice), a sofa and television against the front-left wall, a dining
// table nearer the door on the right, and a handful of pieces that may or
// may not be there at all. Nothing here moves house to house except through
// `r` - two houses with the same id, in the same or a different session,
// come out identical, which is the entire point of seeding from the id
// rather than from wall-clock time.
#define HOUSE_MAX_FURNITURE 24

struct city_house_piece { int one; vec3 pos; float yaw; };

static int city_house_layout(uint32_t house_id, city_house_piece* out) {
    rng r = rng_seed(house_id ^ 0x1D0011u);
    int n = 0;
    #define PUT(ONE, X, Z, YAW) do { if (n < HOUSE_MAX_FURNITURE) { \
        out[n].one = (ONE); out[n].pos = v3((X), 0.0f, (Z)); out[n].yaw = (YAW); n++; } } while (0)

    bool kitchen_right = rng_chance(r, 0.5f);
    float bed_x = kitchen_right ? -2.5f : 2.5f;
    float kit_x = kitchen_right ? 2.5f : -2.5f;
    float side_sign = kitchen_right ? -1.0f : 1.0f;   // which way the bed's nightstand sits

    // ---- bed corner, against the back wall ----
    PUT(ONE_BED, bed_x, -2.7f, 3.14159265f);
    if (rng_chance(r, 0.7f))
        PUT(ONE_SIDE_TABLE, bed_x + side_sign * 1.1f, -2.7f, 3.14159265f);

    // ---- kitchen corner, against the back wall ----
    PUT(ONE_KITCHEN_COUNTER, kit_x, -2.7f, 3.14159265f);
    PUT(ONE_FRIDGE, kit_x - side_sign * 1.1f, -2.7f, 3.14159265f);
    if (rng_chance(r, 0.65f))
        PUT(ONE_STOVE, kit_x + side_sign * 1.1f, -2.7f, 3.14159265f);
    if (rng_chance(r, 0.5f))
        PUT(ONE_KITCHEN_UPPER, kit_x, -3.0f, 3.14159265f);

    // ---- living corner, against the west wall, facing into the room ----
    PUT(ONE_SOFA, -2.9f, 1.5f, -1.57079633f);
    PUT(ONE_COFFEE_TABLE, -1.7f, 1.5f, 0.0f);
    if (rng_chance(r, 0.75f)) {
        PUT(ONE_TV_CABINET, -2.9f, 0.0f, -1.57079633f);
        PUT(ONE_TV, -2.75f, 0.0f, -1.57079633f);
    }
    if (rng_chance(r, 0.45f))
        PUT(ONE_BOOKCASE, -2.9f, -1.5f, -1.57079633f);
    if (rng_chance(r, 0.6f))
        PUT(ONE_RUG, -1.7f, 1.6f, 0.0f);
    if (rng_chance(r, 0.5f))
        PUT(ONE_FLOOR_LAMP, -3.4f, 2.7f, 0.0f);

    // ---- dining corner, nearer the door on the other side ----
    PUT(ONE_DINING_TABLE, 1.3f, 1.7f, 0.0f);
    PUT(ONE_DINING_CHAIR, 1.3f, 1.15f, 3.14159265f);
    PUT(ONE_DINING_CHAIR, 1.3f, 2.25f, 0.0f);
    if (rng_chance(r, 0.55f))
        PUT(ONE_DINING_CHAIR, 0.55f, 1.7f, -1.57079633f);
    if (rng_chance(r, 0.4f))
        PUT(ONE_PLANT, 3.4f, 2.9f, 0.0f);
    if (rng_chance(r, 0.35f))
        PUT(ONE_PLANT, -3.4f, -3.0f, 0.0f);

    #undef PUT
    return n;
}

static void city_house_draw(const city_house_system& hs, const city_catalog& cat,
                            pix_renderer& renderer) {
    if (!hs.indoors) return;

    // One warm bulb near the ceiling. Everything else in the room reads off
    // the same sky ambient the outdoors does - this engine's ambient term is
    // not occluded by geometry, so a roofed room is not pitch black under
    // it - but a home with not one light of its own on inside would still
    // read as unlived-in.
    push_light(renderer, pix_point_light(v3add(INTERIOR_ORIGIN, v3(0.0f, ROOM_HEIGHT - 0.25f, 0.0f)),
                                         7.0f, v3scale(v3(1.0f, 0.86f, 0.62f), 3.2f)));

    // city_house__push_box takes the box's *centre*, not its base - the unit
    // slab it draws with is base-at-0, so getting this wrong (as an earlier
    // version of this function did for the walls, passing their full height
    // instead of half of it) floats the whole wall a wall-height off the
    // ground instead of standing it on the floor.
    float fw = ROOM_HALF_W * 2.0f + ROOM_WALL_T * 2.0f;
    float fd = ROOM_HALF_D * 2.0f + ROOM_WALL_T * 2.0f;
    city_house__push_box(renderer, cat, ONE_INTERIOR_FLOOR,   v3(0, 0.0f, 0), v3(fw, 0.08f, fd));
    city_house__push_box(renderer, cat, ONE_INTERIOR_CEILING, v3(0, ROOM_HEIGHT + 0.04f, 0),
                         v3(fw, 0.08f, fd));

    city_house__box boxes[8];
    int count = 0;
    city_house__wall_boxes(boxes, &count);
    for (int i = 0; i < count; i++)
        city_house__push_box(renderer, cat, ONE_INTERIOR_WALL,
                             v3(boxes[i].centre.x, boxes[i].size.y * 0.5f, boxes[i].centre.z),
                             boxes[i].size);

    city_house_piece pieces[HOUSE_MAX_FURNITURE];
    int n = city_house_layout(hs.house_id, pieces);
    for (int i = 0; i < n; i++)
        city_house__push(renderer, cat, pieces[i].one, pieces[i].pos, pieces[i].yaw);
}

// Distance from `pos` to a house's doorstep, in the XZ plane - the door
// marker sits at y = 0 regardless of where the player's eye height is.
static float city_house__door_dist(const city_house& h, vec3 pos) {
    float dx = h.door_pos.x - pos.x, dz = h.door_pos.z - pos.z;
    return sqrtf(dx * dx + dz * dz);
}

static int city_house__find_near(const city_world& w, vec3 pos, float range) {
    int best = -1;
    float best_d = range;
    for (size_t i = 0; i < w.house_count; i++) {
        float d = city_house__door_dist(w.houses[i], pos);
        if (d < best_d) { best_d = d; best = (int)i; }
    }
    return best;
}

static void city_house__teleport(city_player& p, phys_world& phys, vec3 pos, float yaw) {
    phys_body* b = phys_get_body(phys, p.body);
    if (b) {
        b->position = pos;
        b->velocity = v3(0.0f, 0.0f, 0.0f);
    }
    p.position = pos;
    p.yaw = yaw;
    // face the same way the body now does, so the very first frame's camera
    // does not swing round to catch up
    p.cam_yaw = yaw;
}

static void city_house_enter(city_house_system& hs, const city_world& w, city_player& p,
                             phys_world& phys, int house_index) {
    if (house_index < 0 || (size_t)house_index >= w.house_count) return;
    hs.outdoor_pos = p.position;
    hs.outdoor_yaw = p.yaw;
    hs.indoors = true;
    hs.house_index = house_index;
    hs.house_id = w.houses[house_index].id;
    hs.near_house = -1;

    vec3 spawn = v3add(INTERIOR_ORIGIN, v3(0.0f, 0.0f, ROOM_HALF_D - 1.0f));
    city_house__teleport(p, phys, spawn, 0.0f);   // facing -Z: into the room
}

static void city_house_exit(city_house_system& hs, const city_world& w, city_player& p,
                            phys_world& phys) {
    vec3 pos = hs.outdoor_pos;
    float yaw = hs.outdoor_yaw;
    if ((size_t)hs.house_index < w.house_count) {
        // the saved spot is already right, but falling back to the house's
        // own doorstep keeps exiting well-defined even if that index ever
        // stopped matching what it was when we came in
        pos = w.houses[hs.house_index].door_pos;
        yaw = w.houses[hs.house_index].exit_yaw;
    }
    hs.indoors = false;
    city_house__teleport(p, phys, pos, yaw);
}

static void city_house_update(city_house_system& hs, const city_world& w, city_player& p,
                              phys_world& phys, const pix_window& window, float dt) {
    if (hs.enter_cooldown > 0.0f) hs.enter_cooldown -= dt;
    bool interact = window.keystates['E'].pressed;

    if (!hs.indoors) {
        hs.near_house = city_player_on_foot(p)
            ? city_house__find_near(w, p.position, HOUSE_ENTER_RANGE) : -1;
        if (interact && hs.near_house >= 0 && hs.enter_cooldown <= 0.0f) {
            city_house_enter(hs, w, p, phys, hs.near_house);
            hs.enter_cooldown = 0.4f;
        }
    } else {
        vec3 door = v3add(INTERIOR_ORIGIN, v3(0.0f, 0.0f, ROOM_HALF_D - 0.6f));
        float dx = door.x - p.position.x, dz = door.z - p.position.z;
        bool can_exit = sqrtf(dx * dx + dz * dz) < HOUSE_EXIT_RANGE;
        if (interact && can_exit && hs.enter_cooldown <= 0.0f) {
            city_house_exit(hs, w, p, phys);
            hs.enter_cooldown = 0.4f;
        }
    }
}
