#pragma once
#include <math.h>
#include "dtype.hpp"
#include "math.hpp"

// Collision shapes and the tests between them. Pure geometry: nothing here
// knows about bodies, mass, velocity or time - it answers "do these two shapes
// overlap, and by how much, in which direction". physics.hpp is the layer that
// turns those answers into motion, and anything else that needs a shape query
// (placement checks, camera probes, triggers, editor tools) can use this header
// without pulling the simulation in with it.
//
// The world this serves is flat, so the useful tests are 2D in the XZ plane
// with a separate vertical band check. Both are provided.
//
// Every test that reports an overlap returns the *minimum translation*: a unit
// normal pointing from the first shape toward the second, and the depth to push
// them apart along it.

struct aabb {
    vec3 min;
    vec3 max;
};

// axis aligned rectangle in XZ
struct rect2 {
    vec2 min;
    vec2 max;
};

struct circle2 {
    vec2  centre;
    float radius;
};

// rotated rectangle in XZ; `yaw` matches mat4_rotate_y, so local +X points
// along (cos yaw, -sin yaw) in world XZ
struct obb2 {
    vec2  centre;
    vec2  half;
    float yaw;
};

struct contact2 {
    vec2  normal;   // unit, from shape a toward shape b
    float depth;    // overlap along the normal; always > 0 on a hit
};

// ---- constructors ----

static aabb   aabb_make(vec3 min, vec3 max);
static aabb   aabb_from_centre(vec3 centre, vec3 half_extents);
static vec3   aabb_centre(const aabb& b);
static vec3   aabb_half(const aabb& b);
static float  aabb_height(const aabb& b);
static rect2  aabb_footprint(const aabb& b);
// the axis aligned bound of a rotated rectangle extruded to `height`
static aabb   aabb_from_obb(const obb2& box, float base_y, float height);

static bool   aabb_overlap(const aabb& a, const aabb& b);
static bool   aabb_contains_xz(const aabb& b, vec2 p);
// do two vertical spans [a0, a0+ah) and [b0, b0+bh) share any height at all
static bool   span_overlap(float a0, float ah, float b0, float bh);

// ---- 2D queries ----

static vec2 closest_point_rect(const rect2& r, vec2 p);
static vec2 closest_point_obb(const obb2& b, vec2 p);

static bool collide_circle_circle(const circle2& a, const circle2& b, contact2* out);
static bool collide_circle_rect(const circle2& c, const rect2& r, contact2* out);
static bool collide_circle_obb(const circle2& c, const obb2& b, contact2* out);
static bool collide_obb_obb(const obb2& a, const obb2& b, contact2* out);
static bool collide_obb_rect(const obb2& a, const rect2& r, contact2* out);

// ---- rays ----

// slab test; `inv_dir` is the componentwise reciprocal of a normalised
// direction, precomputed because a raycast tests the same ray against many
// boxes. *out_t is the near hit distance, clamped to 0 when the ray starts
// inside.
static bool ray_vs_aabb(vec3 origin, vec3 inv_dir, const aabb& box, float max_distance,
                        float* out_t);

// ---------------- implementation ----------------

static aabb aabb_make(vec3 min, vec3 max) { aabb b; b.min = min; b.max = max; return b; }

static aabb aabb_from_centre(vec3 centre, vec3 half_extents) {
    return aabb_make(v3sub(centre, half_extents), v3add(centre, half_extents));
}

static vec3 aabb_centre(const aabb& b) { return v3scale(v3add(b.min, b.max), 0.5f); }
static vec3 aabb_half(const aabb& b)   { return v3scale(v3sub(b.max, b.min), 0.5f); }
static float aabb_height(const aabb& b) { return b.max.y - b.min.y; }

static rect2 aabb_footprint(const aabb& b) {
    rect2 r;
    r.min = v2(b.min.x, b.min.z);
    r.max = v2(b.max.x, b.max.z);
    return r;
}

static aabb aabb_from_obb(const obb2& box, float base_y, float height) {
    // project both local axes onto world X and Z
    float c = fabsf(cosf(box.yaw)), s = fabsf(sinf(box.yaw));
    float ex = box.half.x * c + box.half.y * s;
    float ez = box.half.x * s + box.half.y * c;
    return aabb_make(v3(box.centre.x - ex, base_y, box.centre.y - ez),
                     v3(box.centre.x + ex, base_y + height, box.centre.y + ez));
}

static bool aabb_overlap(const aabb& a, const aabb& b) {
    return a.min.x <= b.max.x && a.max.x >= b.min.x
        && a.min.y <= b.max.y && a.max.y >= b.min.y
        && a.min.z <= b.max.z && a.max.z >= b.min.z;
}

static bool aabb_contains_xz(const aabb& b, vec2 p) {
    return p.x >= b.min.x && p.x <= b.max.x && p.y >= b.min.z && p.y <= b.max.z;
}

static bool span_overlap(float a0, float ah, float b0, float bh) {
    return a0 < b0 + bh && b0 < a0 + ah;
}

// ---- 2D ----

static vec2 closest_point_rect(const rect2& r, vec2 p) {
    return v2(p.x < r.min.x ? r.min.x : (p.x > r.max.x ? r.max.x : p.x),
              p.y < r.min.y ? r.min.y : (p.y > r.max.y ? r.max.y : p.y));
}

// rotates a world offset into a box's local frame. Local +X is (cos, -sin), so
// the inverse rotation is this pair of dot products.
static vec2 obb__to_local(const obb2& b, vec2 world) {
    float cs = cosf(b.yaw), sn = sinf(b.yaw);
    float dx = world.x - b.centre.x, dz = world.y - b.centre.y;
    return v2(dx * cs - dz * sn, dx * sn + dz * cs);
}

static vec2 obb__to_world_dir(const obb2& b, vec2 local) {
    float cs = cosf(b.yaw), sn = sinf(b.yaw);
    return v2(local.x * cs + local.y * sn, -local.x * sn + local.y * cs);
}

static vec2 closest_point_obb(const obb2& b, vec2 p) {
    rect2 local = { v2(-b.half.x, -b.half.y), v2(b.half.x, b.half.y) };
    vec2 c = closest_point_rect(local, obb__to_local(b, p));
    return v2add(b.centre, obb__to_world_dir(b, c));
}

static bool collide_circle_circle(const circle2& a, const circle2& b, contact2* out) {
    vec2 d = v2sub(b.centre, a.centre);
    float rr = a.radius + b.radius;
    float d2 = d.x * d.x + d.y * d.y;
    if (d2 >= rr * rr) return false;
    float len = sqrtf(d2);
    // exactly coincident centres have no meaningful normal; any axis will do
    out->normal = len > 1e-6f ? v2scale(d, 1.0f / len) : v2(1.0f, 0.0f);
    out->depth = rr - len;
    return true;
}

static bool collide_circle_rect(const circle2& c, const rect2& r, contact2* out) {
    vec2 closest = closest_point_rect(r, c.centre);
    vec2 d = v2sub(c.centre, closest);
    float d2 = d.x * d.x + d.y * d.y;
    if (d2 > c.radius * c.radius) return false;

    if (d2 > 1e-8f) {
        // the usual case: the centre is outside, push along the gap. Normal
        // points rect -> circle here, and is flipped below to match the
        // documented a -> b convention (a is the circle).
        float len = sqrtf(d2);
        out->normal = v2scale(d, -1.0f / len);
        out->depth = c.radius - len;
        return true;
    }

    // the centre is inside the rectangle: leave by the nearest face
    float to_min_x = c.centre.x - r.min.x, to_max_x = r.max.x - c.centre.x;
    float to_min_y = c.centre.y - r.min.y, to_max_y = r.max.y - c.centre.y;
    float best = to_min_x;
    vec2 n = v2(1.0f, 0.0f);                   // push circle toward -x => normal circle->rect is +x
    if (to_max_x < best) { best = to_max_x; n = v2(-1.0f, 0.0f); }
    if (to_min_y < best) { best = to_min_y; n = v2(0.0f, 1.0f); }
    if (to_max_y < best) { best = to_max_y; n = v2(0.0f, -1.0f); }
    out->normal = n;
    out->depth = best + c.radius;
    return true;
}

static bool collide_circle_obb(const circle2& c, const obb2& b, contact2* out) {
    circle2 local = { obb__to_local(b, c.centre), c.radius };
    rect2 lr = { v2(-b.half.x, -b.half.y), v2(b.half.x, b.half.y) };
    contact2 hit;
    if (!collide_circle_rect(local, lr, &hit)) return false;
    out->normal = obb__to_world_dir(b, hit.normal);
    out->depth = hit.depth;
    return true;
}

// Separating axis test. Two rectangles cannot overlap if any of their four edge
// normals separates them, so testing all four and keeping the smallest overlap
// gives both the answer and the cheapest way out.
static bool collide_obb_obb(const obb2& a, const obb2& b, contact2* out) {
    vec2 axis[4];
    axis[0] = v2(cosf(a.yaw), -sinf(a.yaw));   // a's local x
    axis[1] = v2(sinf(a.yaw),  cosf(a.yaw));   // a's local z
    axis[2] = v2(cosf(b.yaw), -sinf(b.yaw));
    axis[3] = v2(sinf(b.yaw),  cosf(b.yaw));

    vec2 delta = v2sub(b.centre, a.centre);
    float best = 1e30f;
    vec2  best_axis = v2(1.0f, 0.0f);

    for (int i = 0; i < 4; i++) {
        vec2 ax = axis[i];
        float ra = a.half.x * fabsf(v2dot(ax, axis[0])) + a.half.y * fabsf(v2dot(ax, axis[1]));
        float rb = b.half.x * fabsf(v2dot(ax, axis[2])) + b.half.y * fabsf(v2dot(ax, axis[3]));
        float dist = v2dot(delta, ax);
        float overlap = ra + rb - fabsf(dist);
        if (overlap <= 0.0f) return false;
        if (overlap < best) {
            best = overlap;
            best_axis = dist < 0.0f ? v2scale(ax, -1.0f) : ax;   // always a -> b
        }
    }
    out->normal = best_axis;
    out->depth = best;
    return true;
}

static bool collide_obb_rect(const obb2& a, const rect2& r, contact2* out) {
    obb2 b;
    b.centre = v2scale(v2add(r.min, r.max), 0.5f);
    b.half   = v2scale(v2sub(r.max, r.min), 0.5f);
    b.yaw    = 0.0f;
    return collide_obb_obb(a, b, out);
}

// ---- rays ----

static bool ray_vs_aabb(vec3 origin, vec3 inv_dir, const aabb& box, float max_distance,
                        float* out_t) {
    float t0 = 0.0f, t1 = max_distance;
    const float* bmin = &box.min.x;
    const float* bmax = &box.max.x;
    const float* o    = &origin.x;
    const float* inv  = &inv_dir.x;
    for (int a = 0; a < 3; a++) {
        float ta = (bmin[a] - o[a]) * inv[a];
        float tb = (bmax[a] - o[a]) * inv[a];
        if (ta > tb) { float tmp = ta; ta = tb; tb = tmp; }
        if (ta > t0) t0 = ta;
        if (tb < t1) t1 = tb;
        if (t0 > t1) return false;
    }
    if (out_t) *out_t = t0;
    return true;
}
