#pragma once
#include "../core/random.hpp"
#include "../core/physics.hpp"
#include "city_map.hpp"

// Traffic.
//
// Cars navigate the road grid one cell at a time. Each car holds the cell it
// came from and the cell it is heading to, and steers toward a point offset to
// the right of that cell's centre - which is all "drive on the right" needs to
// be. On arrival it picks the next cell, preferring to carry straight on, so
// traffic flows down a street instead of milling about.
//
// On top of that navigation sit the four behaviours that make it read as
// traffic rather than as moving objects:
//
//   * lights     every junction shares one phase, so a whole avenue goes green
//                together and you can catch a green wave
//   * following  a car brakes for whatever is in the cone ahead of it, which
//                produces queues at red lights without any queue logic
//   * yielding   pedestrians in the road stop a car dead
//   * reserving  a junction cell is claimed by one car at a time
//
// That last one is what stops junctions clotting. Braking for what is in front
// only works while "in front" means the direction the car is pointing, and at a
// junction the car it is about to hit is the one crossing it side-on, which no
// forward cone ever sees. So a junction box is a resource: a car may only enter
// it if nothing else has claimed it, may only claim it if it can also see a way
// out the far side (which is what "do not block the box" means), and gives it up
// the moment it is through. Junctions only - a straight two-way street would
// gridlock instantly if opposing traffic had to take turns down it, and there
// the lane offset already keeps the two directions apart.
//
// Steering is a kinematic bicycle model, and the result is handed to the
// physics body as a velocity - so a car that clips a building or another car
// gets pushed by the solver like anything else.

#define CAR_LENGTH_SCALE  0.85f    // wheelbase as a fraction of body length
#define CAR_MAX_SPEED     15.5f
#define CAR_ACCEL          7.0f
#define CAR_BRAKE         18.0f
// Far enough that a car at CAR_MAX_SPEED can still stop inside it: 15.5 m/s
// against CAR_BRAKE needs 6.7 m, and the rest is the gap it keeps once stopped.
#define CAR_LOOKAHEAD     14.0f
#define CAR_GAP            2.2f    // bumper to bumper, stationary
#define CAR_CLAIM_NONE   0xFFFFu

struct city_car {
    bool    active;
    bool    player_driven;
    uint8_t model;          // index into city_catalog::vehicles
    idx     body;           // physics body handle

    vec3    position;
    float   yaw;
    float   speed;          // signed, metres per second along the facing
    float   steer;          // -1 .. 1, what the front wheels are doing
    float   wheel_spin;     // accumulated roll, radians

    int     node_x, node_z; // the road cell being driven from
    int     next_x, next_z; // the road cell being driven to
    int     claim_x, claim_z;  // junction cell reserved for this car, -1 if none
    float   blocked_time;   // how long it has been unable to move
    float   brake_light;    // 0..1, for anything that wants to show it
};

struct city_traffic {
    city_car cars[MAX_CARS];
    size_t   count;
    // which car owns each junction box, CAR_CLAIM_NONE for free. One byte short
    // per cell over the whole map is cheaper than any sparse structure and
    // needs no rebuild.
    uint16_t claim[CITY_CELLS * CITY_CELLS];
    rng      random;
};

// ---------------- implementation ----------------

static void city_traffic_init(city_traffic& t, uint32_t seed) {
    memset(&t, 0, sizeof(t));
    for (size_t i = 0; i < CITY_CELLS * CITY_CELLS; i++) t.claim[i] = CAR_CLAIM_NONE;
    t.random = rng_seed(seed);
}

// ---- junction reservations ----

static bool city__is_junction(const city_world& w, int x, int z) {
    return city_in_bounds(x, z) && (city_at(w, x, z).flags & CELLF_JUNCTION) != 0;
}

// Can this car put itself in that cell? A cell that is not a junction is never
// reserved; a junction is free, or already ours.
static bool city__claim_free(const city_traffic& t, const city_world& w,
                             int x, int z, int self) {
    if (!city__is_junction(w, x, z)) return true;
    uint16_t owner = t.claim[(size_t)z * CITY_CELLS + x];
    return owner == CAR_CLAIM_NONE || owner == (uint16_t)self;
}

static void city__claim_release(city_traffic& t, city_car& c) {
    if (c.claim_x < 0) return;
    size_t id = (size_t)c.claim_z * CITY_CELLS + c.claim_x;
    if (t.claim[id] != CAR_CLAIM_NONE) t.claim[id] = CAR_CLAIM_NONE;
    c.claim_x = c.claim_z = -1;
}

static void city__claim_take(city_traffic& t, city_car& c, int self, int x, int z) {
    city__claim_release(t, c);
    t.claim[(size_t)z * CITY_CELLS + x] = (uint16_t)self;
    c.claim_x = x;
    c.claim_z = z;
}

// Picks the cell a car should head to after `to`, coming from `from`.
// Straight on is weighted heavily; a U-turn only happens at a dead end.
static bool city__next_road(const city_world& w, rng& r, int from_x, int from_z,
                            int to_x, int to_z, int* out_x, int* out_z) {
    int dx = to_x - from_x, dz = to_z - from_z;
    int best[4][2];
    int weight[4];
    int count = 0, total = 0;

    for (int d = 0; d < 4; d++) {
        int nx = to_x + DIR_DX[d], nz = to_z + DIR_DZ[d];
        if (!city_is_road(w, nx, nz)) continue;
        if (nx == from_x && nz == from_z) continue;          // never double back
        int wgt = (DIR_DX[d] == dx && DIR_DZ[d] == dz) ? 6 : 1;
        best[count][0] = nx; best[count][1] = nz;
        weight[count] = wgt;
        total += wgt;
        count++;
    }
    if (!count) {                                            // dead end: turn round
        *out_x = from_x; *out_z = from_z;
        return city_is_road(w, from_x, from_z);
    }

    int roll = rng_int(r, 0, total - 1);
    for (int i = 0; i < count; i++) {
        roll -= weight[i];
        if (roll < 0) { *out_x = best[i][0]; *out_z = best[i][1]; return true; }
    }
    *out_x = best[0][0]; *out_z = best[0][1];
    return true;
}


// The point a car aims at: the centre of its target cell, pushed to the right
// hand lane so opposing traffic passes on the correct side.
static vec3 city__lane_point(int from_x, int from_z, int to_x, int to_z) {
    vec3 travel = v3norm(v3((float)(to_x - from_x), 0.0f, (float)(to_z - from_z)));
    vec3 right = dir_right(travel);
    vec3 centre = cell_centre(to_x, to_z, ROAD_SURFACE_Y);
    return v3add(centre, v3scale(right, ROAD_LANE_OFFSET));
}

// Where a car is really aiming. Steering at the centre of the very next cell
// makes a car swing hard as it arrives and then hard back, which is most of
// what the weaving looked like; aiming at a point carried on toward the cell
// *after* it, weighted by how close the near one already is, is a pure pursuit
// target and it takes a corner as one arc.
static vec3 city__pursuit_point(const city_world& w, const city_car& c, float distance) {
    vec3 near_point = city__lane_point(c.node_x, c.node_z, c.next_x, c.next_z);

    int dx = c.next_x - c.node_x, dz = c.next_z - c.node_z;
    int ax = c.next_x + dx, az = c.next_z + dz;
    if (!city_is_road(w, ax, az)) return near_point;

    vec3 far_point = city__lane_point(c.next_x, c.next_z, ax, az);
    // fully on the near point a cell out, fully on the far one on arrival
    float blend = clampf(1.0f - distance / (CITY_TILE * 0.9f), 0.0f, 0.65f);
    return v3add(near_point, v3scale(v3sub(far_point, near_point), blend));
}

// is there already a vehicle close enough that spawning here would drop one
// car inside another
static bool city__road_occupied(const city_traffic& t, vec3 at, float clearance) {
    float r2 = clearance * clearance;
    for (size_t i = 0; i < MAX_CARS; i++) {
        if (!t.cars[i].active) continue;
        float dx = t.cars[i].position.x - at.x, dz = t.cars[i].position.z - at.z;
        if (dx * dx + dz * dz < r2) return true;
    }
    return false;
}

static int city_traffic_spawn(city_traffic& t, const city_world& w, const city_catalog& cat,
                              phys_world& phys, int cx, int cz) {
    if (!cat.vehicle_count || !city_is_road(w, cx, cz)) return -1;
    if (city__road_occupied(t, cell_centre(cx, cz), CITY_TILE * 2.2f)) return -1;

    int slot = -1;
    for (size_t i = 0; i < MAX_CARS; i++)
        if (!t.cars[i].active) { slot = (int)i; break; }
    if (slot < 0) return -1;

    // a road cell with no road neighbour is not somewhere a car can start
    int exits[4][2], exit_count = 0;
    for (int d = 0; d < 4; d++) {
        int nx = cx + DIR_DX[d], nz = cz + DIR_DZ[d];
        if (city_is_road(w, nx, nz)) { exits[exit_count][0] = nx; exits[exit_count][1] = nz; exit_count++; }
    }
    if (!exit_count) return -1;
    int chosen = rng_int(t.random, 0, exit_count - 1);

    city_car& c = t.cars[slot];
    memset(&c, 0, sizeof(c));
    c.claim_x = c.claim_z = -1;
    c.model = (uint8_t)(rng_u32(t.random) % cat.vehicle_count);
    c.node_x = cx; c.node_z = cz;
    c.next_x = exits[chosen][0];
    c.next_z = exits[chosen][1];

    vec3 travel = v3norm(v3((float)(c.next_x - cx), 0.0f, (float)(c.next_z - cz)));
    c.position = v3add(cell_centre(cx, cz, ROAD_SURFACE_Y),
                       v3scale(dir_right(travel), ROAD_LANE_OFFSET));
    c.yaw = yaw_from_forward(travel);
    c.speed = rng_range(t.random, 5.0f, 10.0f);
    c.active = true;

    const city_vehicle_model& vm = cat.vehicles[c.model];
    phys_body b = {};
    b.shape = PHYS_BOX;
    b.position = c.position;
    b.yaw = c.yaw;
    b.half = v2(vm.half_width, vm.half_length);
    b.height = vm.height;
    b.inv_mass = 1.0f / 1200.0f;
    // Nothing about a car nudging another car should bounce. Any restitution at
    // all turns a queue at a red light into a row of things jostling each other.
    b.restitution = 0.0f;
    b.group = PHYS_LAYER_VEHICLE;
    b.collides = PHYS_LAYER_ALL;
    b.gravity = false;
    c.body = phys_add_body(phys, b);
    if (c.body == (idx)-1) { c.active = false; return -1; }

    t.count++;
    return slot;
}

static void city__despawn(city_traffic& t, phys_world& phys, city_car& c) {
    if (!c.active) return;
    city__claim_release(t, c);
    phys_remove_body(phys, c.body);
    c.active = false;
    if (t.count) t.count--;
}

// How much of the road ahead is clear, in metres, capped at CAR_LOOKAHEAD.
// Everything that can be in a car's way - other cars and people - is a body in
// the physics world already, so one neighbourhood query answers both.
//
// The cone is measured along the direction the car is *travelling toward*, not
// the way it currently points. Through a turn those differ by most of a right
// angle, and a cone aimed down the old heading looks straight at the kerb while
// the car it is about to run into sits outside it - which is exactly how a
// queue of turning cars used to end up shunting each other round a corner.
static float city__clear_ahead(const phys_world& phys, const city_car& self,
                               const city_vehicle_model& vm, vec3 heading) {
    float reach = vm.half_length + CAR_LOOKAHEAD;
    float nearest = reach;

    idx hits[64];
    size_t n = phys_query_neighbours(phys, self.position, reach + 2.0f, hits, 64);
    for (size_t k = 0; k < n; k++) {
        idx i = hits[k];
        const phys_body& b = phys.bodies[i];
        if (!b.active || i == self.body) continue;
        if (!(b.group & (PHYS_LAYER_VEHICLE | PHYS_LAYER_PED | PHYS_LAYER_PLAYER))) continue;

        vec3 to = v3sub(b.position, self.position);
        float along = to.x * heading.x + to.z * heading.z;
        if (along <= 0.0f || along > reach) continue;
        // Lateral gap, widened by the other body's own footprint - and widened
        // again with distance, so a car far enough ahead to still be swinging
        // through a bend is not missed by a cone that only looks straight.
        float side = fabsf(to.x * heading.z - to.z * heading.x);
        float clearance = vm.half_width + (b.shape == PHYS_BOX ? b.half.x : b.radius)
                        + 0.25f + along * 0.16f;
        if (side > clearance) continue;
        if (along < nearest) nearest = along;
    }
    return nearest - vm.half_length;
}

static void city__drive_ai(city_traffic& t, city_car& c, int self, city_world& w,
                           const city_catalog& cat, phys_world& phys, rng& r,
                           float time, float dt) {
    const city_vehicle_model& vm = cat.vehicles[c.model];

    // ---- navigation ----
    vec3 target = city__lane_point(c.node_x, c.node_z, c.next_x, c.next_z);
    vec3 to_target = v3sub(target, c.position);
    to_target.y = 0.0f;
    float distance = v3len(to_target);

    if (distance < CITY_TILE * 0.42f) {
        int nx, nz;
        if (city__next_road(w, r, c.node_x, c.node_z, c.next_x, c.next_z, &nx, &nz)) {
            c.node_x = c.next_x; c.node_z = c.next_z;
            c.next_x = nx;       c.next_z = nz;
            // the cell just left is no longer needed, whoever is queued behind
            // can have it
            if (c.claim_x >= 0 && (c.claim_x != c.node_x || c.claim_z != c.node_z)
                && (c.claim_x != c.next_x || c.claim_z != c.next_z))
                city__claim_release(t, c);
        }
    }

    target = city__pursuit_point(w, c, distance);
    to_target = v3sub(target, c.position);
    to_target.y = 0.0f;
    distance = v3len(to_target);

    // ---- steering ----
    // Damped rather than set outright: the wheels of a real car take a moment
    // to come round, and a steering angle that snaps to the heading error makes
    // a car hunt from lock to lock down a straight road.
    float want_yaw = yaw_from_forward(v3norm(to_target));
    float turn = angle_delta(c.yaw, want_yaw);
    c.steer = damp(c.steer, clampf(-turn * 1.5f, -1.0f, 1.0f), 9.0f, dt);

    // ---- speed ----
    float target_speed = CAR_MAX_SPEED * (1.0f - 0.45f * fabsf(c.steer));

    // ---- the junction ahead ----
    //
    // Distance to the mouth of the next cell, which is where a car has to be
    // able to stop: the lane point is in the middle of it, half a tile further.
    const city_cell& next_cell = city_at(w, c.next_x, c.next_z);
    float to_next = v3len(v3sub(city__lane_point(c.node_x, c.node_z, c.next_x, c.next_z),
                                c.position));
    float stop_gap = to_next - CITY_TILE * 0.55f;
    bool must_stop = false;

    if (next_cell.flags & CELLF_JUNCTION) {
        int axis = (c.next_x != c.node_x) ? 0 : 1;    // 0 = travelling along X
        if (!traffic_axis_green(time, axis)) must_stop = true;

        if (!must_stop && !city__claim_free(t, w, c.next_x, c.next_z, self)) must_stop = true;

        // Do not block the box: a car may only take the junction if the cell it
        // means to leave by is somewhere it can also stand. Without this a car
        // rolls into the middle of a crossing, stops behind the queue on the far
        // side, and every other approach is dead until it moves.
        if (!must_stop && !city__claim_free(t, w, c.next_x + (c.next_x - c.node_x),
                                            c.next_z + (c.next_z - c.node_z), self))
            must_stop = true;

        // clear to go: take the box before anyone else does
        if (!must_stop && stop_gap < CITY_TILE)
            city__claim_take(t, c, self, c.next_x, c.next_z);
    }

    if (must_stop && stop_gap < CAR_LOOKAHEAD) {
        float allowed = stop_gap > 0.6f ? stop_gap * 0.9f : 0.0f;
        if (allowed < target_speed) target_speed = allowed;
    }

    // whatever is in front, measured along the way the car is going
    vec3 heading = distance > 1e-3f ? v3scale(to_target, 1.0f / distance)
                                    : forward_from_yaw(c.yaw);
    float clear = city__clear_ahead(phys, c, vm, heading);
    if (clear < CAR_LOOKAHEAD) {
        // Stop with CAR_GAP still in hand, and approach that gap at a speed the
        // brakes can actually shed - sqrt(2 a d) is the exact answer, and using
        // it instead of a linear ramp is the difference between a queue that
        // settles and one that keeps rear-ending itself.
        float gap = clear - CAR_GAP;
        float allowed = gap > 0.0f ? sqrtf(2.0f * CAR_BRAKE * 0.75f * gap) : 0.0f;
        if (allowed < target_speed) target_speed = allowed;
    }

    float accel = target_speed > c.speed ? CAR_ACCEL : CAR_BRAKE;
    c.speed += clampf(target_speed - c.speed, -CAR_BRAKE * dt, accel * dt);
    if (c.speed < 0.0f) c.speed = 0.0f;
    c.brake_light = target_speed < c.speed - 0.5f ? 1.0f : 0.0f;

    // A car wedged against geometry gives up and is recycled elsewhere rather
    // than sitting in the world forever blocking the lane behind it. Waiting at
    // a red light or behind a queue is not being wedged, so neither counts.
    bool waiting = must_stop || clear < CAR_LOOKAHEAD;
    if (c.speed < 0.4f && !waiting) c.blocked_time += dt;
    else c.blocked_time = 0.0f;
}

// Kinematic bicycle: the heading turns at a rate set by speed and steering
// angle, so a stationary car cannot pivot on the spot and a fast one takes a
// wide line. The result becomes the body's velocity and the solver does the
// rest.
static void city__integrate_car(city_car& c, const city_vehicle_model& vm,
                                phys_world& phys, float dt) {
    float wheelbase = vm.half_length * 2.0f * CAR_LENGTH_SCALE;
    if (wheelbase < 0.5f) wheelbase = 0.5f;
    float steer_angle = c.steer * 0.62f;
    // `steer` is +1 for a right turn. Increasing yaw swings the heading toward
    // the left (forward is (-sin, -cos), whose derivative is -right), so a
    // right turn is a decreasing yaw.
    c.yaw -= (c.speed / wheelbase) * tanf(steer_angle) * dt;

    if (vm.wheel_radius > 0.01f) c.wheel_spin += (c.speed / vm.wheel_radius) * dt;

    phys_body* b = phys_get_body(phys, c.body);
    if (!b) return;
    vec3 fwd = forward_from_yaw(c.yaw);
    b->velocity = v3scale(fwd, c.speed);
    b->yaw = c.yaw;
    b->position.y = ROAD_SURFACE_Y;
}

static void city__read_back_car(city_car& c, phys_world& phys) {
    phys_body* b = phys_get_body(phys, c.body);
    if (!b) return;
    c.position = b->position;
    // scraping a wall bleeds speed off, the same way it would in the world
    if (b->touched) {
        float along = b->velocity.x * -sinf(c.yaw) + b->velocity.z * -cosf(c.yaw);
        c.speed = along;
    }
}

static void city_traffic_update(city_traffic& t, city_world& w, const city_catalog& cat,
                                phys_world& phys, vec3 focus, float time, float dt) {
    for (size_t i = 0; i < MAX_CARS; i++) {
        city_car& c = t.cars[i];
        if (!c.active) continue;

        float dx = c.position.x - focus.x, dz = c.position.z - focus.z;
        float dist2 = dx * dx + dz * dz;
        if (dist2 > (CAR_SPAWN_RADIUS * 1.35f) * (CAR_SPAWN_RADIUS * 1.35f)
            || c.blocked_time > 6.0f) {
            city__despawn(t, phys, c);
            continue;
        }
        if (c.player_driven) {
            // The player still has to take a junction off the queue, or the AI
            // will drive through the box the player is sitting in.
            if (city__is_junction(w, world_to_cell(c.position.x), world_to_cell(c.position.z)))
                city__claim_take(t, c, (int)i, world_to_cell(c.position.x),
                                 world_to_cell(c.position.z));
            else
                city__claim_release(t, c);
            continue;
        }

        city__drive_ai(t, c, (int)i, w, cat, phys, t.random, time, dt);
        city__integrate_car(c, cat.vehicles[c.model], phys, dt);
    }

    // Top up around the player. Spawning is attempted a few times a frame on
    // random road cells in a ring: close enough to matter, far enough that a
    // car never appears in view.
    if (w.road_cell_count) {
        for (int attempt = 0; attempt < 4 && t.count < MAX_CARS; attempt++) {
            uint32_t pick = rng_u32(t.random) % (uint32_t)w.road_cell_count;
            uint32_t cell = w.road_cells[pick];
            int cx = (int)(cell % CITY_CELLS), cz = (int)(cell / CITY_CELLS);
            vec3 p = cell_centre(cx, cz);
            float dx = p.x - focus.x, dz = p.z - focus.z;
            float d = sqrtf(dx * dx + dz * dz);
            if (d < 55.0f || d > CAR_SPAWN_RADIUS) continue;
            // never drop a car into a junction box: it would arrive with no
            // claim on a cell it is already standing in
            if (city_at(w, cx, cz).flags & CELLF_JUNCTION) continue;
            city_traffic_spawn(t, w, cat, phys, cx, cz);
        }
    }
}

static void city__read_back_all(city_traffic& t, phys_world& phys) {
    for (size_t i = 0; i < MAX_CARS; i++)
        if (t.cars[i].active) city__read_back_car(t.cars[i], phys);
}

static void city_car_draw(const city_car& c, const city_catalog& cat, pix_renderer& renderer) {
    const city_vehicle_model& vm = cat.vehicles[c.model];

    // The chassis frame folds in the kit's own facing, so every offset below is
    // read straight out of the model without a second correction. Inside it,
    // +Z is the way the car is driving and +X is its left.
    mat4 chassis = mat4_mul(mat4_translate(c.position.x, c.position.y, c.position.z),
                            mat4_rotate_y(c.yaw + vm.model_yaw));
    mat4 scale = mat4_scale(vm.scale, vm.scale, vm.scale);

    mat4 body = mat4_mul(chassis,
                mat4_mul(mat4_translate(vm.body_offset.x, vm.body_offset.y, vm.body_offset.z),
                         scale));
    push_instance(renderer, vm.body_mesh, vm.material, body);

    for (int i = 0; i < vm.part_count; i++) {
        mat4 m = mat4_mul(chassis,
                 mat4_mul(mat4_translate(vm.part_offset[i].x, vm.part_offset[i].y,
                                         vm.part_offset[i].z), scale));
        push_instance(renderer, vm.part_mesh[i], vm.material, m);
    }

    if (vm.wheel_mesh == (idx)-1) return;

    // front wheels steer, all four roll; the pivots came out of the model's own
    // named groups so they sit exactly where the art put them
    for (int i = 0; i < 4; i++) {
        float steer = (i < 2) ? -c.steer * 0.55f : 0.0f;   // +X is left in here
        mat4 m = mat4_mul(chassis,
                 mat4_mul(mat4_translate(vm.wheel_pivot[i].x, vm.wheel_pivot[i].y, vm.wheel_pivot[i].z),
                 mat4_mul(mat4_rotate_y(steer),
                 mat4_mul(mat4_rotate_x(c.wheel_spin), scale))));
        push_instance(renderer, vm.wheel_mesh, vm.material, m);
    }
}

static void city_traffic_draw(const city_traffic& t, const city_catalog& cat,
                              pix_renderer& renderer, const frustum& view, vec3 eye) {
    for (size_t i = 0; i < MAX_CARS; i++) {
        const city_car& c = t.cars[i];
        if (!c.active || c.player_driven) continue;
        float dx = c.position.x - eye.x, dz = c.position.z - eye.z;
        if (dx * dx + dz * dz > VIEW_DISTANCE * VIEW_DISTANCE) continue;
        if (!frustum_test_sphere(view, c.position, 4.0f)) continue;
        city_car_draw(c, cat, renderer);
    }
}
