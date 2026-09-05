#pragma once
#include <stdio.h>
#include <string.h>
#include "../core/renderer.hpp"
#include "../core/animation.hpp"
#include "../loader/data_loader.hpp"
#include "city_config.hpp"

// The art catalogue. Everything the city can place is loaded once into a flat
// table of `city_model`s; the generator then works entirely in terms of *sets*
// - "a downtown tower", "a park tree", "a kerbside bin" - and never names a
// file. Adding a model to a kit is a one line change in the tables below and
// it starts appearing in the world.
//
// Models are loaded in set order so a set is just a contiguous run, which makes
// "pick a random one of these" a single modulo.

#define CITY_MAX_MODELS 400

// asset roots
#define KAY_DIR    "assets/city/kaykit/"
#define SUB_DIR    "assets/city/suburban/"
#define VEH_DIR    "assets/city/vehicles/"
#define NAT_DIR    "assets/city/nature/"
#define RAIL_DIR   "assets/city/rail/"
#define CHAR_DIR   "assets/characters/"
#define DOG_MODEL  "assets/glTF/Fox/Fox.glb"

// ---- the cast ----
//
// Each of these rigs is authored as several meshes (body, head, legs, feet)
// over one armature, with its colours in per-material baseColorFactors rather
// than a texture. The glTF loader merges the meshes and bakes those colours
// into a palette image, so a whole character is one skinned draw.
#define CITY_MAX_CHARACTERS 4
#define CITY_MAX_CHAR_TINTS 5

// The clips the game asks for by role. A rig that does not ship one falls back
// to the next best thing it does have (see CLIP_NAMES), so a character with a
// smaller clip set still animates rather than freezing in its rest pose.
enum city_clip {
    CLIP_IDLE,      // standing, at rest
    CLIP_WALK,
    CLIP_RUN,
    CLIP_JUMP,
    CLIP_WAVE,      // greeting a passer-by
    CLIP_TALK,      // a second standing pose, so a crowd is not one silhouette
    CLIP_SIT,
    CLIP_COUNT
};

struct city_character {
    idx    model;                          // index into loader.model_files
    idx    mesh;                           // skinned mesh in the renderer
    idx    materials[CITY_MAX_CHAR_TINTS]; // palette + a tint per variant
    size_t material_count;
    float  scale;                          // rig units -> PLAYER_HEIGHT metres
    float  yaw_offset;                     // rig's facing vs our -Z convention
    idx    clips[CLIP_COUNT];              // index into an animator's clip list
    bool   ok;
};

enum city_set {
    SET_ROAD,          // straight, corner, tsplit, junction, crossing, curved corner
    SET_BUILDING,      // KayKit's building_A..H - every occupied lot, any zone
    SET_YARD,          // fences, paths, driveways, planters
    SET_STREET,        // lamps, lights, bins, hydrants, benches, boxes
    SET_TREE_STREET,   // narrow enough to stand on a pavement
    SET_TREE_PARK,     // anything goes
    SET_SHRUB,         // bushes, grass tufts, flowers
    SET_ROCK,          // rocks, stones, logs, stumps
    SET_PARK_FEATURE,  // statues, pots, campfires, tents
    SET_INDUSTRIAL,    // water towers, crates, dumpsters
    SET_RAILROAD,      // track pieces
    SET_TRAIN,         // locomotives and carriages
    SET_COUNT
};

// named single models the generator reaches for by hand
enum city_single {
    ONE_ROAD_STRAIGHT, ONE_ROAD_CORNER, ONE_ROAD_TSPLIT, ONE_ROAD_JUNCTION,
    ONE_ROAD_CROSSING, ONE_ROAD_CORNER_CURVED,
    ONE_STREETLIGHT, ONE_TRAFFICLIGHT, ONE_BENCH, ONE_HYDRANT, ONE_BIN, ONE_DUMPSTER,
    ONE_QUAD, ONE_SLAB, ONE_BRIDGE, ONE_PIER,
    ONE_COUNT
};

struct city_model {
    idx  mesh;
    idx  material;
    vec3 bounds_min;
    vec3 bounds_max;
    float scale;        // kit -> world, already folded in by the generator
};

// A vehicle is the one model kind that is not a single mesh: the body and the
// wheels have to move independently, so the OBJ's named groups are split apart
// at load time and the wheel pivots kept.
#define CITY_MAX_VEHICLES 24

#define CITY_MAX_VEHICLE_PARTS 3

struct city_vehicle_model {
    idx   body_mesh;
    idx   wheel_mesh;
    idx   material;
    vec3  body_offset;      // where the body group's own centre sits, world scaled

    // grills, doors, a bin lifter: anything the kit authored as its own group
    // that is neither the shell nor a wheel, and would otherwise be dropped
    idx   part_mesh[CITY_MAX_VEHICLE_PARTS];
    vec3  part_offset[CITY_MAX_VEHICLE_PARTS];
    int   part_count;

    vec3  wheel_pivot[4];   // front-left, front-right, rear-left, rear-right
    float wheel_radius;
    float scale;
    // The car kit is authored nose-along-+Z, the opposite of every other kit
    // here (verified from where each model puts its front wheels), so this is
    // the spin that turns a vehicle to face the way it is driving.
    float model_yaw;
    float half_length;      // world metres, for the collider and for parking
    float half_width;
    float height;
    bool  valid;
};

struct city_set_range { idx first; idx count; };

struct city_catalog {
    city_model models[CITY_MAX_MODELS];
    size_t     model_count;

    city_set_range sets[SET_COUNT];
    idx            singles[ONE_COUNT];

    city_vehicle_model vehicles[CITY_MAX_VEHICLES];
    size_t             vehicle_count;

    // materials
    idx mat_city, mat_suburban, mat_vehicle, mat_rail, mat_palette;
    idx mat_grass, mat_concrete, mat_asphalt, mat_dirt, mat_water, mat_horizon;
    // Same citybits atlas, a few shades apart. Real streets are not built out of
    // one batch of concrete, and one tint per building breaks the repetition
    // without reintroducing the primary-coloured blocks.
    idx mat_city_shades[4];
    size_t mat_city_shade_count;

    // the animated cast
    city_character characters[CITY_MAX_CHARACTERS];
    size_t         character_count;

    idx    dog_model, dog_mesh, dog_material;
    float  dog_scale;

    bool ok;
};

static bool city_load_catalog(city_catalog& cat, pix_data_loader& loader, pix_renderer& renderer);
static const city_model* city_pick(const city_catalog& cat, int set, uint32_t roll);
static const city_model* city_get(const city_catalog& cat, idx model);

// ---------------- implementation ----------------

static const city_model* city_get(const city_catalog& cat, idx model) {
    return model < cat.model_count ? &cat.models[model] : 0;
}

static const city_model* city_pick(const city_catalog& cat, int set, uint32_t roll) {
    const city_set_range& r = cat.sets[set];
    if (!r.count) return 0;
    return &cat.models[r.first + roll % r.count];
}

static idx city__add_model(city_catalog& cat, pix_data_loader& loader, pix_renderer& renderer,
                           const char* dir, const char* name, idx material, float scale) {
    if (cat.model_count >= CITY_MAX_MODELS) return (idx)-1;
    char path[512];
    snprintf(path, sizeof(path), "%s%s.obj", dir, name);
    idx file = load_mesh_obj_file(loader, path);
    if (file == (idx)-1) return (idx)-1;

    mesh_file_data& md = loader.mesh_files[file];
    idx mesh = load_mesh(renderer, md);
    if (mesh == (idx)-1) return (idx)-1;      // pool full; better an absent prop than an invisible one

    idx id = (idx)cat.model_count++;
    city_model& m = cat.models[id];
    m.mesh = mesh;
    m.material = material;
    m.bounds_min = md.bounds_min;
    m.bounds_max = md.bounds_max;
    m.scale = scale;
    return id;
}

// loads a whole family in one go and records it as a set
static void city__add_set(city_catalog& cat, pix_data_loader& loader, pix_renderer& renderer,
                          int set, const char* dir, const char* const* names, int count,
                          idx material, float scale) {
    cat.sets[set].first = (idx)cat.model_count;
    cat.sets[set].count = 0;
    for (int i = 0; i < count; i++) {
        if (city__add_model(cat, loader, renderer, dir, names[i], material, scale) != (idx)-1)
            cat.sets[set].count++;
    }
}

// appends to a set that is already open (several kits feed one set)
static void city__extend_set(city_catalog& cat, pix_data_loader& loader, pix_renderer& renderer,
                             int set, const char* dir, const char* const* names, int count,
                             idx material, float scale) {
    if (!cat.sets[set].count) { cat.sets[set].first = (idx)cat.model_count; }
    for (int i = 0; i < count; i++) {
        if (city__add_model(cat, loader, renderer, dir, names[i], material, scale) != (idx)-1)
            cat.sets[set].count++;
    }
}

// ---- generated geometry ----

// a flat unit quad in XZ, so one mesh scaled by a plain material colour covers
// grass, tarmac, dirt and pavement tops without a texture between them
static idx city__make_quad(pix_renderer& renderer) {
    static vertex verts[4];
    static uint16_t inds[6] = { 0, 1, 2, 0, 2, 3 };
    verts[0].position = v3(-0.5f, 0.0f, -0.5f);
    verts[1].position = v3( 0.5f, 0.0f, -0.5f);
    verts[2].position = v3( 0.5f, 0.0f,  0.5f);
    verts[3].position = v3(-0.5f, 0.0f,  0.5f);
    for (int i = 0; i < 4; i++) {
        verts[i].normal = v3(0.0f, 1.0f, 0.0f);
        verts[i].uv = v2(0.5f, 0.5f);
    }
    mesh_file_data md = {};
    md.vertex_count = 4; md.vertex_data = verts;
    md.index_count = 6;  md.index_data = inds;
    return load_mesh(renderer, md);
}

// a unit box sitting on y = 0, used for pavement slabs and building colliders
static idx city__make_slab(pix_renderer& renderer) {
    static vertex verts[24];
    static uint16_t inds[36];
    static const float FACE[6][3] = {
        { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 }, { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }
    };
    // corners of each face, wound counter-clockwise seen from outside
    static const float CORNER[6][4][3] = {
        { { -0.5f, 1, -0.5f }, { -0.5f, 1, 0.5f }, { 0.5f, 1, 0.5f }, { 0.5f, 1, -0.5f } },
        { { -0.5f, 0, -0.5f }, { 0.5f, 0, -0.5f }, { 0.5f, 1, -0.5f }, { -0.5f, 1, -0.5f } },
        { { 0.5f, 0, 0.5f }, { -0.5f, 0, 0.5f }, { -0.5f, 1, 0.5f }, { 0.5f, 1, 0.5f } },
        { { -0.5f, 0, 0.5f }, { -0.5f, 0, -0.5f }, { -0.5f, 1, -0.5f }, { -0.5f, 1, 0.5f } },
        { { 0.5f, 0, -0.5f }, { 0.5f, 0, 0.5f }, { 0.5f, 1, 0.5f }, { 0.5f, 1, -0.5f } },
        { { -0.5f, 0, 0.5f }, { 0.5f, 0, 0.5f }, { 0.5f, 0, -0.5f }, { -0.5f, 0, -0.5f } }
    };
    int v = 0, i = 0;
    for (int f = 0; f < 6; f++) {
        int base = v;
        for (int c = 0; c < 4; c++) {
            verts[v].position = v3(CORNER[f][c][0], CORNER[f][c][1], CORNER[f][c][2]);
            verts[v].normal = v3(FACE[f][0], FACE[f][1], FACE[f][2]);
            verts[v].uv = v2(0.5f, 0.5f);
            v++;
        }
        inds[i++] = (uint16_t)(base + 0); inds[i++] = (uint16_t)(base + 1); inds[i++] = (uint16_t)(base + 2);
        inds[i++] = (uint16_t)(base + 0); inds[i++] = (uint16_t)(base + 2); inds[i++] = (uint16_t)(base + 3);
    }
    mesh_file_data md = {};
    md.vertex_count = 24; md.vertex_data = verts;
    md.index_count = 36;  md.index_data = inds;
    return load_mesh(renderer, md);
}

static idx city__register_generated(city_catalog& cat, idx mesh, idx material) {
    if (cat.model_count >= CITY_MAX_MODELS) return (idx)-1;
    idx id = (idx)cat.model_count++;
    city_model& m = cat.models[id];
    m.mesh = mesh;
    m.material = material;
    m.bounds_min = v3(-0.5f, 0.0f, -0.5f);
    m.bounds_max = v3(0.5f, 1.0f, 0.5f);
    m.scale = 1.0f;
    return id;
}

// ---- palette grading ----

static void city__rgb_to_hsl(vec3 c, float* h, float* s, float* l) {
    float mx = c.x > c.y ? (c.x > c.z ? c.x : c.z) : (c.y > c.z ? c.y : c.z);
    float mn = c.x < c.y ? (c.x < c.z ? c.x : c.z) : (c.y < c.z ? c.y : c.z);
    float d = mx - mn;
    *l = (mx + mn) * 0.5f;
    if (d < 1e-6f) { *h = 0.0f; *s = 0.0f; return; }
    *s = (*l > 0.5f) ? d / (2.0f - mx - mn) : d / (mx + mn);
    if (mx == c.x)      *h = 60.0f * (((c.y - c.z) / d) + (c.y < c.z ? 6.0f : 0.0f));
    else if (mx == c.y) *h = 60.0f * (((c.z - c.x) / d) + 2.0f);
    else                *h = 60.0f * (((c.x - c.y) / d) + 4.0f);
}

static float city__hue_channel(float p, float q, float t) {
    if (t < 0.0f) t += 1.0f;
    if (t > 1.0f) t -= 1.0f;
    if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
    if (t < 1.0f / 2.0f) return q;
    if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
}

static vec3 city__hsl_to_rgb(float h, float s, float l) {
    if (s < 1e-6f) return v3(l, l, l);
    float q = (l < 0.5f) ? l * (1.0f + s) : l + s - l * s;
    float p = 2.0f * l - q;
    float t = h / 360.0f;
    return v3(city__hue_channel(p, q, t + 1.0f / 3.0f),
              city__hue_channel(p, q, t),
              city__hue_channel(p, q, t - 1.0f / 3.0f));
}

// Kenney's nature kit paints every leaf a stylised turquoise - `leafsGreen` is
// literally (0.16, 0.79, 0.67), a hue of 169 degrees. Against a photographic
// sky and brick buildings that reads as a coral reef, not a street tree. The
// .mtl colours arrive at runtime, so the fix has to happen here: rotate the
// cyan-green band down to a real leaf green and take some saturation out of it.
//
// The band is deliberately narrow and skips pale colours, so genuinely cyan
// things in the kit (ice, water, glass) keep their hue.
static void city__grade_palette(obj_palette& palette) {
    for (size_t i = 0; i < palette.count; i++) {
        float h, s, l;
        city__rgb_to_hsl(palette.colors[i], &h, &s, &l);
        if (h < 145.0f || h > 183.0f) continue;
        if (s < 0.20f || l > 0.72f) continue;

        // 145..183 maps onto 96..124 degrees: the darker teals become deep
        // forest green, the brighter ones a fresher leaf green
        float u = (h - 145.0f) / 38.0f;
        palette.colors[i] = city__hsl_to_rgb(96.0f + u * 28.0f, s * 0.66f, l * 0.92f);
    }
}

// ---- rigged models ----

struct city_rig_bounds { vec3 min; vec3 max; };

// The true world-space extents of a rig standing in its rest pose. Raw vertex
// positions are not enough: a skinned mesh lives in bind space, and the
// skeleton's root transform (which is where glTF hides its axis and unit
// fixups) sits above it. Posing the mesh is the only way to find out how tall
// a character actually is, and every gameplay number - eye height, collider,
// how far a doorway is - depends on getting it right.
static city_rig_bounds city__measure_rig(pix_data_loader& loader, idx model) {
    city_rig_bounds b;
    b.min = v3( 1e30f,  1e30f,  1e30f);
    b.max = v3(-1e30f, -1e30f, -1e30f);

    skinned_mesh_file_data* md = get_model_mesh(loader, model);
    skeleton_file_data* sk = get_model_skeleton(loader, model);
    if (!md || !sk) { b.min = v3(0, 0, 0); b.max = v3(1, 1, 1); return b; }

    static animation rest;
    animation_rest_pose(*sk, &rest);

    for (size_t i = 0; i < md->vertex_count; i++) {
        const vertex_rigged& v = md->vertex_data[i];
        const float ids[4] = { v.bone_ids.x, v.bone_ids.y, v.bone_ids.z, v.bone_ids.w };
        const float wts[4] = { v.bone_weights.x, v.bone_weights.y,
                               v.bone_weights.z, v.bone_weights.w };
        vec3 p = v3(0.0f, 0.0f, 0.0f);
        float total = 0.0f;
        for (int k = 0; k < 4; k++) {
            if (wts[k] <= 0.0f) continue;
            size_t bone = (size_t)(ids[k] + 0.5f);
            if (bone >= rest.bone_count) continue;
            p = v3add(p, v3scale(mat4_mul_point(rest.bones[bone], v.position), wts[k]));
            total += wts[k];
        }
        if (total < 1e-4f) p = v.position;             // unweighted vertex

        if (p.x < b.min.x) b.min.x = p.x;
        if (p.y < b.min.y) b.min.y = p.y;
        if (p.z < b.min.z) b.min.z = p.z;
        if (p.x > b.max.x) b.max.x = p.x;
        if (p.y > b.max.y) b.max.y = p.y;
        if (p.z > b.max.z) b.max.z = p.z;
    }
    return b;
}

// ---- characters ----

// A clip is exported as "<Armature>|<Name>", and one of these rigs prefixes
// every name with "Man_" on top of that. Reduce both to the bare role name so
// the tables below can just say "Walk".
static const char* city__clip_role(const char* full) {
    const char* bar = strrchr(full, '|');
    const char* name = bar ? bar + 1 : full;
    if (strncmp(name, "Man_", 4) == 0) name += 4;
    return name;
}

// Preference order per role: the first one a rig actually ships wins. The
// fallbacks are what keep a rig with 11 clips usable next to one with 24.
static const char* const CLIP_NAMES[CLIP_COUNT][4] = {
    /* IDLE */ { "Idle", "Idle_Neutral", "Standing", 0 },
    /* WALK */ { "Walk", "Run", 0, 0 },
    /* RUN  */ { "Run", "Walk", 0, 0 },
    /* JUMP */ { "Jump", "RunningJump", "Roll", 0 },
    /* WAVE */ { "Wave", "Clapping", "Interact", 0 },
    /* TALK */ { "Idle_Neutral", "Standing", "Interact", "Idle" },
    /* SIT  */ { "Sitting", "Idle", 0, 0 },
};

// Outfit tints. These multiply the rig's own palette rather than replacing it,
// so a character keeps its authored skin and hair while its clothes shift -
// a flat recolour of the whole model reads as a painted statue.
static const vec3 CHAR_TINTS[CITY_MAX_CHAR_TINTS] = {
    { 1.00f, 1.00f, 1.00f }, { 0.86f, 0.90f, 1.00f }, { 1.00f, 0.92f, 0.86f },
    { 0.90f, 1.00f, 0.92f }, { 1.04f, 0.98f, 0.88f }
};

// Fills one animator with every clip a rig ships, in export order - which is
// exactly the order city_character::clips was resolved against, so a role index
// addresses the animator directly.
static bool city_build_animator(animator& out, pix_data_loader& loader, idx model) {
    skeleton_file_data* sk = get_model_skeleton(loader, model);
    if (!sk) return false;
    out = pix_create_animator(sk);
    const model_file_data& mf = loader.model_files[model];
    for (size_t i = 0; i < mf.animation_count; i++)
        animator_add_clip(out, &loader.animation_clips[mf.first_animation + i]);
    return out.clip_count > 0;
}

static bool city__load_character(city_character& ch, pix_data_loader& loader,
                                 pix_renderer& renderer, const char* file) {
    memset(&ch, 0, sizeof(ch));
    ch.model = (idx)-1;
    ch.mesh = (idx)-1;
    for (int i = 0; i < CLIP_COUNT; i++) ch.clips[i] = (idx)-1;

    char path[512];
    snprintf(path, sizeof(path), "%s%s", CHAR_DIR, file);
    ch.model = load_model_gltf_file(loader, path);
    if (ch.model == (idx)-1) return false;

    skinned_mesh_file_data* md = get_model_mesh(loader, ch.model);
    if (!md) { ch.model = (idx)-1; return false; }
    ch.mesh = load_skinned_mesh(renderer, *md);
    if (ch.mesh == (idx)-1) { ch.model = (idx)-1; return false; }

    city_rig_bounds rb = city__measure_rig(loader, ch.model);
    float raw = rb.max.y - rb.min.y;
    ch.scale = (raw > 0.05f) ? PLAYER_HEIGHT / raw : 1.0f;
    ch.yaw_offset = 3.14159265f;     // these rigs are modelled facing +Z

    // resolve the clip roles against what this rig actually exports
    const model_file_data& mf = loader.model_files[ch.model];
    for (int role = 0; role < CLIP_COUNT; role++) {
        for (int pref = 0; pref < 4 && ch.clips[role] == (idx)-1; pref++) {
            const char* want = CLIP_NAMES[role][pref];
            if (!want) break;
            for (size_t i = 0; i < mf.animation_count; i++) {
                const char* have = loader.animation_clips[mf.first_animation + i].name;
                if (strcmp(city__clip_role(have), want) == 0) { ch.clips[role] = (idx)i; break; }
            }
        }
    }

    // The colours arrived as a generated palette, so the base material samples
    // it unfiltered; every tint rides on the same upload.
    image_file_data* img = mf.image >= 0 ? &loader.image_files[mf.image] : 0;
    idx base = load_material_image(renderer, img, CHAR_TINTS[0], 0.0f, 0.72f,
                                   mf.image_is_palette);
    ch.materials[ch.material_count++] = base;
    for (int i = 1; i < CITY_MAX_CHAR_TINTS; i++)
        ch.materials[ch.material_count++] = clone_material(renderer, base, CHAR_TINTS[i], 0.0f, 0.72f);

    ch.ok = true;
    return true;
}

// ---- vehicles ----

// The Kenney car kit authors every vehicle as one OBJ with `g body` and four
// `g wheel-*` groups. Splitting them lets the wheels roll and the front pair
// steer; the pivots come back in model space and are scaled here once.
static bool city__load_vehicle(city_catalog& cat, pix_data_loader& loader, pix_renderer& renderer,
                               const char* name, float scale) {
    if (cat.vehicle_count >= CITY_MAX_VEHICLES) return false;
    char path[512];
    snprintf(path, sizeof(path), "%s%s.obj", VEH_DIR, name);
    idx file = load_mesh_obj_file(loader, path);
    if (file == (idx)-1) return false;

    static const char* WHEEL_GROUP[4] = {
        "wheel-front-left", "wheel-front-right", "wheel-back-left", "wheel-back-right"
    };

    city_vehicle_model v = {};
    v.material = cat.mat_vehicle;
    v.scale = scale;
    v.model_yaw = 3.14159265f;
    for (int i = 0; i < CITY_MAX_VEHICLE_PARTS; i++) v.part_mesh[i] = (idx)-1;

    const mesh_file_data& whole = loader.mesh_files[file];
    v.half_width  = (whole.bounds_max.x - whole.bounds_min.x) * 0.5f * scale;
    v.half_length = (whole.bounds_max.z - whole.bounds_min.z) * 0.5f * scale;
    v.height      = (whole.bounds_max.y - whole.bounds_min.y) * scale;

    vec3 body_pivot = v3(0.0f, 0.0f, 0.0f);
    idx body_file = load_mesh_obj_group(loader, file, "body", &body_pivot);

    int wheels_found = 0;
    idx wheel_file = (idx)-1;
    for (int i = 0; i < 4; i++) {
        vec3 pivot;
        int g = obj_find_group(loader.mesh_files[file], WHEEL_GROUP[i]);
        if (g < 0) continue;
        // every wheel of a car is the same geometry once re-centred, so only
        // the first one becomes a mesh - the rest contribute just their pivot
        if (wheel_file == (idx)-1) {
            wheel_file = load_mesh_obj_group(loader, file, WHEEL_GROUP[i], &pivot);
            if (wheel_file == (idx)-1) continue;
            v.wheel_radius = (loader.mesh_files[wheel_file].bounds_max.y
                            - loader.mesh_files[wheel_file].bounds_min.y) * 0.5f * scale;
        } else {
            pivot = loader.mesh_files[file].groups[g].pivot;
        }
        v.wheel_pivot[i] = v3scale(pivot, scale);
        wheels_found++;
    }

    if (body_file != (idx)-1 && wheels_found == 4 && wheel_file != (idx)-1) {
        v.body_mesh   = load_mesh(renderer, loader.mesh_files[body_file]);
        v.wheel_mesh  = load_mesh(renderer, loader.mesh_files[wheel_file]);
        v.body_offset = v3scale(body_pivot, scale);

        // pick up whatever else the model was built out of
        const mesh_file_data& src = loader.mesh_files[file];
        for (size_t gi = 0; gi < src.group_count && v.part_count < CITY_MAX_VEHICLE_PARTS; gi++) {
            const char* name = src.groups[gi].name;
            if (strcmp(name, "body") == 0) continue;
            if (strncmp(name, "wheel", 5) == 0) continue;
            vec3 pivot;
            idx part = load_mesh_obj_group(loader, file, name, &pivot);
            if (part == (idx)-1) continue;
            idx mesh = load_mesh(renderer, loader.mesh_files[part]);
            if (mesh == (idx)-1) continue;
            v.part_mesh[v.part_count] = mesh;
            v.part_offset[v.part_count] = v3scale(pivot, scale);
            v.part_count++;
        }
    } else {
        // not authored in parts (or a part is missing): draw it whole, no wheels
        v.body_mesh  = load_mesh(renderer, loader.mesh_files[file]);
        v.wheel_mesh = (idx)-1;
        v.body_offset = v3(0.0f, 0.0f, 0.0f);
    }

    v.valid = true;
    cat.vehicles[cat.vehicle_count++] = v;
    return true;
}

// ---- the tables ----

static bool city_load_catalog(city_catalog& cat, pix_data_loader& loader, pix_renderer& renderer) {
    memset(&cat, 0, sizeof(cat));
    for (int i = 0; i < ONE_COUNT; i++) cat.singles[i] = (idx)-1;

    // Materials first: a kit's texture atlas is shared by every model in it, so
    // the whole kit collapses into one instanced draw per mesh.
    // citybits_city.png is the stock atlas re-graded into a coherent brick /
    // stone / slate palette by tools/make_city_atlas.py - the stock one paints
    // each building model a different primary and a street of them reads as
    // toy blocks. Falls back to the original if the graded file is missing.
    cat.mat_city       = load_material(renderer, KAY_DIR  "citybits_city.png",    v3(1,1,1), 0.0f, 0.62f);
    if (!renderer.materials[cat.mat_city].texture)
        cat.mat_city   = load_material(renderer, KAY_DIR  "citybits_texture.png", v3(1,1,1), 0.0f, 0.62f);
    // a few shades of the same atlas, handed out per building by the generator
    static const vec3 CITY_SHADES[4] = {
        { 1.00f, 1.00f, 1.00f }, { 0.93f, 0.94f, 0.97f },
        { 1.05f, 1.02f, 0.97f }, { 0.88f, 0.89f, 0.90f }
    };
    cat.mat_city_shades[cat.mat_city_shade_count++] = cat.mat_city;
    for (int i = 1; i < 4; i++)
        cat.mat_city_shades[cat.mat_city_shade_count++] =
            clone_material(renderer, cat.mat_city, CITY_SHADES[i], 0.0f, 0.62f);

    cat.mat_suburban   = load_material(renderer, SUB_DIR  "colormap.png",         v3(1,1,1), 0.0f, 0.64f);
    // car paint: a glossy dielectric, not bare metal - a low roughness so it
    // catches a crisp sky reflection and a sun highlight along the body
    cat.mat_vehicle    = load_material(renderer, VEH_DIR  "colormap.png",         v3(1,1,1), 0.05f, 0.30f);
    cat.mat_rail       = load_material(renderer, RAIL_DIR "colormap.png",         v3(1,1,1), 0.15f, 0.45f);
    // Claimed now so untextured models can reference it while they load; its
    // texture cannot be built until every .mtl colour has been seen, so it is
    // filled in at the bottom of this function.
    cat.mat_palette    = load_material(renderer, 0, v3(1,1,1), 0.0f, 0.9f);

    cat.mat_grass    = load_material(renderer, 0, v3(0.31f, 0.47f, 0.24f), 0.0f, 1.0f);
    cat.mat_concrete = load_material(renderer, 0, v3(0.58f, 0.57f, 0.54f), 0.0f, 0.88f);
    cat.mat_asphalt  = load_material(renderer, 0, v3(0.19f, 0.19f, 0.21f), 0.0f, 0.82f);
    cat.mat_dirt     = load_material(renderer, 0, v3(0.52f, 0.42f, 0.30f), 0.0f, 1.0f);
    cat.mat_water    = load_material(renderer, 0, v3(0.22f, 0.42f, 0.58f), 0.3f, 0.2f);
    // sits under the whole map; muted so the horizon reads as haze rather than
    // as a hard edge where the per-cell ground stops
    cat.mat_horizon  = load_material(renderer, 0, v3(0.42f, 0.50f, 0.42f), 0.0f, 1.0f);

    // ---- KayKit: roads and street furniture ----
    static const char* ROADS[] = {
        "road_straight", "road_corner", "road_tsplit", "road_junction",
        "road_straight_crossing", "road_corner_curved"
    };
    city__add_set(cat, loader, renderer, SET_ROAD, KAY_DIR, ROADS, 6, cat.mat_city, KIT_ROAD_SCALE);
    for (int i = 0; i < 6 && (idx)i < cat.sets[SET_ROAD].count; i++)
        cat.singles[ONE_ROAD_STRAIGHT + i] = cat.sets[SET_ROAD].first + i;

    static const char* STREET[] = {
        "streetlight", "trafficlight_A", "bench", "firehydrant", "trash_A",
        "dumpster", "trafficlight_B", "trafficlight_C", "trash_B", "box_A", "box_B"
    };
    city__add_set(cat, loader, renderer, SET_STREET, KAY_DIR, STREET, 11, cat.mat_city, KIT_PROP_SCALE);
    if (cat.sets[SET_STREET].count >= 6) {
        idx f = cat.sets[SET_STREET].first;
        cat.singles[ONE_STREETLIGHT]  = f + 0;
        cat.singles[ONE_TRAFFICLIGHT] = f + 1;
        cat.singles[ONE_BENCH]        = f + 2;
        cat.singles[ONE_HYDRANT]      = f + 3;
        cat.singles[ONE_BIN]          = f + 4;
        cat.singles[ONE_DUMPSTER]     = f + 5;
    }

    static const char* INDUSTRIAL[] = { "watertower", "box_A", "box_B", "dumpster" };
    city__add_set(cat, loader, renderer, SET_INDUSTRIAL, KAY_DIR, INDUSTRIAL, 4,
                  cat.mat_city, KIT_PROP_SCALE);

    // ---- KayKit: every building in the city, from a downtown tower down to a
    // suburban house, is one of these 8 - only their vertical scale and how
    // often a lot gets one at all differ by zone. Each is modelled to fill
    // exactly one road tile, so it drops onto a lot cell with no fitting.
    //
    // Textured with the same citybits atlas as the roads and street furniture -
    // this is the asset's own authored colouring, not a colour picked here.
    static const char* BUILDINGS[] = {
        "building_A", "building_B", "building_C", "building_D",
        "building_E", "building_F", "building_G", "building_H"
    };
    city__add_set(cat, loader, renderer, SET_BUILDING, KAY_DIR, BUILDINGS, 8,
                  cat.mat_city, KIT_ROAD_SCALE);

    // ---- Kenney suburban: yard dressing only, not buildings ----
    static const char* YARD[] = {
        "fence", "fence-low", "fence-1x2", "fence-1x3", "fence-1x4",
        "fence-2x2", "fence-2x3", "fence-3x2", "fence-3x3",
        "path-long", "path-short", "path-stones-long", "path-stones-short",
        "path-stones-messy", "driveway-long", "driveway-short", "planter"
    };
    city__add_set(cat, loader, renderer, SET_YARD, SUB_DIR, YARD, 17,
                  cat.mat_suburban, KIT_BUILDING_SCALE);

    static const char* SUB_TREES[] = { "tree-large", "tree-small" };
    city__add_set(cat, loader, renderer, SET_TREE_STREET, SUB_DIR, SUB_TREES, 2,
                  cat.mat_suburban, KIT_BUILDING_SCALE);

    // ---- Kenney nature (no texture; its .mtl colours go through the palette) ----
    static const char* STREET_TREES[] = {
        "tree_thin", "tree_small", "tree_simple", "tree_cone", "tree_tall", "tree_plateau"
    };
    city__extend_set(cat, loader, renderer, SET_TREE_STREET, NAT_DIR, STREET_TREES, 6,
                     cat.mat_palette, KIT_NATURE_SCALE);

    static const char* PARK_TREES[] = {
        "tree_default", "tree_oak", "tree_detailed", "tree_fat", "tree_blocks",
        "tree_pineDefaultA", "tree_pineTallA", "tree_pineRoundC", "tree_pineSmallB",
        "tree_default_fall", "tree_oak_fall", "tree_detailed_dark", "tree_tall_dark",
        "tree_palmTall", "tree_palmDetailedShort"
    };
    city__add_set(cat, loader, renderer, SET_TREE_PARK, NAT_DIR, PARK_TREES, 15,
                  cat.mat_palette, KIT_NATURE_SCALE);

    static const char* SHRUBS[] = {
        "plant_bush", "plant_bushDetailed", "plant_bushLarge", "plant_bushSmall",
        "plant_bushTriangle", "grass", "grass_large", "grass_leafs",
        "flower_redA", "flower_yellowB", "flower_purpleC", "mushroom_redGroup",
        "crops_leafsStageB", "plant_flatTall"
    };
    city__add_set(cat, loader, renderer, SET_SHRUB, NAT_DIR, SHRUBS, 14,
                  cat.mat_palette, KIT_NATURE_SCALE);

    static const char* ROCKS[] = {
        "rock_smallA", "rock_smallC", "rock_largeB", "rock_tallD",
        "stone_smallB", "stone_largeE", "log", "log_stack", "stump_round", "stump_squareDetailed"
    };
    city__add_set(cat, loader, renderer, SET_ROCK, NAT_DIR, ROCKS, 10,
                  cat.mat_palette, KIT_NATURE_SCALE);

    static const char* FEATURES[] = {
        "statue_column", "statue_obelisk", "statue_ring", "statue_block",
        "pot_large", "pot_small", "campfire_stones", "sign", "path_stoneCircle",
        "tent_smallClosed", "canoe", "lily_large"
    };
    city__add_set(cat, loader, renderer, SET_PARK_FEATURE, NAT_DIR, FEATURES, 12,
                  cat.mat_palette, KIT_NATURE_SCALE);

    // ---- Kenney rail: a freight line along the city's edge ----
    static const char* RAILROAD[] = { "railroad-straight", "railroad-corner-large" };
    city__add_set(cat, loader, renderer, SET_RAILROAD, RAIL_DIR, RAILROAD, 2,
                  cat.mat_rail, KIT_RAIL_SCALE);

    static const char* TRAIN[] = {
        "train-locomotive-a", "train-carriage-box", "train-carriage-container-red",
        "train-carriage-container-blue", "train-carriage-tank", "train-carriage-lumber",
        "train-carriage-flatbed", "train-carriage-coal"
    };
    city__add_set(cat, loader, renderer, SET_TRAIN, RAIL_DIR, TRAIN, 8,
                  cat.mat_rail, KIT_RAIL_SCALE);

    // ---- vehicles ----
    static const char* CARS[] = {
        "sedan", "sedan-sports", "hatchback-sports", "suv", "suv-luxury", "van",
        "taxi", "police", "ambulance", "delivery", "truck", "garbage-truck",
        "firetruck", "truck-flat", "delivery-flat", "race"
    };
    for (int i = 0; i < 16; i++) city__load_vehicle(cat, loader, renderer, CARS[i], KIT_VEHICLE_SCALE);

    // ---- generated ground geometry ----
    idx quad = city__make_quad(renderer);
    idx slab = city__make_slab(renderer);
    cat.singles[ONE_QUAD] = city__register_generated(cat, quad, cat.mat_grass);
    cat.singles[ONE_SLAB] = city__register_generated(cat, slab, cat.mat_concrete);
    // a bridge deck and its piers are the same unit box, stretched
    cat.singles[ONE_BRIDGE] = city__register_generated(cat, slab, cat.mat_concrete);
    cat.singles[ONE_PIER]   = city__register_generated(cat, slab, cat.mat_dirt);

    // The palette texture can only be built once every .obj has been parsed,
    // since each new untextured material adds a colour to it.
    {
        city__grade_palette(loader.palette);
        static unsigned char pixels[OBJ_PALETTE_MAX * 4];
        obj_palette_pixels(loader.palette, pixels);
        idx tex = opengl_create_texture2d(OBJ_PALETTE_DIM, OBJ_PALETTE_DIM, 4, pixels, TEXTURE_PIXELATED);
        // mat_palette was handed out as index 0 before the texture existed, so
        // patch the material in place rather than renumbering every model
        renderer.materials[cat.mat_palette].texture = tex;
        renderer.materials[cat.mat_palette].color = v3(1.0f, 1.0f, 1.0f);
    }

    // ---- the animated cast ----
    static const char* CHARACTERS[CITY_MAX_CHARACTERS] = {
        "Hoodie Character by Quaternius - gKLBoRsyKe.glb",
        "Business Man by Quaternius - JFrLIKqvCH.glb",
        "Animated Woman by Quaternius - qJ2gsTUBHL.glb",
        "Man by Quaternius - HMnuH5geEG.glb"
    };
    for (int i = 0; i < CITY_MAX_CHARACTERS; i++) {
        city_character ch;
        if (city__load_character(ch, loader, renderer, CHARACTERS[i]))
            cat.characters[cat.character_count++] = ch;
    }

    // strays in the parks - the fox rig ships with idle, walk and run clips
    cat.dog_model = load_model_gltf_file(loader, DOG_MODEL);
    if (cat.dog_model != (idx)-1) {
        skinned_mesh_file_data* md = get_model_mesh(loader, cat.dog_model);
        if (md) {
            cat.dog_mesh = load_skinned_mesh(renderer, *md);
            model_file_data& mf = loader.model_files[cat.dog_model];
            cat.dog_material = load_material_image(renderer,
                mf.image >= 0 ? &loader.image_files[mf.image] : 0, v3(1, 1, 1), 0.1f, 0.8f);
            city_rig_bounds rb = city__measure_rig(loader, cat.dog_model);
            float raw = rb.max.y - rb.min.y;
            cat.dog_scale = (raw > 0.05f) ? 0.75f / raw : 1.0f;   // a fox stands ~0.75 m
        }
    }

    cat.ok = cat.sets[SET_ROAD].count > 0 && cat.sets[SET_BUILDING].count > 0;
    return cat.ok;
}
