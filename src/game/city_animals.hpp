#pragma once
#include "../core/random.hpp"
#include "../core/physics.hpp"
#include "../core/animation.hpp"
#include "city_map.hpp"

// Dogs.
//
// A city with nothing alive in it but commuters is a diorama. These are the
// cheapest possible thing that fixes that: a handful of strays and off-lead
// pets trotting the pavements and the parks, wandering, stopping to sniff at
// something, and breaking into a run for no reason the way dogs do.
//
// Deliberately not pedestrians-with-a-different-mesh. A dog never uses a
// crossing and never waits at a kerb - it simply does not leave the footpath,
// which removes the whole traffic-light half of the pedestrian AI and, more
// usefully, means a dog is never standing in the road. What is shared with
// city_peds.hpp is the part worth sharing: the pose bank, so a pack of them
// costs a fixed handful of skeleton evaluations rather than one each.
//
// Everything about the rig comes through city_character (see city__load_actor)
// so nothing here knows a dog is not a person - which is the point: the next
// animal is a table entry in city_load_catalog, not another file like this one.

#define MAX_DOGS          14
#define DOG_WALK_POSES     4
#define DOG_IDLE_POSES     2
#define DOG_RUN_POSES      3

#define DOG_WALK_SPEED   1.60f
#define DOG_RUN_SPEED    5.20f
#define DOG_SPAWN_RADIUS  90.0f    // well inside the pedestrian radius; they are small

#define DOG_STATE_WALK  0
#define DOG_STATE_SIT   1          // stopped, sniffing at something
#define DOG_STATE_RUN   2

struct city_dog {
    bool    active;
    vec3    position;
    float   yaw;
    float   speed;
    int     node_x, node_z;
    int     next_x, next_z;
    idx     body;
    uint8_t kind;          // index into city_catalog::animals
    uint8_t material;
    uint8_t pose;
    uint8_t state;
    float   state_time;    // seconds left in the current state
    float   height;        // per-dog size variation
    float   lateral;       // offset from the cell centreline
    float   stuck;
};

struct city_dog_bank {
    animator  source;
    animation walk[DOG_WALK_POSES];
    animation idle[DOG_IDLE_POSES];
    animation run[DOG_RUN_POSES];
    float     walk_duration, idle_duration, run_duration;
    idx       walk_clip, idle_clip, run_clip;
    bool      ready;
};

struct city_dogs {
    city_dog      pack[MAX_DOGS];
    size_t        count;
    city_dog_bank banks[CITY_MAX_ANIMALS];
    size_t        bank_count;
    rng           random;
};

static void city_dogs_init(city_dogs& d, pix_data_loader& loader, const city_catalog& cat,
                           uint32_t seed);
static void city_dogs_update(city_dogs& d, const city_world& w, const city_catalog& cat,
                             phys_world& phys, vec3 focus, float time, float dt);
static void city_dogs_draw(const city_dogs& d, const city_catalog& cat, pix_renderer& renderer,
                           const frustum& view, vec3 eye);

// ---------------- implementation ----------------

static void city_dogs_init(city_dogs& d, pix_data_loader& loader, const city_catalog& cat,
                           uint32_t seed) {
    memset(&d, 0, sizeof(d));
    d.random = rng_seed(seed);

    for (size_t a = 0; a < cat.animal_count; a++) {
        const city_character& an = cat.animals[a];
        city_dog_bank& bank = d.banks[d.bank_count++];
        memset(&bank, 0, sizeof(bank));
        if (!an.ok || !city_build_character_animator(bank.source, loader, an)) continue;

        bank.walk_clip = an.clips[CLIP_WALK];
        bank.idle_clip = an.clips[CLIP_IDLE];
        bank.run_clip  = an.clips[CLIP_RUN] != (idx)-1 ? an.clips[CLIP_RUN] : bank.walk_clip;
        if (bank.walk_clip == (idx)-1 || bank.idle_clip == (idx)-1) continue;

        bank.walk_duration = animator_duration(bank.source, bank.walk_clip);
        bank.idle_duration = animator_duration(bank.source, bank.idle_clip);
        bank.run_duration  = animator_duration(bank.source, bank.run_clip);
        bank.ready = true;
    }
}

static void city__dog_bank_update(city_dog_bank& bank, float time) {
    if (!bank.ready) return;
    float wd = bank.walk_duration > 0.01f ? bank.walk_duration : 1.0f;
    for (int i = 0; i < DOG_WALK_POSES; i++)
        animator_sample(bank.source, bank.walk_clip,
                        time + wd * ((float)i / (float)DOG_WALK_POSES), &bank.walk[i]);

    float id = bank.idle_duration > 0.01f ? bank.idle_duration : 1.0f;
    for (int i = 0; i < DOG_IDLE_POSES; i++)
        animator_sample(bank.source, bank.idle_clip,
                        time + id * ((float)i / (float)DOG_IDLE_POSES), &bank.idle[i]);

    float rd = bank.run_duration > 0.01f ? bank.run_duration : 1.0f;
    for (int i = 0; i < DOG_RUN_POSES; i++)
        animator_sample(bank.source, bank.run_clip,
                        time + rd * ((float)i / (float)DOG_RUN_POSES), &bank.run[i]);
}

// Where a dog is heading inside its target cell. Same lateral offset trick the
// crowd uses, for the same reason: without it a footpath becomes a queue.
static vec3 city__dog_target(const city_world& w, const city_dog& dog) {
    int dx = dog.next_x - dog.node_x, dz = dog.next_z - dog.node_z;
    vec3 travel = v3norm(v3((float)dx, 0.0f, (float)dz));
    return v3add(city_stand_point(w, dog.next_x, dog.next_z),
                 v3scale(dir_right(travel), dog.lateral));
}

// A dog picks a walkable neighbour, preferring to keep going. It never steps
// into a road cell at all - no crossings, no kerb waiting, and nothing for the
// traffic to run over.
static void city__dog_choose(city_dog& dog, const city_world& w, rng& r) {
    int back = -1;
    for (int i = 0; i < 4; i++)
        if (dog.node_x + DIR_DX[i] == dog.next_x && dog.node_z + DIR_DZ[i] == dog.next_z)
            back = (i + 2) & 3;

    dog.node_x = dog.next_x;
    dog.node_z = dog.next_z;

    int options[4], weights[4], count = 0, total = 0;
    for (int i = 0; i < 4; i++) {
        int nx = dog.node_x + DIR_DX[i], nz = dog.node_z + DIR_DZ[i];
        if (!city_is_walkable(w, nx, nz)) continue;
        int wgt = (i == back) ? 1 : 5;
        options[count] = i; weights[count] = wgt; total += wgt; count++;
    }
    if (!count) {                                    // boxed in: turn around
        int d = back < 0 ? 0 : back;
        dog.next_x = dog.node_x - DIR_DX[d];
        dog.next_z = dog.node_z - DIR_DZ[d];
        return;
    }

    int roll = rng_int(r, 0, total - 1), pick = options[0];
    for (int i = 0; i < count; i++) { roll -= weights[i]; if (roll < 0) { pick = options[i]; break; } }
    dog.next_x = dog.node_x + DIR_DX[pick];
    dog.next_z = dog.node_z + DIR_DZ[pick];
}

static int city_dogs_spawn(city_dogs& d, const city_world& w, const city_catalog& cat,
                           phys_world& phys, int cx, int cz) {
    if (!cat.animal_count || !city_is_walkable(w, cx, cz)) return -1;

    int slot = -1;
    for (int i = 0; i < MAX_DOGS; i++) if (!d.pack[i].active) { slot = i; break; }
    if (slot < 0) return -1;

    city_dog& dog = d.pack[slot];
    memset(&dog, 0, sizeof(dog));
    dog.kind = (uint8_t)(rng_u32(d.random) % cat.animal_count);
    if (!d.banks[dog.kind].ready) return -1;

    dog.node_x = dog.next_x = cx;
    dog.node_z = dog.next_z = cz;
    dog.position = city_stand_point(w, cx, cz);
    float spread = (SIDEWALK_WIDTH - KERB_STRIP) * 0.5f - 0.3f;
    dog.lateral = rng_range(d.random, -spread, spread);
    dog.material = (uint8_t)(rng_u32(d.random)
                             % (cat.animals[dog.kind].material_count | 1u));
    dog.pose = (uint8_t)(rng_u32(d.random) % DOG_WALK_POSES);
    dog.height = rng_range(d.random, 0.88f, 1.15f);
    dog.speed = DOG_WALK_SPEED * dog.height * rng_range(d.random, 0.85f, 1.15f);
    dog.state = DOG_STATE_WALK;
    dog.state_time = rng_range(d.random, 3.0f, 9.0f);
    dog.active = true;
    city__dog_choose(dog, w, d.random);

    phys_body b = {};
    b.shape = PHYS_CYLINDER;
    b.position = dog.position;
    b.radius = 0.26f;
    b.height = 0.6f * dog.height;
    b.inv_mass = 1.0f / 14.0f;
    b.restitution = 0.0f;
    b.drag = 6.0f;
    b.group = PHYS_LAYER_PED;
    b.collides = PHYS_LAYER_ALL;
    b.gravity = true;
    dog.body = phys_add_body(phys, b);
    if (dog.body == (idx)-1) { dog.active = false; return -1; }

    d.count++;
    return slot;
}

static void city_dogs_update(city_dogs& d, const city_world& w, const city_catalog& cat,
                             phys_world& phys, vec3 focus, float time, float dt) {
    for (size_t b = 0; b < d.bank_count; b++) city__dog_bank_update(d.banks[b], time);

    for (int i = 0; i < MAX_DOGS; i++) {
        city_dog& dog = d.pack[i];
        if (!dog.active) continue;

        phys_body* body = phys_get_body(phys, dog.body);
        if (!body) { dog.active = false; if (d.count) d.count--; continue; }
        dog.position = body->position;

        float dx = dog.position.x - focus.x, dz = dog.position.z - focus.z;
        if (dx * dx + dz * dz > (DOG_SPAWN_RADIUS * 1.4f) * (DOG_SPAWN_RADIUS * 1.4f)) {
            phys_remove_body(phys, dog.body);
            dog.active = false;
            if (d.count) d.count--;
            continue;
        }

        // Wander, stop, bolt: one timer picking between three states is the
        // whole of it, and it is enough, because what reads as an animal is
        // the *changes* of pace rather than any one of them.
        dog.state_time -= dt;
        if (dog.state_time <= 0.0f) {
            float roll = rng_float(d.random);
            if (roll < 0.30f)      { dog.state = DOG_STATE_SIT; dog.state_time = rng_range(d.random, 1.5f, 5.0f); }
            else if (roll < 0.45f) { dog.state = DOG_STATE_RUN; dog.state_time = rng_range(d.random, 1.5f, 4.0f); }
            else                   { dog.state = DOG_STATE_WALK; dog.state_time = rng_range(d.random, 4.0f, 12.0f); }
        }

        if (dog.state == DOG_STATE_SIT) {
            body->velocity.x = 0.0f;
            body->velocity.z = 0.0f;
            continue;
        }

        vec3 target = city__dog_target(w, dog);
        vec3 to = v3sub(target, dog.position);
        to.y = 0.0f;
        float distance = v3len(to);
        if (distance < 0.8f) {
            city__dog_choose(dog, w, d.random);
            target = city__dog_target(w, dog);
            to = v3sub(target, dog.position);
            to.y = 0.0f;
            distance = v3len(to);
        }

        vec3 dir = distance > 1e-3f ? v3scale(to, 1.0f / distance) : forward_from_yaw(dog.yaw);
        float speed = (dog.state == DOG_STATE_RUN) ? DOG_RUN_SPEED * dog.height : dog.speed;
        body->velocity.x = dir.x * speed;
        body->velocity.z = dir.z * speed;
        dog.yaw = yaw_from_forward(dir);

        if (body->touched) dog.stuck += dt; else dog.stuck = 0.0f;
        if (dog.stuck > 1.5f) { city__dog_choose(dog, w, d.random); dog.stuck = 0.0f; }
    }

    // Keep a few around the player. Far rarer than pedestrians on purpose:
    // one dog every block or so reads as a city with dogs in it, and one on
    // every corner reads as a kennel.
    if (w.walk_cell_count && cat.animal_count) {
        for (int attempt = 0; attempt < 2 && d.count < MAX_DOGS; attempt++) {
            uint32_t cell = w.walk_cells[rng_u32(d.random) % (uint32_t)w.walk_cell_count];
            int cx = (int)(cell % CITY_CELLS), cz = (int)(cell / CITY_CELLS);
            vec3 q = cell_centre(cx, cz);
            float ddx = q.x - focus.x, ddz = q.z - focus.z;
            float dist = sqrtf(ddx * ddx + ddz * ddz);
            if (dist < 25.0f || dist > DOG_SPAWN_RADIUS) continue;
            city_dogs_spawn(d, w, cat, phys, cx, cz);
        }
    }
}

static void city_dogs_draw(const city_dogs& d, const city_catalog& cat, pix_renderer& renderer,
                           const frustum& view, vec3 eye) {
    for (int i = 0; i < MAX_DOGS; i++) {
        const city_dog& dog = d.pack[i];
        if (!dog.active || dog.kind >= d.bank_count) continue;
        const city_dog_bank& bank = d.banks[dog.kind];
        const city_character& an = cat.animals[dog.kind];
        if (!bank.ready || !an.ok) continue;

        float dx = dog.position.x - eye.x, dz = dog.position.z - eye.z;
        if (dx * dx + dz * dz > ANIMATED_DISTANCE * ANIMATED_DISTANCE) continue;
        if (!frustum_test_sphere(view, v3(dog.position.x, dog.position.y + 0.4f, dog.position.z), 1.0f))
            continue;

        const animation* pose;
        if (dog.state == DOG_STATE_SIT)      pose = &bank.idle[dog.pose % DOG_IDLE_POSES];
        else if (dog.state == DOG_STATE_RUN) pose = &bank.run[dog.pose % DOG_RUN_POSES];
        else                                 pose = &bank.walk[dog.pose % DOG_WALK_POSES];

        pix_render_instance inst = {};
        inst.mesh = an.mesh;
        inst.material = an.materials[dog.material % an.material_count];
        inst.transform = mat4_trs_y(dog.position, dog.yaw + an.yaw_offset,
                                    an.scale * dog.height);
        push_animated_instance(renderer, inst, *pose);
    }
}
