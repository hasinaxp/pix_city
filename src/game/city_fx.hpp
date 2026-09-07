#pragma once
#include "../core/math.hpp"
#include "../core/random.hpp"
#include "../core/renderer.hpp"

// Particle effects.
//
// One flat pool of blobs, no emitters and no lifetimes owned by anything but
// the particle itself: everything in the city that wants to throw something
// into the air calls one of the city_fx_* functions below, and from that
// moment the burst belongs to this file. A car crash does not hold a smoke
// emitter, and a gun does not hold a muzzle flash - the shot happens, the
// sparks are handed over, and the shot is done.
//
// Each particle is described by where it is, where it is going, and two curves
// over its life: size and colour. That is deliberately the whole vocabulary.
// A spark is a small bright thing that shrinks; smoke is a big dim thing that
// grows; blood is a small dark thing that falls. Three effects that read as
// completely different things out of one integrator and six numbers.

#define MAX_FX_PARTICLES 3000

// Anything past this from the camera is not worth simulating, let alone
// drawing: a gunfight two blocks away is a sound, not a light show.
#define FX_CULL_DISTANCE 140.0f

struct city_particle {
    vec3  position;
    vec3  velocity;
    vec3  color;        // linear radiance at birth
    vec3  color_end;    // and at death; the two are lerped over the life
    float size;         // half width in metres, at birth
    float size_end;
    float alpha;        // coverage at birth, faded to nothing by the end
    float life;         // seconds left
    float life_max;
    float gravity;      // metres per second squared, positive is down
    float drag;         // per-second velocity damping
    bool  ground;       // settles on the floor instead of sinking through it
    float floor_y;      // where that floor is
};

struct city_fx {
    city_particle items[MAX_FX_PARTICLES];
    size_t        count;
    rng           random;
};

static void city_fx_init(city_fx& fx, uint32_t seed);
static void city_fx_update(city_fx& fx, float dt, vec3 focus);
static void city_fx_draw(const city_fx& fx, pix_renderer& renderer);

// ---- the effects themselves ----
// `dir` is always a unit vector pointing the way the event was going: out of a
// barrel, along a bullet, away from a wall.
static void city_fx_muzzle(city_fx& fx, vec3 at, vec3 dir, float scale);
static void city_fx_tracer(city_fx& fx, vec3 from, vec3 to);
static void city_fx_impact(city_fx& fx, vec3 at, vec3 dir);
static void city_fx_blood(city_fx& fx, vec3 at, vec3 dir);
static void city_fx_casing(city_fx& fx, vec3 at, vec3 right, float floor_y);
static void city_fx_dust(city_fx& fx, vec3 at, float radius, int count);
static void city_fx_sparks(city_fx& fx, vec3 at, vec3 dir, int count);
static void city_fx_smoke(city_fx& fx, vec3 at, float rise);

// ---------------- implementation ----------------

static void city_fx_init(city_fx& fx, uint32_t seed) {
    memset(&fx, 0, sizeof(fx));
    fx.random = rng_seed(seed ? seed : 1u);
}

// Claims a slot. Full means the newest burst is dropped rather than the oldest
// particle stolen: a stolen particle vanishes mid-flight in the middle of the
// screen, which is far more visible than one spark of forty never appearing.
static city_particle* city__fx_take(city_fx& fx) {
    if (fx.count >= MAX_FX_PARTICLES) return 0;
    city_particle* p = &fx.items[fx.count++];
    memset(p, 0, sizeof(*p));
    p->drag = 1.0f;
    return p;
}

// A unit vector inside a cone of `spread` radians about `axis`. Every burst in
// this file is some version of "throw a handful of things roughly that way".
static vec3 city__fx_cone(rng& r, vec3 axis, float spread) {
    vec3 up = fabsf(axis.y) > 0.9f ? v3(1.0f, 0.0f, 0.0f) : v3(0.0f, 1.0f, 0.0f);
    vec3 right = v3norm(v3cross(axis, up));
    vec3 top = v3cross(right, axis);
    float angle = rng_range(r, 0.0f, 6.2831853f);
    float radius = spread * sqrtf(rng_float(r));
    vec3 offset = v3add(v3scale(right, cosf(angle) * radius),
                        v3scale(top,   sinf(angle) * radius));
    return v3norm(v3add(axis, offset));
}

static void city_fx_update(city_fx& fx, float dt, vec3 focus) {
    for (size_t i = 0; i < fx.count; ) {
        city_particle& p = fx.items[i];
        p.life -= dt;

        float dx = p.position.x - focus.x, dz = p.position.z - focus.z;
        bool gone = p.life <= 0.0f
                 || dx * dx + dz * dz > FX_CULL_DISTANCE * FX_CULL_DISTANCE;
        if (gone) {
            // swap-with-last, so the pool stays dense and the loop stays linear
            fx.items[i] = fx.items[--fx.count];
            continue;
        }

        p.velocity.y -= p.gravity * dt;
        float keep = 1.0f - p.drag * dt;
        if (keep < 0.0f) keep = 0.0f;
        p.velocity = v3scale(p.velocity, keep);
        p.position = v3add(p.position, v3scale(p.velocity, dt));

        if (p.ground && p.position.y <= p.floor_y) {
            p.position.y = p.floor_y;
            // A little bounce, then it lies there for the rest of its life -
            // debris that stops dead on contact reads as sticky.
            p.velocity = v3(p.velocity.x * 0.35f, fabsf(p.velocity.y) * 0.28f,
                            p.velocity.z * 0.35f);
            if (p.velocity.y < 0.4f) { p.velocity = v3(0.0f, 0.0f, 0.0f); p.gravity = 0.0f; }
        }
        i++;
    }
}

static void city_fx_draw(const city_fx& fx, pix_renderer& renderer) {
    for (size_t i = 0; i < fx.count; i++) {
        const city_particle& p = fx.items[i];
        float u = p.life_max > 0.0f ? 1.0f - p.life / p.life_max : 1.0f;
        u = clampf(u, 0.0f, 1.0f);
        // Coverage falls off as the square of what is left, not linearly: a
        // linear fade spends half a spark's life at more than half opacity,
        // which is what makes a burst look like it is switched off rather
        // than dying away.
        float fade = (1.0f - u) * (1.0f - u);
        push_particle(renderer, p.position, lerpf(p.size, p.size_end, u),
                      v3lerp(p.color, p.color_end, u), p.alpha * fade);
    }
}

// ---- muzzle flash ----
//
// Written well above 1 on purpose. The scene target is HDR and the bloom pass
// threshold sits just over diffuse white, so a flash at this radiance is the
// one thing in the frame that genuinely glares - which is what makes a shot
// fired at night light up the street rather than draw a yellow dot on it.
static void city_fx_muzzle(city_fx& fx, vec3 at, vec3 dir, float scale) {
    city_particle* core = city__fx_take(fx);
    if (core) {
        core->position = at;
        core->velocity = v3scale(dir, 1.5f);
        core->color = v3(7.0f, 4.2f, 1.5f);
        core->color_end = v3(2.2f, 0.8f, 0.15f);
        core->size = 0.13f * scale;
        core->size_end = 0.26f * scale;
        core->alpha = 1.0f;
        core->life = core->life_max = 0.055f;
        core->drag = 6.0f;
    }
    // and a few embers thrown out of the barrel with it
    for (int i = 0; i < 5; i++) {
        city_particle* s = city__fx_take(fx);
        if (!s) break;
        s->position = at;
        s->velocity = v3scale(city__fx_cone(fx.random, dir, 0.45f),
                              rng_range(fx.random, 3.0f, 9.0f));
        s->color = v3(4.5f, 2.4f, 0.6f);
        s->color_end = v3(1.2f, 0.35f, 0.05f);
        s->size = 0.035f * scale;
        s->size_end = 0.008f;
        s->alpha = 1.0f;
        s->life = s->life_max = rng_range(fx.random, 0.06f, 0.16f);
        s->gravity = 6.0f;
        s->drag = 3.0f;
    }
    city_fx_smoke(fx, v3add(at, v3scale(dir, 0.15f)), 0.5f);
}

// The line the round took. A few blobs strung along it for a couple of frames,
// which at a bullet's speed is exactly as much as the eye ever sees of one.
static void city_fx_tracer(city_fx& fx, vec3 from, vec3 to) {
    vec3 span = v3sub(to, from);
    float length = v3len(span);
    if (length < 0.2f) return;
    int steps = (int)(length / 2.4f);
    if (steps > 14) steps = 14;
    if (steps < 2) steps = 2;
    for (int i = 0; i < steps; i++) {
        city_particle* p = city__fx_take(fx);
        if (!p) return;
        float t = (float)(i + 1) / (float)(steps + 1);
        p->position = v3add(from, v3scale(span, t));
        p->color = v3(3.4f, 2.5f, 1.1f);
        p->color_end = v3(0.9f, 0.6f, 0.22f);
        p->size = 0.035f;
        p->size_end = 0.012f;
        p->alpha = 0.85f;
        // The far end lingers a fraction longer than the near end, so the
        // streak reads as travelling rather than as a static dotted line.
        p->life = p->life_max = 0.05f + t * 0.03f;
        p->drag = 0.0f;
    }
}

// A bullet meeting masonry: chips thrown back along the surface and a puff of
// pulverised whatever-it-was hanging where it hit.
static void city_fx_impact(city_fx& fx, vec3 at, vec3 dir) {
    vec3 back = v3scale(dir, -1.0f);
    city_fx_sparks(fx, at, back, 7);
    for (int i = 0; i < 3; i++) {
        city_particle* p = city__fx_take(fx);
        if (!p) break;
        p->position = at;
        p->velocity = v3scale(city__fx_cone(fx.random, back, 0.9f),
                              rng_range(fx.random, 0.4f, 1.6f));
        p->color = v3(0.34f, 0.32f, 0.30f);
        p->color_end = v3(0.22f, 0.21f, 0.20f);
        p->size = 0.07f;
        p->size_end = 0.30f;
        p->alpha = 0.55f;
        p->life = p->life_max = rng_range(fx.random, 0.4f, 0.8f);
        p->drag = 2.6f;
        p->gravity = -0.6f;          // dust rises as it thins
    }
}

static void city_fx_blood(city_fx& fx, vec3 at, vec3 dir) {
    for (int i = 0; i < 12; i++) {
        city_particle* p = city__fx_take(fx);
        if (!p) break;
        p->position = at;
        p->velocity = v3scale(city__fx_cone(fx.random, dir, 0.7f),
                              rng_range(fx.random, 1.5f, 6.0f));
        p->color = v3(0.42f, 0.02f, 0.02f);
        p->color_end = v3(0.14f, 0.01f, 0.01f);
        p->size = rng_range(fx.random, 0.03f, 0.07f);
        p->size_end = 0.015f;
        p->alpha = 0.9f;
        p->life = p->life_max = rng_range(fx.random, 0.35f, 0.9f);
        p->gravity = 16.0f;
        p->drag = 0.9f;
    }
    // and a short-lived mist around the wound, which is what actually reads
    // as a hit at any distance the individual droplets do not
    for (int i = 0; i < 3; i++) {
        city_particle* p = city__fx_take(fx);
        if (!p) break;
        p->position = at;
        p->velocity = v3scale(city__fx_cone(fx.random, dir, 1.1f),
                              rng_range(fx.random, 0.3f, 1.2f));
        p->color = v3(0.30f, 0.02f, 0.03f);
        p->color_end = v3(0.16f, 0.02f, 0.02f);
        p->size = 0.10f;
        p->size_end = 0.26f;
        p->alpha = 0.42f;
        p->life = p->life_max = rng_range(fx.random, 0.25f, 0.5f);
        p->drag = 3.5f;
    }
}

// The brass. One per shot, ejected sideways, and it lies on the pavement for a
// few seconds afterwards - the only thing this file makes that outlives the
// event that made it, and the only reason a firefight leaves a trace.
static void city_fx_casing(city_fx& fx, vec3 at, vec3 right, float floor_y) {
    city_particle* p = city__fx_take(fx);
    if (!p) return;
    p->position = at;
    p->velocity = v3add(v3scale(right, rng_range(fx.random, 1.6f, 3.0f)),
                        v3(0.0f, rng_range(fx.random, 1.4f, 2.4f), 0.0f));
    p->color = v3(0.75f, 0.55f, 0.18f);
    p->color_end = v3(0.45f, 0.33f, 0.10f);
    p->size = 0.022f;
    p->size_end = 0.022f;
    p->alpha = 1.0f;
    p->life = p->life_max = 6.0f;
    p->gravity = 20.0f;
    p->drag = 0.2f;
    p->ground = true;
    p->floor_y = floor_y;
}

static void city_fx_dust(city_fx& fx, vec3 at, float radius, int count) {
    for (int i = 0; i < count; i++) {
        city_particle* p = city__fx_take(fx);
        if (!p) break;
        float angle = rng_range(fx.random, 0.0f, 6.2831853f);
        float r = radius * sqrtf(rng_float(fx.random));
        p->position = v3(at.x + cosf(angle) * r, at.y + rng_range(fx.random, 0.0f, 0.3f),
                         at.z + sinf(angle) * r);
        p->velocity = v3(cosf(angle) * rng_range(fx.random, 0.5f, 2.0f),
                         rng_range(fx.random, 0.3f, 1.1f),
                         sinf(angle) * rng_range(fx.random, 0.5f, 2.0f));
        p->color = v3(0.40f, 0.37f, 0.33f);
        p->color_end = v3(0.26f, 0.25f, 0.24f);
        p->size = rng_range(fx.random, 0.15f, 0.35f);
        p->size_end = rng_range(fx.random, 0.7f, 1.4f);
        p->alpha = 0.34f;
        p->life = p->life_max = rng_range(fx.random, 0.7f, 1.6f);
        p->drag = 1.8f;
        p->gravity = -0.35f;
    }
}

static void city_fx_sparks(city_fx& fx, vec3 at, vec3 dir, int count) {
    for (int i = 0; i < count; i++) {
        city_particle* p = city__fx_take(fx);
        if (!p) break;
        p->position = at;
        p->velocity = v3scale(city__fx_cone(fx.random, dir, 1.0f),
                              rng_range(fx.random, 2.5f, 9.0f));
        p->color = v3(5.0f, 3.0f, 0.8f);
        p->color_end = v3(1.4f, 0.4f, 0.06f);
        p->size = 0.025f;
        p->size_end = 0.006f;
        p->alpha = 1.0f;
        p->life = p->life_max = rng_range(fx.random, 0.12f, 0.34f);
        p->gravity = 14.0f;
        p->drag = 1.4f;
    }
}

static void city_fx_smoke(city_fx& fx, vec3 at, float rise) {
    for (int i = 0; i < 2; i++) {
        city_particle* p = city__fx_take(fx);
        if (!p) break;
        p->position = at;
        p->velocity = v3(rng_range(fx.random, -0.3f, 0.3f), rise,
                         rng_range(fx.random, -0.3f, 0.3f));
        p->color = v3(0.30f, 0.30f, 0.31f);
        p->color_end = v3(0.20f, 0.20f, 0.21f);
        p->size = rng_range(fx.random, 0.06f, 0.12f);
        p->size_end = rng_range(fx.random, 0.4f, 0.7f);
        p->alpha = 0.30f;
        p->life = p->life_max = rng_range(fx.random, 0.5f, 1.1f);
        p->drag = 1.6f;
        p->gravity = -0.5f;
    }
}
