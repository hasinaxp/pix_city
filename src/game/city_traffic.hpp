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
// On top of that navigation sit the three behaviours that make it read as
// traffic rather than as moving objects:
//
//   * lights   every junction shares one phase, so a whole avenue goes green
//              together and you can catch a green wave
//   * following a car brakes for whatever is in the cone ahead of it, which
//              produces queues at red lights without any queue logic
//   * yielding pedestrians in the road stop a car dead
//
// Steering is a kinematic bicycle model, and the result is handed to the
// physics body as a velocity - so a car that clips a building or another car
// gets pushed by the solver like anything else.

#define CAR_LENGTH_SCALE  0.85f    // wheelbase as a fraction of body length
#define CAR_MAX_SPEED     15.5f
#define CAR_ACCEL         7.0f
#define CAR_BRAKE        18.0f
#define CAR_LOOKAHEAD     9.0f

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
    float   blocked_time;   // how long it has been unable to move
    float   brake_light;    // 0..1, for anything that wants to show it
};

struct city_traffic {
    city_car cars[MAX_CARS];
    size_t   count;
    rng      random;
};

static void city_traffic_init(city_traffic& t, uint32_t seed);
static void city_traffic_update(city_traffic& t, city_world& world, const city_catalog& cat,
                                phys_world& phys, vec3 focus, float time, float dt);
static void city_traffic_draw(const city_traffic& t, const city_catalog& cat,
                              pix_renderer& renderer, const frustum& view, vec3 eye);

// spawns one car on a road cell; returns its index or -1
static int  city_traffic_spawn(city_traffic& t, const city_world& w, const city_catalog& cat,
                               phys_world& phys, int cx, int cz);
// draws a single car, shared by the traffic pass and the player's own vehicle
static void city_car_draw(const city_car& car, const city_catalog& cat, pix_renderer& renderer);

// ---------------- implementation ----------------

static void city_traffic_init(city_traffic& t, uint32_t seed) {
    memset(&t, 0, sizeof(t));
    t.random = rng_seed(seed);
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
    if (city__road_occupied(t, cell_centre(cx, cz), CITY_TILE * 1.6f)) return -1;

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
    b.restitution = 0.05f;
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
    phys_remove_body(phys, c.body);
    c.active = false;
    if (t.count) t.count--;
}

// How much of the road ahead is clear, in metres, capped at CAR_LOOKAHEAD.
// Everything that can be in a car's way - other cars and people - is a body in
// the physics world already, so one neighbourhood query answers both.
static float city__clear_ahead(const phys_world& phys, const city_car& self,
                               const city_vehicle_model& vm) {
    vec3 fwd = forward_from_yaw(self.yaw);
    float reach = vm.half_length + CAR_LOOKAHEAD;
    float nearest = reach;

    idx hits[48];
    size_t n = phys_query_neighbours(phys, self.position, reach + 2.0f, hits, 48);
    for (size_t k = 0; k < n; k++) {
        idx i = hits[k];
        const phys_body& b = phys.bodies[i];
        if (!b.active || i == self.body) continue;
        if (!(b.group & (PHYS_LAYER_VEHICLE | PHYS_LAYER_PED | PHYS_LAYER_PLAYER))) continue;

        vec3 to = v3sub(b.position, self.position);
        float along = to.x * fwd.x + to.z * fwd.z;
        if (along <= 0.0f || along > reach) continue;
        // lateral gap, widened by the other body's own footprint
        float side = fabsf(to.x * fwd.z - to.z * fwd.x);
        float clearance = vm.half_width + (b.shape == PHYS_BOX ? b.half.x : b.radius) + 0.25f;
        if (side > clearance) continue;
        if (along < nearest) nearest = along;
    }
    return nearest - vm.half_length;
}

static void city__drive_ai(city_car& c, city_world& w, const city_catalog& cat,
                           phys_world& phys, rng& r, float time, float dt) {
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
            target = city__lane_point(c.node_x, c.node_z, c.next_x, c.next_z);
            to_target = v3sub(target, c.position);
            to_target.y = 0.0f;
            distance = v3len(to_target);
        }
    }

    // ---- steering ----
    float want_yaw = yaw_from_forward(v3norm(to_target));
    float turn = angle_delta(c.yaw, want_yaw);
    c.steer = clampf(-turn * 1.6f, -1.0f, 1.0f);   // steer is +1 right, yaw grows left

    // ---- speed ----
    float target_speed = CAR_MAX_SPEED * (1.0f - 0.45f * fabsf(c.steer));

    // red light: stop at the mouth of the junction, not in the middle of it
    const city_cell& next_cell = city_at(w, c.next_x, c.next_z);
    if (next_cell.flags & CELLF_JUNCTION) {
        int axis = (c.next_x != c.node_x) ? 0 : 1;    // 0 = travelling along X
        if (!traffic_axis_green(time, axis)) {
            float stop_gap = distance - CITY_TILE * 0.55f;
            if (stop_gap < 12.0f) {
                float allowed = stop_gap > 0.5f ? stop_gap * 0.8f : 0.0f;
                if (allowed < target_speed) target_speed = allowed;
            }
        }
    }

    // whatever is in front
    float clear = city__clear_ahead(phys, c, vm);
    if (clear < CAR_LOOKAHEAD) {
        float allowed = (clear - 1.6f) * 1.6f;
        if (allowed < 0.0f) allowed = 0.0f;
        if (allowed < target_speed) target_speed = allowed;
    }

    float accel = target_speed > c.speed ? CAR_ACCEL : CAR_BRAKE;
    c.speed += clampf(target_speed - c.speed, -CAR_BRAKE * dt, accel * dt);
    if (c.speed < 0.0f) c.speed = 0.0f;
    c.brake_light = target_speed < c.speed - 0.5f ? 1.0f : 0.0f;

    // A car wedged against geometry gives up and is recycled elsewhere rather
    // than sitting in the world forever blocking the lane behind it.
    if (c.speed < 0.4f && target_speed > 2.0f) c.blocked_time += dt;
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
        if (c.player_driven) continue;      // the player's own car is driven elsewhere

        city__drive_ai(c, w, cat, phys, t.random, time, dt);
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
