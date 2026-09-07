#pragma once
#include "../core/physics.hpp"
#include "../core/sound.hpp"
#include "city_assets.hpp"
#include "city_map.hpp"
#include "city_peds.hpp"
#include "city_fx.hpp"

// Firearms: what is lying in the street to be picked up, what the player is
// carrying, and what happens between a trigger being pulled and something
// falling over.
//
// The catalogue already describes the three guns (see city_weapon) - damage,
// rate, spread, magazine, recoil - so nothing in here branches on which gun is
// being fired. A revolver and a rifle go down exactly the same code path and
// come out feeling completely different, which is the only way three weapons
// stay three numbers rather than three special cases.
//
// The shot itself is a ray, not a projectile. At pistol muzzle velocity a
// bullet crosses this whole city inside two frames, so simulating one buys
// nothing the tracer particles do not already sell - and a ray can ask the
// static world and the dynamic bodies which came first, which is the only
// question that actually matters.

// Enough that a walk of a couple of blocks turns one up, which is the whole
// point of scattering them rather than handing one out: sixty over a city a
// kilometre across is one gun per four hundred cells, and a player could
// reasonably never meet one.
#define MAX_PICKUPS      240
// How close the player has to be to a gun on the ground to take it, and how
// high above the pavement one floats while it waits.
#define PICKUP_RANGE     2.2f
#define PICKUP_HEIGHT    0.55f
#define PICKUP_BOB       0.09f

// Spare rounds a dropped gun comes with, as a multiple of its magazine.
#define PICKUP_MAGS      3

// How fast the camera settles back after a shot kicks it. Fast enough that a
// pistol is controllable, slow enough that holding a rifle's trigger walks the
// muzzle up the way it should.
#define RECOIL_RECOVER   7.0f

// How long a muzzle flash lights the street for. Two frames at sixty, which
// is all a real one lasts.
#define MUZZLE_LIGHT_TIME 0.045f
#define MUZZLE_LIGHT_RANGE 9.0f
// Comparable to a street lamp rather than to the sun, and mostly a night-time
// effect. The first number tried here was 120 flat, which by day threw a nine
// metre pool of white across the pavement on every round - a muzzle flash is
// bright *for its size*, and by daylight what sells it is the bloom around the
// flash itself rather than anything it lights up. So the light rides on the
// same night factor the street lamps do (see city__submit_lights): barely
// there at noon, and the brightest thing on the street at midnight.
#define MUZZLE_INTENSITY  30.0f
#define MUZZLE_DAY_FLOOR   0.12f

// Extra damage for a shot that lands above the shoulders. Not a separate
// hitbox - the ray already knows where on the body it crossed, so this is one
// compare rather than a second collision shape per pedestrian.
#define HEADSHOT_HEIGHT  1.45f
#define HEADSHOT_BONUS   2.6f

struct city_pickup {
    bool  active;
    vec3  position;
    float yaw;
    int   weapon;      // city_weapon_kind
};

struct city_pickups {
    city_pickup items[MAX_PICKUPS];
    size_t      count;
    rng         random;
};

// The player's side of a gun. Deliberately not "an inventory": three slots,
// one of them in your hand, and the rest of it is timers.
struct city_arms {
    int   weapon;                     // WEAPON_*, or -1 for empty handed
    bool  owned[WEAPON_COUNT];
    int   magazine[WEAPON_COUNT];     // rounds in the gun
    int   reserve[WEAPON_COUNT];      // rounds in the pocket

    bool  aiming;
    float cooldown;                   // seconds until the next round can leave
    float reload;                     // seconds left of a reload, 0 when not reloading
    float recoil;                     // radians of kick still owed back to the camera
    float shoot;                      // seconds into the firing clip, <= 0 when not firing

    // Where the barrel was and which way it pointed the last frame the gun was
    // drawn. Written by the player's own draw-time hand solve and read by
    // everything that needs a shot to start somewhere believable.
    vec3  muzzle;
    vec3  muzzle_dir;
    vec3  muzzle_right;
    bool  muzzle_valid;

    // The three one-frame flags that carry a shot across the systems that
    // have to cooperate on it, exactly the way city_ped::struck does: the
    // player raises `trigger` because only the player knows the button was
    // pressed, city_game.hpp turns that into a round because only it can
    // reach the crowd and the particles, and `fired` comes back so the audio
    // pass knows to make a noise. `clicked` is an empty gun, which is a
    // different sound and no bullet.
    bool  trigger;
    bool  clicked;
    bool  fired;
    float flash;                      // seconds of muzzle light left
    vec3  flash_at;

    int   hand_bone;                  // resolved once against the player's rig, -1 until then
    float dry_click;                  // stops an empty gun clicking every frame the trigger is held
};

// What one shot found.
struct city_shot {
    bool  hit;
    vec3  point;
    vec3  normal_dir;   // the direction the round was travelling when it landed
    int   ped;          // index into city_peds::people, -1 if it was not a person
    bool  solid;        // static geometry rather than a body
};

static void city_arms_init(city_arms& a);
static const city_weapon* city_arms_current(const city_arms& a, const city_catalog& cat);
// Timers, reloads and the recoil spring. Everything here is per frame and
// independent of whether the trigger was touched. Returns the radians of kick
// paid back to the camera this frame - a shot pushes the camera itself, and
// this is what walks it back down, so the two are the same number seen from
// opposite ends rather than a recoil animation played over the real aim.
static float city_arms_tick(city_arms& a, const city_catalog& cat, float dt);
static bool city_arms_start_reload(city_arms& a, const city_catalog& cat);
// Cycles to the next gun the player actually has, and past the last one back
// to empty handed - so one key both switches weapons and holsters.
static void city_arms_next(city_arms& a, const city_catalog& cat);
static bool city_arms_take(city_arms& a, const city_catalog& cat, int weapon, int spare);

// True when the trigger being held or pressed should put a round downrange
// this frame. `pressed` and `held` are separated so a revolver needs a fresh
// press per shot and a rifle does not.
static bool city_arms_wants_shot(const city_arms& a, const city_catalog& cat,
                                 bool pressed, bool held);

// Fires one round. `from`/`dir` is the line it takes; `ignore` is the
// shooter's own collider. Everything the shot does to the world - damage,
// knockback, particles, the crowd hearing it - happens in here.
static city_shot city_weapon_fire(city_arms& a, const city_catalog& cat, city_peds& peds,
                                  phys_world& phys, city_fx& fx, vec3 from, vec3 dir,
                                  idx ignore);

// ---- guns lying about the city ----
static void city_pickups_scatter(city_pickups& p, const city_world& w, uint32_t seed);
static int  city_pickups_nearest(const city_pickups& p, vec3 at, float range);
static void city_pickups_draw(const city_pickups& p, const city_catalog& cat,
                              pix_renderer& renderer, const frustum& view, vec3 eye, float time);

// ---- the noise ----
// Synthesised rather than loaded: a gunshot is a noise burst under an
// exponential envelope with a low thump beneath it, which is four lines of
// maths and no file to ship or fail to find.
static idx city_make_gunshot(pix_audio& audio, float length, float pitch, uint32_t seed);

// ---------------- implementation ----------------

static void city_arms_init(city_arms& a) {
    memset(&a, 0, sizeof(a));
    a.weapon = -1;
    a.hand_bone = -1;
}

static const city_weapon* city_arms_current(const city_arms& a, const city_catalog& cat) {
    if (a.weapon < 0 || a.weapon >= WEAPON_COUNT) return 0;
    const city_weapon& w = cat.weapons[a.weapon];
    return w.model == (idx)-1 ? 0 : &w;
}

static float city_arms_tick(city_arms& a, const city_catalog& cat, float dt) {
    if (a.cooldown > 0.0f) a.cooldown -= dt;
    if (a.dry_click > 0.0f) a.dry_click -= dt;
    if (a.flash > 0.0f) a.flash -= dt;
    if (a.shoot > 0.0f) a.shoot -= dt;

    // The camera walks back down to where it was pointing before the shot,
    // rather than being snapped back: what makes recoil a thing to fight is
    // that some of it is still there when the next round goes off.
    float recovered = 0.0f;
    if (a.recoil > 0.0f) {
        recovered = a.recoil * RECOIL_RECOVER * dt;
        if (recovered > a.recoil) recovered = a.recoil;
        a.recoil -= recovered;
        if (a.recoil < 1e-4f) a.recoil = 0.0f;
    }

    if (a.reload > 0.0f) {
        a.reload -= dt;
        if (a.reload <= 0.0f) {
            a.reload = 0.0f;
            const city_weapon* w = city_arms_current(a, cat);
            if (w) {
                int want = w->magazine - a.magazine[a.weapon];
                if (want > a.reserve[a.weapon]) want = a.reserve[a.weapon];
                a.magazine[a.weapon] += want;
                a.reserve[a.weapon]  -= want;
            }
        }
    }
    return recovered;
}

static bool city_arms_start_reload(city_arms& a, const city_catalog& cat) {
    const city_weapon* w = city_arms_current(a, cat);
    if (!w || a.reload > 0.0f) return false;
    if (a.magazine[a.weapon] >= w->magazine) return false;
    if (a.reserve[a.weapon] <= 0) return false;
    a.reload = w->reload_time;
    return true;
}

static void city_arms_next(city_arms& a, const city_catalog& cat) {
    // WEAPON_COUNT + 1 stops, because empty handed is one of them: from the
    // last gun the cycle goes to nothing, and from nothing back to the first.
    for (int step = 1; step <= WEAPON_COUNT + 1; step++) {
        int next = a.weapon + step;
        if (next >= WEAPON_COUNT) { a.weapon = -1; a.reload = 0.0f; a.aiming = false; return; }
        if (a.owned[next] && cat.weapons[next].model != (idx)-1) {
            a.weapon = next;
            a.reload = 0.0f;
            return;
        }
    }
}

static bool city_arms_take(city_arms& a, const city_catalog& cat, int weapon, int spare) {
    if (weapon < 0 || weapon >= WEAPON_COUNT) return false;
    const city_weapon& w = cat.weapons[weapon];
    if (w.model == (idx)-1) return false;

    if (!a.owned[weapon]) {
        a.owned[weapon] = true;
        a.magazine[weapon] = w.magazine;    // a gun found on the ground is loaded
        a.reserve[weapon] += spare;
        a.weapon = weapon;                  // and goes straight into the hand
        a.reload = 0.0f;
        return true;
    }
    // Already have one: it is ammunition now. Picking up a third pistol and
    // being told nothing happened is the thing that makes a pickup feel broken.
    a.reserve[weapon] += spare + w.magazine;
    return true;
}

static bool city_arms_wants_shot(const city_arms& a, const city_catalog& cat,
                                 bool pressed, bool held) {
    const city_weapon* w = city_arms_current(a, cat);
    if (!w || a.reload > 0.0f || a.cooldown > 0.0f) return false;
    return w->automatic ? (held || pressed) : pressed;
}

// Which pedestrian owns a physics body, or -1. A linear scan over the crowd,
// once per shot - the alternative is a back pointer on every body kept in step
// with a pool that recycles slots, which is a great deal more to get wrong for
// a lookup that happens when a trigger is pulled.
static int city__ped_of_body(const city_peds& peds, idx body) {
    if (body == (idx)-1) return -1;
    for (size_t i = 0; i < MAX_PEDS; i++)
        if (peds.people[i].active && peds.people[i].body == body) return (int)i;
    return -1;
}

// The cone a round leaves the barrel in. Hip fired it is wide enough that
// range is a real limit; down the arm it is tight enough to pick somebody out
// of a crowd, which is the entire reward for aiming.
static vec3 city__spread_dir(rng& r, vec3 dir, float spread) {
    if (spread <= 0.0f) return dir;
    return city__fx_cone(r, dir, spread);
}

static city_shot city_weapon_fire(city_arms& a, const city_catalog& cat, city_peds& peds,
                                  phys_world& phys, city_fx& fx, vec3 from, vec3 dir,
                                  idx ignore) {
    city_shot shot = {};
    shot.ped = -1;
    const city_weapon* w = city_arms_current(a, cat);
    if (!w) return shot;

    a.magazine[a.weapon]--;
    a.cooldown = w->rate;
    a.recoil += w->recoil;
    a.shoot = w->rate < 0.22f ? w->rate : 0.22f;
    a.fired = true;
    a.flash = MUZZLE_LIGHT_TIME;
    a.flash_at = from;

    vec3 line = v3norm(city__spread_dir(fx.random, dir,
                                        a.aiming ? w->aim_spread : w->spread));

    // Two questions, asked separately because they are answered by different
    // structures: what wall is in the way, and what body is. Whichever is
    // nearer is what the round actually hit.
    float wall_t = w->range;
    bool  wall = phys_raycast(phys, from, line, w->range, &wall_t);
    float body_t = wall ? wall_t : w->range;
    idx   body = phys_raycast_bodies(phys, from, line, body_t,
                                     PHYS_LAYER_PED | PHYS_LAYER_VEHICLE, ignore, &body_t);

    float travel = w->range;
    if (body != (idx)-1)   { travel = body_t; shot.hit = true; }
    else if (wall)         { travel = wall_t; shot.hit = true; shot.solid = true; }

    shot.point = v3add(from, v3scale(line, travel));
    shot.normal_dir = line;

    city_fx_muzzle(fx, from, line, w->size > 0.4f ? 1.25f : 1.0f);
    city_fx_tracer(fx, v3add(from, v3scale(line, 0.35f)), shot.point);
    city_fx_casing(fx, from, a.muzzle_valid ? a.muzzle_right : dir_right(line), 0.0f);

    if (body != (idx)-1) {
        int who = city__ped_of_body(peds, body);
        if (who >= 0) {
            shot.ped = who;
            const city_ped& victim = peds.people[who];
            // How far up the body the round crossed. The ray already knows,
            // so a head shot costs one subtraction rather than a second
            // collision shape on every pedestrian in the city.
            float height = shot.point.y - victim.position.y;
            float damage = w->damage * (height > HEADSHOT_HEIGHT ? HEADSHOT_BONUS : 1.0f);
            vec3 push = v3(line.x * w->knockback, w->knockback * 0.35f, line.z * w->knockback);
            city_fx_blood(fx, shot.point, line);
            city_ped_hit(peds, cat, phys, who, push, damage, from);
        } else {
            // A car, or something else solid enough to stop a bullet.
            city_fx_impact(fx, shot.point, line);
            phys_body* b = phys_get_body(phys, body);
            if (b && b->inv_mass > 0.0f)
                b->velocity = v3add(b->velocity, v3scale(line, w->knockback * 0.25f));
        }
    } else if (wall) {
        city_fx_impact(fx, shot.point, line);
    }

    // A gunshot carries much further than a punch does, and everybody who
    // hears one reacts to it - which is the whole difference between a scuffle
    // on a pavement and a shooting on one. See PED_GUNSHOT_RADIUS.
    city_peds_alarm(peds, from, PED_GUNSHOT_RADIUS);
    peds.reputation -= PED_REPUTATION_KILL * 0.5f;
    if (peds.reputation < -1.0f) peds.reputation = -1.0f;
    return shot;
}

// ---- guns lying about the city ----
//
// Scattered once, at generation time, and never cleaned up or topped up: a gun
// is a thing that is somewhere, and finding the same revolver on the same
// corner twenty minutes later is the point of it being there rather than being
// handed out by a spawner near the player.
static void city_pickups_scatter(city_pickups& p, const city_world& w, uint32_t seed) {
    memset(&p, 0, sizeof(p));
    p.random = rng_seed(seed ? seed : 0x6D5D5u);
    if (!w.walk_cell_count) return;

    // Rarer as they get louder. A pistol on most street corners and a rifle
    // once in a while is a city with guns in it; three of each everywhere is
    // an armoury.
    static const int ODDS[WEAPON_COUNT] = { 6, 3, 1 };
    int total = 0;
    for (int i = 0; i < WEAPON_COUNT; i++) total += ODDS[i];

    for (size_t n = 0; n < MAX_PICKUPS; n++) {
        uint32_t pick = rng_u32(p.random) % (uint32_t)w.walk_cell_count;
        uint32_t cell = w.walk_cells[pick];
        int cx = (int)(cell % CITY_CELLS), cz = (int)(cell / CITY_CELLS);

        int roll = rng_int(p.random, 0, total - 1), kind = 0;
        for (int i = 0; i < WEAPON_COUNT; i++) {
            if (roll < ODDS[i]) { kind = i; break; }
            roll -= ODDS[i];
        }

        city_pickup& it = p.items[p.count++];
        it.active = true;
        it.position = city_stand_point(w, cx, cz);
        it.position.y += PICKUP_HEIGHT;
        it.yaw = rng_range(p.random, 0.0f, 6.2831853f);
        it.weapon = kind;
    }
}

// And a couple within sight of wherever the player starts. Everything above is
// uniform over the map, which over a whole city is the right distribution and
// over the first thirty seconds of a session is a coin flip - see
// city_pickups_scatter for the density it is correcting.
static void city_pickups_seed_near(city_pickups& p, const city_world& w, vec3 at, int count) {
    if (!w.walk_cell_count) return;
    for (int made = 0, tries = 0; made < count && tries < 400; tries++) {
        uint32_t pick = rng_u32(p.random) % (uint32_t)w.walk_cell_count;
        uint32_t cell = w.walk_cells[pick];
        int cx = (int)(cell % CITY_CELLS), cz = (int)(cell / CITY_CELLS);
        vec3 spot = city_stand_point(w, cx, cz);
        float dx = spot.x - at.x, dz = spot.z - at.z;
        float d2 = dx * dx + dz * dz;
        // Near enough to find, far enough that it is found rather than
        // stepped on before the player has worked out which way is forward.
        if (d2 < 12.0f * 12.0f || d2 > 55.0f * 55.0f) continue;

        // Reuse one of the map-wide slots rather than adding to them, so the
        // total number of guns in the city stays what MAX_PICKUPS says it is.
        city_pickup& it = p.items[made % (p.count ? p.count : 1)];
        it.active = true;
        it.position = spot;
        it.position.y += PICKUP_HEIGHT;
        it.yaw = rng_range(p.random, 0.0f, 6.2831853f);
        it.weapon = made == 0 ? WEAPON_PISTOL : rng_int(p.random, 0, WEAPON_COUNT - 1);
        made++;
    }
}

static int city_pickups_nearest(const city_pickups& p, vec3 at, float range) {
    int best = -1;
    float best_d2 = range * range;
    for (size_t i = 0; i < p.count; i++) {
        const city_pickup& it = p.items[i];
        if (!it.active) continue;
        vec3 to = v3sub(it.position, at);
        to.y *= 0.5f;                    // forgiving vertically; a gun floats
        float d2 = v3dot(to, to);
        if (d2 < best_d2) { best_d2 = d2; best = (int)i; }
    }
    return best;
}

static void city_pickups_draw(const city_pickups& p, const city_catalog& cat,
                              pix_renderer& renderer, const frustum& view, vec3 eye,
                              float time) {
    for (size_t i = 0; i < p.count; i++) {
        const city_pickup& it = p.items[i];
        if (!it.active) continue;
        float dx = it.position.x - eye.x, dz = it.position.z - eye.z;
        if (dx * dx + dz * dz > PROP_FADE_NEAR * PROP_FADE_NEAR) continue;
        if (!frustum_test_sphere(view, it.position, 1.0f)) continue;

        const city_weapon& w = cat.weapons[it.weapon];
        const city_model* m = city_get(cat, w.model);
        if (!m) continue;

        // Turning slowly and bobbing. Everything else in this city stands
        // still, so movement is what says "this one is yours to take" without
        // a marker or a label anywhere.
        float phase = time * 1.1f + (float)i;
        vec3 at = it.position;
        at.y += sinf(phase * 1.7f) * PICKUP_BOB;
        push_instance(renderer, m->mesh, m->material,
                      mat4_trs_y(at, it.yaw + phase * 0.55f, m->scale));
        for (int part = 0; part < m->part_count; part++)
            push_instance(renderer, m->part_mesh[part], m->part_material[part],
                          mat4_trs_y(at, it.yaw + phase * 0.55f, m->scale));
    }
}

// ---- the noise ----
//
// A shot is a very short, very loud broadband crack with a low thump under it.
// Both halves come out of the same exponential envelope at different rates -
// the crack decays in a few milliseconds, the thump over a tenth of a second -
// and the whole thing is under a hard-edged attack, because the attack is what
// the ear reads as "gun" rather than "explosion".
static idx city_make_gunshot(pix_audio& audio, float length, float pitch, uint32_t seed) {
    enum { RATE = 44100 };
    static int16_t samples[RATE];        // one second is far more than any of these need
    size_t frames = (size_t)(RATE * length);
    if (frames > RATE) frames = RATE;

    rng r = rng_seed(seed ? seed : 7u);
    float thump_phase = 0.0f;
    // A little of the previous sample mixed into each new one: white noise on
    // its own is a hiss, and a gunshot is weighted much lower than that.
    float smoothed = 0.0f;

    for (size_t i = 0; i < frames; i++) {
        float t = (float)i / (float)RATE;
        float noise = rng_range(r, -1.0f, 1.0f);
        smoothed = smoothed * 0.55f + noise * 0.45f;

        float crack = smoothed * expf(-t * 90.0f / pitch);
        float body  = smoothed * expf(-t * 22.0f / pitch) * 0.55f;

        thump_phase += 6.2831853f * (110.0f * pitch) / (float)RATE;
        float thump = sinf(thump_phase) * expf(-t * 30.0f) * 0.5f;

        // A one millisecond ramp on the front. Without it the very first
        // sample is a step from silence to full scale, which clicks.
        float attack = t < 0.001f ? t * 1000.0f : 1.0f;
        float v = (crack + body + thump) * attack;
        if (v > 1.0f) v = 1.0f; else if (v < -1.0f) v = -1.0f;
        samples[i] = (int16_t)(v * 30000.0f);
    }
    return create_sound(audio, samples, frames, 1, RATE);
}
