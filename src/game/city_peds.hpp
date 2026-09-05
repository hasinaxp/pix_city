#pragma once
#include "../core/random.hpp"
#include "../core/physics.hpp"
#include "../core/animation.hpp"
#include "city_map.hpp"

// Pedestrians.
//
// People walk the pavement graph the same way cars walk the road graph: cell to
// cell, preferring to keep going the way they were headed. Roads are crossed
// only at the marked crossings beside junctions, and only when the lights say
// the traffic on that street has stopped - so a crowd builds up on the kerb and
// then goes over together, which is most of what makes a street look alive.
//
// The expensive part of a crowd is not the walking, it is the skinning: one
// pose is a full skeleton evaluation and a bone matrix upload. So poses are
// pooled. A handful of copies of each cycle are advanced every frame at
// staggered phases and each pedestrian just points at one of them, which keeps
// the per-frame animation cost flat no matter how many people are on screen and
// still avoids a street of people marching in lockstep.
//
// There is one bank per character rig, and each bank carries a walk cycle *and*
// an idle. Somebody waiting at a crossing standing still while their legs keep
// walking is the single most obvious tell that a crowd is faked, and an idle
// clip costs one more pose per rig to fix.

#define PED_WALK_POSES   8
#define PED_IDLE_POSES   3
// Faster than a real pedestrian for the same reason the player is, and kept
// well under the player's walk so that overtaking a crowd still reads as
// overtaking it.
#define PED_WALK_SPEED   1.85f
#define PED_CROSS_SPEED  2.75f

#define PED_STATE_WALK   0
#define PED_STATE_WAIT   1   // on the kerb, waiting for the traffic to stop
#define PED_STATE_CROSS  2

struct city_ped {
    bool    active;
    vec3    position;
    float   yaw;
    float   speed;

    int     node_x, node_z;
    int     next_x, next_z;
    int     cross_dir;       // direction being crossed in, DIR_NONE otherwise

    idx     body;
    uint8_t character;       // which rig, index into city_catalog::characters
    uint8_t material;        // which outfit tint of that rig
    uint8_t pose;
    uint8_t state;
    uint8_t bank_pose;       // which standing pose a waiting one holds
    float   height;          // multiplier on the rig's own scale
    float   lateral;         // offset from the cell centreline, so they do not single-file
    float   stuck;
};

// Staggered copies of one rig's cycles, shared by everyone using that rig.
struct city_pose_bank {
    animator  source;
    animation walk[PED_WALK_POSES];
    animation idle[PED_IDLE_POSES];
    // A second standing cycle. Everyone waiting at a crossing holding the same
    // pose is as obvious as everyone walking in step, and it is more obvious,
    // because a queue at a kerb is exactly where the eye stops to look.
    animation stand[PED_IDLE_POSES];
    float     walk_duration;
    float     idle_duration;
    float     stand_duration;
    idx       walk_clip;
    idx       idle_clip;
    idx       stand_clip;
    bool      ready;
};

struct city_peds {
    city_ped       people[MAX_PEDS];
    size_t         count;
    city_pose_bank banks[CITY_MAX_CHARACTERS];
    size_t         bank_count;
    rng            random;
};

static void city_peds_init(city_peds& p, pix_data_loader& loader, const city_catalog& cat,
                           uint32_t seed);
static void city_peds_update(city_peds& p, const city_world& w, const city_catalog& cat,
                             phys_world& phys, vec3 focus, float time, float dt);
static void city_peds_draw(const city_peds& p, const city_catalog& cat, pix_renderer& renderer,
                           const frustum& view, vec3 eye);

// ---------------- implementation ----------------

static void city_peds_init(city_peds& p, pix_data_loader& loader, const city_catalog& cat,
                           uint32_t seed) {
    memset(&p, 0, sizeof(p));
    p.random = rng_seed(seed);

    for (size_t c = 0; c < cat.character_count; c++) {
        const city_character& ch = cat.characters[c];
        city_pose_bank& bank = p.banks[p.bank_count];
        memset(&bank, 0, sizeof(bank));
        if (!ch.ok || !city_build_animator(bank.source, loader, ch.model)) {
            p.bank_count++;                    // keep bank indices aligned with characters
            continue;
        }
        bank.walk_clip  = ch.clips[CLIP_WALK] != (idx)-1 ? ch.clips[CLIP_WALK] : 0;
        bank.idle_clip  = ch.clips[CLIP_IDLE] != (idx)-1 ? ch.clips[CLIP_IDLE] : bank.walk_clip;
        bank.stand_clip = ch.clips[CLIP_TALK] != (idx)-1 ? ch.clips[CLIP_TALK] : bank.idle_clip;
        bank.walk_duration  = animator_duration(bank.source, bank.walk_clip);
        bank.idle_duration  = animator_duration(bank.source, bank.idle_clip);
        bank.stand_duration = animator_duration(bank.source, bank.stand_clip);
        bank.ready = true;
        p.bank_count++;
    }
}

static void city__bank_update(city_pose_bank& bank, float time) {
    if (!bank.ready) return;

    float wd = bank.walk_duration > 0.01f ? bank.walk_duration : 1.0f;
    for (int i = 0; i < PED_WALK_POSES; i++) {
        // evenly spread around the cycle, so no two neighbours are in step
        float phase = time + wd * ((float)i / (float)PED_WALK_POSES);
        animator_sample(bank.source, bank.walk_clip, phase, &bank.walk[i]);
    }

    float idur = bank.idle_duration > 0.01f ? bank.idle_duration : 1.0f;
    for (int i = 0; i < PED_IDLE_POSES; i++) {
        float phase = time + idur * ((float)i / (float)PED_IDLE_POSES);
        animator_sample(bank.source, bank.idle_clip, phase, &bank.idle[i]);
    }

    float sdur = bank.stand_duration > 0.01f ? bank.stand_duration : 1.0f;
    for (int i = 0; i < PED_IDLE_POSES; i++) {
        float phase = time + sdur * ((float)i / (float)PED_IDLE_POSES);
        animator_sample(bank.source, bank.stand_clip, phase, &bank.stand[i]);
    }
}

// Where inside a cell a person walks. Keeping a fixed lateral offset per
// pedestrian is what stops a pavement turning into a single file queue.
static vec3 city__ped_target(const city_world& w, const city_ped& ped) {
    vec3 centre;
    int dx = ped.next_x - ped.node_x, dz = ped.next_z - ped.node_z;
    vec3 travel = v3norm(v3((float)dx, 0.0f, (float)dz));

    centre = city_stand_point(w, ped.next_x, ped.next_z);
    return v3add(centre, v3scale(dir_right(travel), ped.lateral));
}

// Is there a marked crossing leading out of this pavement cell in `dir`, and
// somewhere to land on the other side?
static bool city__crossing_available(const city_world& w, int x, int z, int dir, int* out_span) {
    int rx = x + DIR_DX[dir], rz = z + DIR_DZ[dir];
    if (!city_is_road(w, rx, rz)) return false;
    const city_cell& road = city_at(w, rx, rz);
    if (!(road.flags & (CELLF_CROSSING | CELLF_JUNCTION))) return false;

    // roads here are one cell wide, but a junction can be two cells across on
    // the diagonal, so walk out until pavement reappears
    for (int span = 2; span <= 3; span++) {
        int tx = x + DIR_DX[dir] * span, tz = z + DIR_DZ[dir] * span;
        if (city_is_walkable(w, tx, tz)) { *out_span = span; return true; }
        if (!city_is_road(w, tx, tz)) return false;
    }
    return false;
}

static void city__ped_choose(city_ped& ped, const city_world& w, rng& r) {
    int back = -1;
    for (int d = 0; d < 4; d++)
        if (ped.node_x + DIR_DX[d] == ped.next_x && ped.node_z + DIR_DZ[d] == ped.next_z) {
            // `back` is where we came from relative to the cell we now stand on
            back = (d + 2) & 3;
        }

    int options[4], weights[4], count = 0, total = 0;
    for (int d = 0; d < 4; d++) {
        int nx = ped.next_x + DIR_DX[d], nz = ped.next_z + DIR_DZ[d];
        if (!city_is_walkable(w, nx, nz)) continue;
        int wgt = (d == back) ? 1 : 5;                 // turning back is a last resort
        options[count] = d;
        weights[count] = wgt;
        total += wgt;
        count++;
    }

    ped.node_x = ped.next_x;
    ped.node_z = ped.next_z;
    ped.cross_dir = DIR_NONE;
    ped.state = PED_STATE_WALK;

    // Every so often, step off the kerb instead of following it - but only
    // where there is actually a crossing to use.
    if (rng_chance(r, 0.28f)) {
        int order = rng_int(r, 0, 3);
        for (int i = 0; i < 4; i++) {
            int d = (order + i) & 3;
            int span;
            if (!city__crossing_available(w, ped.node_x, ped.node_z, d, &span)) continue;
            ped.next_x = ped.node_x + DIR_DX[d] * span;
            ped.next_z = ped.node_z + DIR_DZ[d] * span;
            ped.cross_dir = d;
            ped.state = PED_STATE_WAIT;
            return;
        }
    }

    if (!count) {                                       // boxed in; turn around
        ped.next_x = ped.node_x - DIR_DX[back < 0 ? 0 : back];
        ped.next_z = ped.node_z - DIR_DZ[back < 0 ? 0 : back];
        return;
    }

    int roll = rng_int(r, 0, total - 1);
    int pick = options[0];
    for (int i = 0; i < count; i++) {
        roll -= weights[i];
        if (roll < 0) { pick = options[i]; break; }
    }
    ped.next_x = ped.node_x + DIR_DX[pick];
    ped.next_z = ped.node_z + DIR_DZ[pick];
}

static int city_peds_spawn(city_peds& p, const city_world& w, const city_catalog& cat,
                           phys_world& phys, int cx, int cz) {
    if (!cat.character_count || !city_is_walkable(w, cx, cz)) return -1;

    int slot = -1;
    for (size_t i = 0; i < MAX_PEDS; i++)
        if (!p.people[i].active) { slot = (int)i; break; }
    if (slot < 0) return -1;

    city_ped& ped = p.people[slot];
    memset(&ped, 0, sizeof(ped));
    ped.node_x = ped.next_x = cx;
    ped.node_z = ped.next_z = cz;
    ped.position = city_stand_point(w, cx, cz);
    // spread across the clear strip only, less their own shoulders
    float spread = (SIDEWALK_WIDTH - KERB_STRIP) * 0.5f - 0.35f;
    ped.lateral = rng_range(p.random, -spread, spread);
    ped.character = (uint8_t)(rng_u32(p.random) % cat.character_count);
    ped.material = (uint8_t)(rng_u32(p.random)
                             % (cat.characters[ped.character].material_count | 1u));
    ped.pose = (uint8_t)(rng_u32(p.random) % PED_WALK_POSES);
    ped.bank_pose = (uint8_t)(rng_u32(p.random) % (PED_IDLE_POSES * 2));
    // Adults are not all the same height, and a crowd of identical silhouettes
    // is the first thing that gives a generated one away. Kept narrow: this is
    // build variation, not a cast of giants and children.
    ped.height = rng_range(p.random, 0.92f, 1.08f);
    // and the taller ones cover ground slightly faster
    ped.speed = PED_WALK_SPEED * ped.height * rng_range(p.random, 0.85f, 1.2f);
    ped.cross_dir = DIR_NONE;
    ped.active = true;
    city__ped_choose(ped, w, p.random);

    phys_body b = {};
    b.shape = PHYS_CYLINDER;
    b.position = ped.position;
    b.radius = 0.34f;
    b.height = PLAYER_HEIGHT;
    b.inv_mass = 1.0f / 75.0f;
    b.restitution = 0.0f;
    b.drag = 6.0f;
    b.group = PHYS_LAYER_PED;
    b.collides = PHYS_LAYER_ALL;
    b.gravity = true;
    ped.body = phys_add_body(phys, b);
    if (ped.body == (idx)-1) { ped.active = false; return -1; }

    p.count++;
    return slot;
}

static void city_peds_update(city_peds& p, const city_world& w, const city_catalog& cat,
                             phys_world& phys, vec3 focus, float time, float dt) {
    for (size_t b = 0; b < p.bank_count; b++) city__bank_update(p.banks[b], time);

    for (size_t i = 0; i < MAX_PEDS; i++) {
        city_ped& ped = p.people[i];
        if (!ped.active) continue;

        phys_body* body = phys_get_body(phys, ped.body);
        if (!body) { ped.active = false; if (p.count) p.count--; continue; }
        ped.position = body->position;

        float dx = ped.position.x - focus.x, dz = ped.position.z - focus.z;
        if (dx * dx + dz * dz > (PED_SPAWN_RADIUS * 1.4f) * (PED_SPAWN_RADIUS * 1.4f)) {
            phys_remove_body(phys, ped.body);
            ped.active = false;
            if (p.count) p.count--;
            continue;
        }

        // Waiting on the kerb: the street being crossed runs perpendicular to
        // the crossing direction, so the traffic to worry about is on the other
        // axis from the one being walked.
        if (ped.state == PED_STATE_WAIT) {
            int traffic_axis = (ped.cross_dir == DIR_PX || ped.cross_dir == DIR_NX) ? 1 : 0;
            if (traffic_axis_green(time, traffic_axis)) {
                body->velocity.x = 0.0f;
                body->velocity.z = 0.0f;
                continue;                                 // stand still, cars have right of way
            }
            ped.state = PED_STATE_CROSS;
        }

        vec3 target = city__ped_target(w, ped);
        vec3 to = v3sub(target, ped.position);
        to.y = 0.0f;
        float distance = v3len(to);

        if (distance < 0.9f) {
            city__ped_choose(ped, w, p.random);
            target = city__ped_target(w, ped);
            to = v3sub(target, ped.position);
            to.y = 0.0f;
            distance = v3len(to);
        }

        vec3 dir = distance > 1e-3f ? v3scale(to, 1.0f / distance) : forward_from_yaw(ped.yaw);
        float speed = (ped.state == PED_STATE_CROSS) ? PED_CROSS_SPEED : ped.speed;

        body->velocity.x = dir.x * speed;
        body->velocity.z = dir.z * speed;
        ped.yaw = yaw_from_forward(dir);

        // Shoved into a wall by a car, or wedged in a doorway: re-pick a route
        // rather than grinding against geometry forever.
        if (body->touched) ped.stuck += dt; else ped.stuck = 0.0f;
        if (ped.stuck > 1.5f) {
            city__ped_choose(ped, w, p.random);
            ped.stuck = 0.0f;
        }
    }

    // keep the crowd topped up around the player
    if (w.walk_cell_count) {
        for (int attempt = 0; attempt < 5 && p.count < MAX_PEDS; attempt++) {
            uint32_t pick = rng_u32(p.random) % (uint32_t)w.walk_cell_count;
            uint32_t cell = w.walk_cells[pick];
            int cx = (int)(cell % CITY_CELLS), cz = (int)(cell / CITY_CELLS);
            vec3 q = cell_centre(cx, cz);
            float dx = q.x - focus.x, dz = q.z - focus.z;
            float d = sqrtf(dx * dx + dz * dz);
            if (d < 30.0f || d > PED_SPAWN_RADIUS) continue;
            city_peds_spawn(p, w, cat, phys, cx, cz);
        }
    }
}

static void city_peds_draw(const city_peds& p, const city_catalog& cat, pix_renderer& renderer,
                           const frustum& view, vec3 eye) {
    for (size_t i = 0; i < MAX_PEDS; i++) {
        const city_ped& ped = p.people[i];
        if (!ped.active || ped.character >= p.bank_count) continue;

        const city_pose_bank& bank = p.banks[ped.character];
        const city_character& ch = cat.characters[ped.character];
        if (!bank.ready || !ch.ok) continue;

        float dx = ped.position.x - eye.x, dz = ped.position.z - eye.z;
        if (dx * dx + dz * dz > ANIMATED_DISTANCE * ANIMATED_DISTANCE) continue;
        if (!frustum_test_sphere(view, v3(ped.position.x, ped.position.y + 0.9f, ped.position.z), 1.4f))
            continue;

        // Standing at a kerb waiting for the lights is an idle, not a walk -
        // and which idle is picked per person, so a queue is a group of people
        // rather than one person rendered six times.
        const animation* pose;
        if (ped.state == PED_STATE_WAIT) {
            int slot = ped.bank_pose % PED_IDLE_POSES;
            pose = (ped.bank_pose < PED_IDLE_POSES) ? &bank.idle[slot] : &bank.stand[slot];
        } else {
            pose = &bank.walk[ped.pose % PED_WALK_POSES];
        }

        pix_render_instance inst = {};
        inst.mesh = ch.mesh;
        inst.material = ch.materials[ped.material % ch.material_count];
        inst.transform = mat4_trs_y(ped.position, ped.yaw + ch.yaw_offset,
                                     ch.scale * ped.height);
        push_animated_instance(renderer, inst, *pose);
    }
}
