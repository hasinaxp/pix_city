#pragma once
#include "../core/platform.hpp"
#include "../core/animation.hpp"
#include "city_traffic.hpp"
#include "city_weapons.hpp"

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
// ---- over the shoulder ----
//
// Straight behind the player is the right camera for walking around and the
// wrong one for shooting: the reticle sits in the middle of the screen and so
// does the back of the player's head, so the one thing being aimed at is the
// one thing that cannot be seen. Raising the gun slides the whole camera to
// the right and pulls it in, which puts the player in the left of the frame
// and leaves the line of the shot clear.
//
// The offset moves the camera and what it looks at by the same amount, so the
// view direction does not change - the middle of the screen still points
// exactly along city_player_aim, and the reticle still marks where a round
// goes. Anything that moved only one of the two would leave the two
// disagreeing, which is worse than the head being in the way.
#define CAM_AIM_SHOULDER 0.85f    // metres to the right, aiming
#define CAM_AIM_DIST     3.4f     // and how far back it sits instead of CAM_FOOT_DIST
#define CAM_AIM_RISE     0.12f    // a touch of height, so the gun is not on the horizon

// How the jump clip's own timeline maps onto a jump.
//
// Man_Jump is 1.04 s: a crouch and a launch, feet off the ground at 0.28, back
// down at 0.68, and a tail of standing still after that. Played as a looping
// clip on wall-clock time - which is what it was doing - the crouch happens in
// mid air, the landing happens at the top of the arc, and if the hop is short
// it never reaches the landing at all.
//
// So the clip is driven by the state of the jump instead of by the clock: the
// launch plays at its own speed on take-off, the airborne section is stretched
// or squeezed to cover however long the character is actually off the ground,
// and the landing plays out at its own speed once the feet are back down.
#define JUMP_LAUNCH_END   0.27f   // fraction of the clip: feet leave the ground
#define JUMP_AIR_END      0.66f   // fraction of the clip: feet touch down
#define JUMP_CLIP_END     0.80f   // fraction of the clip: recovered, back to standing

// How long a clip change takes to cross-fade. Long enough to kill the pop,
// short enough that the character is never visibly in two poses at once.
#define ANIM_FADE_TIME    0.14f

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
    // the clip being faded out of, and how far through that fade we are
    idx      anim_prev_clip;
    float    anim_prev_time;
    float    anim_fade;        // 1 at the moment of the switch, ramps to 0
    bool     anim_ready;

    bool     airborne;
    float    air_time;         // seconds since leaving the ground
    float    land_time;        // seconds left of the landing part of the jump clip

    int      car;              // index into city_traffic::cars, -1 when on foot
    float    enter_cooldown;   // stops one keypress toggling twice

    // ---- punching ----
    // The swing itself is just another clip on the same crossfaded animator
    // every other locomotion state already uses - see the CLIP_PUNCH branch
    // in city_player_update. `punch_landed` is what keeps one swing from
    // registering a hit on every frame its fist happens to overlap someone;
    // city_game.hpp's combat code claims it the moment it lands a hit.
    bool     punching;
    bool     punch_landed;

    // ---- carrying a gun ----
    // The state lives in city_arms (see city_weapons.hpp); what the player owns
    // on top of it is where the thing physically is. The hand solve below runs
    // once a frame off the pose the animator just produced, and everything
    // downstream - the gun's own draw, the muzzle flash, where a shot starts -
    // reads these rather than repeating it.
    city_arms arms;
    mat4      gun_transform;
    bool      gun_drawn;         // true when there is a gun in the hand to draw

    // camera
    float cam_yaw, cam_pitch, cam_dist;
    // 0 with the gun down, 1 with it up. What it drives is the camera swinging
    // over the right shoulder - see city_player_camera - and it is damped
    // rather than switched because a camera that jumps a metre sideways the
    // frame a mouse button goes down is unreadable.
    float cam_shoulder;
};

static void city_player_init(city_player& p, pix_data_loader& loader, const city_catalog& cat,
                             phys_world& phys, vec3 spawn);
static void city_player_update(city_player& p, const pix_window& window, city_traffic& traffic,
                               const city_catalog& cat, phys_world& phys, float dt);
static void city_player_camera(const city_player& p, const phys_world& phys, camera* out);
static void city_player_draw(const city_player& p, const city_catalog& cat, pix_renderer& renderer);
static bool city_player_on_foot(const city_player& p) { return p.car < 0; }

// True for exactly one frame per swing, at the moment the animation's fist
// is actually extended - not the frame the key was pressed. city_game.hpp's
// combat code calls this once a frame and, only when it comes back true,
// scans for someone standing in front of the player to hit.
static bool city_player_punch_frame(city_player& p);

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
    // Deliberately far heavier than a pedestrian. The solver splits a
    // penetration between two bodies by mass, so at equal mass a pavement of
    // people walking past nudges the player a little further into the road on
    // every contact until they are standing in the traffic - which is exactly
    // what a narrow footpath made happen. Weighted like this the crowd parts
    // around the player instead.
    b.inv_mass = 1.0f / 600.0f;
    b.restitution = 0.0f;
    b.drag = 9.0f;
    b.group = PHYS_LAYER_PLAYER;
    b.collides = PHYS_LAYER_ALL;
    b.gravity = true;
    p.body = phys_add_body(phys, b);

    p.character = city_pick_player(cat, loader);
    p.anim_clip = 0;
    p.anim_prev_clip = 0;
    if (p.character < cat.character_count) {
        const city_character& ch = cat.characters[p.character];
        if (ch.ok && city_build_character_animator(p.anim, loader, ch)) {
            p.anim_clip = ch.clips[CLIP_IDLE] != (idx)-1 ? ch.clips[CLIP_IDLE] : 0;
            p.anim_prev_clip = p.anim_clip;
            animator_play(p.anim, p.anim_clip);
            p.anim_ready = true;
        }
    }

    // The hand a gun goes in. Two rigs in this cast and two conventions: the
    // Quaternius skeletons name it Wrist.R, the Mixamo-style one RightHand.
    // Resolved once here rather than by name every frame - it is a string
    // compare over sixty bones, and it cannot change for the life of the rig.
    city_arms_init(p.arms);
    if (p.anim_ready) {
        // Three conventions across this cast: the 62-bone Quaternius rigs
        // call it Wrist.R, their 31-bone cousins Palm.R, and the Mixamo-style
        // woman RightHand.
        static const char* const HAND[] = { "Wrist.R", "Palm.R", "RightHand", "Hand.R" };
        p.arms.hand_bone = animator_find_bone(p.anim, HAND, 4);
    }
}

// ---- where the player is looking ----
//
// The camera orbits behind the player (see city_player_camera), so the way
// they are *aiming* is the way that arm of the orbit points inward - which is
// the one direction in this file that is neither the body's facing nor the
// camera's own position, and is what a shot is fired along.
static vec3 city_player_aim(const city_player& p) {
    float cp = cosf(p.cam_pitch);
    return v3(-sinf(p.cam_yaw) * cp, -sinf(p.cam_pitch), -cosf(p.cam_yaw) * cp);
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
        nb.inv_mass = 1.0f / 600.0f;
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
        b->velocity.y = PLAYER_JUMP_SPEED;
    }

    p.position = b->position;
    bool airborne = !b->on_ground;
    if (airborne && !p.airborne) p.air_time = 0.0f;      // just left the ground
    if (!airborne && p.airborne) {
        // just touched down: hand the rest of the clip to the landing
        const float dur_frac = JUMP_CLIP_END - JUMP_AIR_END;
        p.land_time = dur_frac * animator_duration(p.anim, p.anim_clip);
    }
    if (airborne) p.air_time += dt;
    p.airborne = airborne;
    if (p.land_time > 0.0f) p.land_time -= dt;

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
    // `steer` is +1 for a right turn, so left is the negative side
    if (window.keystates['A'].held || window.keystates[KEY_LEFT].held)  steer -= 1.0f;
    if (window.keystates['D'].held || window.keystates[KEY_RIGHT].held) steer += 1.0f;
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

    // The gun being up is what asks for the shoulder camera, and it stays
    // asked for through the recoil and the follow-through of a shot rather
    // than dropping back the instant the trigger is released.
    bool over_shoulder = city_player_on_foot(p) && (p.arms.aiming || p.arms.shoot > 0.0f);
    p.cam_shoulder = damp(p.cam_shoulder, over_shoulder ? 1.0f : 0.0f, 8.0f, dt);

    float want_dist = city_player_on_foot(p) ? CAM_FOOT_DIST : CAM_CAR_DIST;
    if (city_player_on_foot(p))
        want_dist = CAM_FOOT_DIST + (CAM_AIM_DIST - CAM_FOOT_DIST) * p.cam_shoulder;
    p.cam_dist = damp(p.cam_dist, want_dist, 5.0f, dt);

    // ---- get in / get out ----
    // 'E' is the house door interact (see city_house.hpp) - kept off this one
    // so a press near both a car and a doorway cannot fire both at once.
    if (p.enter_cooldown > 0.0f) p.enter_cooldown -= dt;
    bool interact = window.keystates['F'].pressed || pad.buttons[PAD_Y].pressed;
    if (interact && p.enter_cooldown <= 0.0f) {
        p.enter_cooldown = 0.35f;
        if (city_player_on_foot(p)) {
            int car = city__nearest_car(traffic, cat, p.position, ENTER_CAR_RANGE);
            if (car >= 0) city__enter_car(p, traffic, phys, car);
        } else {
            city__exit_car(p, traffic, cat, phys);
        }
    }

    // ---- the gun ----
    //
    // The recoil the tick hands back is added to the camera's pitch rather
    // than played as an animation over it: firing pushed the real aim up, and
    // this is the same number walking the real aim back down. What that buys
    // is that a burst that climbs is genuinely harder to keep on target, not
    // decorated to look like it.
    p.cam_pitch += city_arms_tick(p.arms, cat, dt);
    p.cam_pitch = clampf(p.cam_pitch, -0.35f, 1.15f);

    bool armed = city_player_on_foot(p) && city_arms_current(p.arms, cat) != 0;
    if (!city_player_on_foot(p)) { p.arms.aiming = false; p.arms.reload = 0.0f; }

    if (city_player_on_foot(p)) {
        if (window.keystates['Q'].pressed || pad.buttons[PAD_DPAD_UP].pressed)
            city_arms_next(p.arms, cat);
        // and straight to a numbered slot, for anyone who would rather not
        // cycle past two guns to reach the third
        for (int k = 0; k < WEAPON_COUNT; k++)
            if (window.keystates['1' + k].pressed && p.arms.owned[k]) {
                p.arms.weapon = k;
                p.arms.reload = 0.0f;
            }
        if (window.keystates['R'].pressed || pad.buttons[PAD_B].pressed)
            city_arms_start_reload(p.arms, cat);
    }

    // Aiming is held, not toggled: it is a stance, and a stance you have to
    // remember you are in is a stance you keep firing out of by accident.
    p.arms.aiming = armed && !p.punching
                 && (window.keystates[MOUSE_BUTTON_RIGHT].held
                     || pad.left_trigger > 0.5f);

    // ---- punching ----
    // On foot only, one swing at a time - a second click partway through the
    // first is ignored rather than restarting the clip, which is what stops
    // a fast clicker looking like they never actually threw the punch.
    // A gun in the hand takes the same button over entirely: swinging a fist
    // while holding a rifle is not a choice anyone would make on purpose.
    bool trigger_pressed = window.keystates[MOUSE_BUTTON_LEFT].pressed
                        || pad.buttons[PAD_RIGHT_BUMPER].pressed;
    bool trigger_held = window.keystates[MOUSE_BUTTON_LEFT].held
                     || pad.buttons[PAD_RIGHT_BUMPER].held
                     || pad.right_trigger > 0.5f;
    bool want_punch = !armed && (trigger_pressed || pad.buttons[PAD_X].pressed);
    if (want_punch && city_player_on_foot(p) && !p.punching
        && p.character < cat.character_count
        && cat.characters[p.character].clips[CLIP_PUNCH] != (idx)-1) {
        p.punching = true;
        p.punch_landed = false;
    }

    if (city_player_on_foot(p)) city__update_on_foot(p, window, phys, dt);
    else                        city__update_driving(p, window, traffic, cat, phys, dt);

    // Aiming turns the body to face the shot, whatever direction the feet
    // happen to be carrying it. Letting the aim and the body disagree is what
    // makes third person shooting read as a floating reticle rather than as a
    // person pointing something at something.
    if (p.arms.aiming || p.arms.shoot > 0.0f)
        p.yaw = damp_angle(p.yaw, p.cam_yaw, 16.0f, dt);

    // ---- animation ----
    // A clip per locomotion state rather than one cycle stretched over all of
    // them: idle when stopped, walk, run, and the jump clip for as long as the
    // jump lasts. Playback rate follows ground speed inside walk and run, so a
    // stroll and a jog are the same clip at different tempos, and every change
    // of clip is cross-faded rather than cut.
    if (p.anim_ready && p.character < cat.character_count) {
        const city_character& ch = cat.characters[p.character];
        idx want = ch.clips[CLIP_IDLE];
        float rate = 1.0f;
        bool  jumping = false;

        if (!city_player_on_foot(p)) {
            want = ch.clips[CLIP_SIT];                      // behind the wheel
        } else if (p.punching && ch.clips[CLIP_PUNCH] != (idx)-1) {
            want = ch.clips[CLIP_PUNCH];
        // Armed changes what standing still and what running look like, and
        // nothing else: the walk is still the walk, because the gun is
        // attached to the hand rather than baked into the clip, and a rig
        // with no firearm poses at all falls through GUN_IDLE to its ordinary
        // idle (see CLIP_FALLBACK) and simply carries the thing.
        } else if (armed && (p.arms.aiming || p.arms.shoot > 0.0f)
                   && !p.airborne && p.land_time <= 0.0f) {
            want = p.arms.shoot > 0.0f ? ch.clips[CLIP_GUN_SHOOT] : ch.clips[CLIP_GUN_AIM];
        } else if (armed && p.speed > PLAYER_WALK_SPEED * 1.35f
                   && !p.airborne && p.land_time <= 0.0f) {
            want = ch.clips[CLIP_GUN_RUN];
            rate = clampf(p.speed / PLAYER_RUN_SPEED, 0.65f, 1.55f);
        } else if (armed && p.speed <= 0.3f && !p.airborne && p.land_time <= 0.0f) {
            want = ch.clips[CLIP_GUN_IDLE];
        } else if ((p.airborne || p.land_time > 0.0f) && ch.clips[CLIP_JUMP] != (idx)-1) {
            want = ch.clips[CLIP_JUMP];
            jumping = true;
        } else if (p.speed > PLAYER_WALK_SPEED * 1.35f) {
            want = ch.clips[CLIP_RUN];
            rate = clampf(p.speed / PLAYER_RUN_SPEED, 0.65f, 1.55f);
        } else if (p.speed > 0.3f) {
            want = ch.clips[CLIP_WALK];
            rate = clampf(p.speed / PLAYER_WALK_SPEED, 0.55f, 1.85f);
        }
        if (want == (idx)-1) want = p.anim_clip;

        if (want != p.anim_clip) {
            p.anim_prev_clip = p.anim_clip;
            p.anim_prev_time = p.anim_time;
            p.anim_fade = 1.0f;
            p.anim_clip = want;
            p.anim_time = 0.0f;
        }

        if (jumping) {
            // Position inside the jump clip is read off the jump, not the
            // clock. Airborne, the clip's flight section is stretched over
            // however long the arc actually takes; landed, the recovery plays
            // out at its own speed.
            float duration = animator_duration(p.anim, p.anim_clip);
            float u;
            if (p.airborne) {
                float flight = clampf(p.air_time / PLAYER_AIR_TIME, 0.0f, 1.0f);
                u = JUMP_LAUNCH_END + (JUMP_AIR_END - JUMP_LAUNCH_END) * flight;
            } else {
                float recover = clampf(1.0f - p.land_time
                                       / ((JUMP_CLIP_END - JUMP_AIR_END) * duration + 1e-4f),
                                       0.0f, 1.0f);
                u = JUMP_AIR_END + (JUMP_CLIP_END - JUMP_AIR_END) * recover;
            }
            p.anim_time = u * duration;
        } else {
            p.anim_time += dt * rate;
        }

        // The swing ends when its own clip does, not on a timer kept
        // separately - so a rig with a snappier punch throws faster punches.
        if (p.punching && p.anim_clip == ch.clips[CLIP_PUNCH]
            && p.anim_time >= animator_duration(p.anim, p.anim_clip)) {
            p.punching = false;
        }

        if (p.anim_fade > 0.0f) {
            p.anim_prev_time += dt;
            p.anim_fade -= dt / ANIM_FADE_TIME;
            if (p.anim_fade < 0.0f) p.anim_fade = 0.0f;
        }

        // fade runs 1 -> 0 away from the old clip, so the blend weight toward
        // the new one is its complement
        animator_sample_blend(p.anim, p.anim_prev_clip, p.anim_prev_time,
                              p.anim_clip, p.anim_time, 1.0f - p.anim_fade, &p.anim.pose);
    }

    // ---- putting the gun in the hand ----
    //
    // This runs after the pose because it reads it. The wrist's transform is
    // taken in world space, its axes are renormalised (the rigs carry a
    // hundredfold scale on their bones), and the gun's own frame is built
    // straight out of them rather than by multiplying a correction matrix
    // through: the barrel goes along the wrist's Y, the sights along its -X,
    // which is the arrangement every rig in this cast shares and the one
    // thing about a hand a bone name cannot tell you.
    p.gun_drawn = false;
    p.arms.muzzle_valid = false;
    const city_weapon* held = city_arms_current(p.arms, cat);
    if (held && p.anim_ready && p.arms.hand_bone >= 0 && city_player_on_foot(p)
        && p.character < cat.character_count) {
        const city_character& ch = cat.characters[p.character];
        const city_model* gun = city_get(cat, held->model);
        if (gun) {
            mat4 model = mat4_trs_y(p.position, p.yaw + ch.yaw_offset, ch.scale);
            mat4 hand = mat4_mul(model, animation_bone_world(p.anim, p.anim.pose,
                                                            p.arms.hand_bone));
            vec3 bx = v3norm(v3(hand.data[0], hand.data[1], hand.data[2]));
            vec3 by = v3norm(v3(hand.data[4], hand.data[5], hand.data[6]));
            vec3 bz = v3norm(v3(hand.data[8], hand.data[9], hand.data[10]));

            vec3 forward = by;                       // down the barrel
            vec3 up = v3scale(bx, -1.0f);            // out of the sights
            vec3 side = bz;
            vec3 wrist = v3(hand.data[12], hand.data[13], hand.data[14]);
            vec3 origin = v3add(wrist,
                v3add(v3scale(forward, held->grip.x),
                      v3add(v3scale(up, held->grip.y), v3scale(side, held->grip.z))));

            float k = gun->scale;
            mat4 m = mat4_identity();
            m.data[0] = forward.x * k; m.data[1] = forward.y * k; m.data[2] = forward.z * k;
            m.data[4] = up.x * k;      m.data[5] = up.y * k;      m.data[6] = up.z * k;
            m.data[8] = side.x * k;    m.data[9] = side.y * k;    m.data[10] = side.z * k;
            m.data[12] = origin.x;     m.data[13] = origin.y;     m.data[14] = origin.z;
            p.gun_transform = m;
            p.gun_drawn = true;

            p.arms.muzzle = v3add(origin, v3scale(forward, held->muzzle));
            p.arms.muzzle_dir = forward;
            p.arms.muzzle_right = side;
            p.arms.muzzle_valid = true;
        }
    }

    // ---- the trigger ----
    //
    // Only the decision is made here. What a round does to the world needs the
    // crowd, the traffic and the particle pool, none of which the player knows
    // about, so this raises `fired` for one frame exactly the way a pedestrian
    // raises `struck` and city_game.hpp resolves it - see city__player_shoot.
    if (armed && p.gun_drawn
        && city_arms_wants_shot(p.arms, cat, trigger_pressed, trigger_held)) {
        if (p.arms.magazine[p.arms.weapon] > 0) {
            p.arms.trigger = true;
        } else if (p.arms.dry_click <= 0.0f) {
            // Empty. One click, then a reload starts on its own - hunting for
            // the reload key while somebody shoots back is not tension.
            p.arms.dry_click = 0.35f;
            p.arms.clicked = true;
            city_arms_start_reload(p.arms, cat);
        }
    }
}

// Just under halfway through the swing is roughly where these rigs' fists
// are actually extended rather than still winding up or already pulling
// back - close enough without hand-tuning it per rig, since every clip
// scales its own frame timing into a fixed 0..1 span underneath this.
#define PUNCH_IMPACT_FRACTION 0.42f

static bool city_player_punch_frame(city_player& p) {
    if (!p.punching || p.punch_landed) return false;
    float duration = animator_duration(p.anim, p.anim_clip);
    if (duration <= 0.0f) duration = 0.4f;
    if (p.anim_time < duration * PUNCH_IMPACT_FRACTION) return false;
    p.punch_landed = true;
    return true;
}

// The camera sits behind the target on a spring arm, pulled in whenever a wall
// would otherwise come between it and the player.
static void city_player_camera(const city_player& p, const phys_world& phys, camera* out) {
    vec3 target = v3(p.position.x, p.position.y + CAM_HEIGHT, p.position.z);

    float cp = cosf(p.cam_pitch);
    vec3 back = v3(sinf(p.cam_yaw) * cp, sinf(p.cam_pitch), cosf(p.cam_yaw) * cp);

    // Over the shoulder, if the gun is up. Applied to the point being looked
    // at, so the camera behind it inherits the same shift and the direction
    // between them is untouched - see the CAM_AIM_ notes.
    if (p.cam_shoulder > 0.001f) {
        vec3 right = dir_right(v3norm(v3(-back.x, 0.0f, -back.z)));
        target = v3add(target, v3scale(right, CAM_AIM_SHOULDER * p.cam_shoulder));
        target.y += CAM_AIM_RISE * p.cam_shoulder;
    }

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
    inst.transform = mat4_trs_y(p.position, p.yaw + ch.yaw_offset, ch.scale);
    push_animated_instance(renderer, inst, p.anim.pose);

    // The gun rides on the hand solve from this frame's update, so it is an
    // ordinary rigid instance rather than anything skinned - which is also
    // why it costs nothing and needs no second rig.
    if (p.gun_drawn) {
        const city_weapon* held = city_arms_current(p.arms, cat);
        const city_model* gun = held ? city_get(cat, held->model) : 0;
        if (gun) {
            push_instance(renderer, gun->mesh, gun->material, p.gun_transform);
            for (int part = 0; part < gun->part_count; part++)
                push_instance(renderer, gun->part_mesh[part], gun->part_material[part],
                              p.gun_transform);
        }
    }
}
