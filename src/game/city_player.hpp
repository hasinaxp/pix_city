#pragma once
#include "../core/platform.hpp"
#include "../core/animation.hpp"
#include "city_traffic.hpp"

// The player: on foot by default, behind the wheel when they get in a car.
//
// Both modes share one third person camera that orbits a target point, and the
// only real difference between them is what that target is and who owns the
// movement. On foot the player is a physics cylinder pushed around by input;
// driving, the player *is* a car in the traffic list with its AI switched off,
// which means it collides with the world, other traffic and pedestrians through
// exactly the same solver as every other vehicle.

#define CAM_MIN_DIST     1.2f
#define CAM_FOOT_DIST    6.0f
#define CAM_CAR_DIST     9.5f
#define CAM_HEIGHT       1.55f

#define DRIVE_ENGINE_FORCE  11.0f
#define DRIVE_BRAKE_FORCE   19.0f
#define DRIVE_REVERSE_SPEED  6.0f
#define DRIVE_TOP_SPEED     28.0f

struct city_player {
    vec3    position;
    float   yaw;               // which way the body faces
    float   speed;             // ground speed, for the animation
    idx     body;

    idx      character;        // index into city_catalog::characters
    animator anim;
    float    anim_time;
    idx      anim_clip;        // clip currently playing, an animator index
    bool     anim_ready;
    bool     airborne;

    int      car;              // index into city_traffic::cars, -1 when on foot
    float    enter_cooldown;   // stops one keypress toggling twice

    // camera
    float cam_yaw, cam_pitch, cam_dist;
};

static void city_player_init(city_player& p, pix_data_loader& loader, const city_catalog& cat,
                             phys_world& phys, vec3 spawn);
static void city_player_update(city_player& p, const pix_window& window, city_traffic& traffic,
                               const city_catalog& cat, phys_world& phys, float dt);
static void city_player_camera(const city_player& p, const phys_world& phys, camera* out);
static void city_player_draw(const city_player& p, const city_catalog& cat, pix_renderer& renderer);
static bool city_player_on_foot(const city_player& p) { return p.car < 0; }

// ---------------- implementation ----------------

static void city_player_init(city_player& p, pix_data_loader& loader, const city_catalog& cat,
                             phys_world& phys, vec3 spawn) {
    memset(&p, 0, sizeof(p));
    p.position = spawn;
    p.car = -1;
    p.cam_dist = CAM_FOOT_DIST;
    p.cam_pitch = 0.28f;

    phys_body b = {};
    b.shape = PHYS_CYLINDER;
    b.position = spawn;
    b.radius = PLAYER_RADIUS;
    b.height = PLAYER_HEIGHT;
    b.inv_mass = 1.0f / 82.0f;
    b.restitution = 0.0f;
    b.drag = 9.0f;
    b.group = PHYS_LAYER_PLAYER;
    b.collides = PHYS_LAYER_ALL;
    b.gravity = true;
    p.body = phys_add_body(phys, b);

    p.character = (idx)-1;
    p.anim_clip = 0;
    if (cat.character_count) {
        p.character = 0;                       // the hoodie character leads the cast
        const city_character& ch = cat.characters[p.character];
        if (ch.ok && city_build_animator(p.anim, loader, ch.model)) {
            p.anim_clip = ch.clips[CLIP_IDLE] != (idx)-1 ? ch.clips[CLIP_IDLE] : 0;
            animator_play(p.anim, p.anim_clip);
            p.anim_ready = true;
        }
    }
}

// ---- getting in and out ----

// `range` is clearance beyond the bodywork, so a long vehicle is reachable
// from anywhere along its side rather than only level with its middle
static int city__nearest_car(const city_traffic& t, const city_catalog& cat,
                             vec3 position, float range) {
    int best = -1;
    float best_gap = range;
    for (size_t i = 0; i < MAX_CARS; i++) {
        const city_car& c = t.cars[i];
        if (!c.active || c.player_driven) continue;
        const city_vehicle_model& vm = cat.vehicles[c.model];

        // distance to the car's footprint, in its own frame
        obb2 box = { v2(c.position.x, c.position.z),
                     v2(vm.half_width, vm.half_length), c.yaw + vm.model_yaw };
        vec2 on_body = closest_point_obb(box, v2(position.x, position.z));
        float gap = v2len(v2sub(v2(position.x, position.z), on_body));
        if (gap < best_gap) { best_gap = gap; best = (int)i; }
    }
    return best;
}

static void city__enter_car(city_player& p, city_traffic& t, phys_world& phys, int index) {
    city_car& c = t.cars[index];
    c.player_driven = true;
    c.blocked_time = 0.0f;
    p.car = index;
    // the player is now the car, so their own collider steps out of the world
    phys_body* b = phys_get_body(phys, p.body);
    if (b) b->active = false;
}

static void city__exit_car(city_player& p, city_traffic& t, const city_catalog& cat,
                           phys_world& phys) {
    if (p.car < 0) return;
    city_car& c = t.cars[p.car];
    const city_vehicle_model& vm = cat.vehicles[c.model];
    c.player_driven = false;
    c.speed *= 0.2f;

    // step out on the driver's side, and only if there is room for a person
    vec3 fwd = forward_from_yaw(c.yaw);
    vec3 left = v3scale(dir_right(fwd), -(vm.half_width + 1.1f));
    vec3 spot = v3add(c.position, left);
    spot.y = 0.0f;
    if (phys_point_blocked(phys, spot, PLAYER_RADIUS, PLAYER_HEIGHT)) {
        spot = v3add(c.position, v3scale(dir_right(fwd), vm.half_width + 1.1f));
        spot.y = 0.0f;
    }

    phys_body* b = phys_get_body(phys, p.body);
    if (!b) {
        // the slot was recycled while we were driving; take a fresh one
        phys_body nb = {};
        nb.shape = PHYS_CYLINDER;
        nb.radius = PLAYER_RADIUS;
        nb.height = PLAYER_HEIGHT;
        nb.inv_mass = 1.0f / 82.0f;
        nb.drag = 9.0f;
        nb.group = PHYS_LAYER_PLAYER;
        nb.collides = PHYS_LAYER_ALL;
        nb.gravity = true;
        nb.position = spot;
        p.body = phys_add_body(phys, nb);
    } else {
        b->active = true;
        b->position = spot;
        b->velocity = v3(0.0f, 0.0f, 0.0f);
    }
    p.position = spot;
    p.yaw = c.yaw;
    p.car = -1;
}

// ---- input ----

// Movement is expressed in the camera's frame, which is what makes a third
// person control scheme feel right: pushing forward goes where you are looking,
// not where the character happens to be pointing.
static vec3 city__move_axis(const pix_window& window, float cam_yaw) {
    float x = 0.0f, z = 0.0f;
    if (window.keystates['W'].held || window.keystates[KEY_UP].held)    z += 1.0f;
    if (window.keystates['S'].held || window.keystates[KEY_DOWN].held)  z -= 1.0f;
    // +x is right, matching both dir_right and the gamepad's own left stick
    if (window.keystates['A'].held || window.keystates[KEY_LEFT].held)  x -= 1.0f;
    if (window.keystates['D'].held || window.keystates[KEY_RIGHT].held) x += 1.0f;

    const pix_gamepad& pad = window.gamepads[0];
    if (pad.connected) {
        x += pad.left_x;
        z += pad.left_y;
    }

    float len = sqrtf(x * x + z * z);
    if (len < 0.05f) return v3(0.0f, 0.0f, 0.0f);
    if (len > 1.0f) { x /= len; z /= len; }

    vec3 forward = forward_from_yaw(cam_yaw);
    vec3 right = dir_right(forward);
    return v3(forward.x * z + right.x * x, 0.0f, forward.z * z + right.z * x);
}

static void city__update_on_foot(city_player& p, const pix_window& window, phys_world& phys,
                                 float dt) {
    phys_body* b = phys_get_body(phys, p.body);
    if (!b) return;

    vec3 wish = city__move_axis(window, p.cam_yaw);
    bool sprint = window.keystates[KEY_SHIFT].held
               || window.gamepads[0].buttons[PAD_LEFT_STICK].held
               || window.gamepads[0].left_trigger > 0.5f;
    float top = sprint ? PLAYER_RUN_SPEED : PLAYER_WALK_SPEED;

    float wish_len = v3len(wish);
    if (wish_len > 0.01f) {
        // accelerate toward the wish velocity rather than snapping to it, so
        // turning has a little weight to it
        vec3 want = v3scale(wish, top);
        b->velocity.x = damp(b->velocity.x, want.x, 14.0f, dt);
        b->velocity.z = damp(b->velocity.z, want.z, 14.0f, dt);
        p.yaw = damp_angle(p.yaw, yaw_from_forward(v3norm(wish)), 14.0f, dt);
    } else {
        b->velocity.x = damp(b->velocity.x, 0.0f, 16.0f, dt);
        b->velocity.z = damp(b->velocity.z, 0.0f, 16.0f, dt);
    }

    if ((window.keystates[' '].pressed || window.gamepads[0].buttons[PAD_A].pressed)
        && b->on_ground) {
        b->velocity.y = 7.4f;
    }

    p.position = b->position;
    p.airborne = !b->on_ground;
    p.speed = sqrtf(b->velocity.x * b->velocity.x + b->velocity.z * b->velocity.z);
}

static void city__update_driving(city_player& p, const pix_window& window, city_traffic& t,
                                 const city_catalog& cat, phys_world& phys, float dt) {
    city_car& c = t.cars[p.car];
    const city_vehicle_model& vm = cat.vehicles[c.model];
    const pix_gamepad& pad = window.gamepads[0];

    float throttle = 0.0f, brake = 0.0f, steer = 0.0f;
    if (window.keystates['W'].held || window.keystates[KEY_UP].held)    throttle += 1.0f;
    if (window.keystates['S'].held || window.keystates[KEY_DOWN].held)  brake += 1.0f;
    if (window.keystates['A'].held || window.keystates[KEY_LEFT].held)  steer += 1.0f;
    if (window.keystates['D'].held || window.keystates[KEY_RIGHT].held) steer -= 1.0f;
    if (pad.connected) {
        throttle += pad.right_trigger;
        brake += pad.left_trigger;
        steer += pad.left_x;
    }
    steer = clampf(steer, -1.0f, 1.0f);
    bool handbrake = window.keystates[' '].held || pad.buttons[PAD_B].held;

    // Braking becomes reverse once stopped, which is the behaviour a single
    // pedal expects; a separate reverse key would be one more thing to learn.
    if (throttle > 0.01f) {
        c.speed += DRIVE_ENGINE_FORCE * throttle * dt;
    } else if (brake > 0.01f) {
        if (c.speed > 0.2f) c.speed -= DRIVE_BRAKE_FORCE * brake * dt;
        else                c.speed -= DRIVE_ENGINE_FORCE * 0.6f * brake * dt;
    } else {
        c.speed = damp(c.speed, 0.0f, 1.1f, dt);          // engine braking
    }
    if (handbrake) c.speed = damp(c.speed, 0.0f, 6.0f, dt);

    c.speed = clampf(c.speed, -DRIVE_REVERSE_SPEED, DRIVE_TOP_SPEED);

    // steering authority falls off with speed, so the car is agile parking and
    // stable at the top end
    float grip = 1.0f / (1.0f + fabsf(c.speed) * 0.055f);
    c.steer = damp(c.steer, steer * grip, 9.0f, dt);

    city__integrate_car(c, vm, phys, dt);

    p.position = c.position;
    p.yaw = c.yaw;
    p.speed = fabsf(c.speed);
}

static void city_player_update(city_player& p, const pix_window& window, city_traffic& traffic,
                               const city_catalog& cat, phys_world& phys, float dt) {
    // ---- look ----
    const pix_gamepad& pad = window.gamepads[0];
    p.cam_yaw   -= window.mouse_rel_x * 0.0032f;
    p.cam_pitch += window.mouse_rel_y * 0.0028f;
    if (pad.connected) {
        p.cam_yaw   -= pad.right_x * dt * 2.6f;
        p.cam_pitch -= pad.right_y * dt * 1.9f;
    }
    p.cam_pitch = clampf(p.cam_pitch, -0.35f, 1.15f);

    float want_dist = city_player_on_foot(p) ? CAM_FOOT_DIST : CAM_CAR_DIST;
    p.cam_dist = damp(p.cam_dist, want_dist, 5.0f, dt);

    // ---- get in / get out ----
    if (p.enter_cooldown > 0.0f) p.enter_cooldown -= dt;
    bool interact = window.keystates['F'].pressed || window.keystates['E'].pressed
                 || pad.buttons[PAD_Y].pressed;
    if (interact && p.enter_cooldown <= 0.0f) {
        p.enter_cooldown = 0.35f;
        if (city_player_on_foot(p)) {
            int car = city__nearest_car(traffic, cat, p.position, ENTER_CAR_RANGE);
            if (car >= 0) city__enter_car(p, traffic, phys, car);
        } else {
            city__exit_car(p, traffic, cat, phys);
        }
    }

    if (city_player_on_foot(p)) city__update_on_foot(p, window, phys, dt);
    else                        city__update_driving(p, window, traffic, cat, phys, dt);

    // ---- animation ----
    // A clip per locomotion state rather than one cycle stretched over all of
    // them: idle when stopped, walk, run, and the jump clip whenever the body
    // is off the ground. Playback rate still follows ground speed inside walk
    // and run, so a stroll and a jog are the same clip at different tempos.
    if (p.anim_ready && p.character < cat.character_count) {
        const city_character& ch = cat.characters[p.character];
        idx want = ch.clips[CLIP_IDLE];
        float rate = 1.0f;

        if (!city_player_on_foot(p)) {
            want = ch.clips[CLIP_SIT];                      // behind the wheel
        } else if (p.airborne && ch.clips[CLIP_JUMP] != (idx)-1) {
            want = ch.clips[CLIP_JUMP];
        } else if (p.speed > PLAYER_WALK_SPEED * 1.35f) {
            want = ch.clips[CLIP_RUN];
            rate = clampf(p.speed / PLAYER_RUN_SPEED, 0.65f, 1.55f);
        } else if (p.speed > 0.3f) {
            want = ch.clips[CLIP_WALK];
            rate = clampf(p.speed / PLAYER_WALK_SPEED, 0.55f, 1.85f);
        }
        if (want == (idx)-1) want = p.anim_clip;

        if (want != p.anim_clip) { p.anim_clip = want; p.anim_time = 0.0f; }
        p.anim_time += dt * rate;
        animator_sample(p.anim, p.anim_clip, p.anim_time, &p.anim.pose);
    }
}

// The camera sits behind the target on a spring arm, pulled in whenever a wall
// would otherwise come between it and the player.
static void city_player_camera(const city_player& p, const phys_world& phys, camera* out) {
    vec3 target = v3(p.position.x, p.position.y + CAM_HEIGHT, p.position.z);

    float cp = cosf(p.cam_pitch);
    vec3 back = v3(sinf(p.cam_yaw) * cp, sinf(p.cam_pitch), cosf(p.cam_yaw) * cp);

    float dist = p.cam_dist;
    float hit;
    if (phys_raycast(phys, target, back, dist + 0.6f, &hit)) {
        float allowed = hit - 0.5f;
        if (allowed < CAM_MIN_DIST) allowed = CAM_MIN_DIST;
        if (allowed < dist) dist = allowed;
    }

    out->position = v3add(target, v3scale(back, dist));
    if (out->position.y < 0.6f) out->position.y = 0.6f;
    out->direction = v3norm(v3sub(target, out->position));
    out->up = v3(0.0f, 1.0f, 0.0f);
}

static void city_player_draw(const city_player& p, const city_catalog& cat,
                             pix_renderer& renderer) {
    if (!city_player_on_foot(p)) return;      // the car is drawn by the traffic pass
    if (!p.anim_ready || p.character >= cat.character_count) return;
    const city_character& ch = cat.characters[p.character];
    if (!ch.ok || ch.mesh == (idx)-1) return;

    pix_render_instance inst = {};
    inst.mesh = ch.mesh;
    inst.material = ch.materials[0];
    inst.trainsform = mat4_trs_y(p.position, p.yaw + ch.yaw_offset, ch.scale);
    push_animated_instance(renderer, inst, p.anim.pose);
}
