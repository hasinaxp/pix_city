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
#define CITY_GRASS_SHADES 4

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
#define CITY_MAX_CHAR_TINTS 12

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
    SET_YARD,          // fences and planters
    SET_PAVING,        // garden paths and driveways, flat on the ground
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
    ONE_FENCE, ONE_PATH, ONE_DRIVEWAY,
    ONE_QUAD, ONE_SLAB, ONE_BRIDGE, ONE_PIER, ONE_WATER_TILE,
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
    // SET_BUILDING holds every building once per facade colour, facade-major:
    // model = sets[SET_BUILDING].first + facade * building_kinds + kind
    idx            building_kinds;

    city_vehicle_model vehicles[CITY_MAX_VEHICLES];
    size_t             vehicle_count;

    // materials
    idx mat_city, mat_suburban, mat_vehicle, mat_rail, mat_palette;
    idx mat_grass, mat_concrete, mat_asphalt, mat_dirt, mat_water, mat_horizon;
    idx mat_riverbed, mat_stone;
    // The same citybits atlas as the roads and the street furniture, but its
    // own material: buildings are the only thing whose windows should light up
    // after dark, and emission is a property of the material, not the mesh.
    idx mat_building;
    // a few shades of the same green so a field of grass cells is not one flat sheet
    idx mat_grass_shades[CITY_GRASS_SHADES];
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

// A model plus the size it should be in the world, in metres along its longest
// axis.
//
// One scale factor per kit only works where the kit is internally consistent,
// and the Kenney nature kit is not: inside a single folder a tree is 1.7 units
// tall and a tuft of grass is 0.25, so any factor that makes the trees right
// makes the undergrowth into shrubs the size of cars - which is exactly what
// "the foliage looks off at any scale" is. Sizing each model against its own
// measured bounds is the only thing that fixes it, and it also means adding a
// model to a table is stating how big the thing is rather than guessing a
// multiplier.
struct city_kit_item {
    const char* name;
    float       size;   // metres, longest axis
};

static idx city__add_model_sized(city_catalog& cat, pix_data_loader& loader,
                                 pix_renderer& renderer, const char* dir,
                                 const city_kit_item& item, idx material) {
    idx id = city__add_model(cat, loader, renderer, dir, item.name, material, 1.0f);
    if (id == (idx)-1) return id;
    city_model& m = cat.models[id];
    vec3 e = v3sub(m.bounds_max, m.bounds_min);
    float longest = e.x > e.y ? (e.x > e.z ? e.x : e.z) : (e.y > e.z ? e.y : e.z);
    m.scale = longest > 1e-4f ? item.size / longest : 1.0f;
    return id;
}

static void city__add_set_sized(city_catalog& cat, pix_data_loader& loader, pix_renderer& renderer,
                                int set, const char* dir, const city_kit_item* items, int count,
                                idx material) {
    cat.sets[set].first = (idx)cat.model_count;
    cat.sets[set].count = 0;
    for (int i = 0; i < count; i++)
        if (city__add_model_sized(cat, loader, renderer, dir, items[i], material) != (idx)-1)
            cat.sets[set].count++;
}

static void city__extend_set_sized(city_catalog& cat, pix_data_loader& loader,
                                   pix_renderer& renderer, int set, const char* dir,
                                   const city_kit_item* items, int count, idx material) {
    if (!cat.sets[set].count) cat.sets[set].first = (idx)cat.model_count;
    for (int i = 0; i < count; i++)
        if (city__add_model_sized(cat, loader, renderer, dir, items[i], material) != (idx)-1)
            cat.sets[set].count++;
}

// ---- building facades ----
//
// KayKit paints every building out of the citybits atlas, and the atlas is an
// 8 x 4 grid of swatches, each swatch a vertical lightness gradient (that
// gradient is the baked shading and window detail, and it is the only thing
// making the flat geometry read as architecture).
//
// The trouble is which swatches a single building reaches for. Measured off the
// meshes - area of the vertical faces only, so slabs and roofs are excluded -
// every one of the eight buildings paints its walls out of *two or three*
// different swatches stacked by height:
//
//   building_A   steel blue   y 0.10-0.90   +  slate      y 0.90-1.55
//   building_D   brown        y 0.90-2.25   +  tan        y 0.10-2.41
//   building_H   slate        y 0.10-2.95   +  sandstone  y 0.10-2.25
//
// which is why a building reads as a stack of differently coloured blocks
// rather than as one building. The nine swatches below are the kit's whole
// wall palette (no swatch in it is used for anything but walls in any
// meaningful quantity); collapsing all nine onto one target swatch gives a
// building a single facade material while leaving the concrete slabs, the
// white window trim and the small accent details exactly where they were.
//
// Variety then comes from loading each building once per target swatch and
// letting the generator pick, which is real variety between buildings instead
// of stripes within one.
#define ATLAS_COLS 8
#define ATLAS_ROWS 4    // swatch rows; each is two atlas cells tall

struct atlas_swatch { int row, col; };

// every swatch the eight buildings use on a vertical face
static const atlas_swatch BUILDING_WALL_SWATCHES[] = {
    { 0, 5 }, { 0, 7 }, { 1, 0 }, { 1, 2 }, { 1, 3 },
    { 1, 5 }, { 2, 2 }, { 2, 3 }, { 2, 7 }
};
#define BUILDING_WALL_SWATCH_COUNT 9

// The facades a city gets built out of, in the graded atlas: slate, sandstone,
// brick brown, terracotta and a pale blue-grey. Ordered so the first two read
// as commercial and the last three as residential - the generator leans on that.
static const atlas_swatch BUILDING_FACADES[] = {
    { 1, 2 },   // slate grey-blue
    { 1, 0 },   // pale blue-grey
    { 1, 3 },   // sandstone
    { 2, 2 },   // brick brown
    { 2, 7 }    // terracotta
};
#define BUILDING_FACADE_COUNT 5

// Moves a uv from whichever wall swatch it is in onto `to`, keeping its
// position *inside* the swatch - which is what preserves the baked gradient,
// and also means the uv stays exactly as far from the swatch edge as it was, so
// no new texel bleeding is introduced.
//
// OBJ v is bottom-origin and the vertex shader flips it, so the atlas row a uv
// lands in is measured from 1 - v.
static vec2 city__reswatch_uv(vec2 uv, atlas_swatch to) {
    float u = uv.x * ATLAS_COLS;
    float v = (1.0f - uv.y) * ATLAS_ROWS;
    int col = (int)floorf(u), row = (int)floorf(v);
    float fu = u - (float)col, fv = v - (float)row;
    if (col < 0) col = 0; else if (col >= ATLAS_COLS) col = ATLAS_COLS - 1;
    if (row < 0) row = 0; else if (row >= ATLAS_ROWS) row = ATLAS_ROWS - 1;

    bool wall = false;
    for (int i = 0; i < BUILDING_WALL_SWATCH_COUNT; i++)
        if (BUILDING_WALL_SWATCHES[i].row == row && BUILDING_WALL_SWATCHES[i].col == col)
            wall = true;
    if (!wall) return uv;

    return v2(((float)to.col + fu) / ATLAS_COLS,
              1.0f - ((float)to.row + fv) / ATLAS_ROWS);
}

// Loads building_A..H once per facade colour, laid out facade-major so a model
// is addressed as first + facade * building_kinds + kind.
//
// Each .obj is parsed once and its authored uvs kept aside; every facade then
// rewrites from that original rather than from what the previous one left
// behind, and only the vertex upload is repeated.
#define CITY_MAX_BUILDING_KINDS 8

static void city__add_buildings(city_catalog& cat, pix_data_loader& loader,
                                pix_renderer& renderer, const char* const* names, int count) {
    if (count > CITY_MAX_BUILDING_KINDS) count = CITY_MAX_BUILDING_KINDS;
    static vec2 original[CITY_MAX_BUILDING_KINDS][8192];
    idx file[CITY_MAX_BUILDING_KINDS];
    int kinds = 0;

    for (int i = 0; i < count; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s%s.obj", KAY_DIR, names[i]);
        idx f = load_mesh_obj_file(loader, path);
        if (f == (idx)-1) continue;
        const mesh_file_data& md = loader.mesh_files[f];
        if (md.vertex_count > 8192) continue;          // no room to keep its uvs
        for (size_t k = 0; k < md.vertex_count; k++) original[kinds][k] = md.vertex_data[k].uv;
        file[kinds++] = f;
    }

    cat.sets[SET_BUILDING].first = (idx)cat.model_count;
    cat.sets[SET_BUILDING].count = 0;
    cat.building_kinds = (idx)kinds;

    for (int f = 0; f < BUILDING_FACADE_COUNT; f++)
        for (int i = 0; i < kinds; i++) {
            if (cat.model_count >= CITY_MAX_MODELS) return;
            mesh_file_data& md = loader.mesh_files[file[i]];
            for (size_t k = 0; k < md.vertex_count; k++)
                md.vertex_data[k].uv = city__reswatch_uv(original[i][k], BUILDING_FACADES[f]);

            idx mesh = load_mesh(renderer, md);
            if (mesh == (idx)-1) { cat.building_kinds = 0; return; }   // pool full: no set at all

            city_model& m = cat.models[cat.model_count++];
            m.mesh = mesh;
            m.material = cat.mat_building;
            m.bounds_min = md.bounds_min;
            m.bounds_max = md.bounds_max;
            m.scale = KIT_ROAD_SCALE;
            cat.sets[SET_BUILDING].count++;
        }
}

// the model for one building kind wearing one facade colour
static const city_model* city_building(const city_catalog& cat, int facade, int kind) {
    if (!cat.building_kinds) return 0;
    facade %= BUILDING_FACADE_COUNT;
    kind %= (int)cat.building_kinds;
    idx id = cat.sets[SET_BUILDING].first + (idx)facade * cat.building_kinds + (idx)kind;
    return id < cat.model_count ? &cat.models[id] : 0;
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

// The water surface tile: the same unit quad, but subdivided, because the wave
// shader displaces its vertices. Four corners can only ever be a flat plane no
// matter what the fragment shader draws on it, and a flat plane is the single
// thing that stops water reading as water - a surface that does not move
// against the horizon is a painted floor. The wave field is a function of world
// position, so neighbouring tiles agree along their shared edge exactly and the
// river comes out as one continuous surface.
#define WATER_TILE_DIV 6

static idx city__make_water_tile(pix_renderer& renderer) {
    static vertex verts[(WATER_TILE_DIV + 1) * (WATER_TILE_DIV + 1)];
    static uint16_t inds[WATER_TILE_DIV * WATER_TILE_DIV * 6];
    int v = 0;
    for (int z = 0; z <= WATER_TILE_DIV; z++)
        for (int x = 0; x <= WATER_TILE_DIV; x++) {
            float u = (float)x / (float)WATER_TILE_DIV;
            float w = (float)z / (float)WATER_TILE_DIV;
            verts[v].position = v3(u - 0.5f, 0.0f, w - 0.5f);
            verts[v].normal = v3(0.0f, 1.0f, 0.0f);
            verts[v].uv = v2(u, w);
            v++;
        }
    int i = 0;
    for (int z = 0; z < WATER_TILE_DIV; z++)
        for (int x = 0; x < WATER_TILE_DIV; x++) {
            uint16_t a = (uint16_t)(z * (WATER_TILE_DIV + 1) + x);
            uint16_t b = (uint16_t)(a + 1);
            uint16_t c = (uint16_t)(a + WATER_TILE_DIV + 1);
            uint16_t d = (uint16_t)(c + 1);
            inds[i++] = a; inds[i++] = c; inds[i++] = d;
            inds[i++] = a; inds[i++] = d; inds[i++] = b;
        }
    mesh_file_data md = {};
    md.vertex_count = (size_t)v; md.vertex_data = verts;
    md.index_count = (size_t)i;  md.index_data = inds;
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
    // No fallback for JUMP. Three of the four rigs ship no jump at all, and
    // the nearest thing they do have is a combat roll - which, played on a
    // loop for the length of a hop, is far worse than leaving the clip
    // unresolved and letting the player keep its locomotion pose. The player
    // deliberately picks a rig that does have one; see city_pick_player.
    /* JUMP */ { "Jump", "RunningJump", 0, 0 },
    /* WAVE */ { "Wave", "Clapping", "Interact", 0 },
    /* TALK */ { "Idle_Neutral", "Standing", "Interact", "Idle" },
    /* SIT  */ { "Sitting", "Idle", 0, 0 },
};

// Outfit tints. These multiply the rig's own palette rather than replacing it,
// so a character keeps its authored skin and hair while its clothes shift - a
// flat recolour of the whole model reads as a painted statue.
//
// Four rigs is not many to build a crowd out of, and the thing that gives a
// crowd away is not the number of faces, it is seeing the same silhouette in
// the same colour twice within a few metres. Twelve tints across four rigs is
// forty-eight visibly different people, and each one is a material clone over
// a texture that was already uploaded, so the whole set costs nothing but
// material slots. They stay near 1 in brightness and spread in hue instead,
// because a tint far from 1 darkens the skin along with the clothes.
static const vec3 CHAR_TINTS[CITY_MAX_CHAR_TINTS] = {
    { 1.00f, 1.00f, 1.00f },   // as authored
    { 0.82f, 0.88f, 1.05f },   // cool blue
    { 1.06f, 0.90f, 0.82f },   // warm rust
    { 0.86f, 1.02f, 0.90f },   // sage
    { 1.06f, 1.00f, 0.84f },   // sand
    { 0.94f, 0.86f, 1.04f },   // violet
    { 1.02f, 0.82f, 0.88f },   // dusty rose
    { 0.80f, 0.94f, 0.98f },   // slate
    { 0.90f, 0.90f, 0.90f },   // muted
    { 1.08f, 1.04f, 0.96f },   // bright
    { 0.86f, 0.82f, 0.78f },   // dark work clothes
    { 0.98f, 1.06f, 1.02f }    // pale mint
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

// Which rig the player gets. Only one of the four ships a real Jump clip (and
// the same one is the only one with a Sitting pose for being behind a wheel),
// so the player takes whichever rig is actually equipped for what the player
// can do; the crowd keeps using all of them.
static idx city_pick_player(const city_catalog& cat) {
    for (size_t i = 0; i < cat.character_count; i++)
        if (cat.characters[i].ok && cat.characters[i].clips[CLIP_JUMP] != (idx)-1) return (idx)i;
    return cat.character_count ? 0 : (idx)-1;
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
    cat.mat_building = clone_material(renderer, cat.mat_city, v3(1, 1, 1), 0.0f, 0.62f);
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

    // Ground. One flat green over a whole district is the other half of "the
    // grass looks off": real turf is never one colour, and at this cell size a
    // single tint turns every park and verge into a billiard table. Four shades
    // of the same green, handed out per cell by a hash of its coordinates, cost
    // three extra materials and break the sheet up without patterning.
    static const vec3 GRASS_SHADES[CITY_GRASS_SHADES] = {
        { 0.29f, 0.42f, 0.21f }, { 0.33f, 0.46f, 0.23f },
        { 0.26f, 0.38f, 0.20f }, { 0.35f, 0.45f, 0.26f }
    };
    for (int i = 0; i < CITY_GRASS_SHADES; i++)
        cat.mat_grass_shades[i] = load_material(renderer, 0, GRASS_SHADES[i], 0.0f, 1.0f);
    cat.mat_grass    = cat.mat_grass_shades[0];
    cat.mat_concrete = load_material(renderer, 0, v3(0.58f, 0.57f, 0.54f), 0.0f, 0.88f);
    // yards, car parks and service ground behind the shops
    cat.mat_asphalt  = load_material(renderer, 0, v3(0.30f, 0.30f, 0.31f), 0.0f, 0.90f);
    // the retaining wall the embankment is built out of
    cat.mat_stone    = load_material(renderer, 0, v3(0.44f, 0.43f, 0.40f), 0.0f, 0.92f);
    cat.mat_dirt     = load_material(renderer, 0, v3(0.32f, 0.28f, 0.22f), 0.0f, 1.0f);
    // The riverbed the water is seen through: dark and desaturated, because a
    // bright bed under a translucent surface is what makes shallow water read
    // as blue paint on mud instead of as depth.
    cat.mat_riverbed = load_material(renderer, 0, v3(0.14f, 0.16f, 0.13f), 0.0f, 1.0f);
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
    city__add_buildings(cat, loader, renderer, BUILDINGS, 8);

    // ---- Kenney suburban: yard dressing only, not buildings ----
    // Yard dressing. Paths and driveways come out right at the kit's own scale
    // (a path tile is 2 x 4 m, a drive 3.6 x 4 m), but the fences do not: at
    // that scale a garden fence is 2.7 m tall, taller than the house it belongs
    // to. Sizing them by length instead puts one cell's worth of fence at a
    // believable 1.7 m high.
    static const city_kit_item YARD[] = {
        { "fence-1x3",         CITY_TILE }, { "fence-1x4",        CITY_TILE * 1.5f },
        { "fence-2x3",         CITY_TILE }, { "planter",                     3.2f }
    };
    city__add_set_sized(cat, loader, renderer, SET_YARD, SUB_DIR, YARD, 4, cat.mat_suburban);
    if (cat.sets[SET_YARD].count) cat.singles[ONE_FENCE] = cat.sets[SET_YARD].first;

    static const char* PAVING[] = {
        "path-long", "path-stones-long", "path-stones-messy", "driveway-long"
    };
    city__add_set(cat, loader, renderer, SET_PAVING, SUB_DIR, PAVING, 4,
                  cat.mat_suburban, KIT_BUILDING_SCALE);
    if (cat.sets[SET_PAVING].count >= 4) {
        cat.singles[ONE_PATH]     = cat.sets[SET_PAVING].first;
        cat.singles[ONE_DRIVEWAY] = cat.sets[SET_PAVING].first + 3;
    }

    // ---- greenery ----
    //
    // Every entry below states how big the thing is in metres along its longest
    // axis, and the loader derives the scale from the model's own bounds. The
    // nature kit ranges from a 1.7 unit tree to a 0.14 unit tuft of grass in the
    // same folder, so no single per-kit multiplier can be right for both: the
    // one that sized the trees was making tufts of grass waist high and garden
    // bushes the size of cars. These are ordinary real-world sizes - a street
    // tree is 7-8 m, a garden bush is knee to waist high, grass is ankle high.
    static const city_kit_item SUB_TREES[] = { { "tree-large", 7.0f }, { "tree-small", 5.5f } };
    city__add_set_sized(cat, loader, renderer, SET_TREE_STREET, SUB_DIR, SUB_TREES, 2,
                        cat.mat_suburban);

    // ---- Kenney nature (no texture; its .mtl colours go through the palette) ----
    // Street trees are the narrow ones - a wide canopy on a 2 m verge overhangs
    // the carriageway and reads as a wood the street was cut through.
    static const city_kit_item STREET_TREES[] = {
        { "tree_thin",    7.5f }, { "tree_small",   5.5f }, { "tree_simple",  7.0f },
        { "tree_cone",    6.5f }, { "tree_tall",    8.5f }, { "tree_plateau", 6.0f }
    };
    city__extend_set_sized(cat, loader, renderer, SET_TREE_STREET, NAT_DIR, STREET_TREES, 6,
                           cat.mat_palette);

    // Palms and the autumn variants are gone: one palm in a temperate street of
    // pines is the loudest wrong note in the whole set, and a park of
    // half-turned trees beside a park of green ones reads as two seasons at
    // once rather than as variety.
    static const city_kit_item PARK_TREES[] = {
        { "tree_default",      9.0f }, { "tree_oak",           8.0f },
        { "tree_detailed",     8.5f }, { "tree_fat",           7.0f },
        { "tree_blocks",       7.5f }, { "tree_pineDefaultA", 10.0f },
        { "tree_pineTallA",   11.0f }, { "tree_pineRoundC",    8.0f },
        { "tree_pineSmallB",   6.0f }, { "tree_detailed_dark", 8.5f },
        { "tree_tall_dark",    9.5f }
    };
    city__add_set_sized(cat, loader, renderer, SET_TREE_PARK, NAT_DIR, PARK_TREES, 11,
                        cat.mat_palette);

    // grass_leafs, plant_flatTall and crops_leafsStageB are modelled as a couple
    // of flat crossed planes, which at any size reads as a green arrow stuck in
    // the ground rather than as a plant. Everything left here has real volume.
    static const city_kit_item SHRUBS[] = {
        { "plant_bush",         1.4f }, { "plant_bushDetailed", 1.7f },
        { "plant_bushLarge",    1.5f }, { "plant_bushSmall",    1.0f },
        { "plant_bushTriangle", 1.2f }, { "grass",              0.8f },
        { "grass_large",        1.0f }, { "flower_redA",        0.5f },
        { "flower_yellowB",     0.4f }, { "flower_purpleC",     0.4f },
        { "mushroom_redGroup",  0.3f }
    };
    city__add_set_sized(cat, loader, renderer, SET_SHRUB, NAT_DIR, SHRUBS, 11,
                        cat.mat_palette);

    static const city_kit_item ROCKS[] = {
        { "rock_smallA",  1.1f }, { "rock_smallC",          0.9f },
        { "rock_largeB",  2.6f }, { "rock_tallD",           2.0f },
        { "stone_smallB", 1.0f }, { "stone_largeE",         2.8f },
        { "log",          2.2f }, { "log_stack",            2.4f },
        { "stump_round",  1.1f }, { "stump_squareDetailed", 1.3f }
    };
    city__add_set_sized(cat, loader, renderer, SET_ROCK, NAT_DIR, ROCKS, 10, cat.mat_palette);

    static const city_kit_item FEATURES[] = {
        { "statue_column",    4.0f }, { "statue_obelisk",   3.6f },
        { "statue_ring",      2.8f }, { "statue_block",     1.6f },
        { "pot_large",        1.4f }, { "pot_small",        1.0f },
        { "campfire_stones",  1.5f }, { "sign",             1.8f },
        { "path_stoneCircle", 2.6f }, { "tent_smallClosed", 2.4f },
        { "canoe",            4.2f }, { "lily_large",       0.9f }
    };
    city__add_set_sized(cat, loader, renderer, SET_PARK_FEATURE, NAT_DIR, FEATURES, 12,
                        cat.mat_palette);

    // ---- Kenney rail: a freight line along the city's edge ----
    // Track is sized so one tile is exactly one cell, which is what lets the
    // line be laid a cell at a time with no gaps and no overlap.
    static const city_kit_item RAILROAD[] = {
        { "railroad-straight", CITY_TILE }, { "railroad-corner-large", CITY_TILE }
    };
    city__add_set_sized(cat, loader, renderer, SET_RAILROAD, RAIL_DIR, RAILROAD, 2,
                        cat.mat_rail);

    // Rolling stock by width rather than length: the kit's carriages are short
    // and fat, and sizing them by length gives a six metre wide boxcar.
    static const city_kit_item TRAIN[] = {
        { "train-locomotive-a",           7.4f }, { "train-carriage-box",            7.0f },
        { "train-carriage-container-red", 7.0f }, { "train-carriage-container-blue", 7.0f },
        { "train-carriage-tank",          7.0f }, { "train-carriage-lumber",         7.0f },
        { "train-carriage-flatbed",       7.0f }, { "train-carriage-coal",           7.0f }
    };
    city__add_set_sized(cat, loader, renderer, SET_TRAIN, RAIL_DIR, TRAIN, 8, cat.mat_rail);

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
    cat.singles[ONE_WATER_TILE] =
        city__register_generated(cat, city__make_water_tile(renderer), cat.mat_water);
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
