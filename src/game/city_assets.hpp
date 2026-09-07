#pragma once
#include <stdio.h>
#include <string.h>
#include "../core/renderer.hpp"
#include "../core/animation.hpp"
#include "../loader/data_loader.hpp"
#include "city_config.hpp"
#include "city_anim.hpp"

// The art catalogue. Everything the city can place is loaded once into a flat
// table of `city_model`s; the generator then works entirely in terms of *sets*
// - "a downtown tower", "a park tree", "a kerbside bin" - and never names a
// file. Adding a model to a kit is a one line change in the tables below and
// it starts appearing in the world.
//
// Models are loaded in set order so a set is just a contiguous run, which makes
// "pick a random one of these" a single modulo.

#define CITY_MAX_MODELS 650
#define CITY_GRASS_SHADES 4

// asset roots
#define KAY_DIR    "assets/city/kaykit/"
#define SUB_DIR    "assets/city/suburban/"
// Non-Kenney packs (mostly Quaternius, a couple of pieces by other authors -
// see each glb's own name) that replace or extend the kits above; every one
// of these is loaded through the glTF path, not the OBJ one.
// characters share CHAR_DIR with the rigs already there - no separate root needed
#define NEW_NATURE_DIR "assets/nature/"
#define NEW_PATHS_DIR  "assets/paths/"
#define NEW_ROAD_DIR   "assets/road_objects/"
#define NEW_HOUSE_DIR  "assets/house/"
#define NEW_PARK_DIR   "assets/park/"
#define NEW_BRIDGE_DIR "assets/bridge/"
#define NEW_FARM_DIR   "assets/farm/"
#define NEW_GRAVE_DIR  "assets/grave/"
#define NEW_CARS_DIR   "assets/cars/"
#define VEH_DIR    "assets/city/vehicles/"
#define NAT_DIR    "assets/city/nature/"
#define RAIL_DIR   "assets/city/rail/"
#define FURN_DIR   "assets/interior/furniture/"
#define CHAR_DIR   "assets/characters/"
#define ANIMAL_DIR "assets/animals/"
#define ARMS_DIR   "assets/arms/"

// ---- the cast ----
//
// Each of these rigs is authored as several meshes (body, head, legs, feet)
// over one armature, with its colours in per-material baseColorFactors rather
// than a texture. The glTF loader merges the meshes and bakes those colours
// into a palette image, so a whole character is one skinned draw.
#define CITY_MAX_CHARACTERS 14
#define CITY_MAX_CHAR_TINTS 20
// Dogs today; anything else on four legs later. Kept in the same table type as
// the people because a rig is a rig - see city__load_actor.
#define CITY_MAX_ANIMALS 4

// The clips the game asks for by role.
//
// A rig resolves each role in three steps, in this order:
//
//   1. its own clips, by name (CLIP_NAMES)
//   2. the same role borrowed off another rig in the cast and retargeted onto
//      this skeleton - see city_anim.hpp and city__share_clips. This is what
//      gives the six rigs that ship no jump at all a real jump, instead of
//      the game having to pick its player character by who owns one clip.
//   3. a related role this rig does have (CLIP_FALLBACK): a run stands in for
//      a walk, an idle for a talk.
//
// Anything still unresolved after all three is left as (idx)-1 and the systems
// that use it skip that behaviour outright, which is always better than
// playing whatever clip happened to be first in the file - a pedestrian
// walking around in its Death pose is exactly what that mistake looks like.
enum city_clip {
    CLIP_IDLE,      // standing, at rest
    CLIP_WALK,
    CLIP_RUN,
    CLIP_JUMP,
    CLIP_WAVE,      // greeting a passer-by
    CLIP_TALK,      // a second standing pose, so a crowd is not one silhouette
    CLIP_SIT,
    CLIP_PUNCH,     // the player's own attack swing
    CLIP_HIT,       // a pedestrian staggering from a punch or a light bump
    CLIP_DEATH,     // held on its last frame once it finishes - see city_ped_hit
    // Holding a gun. These four are what turn a pistol from a prop stuck to a
    // hand into a weapon: the rig carries it at its side while walking, brings
    // it up to point when the player aims, kicks when it fires, and keeps it
    // up while running. Quaternius authored every one of them on the
    // twenty-four clip rigs; the shorter rigs fall back through GUN_IDLE to
    // the ordinary idle, which reads as somebody holding something rather
    // than as somebody armed, and is the right answer for a rig with no
    // firearm poses at all.
    CLIP_GUN_IDLE,  // "Idle_Gun"      - stood holding it, muzzle down
    CLIP_GUN_AIM,   // "Idle_Gun_Poin" - brought up and pointed
    CLIP_GUN_SHOOT, // "Gun_Shoot"     - the shot itself
    CLIP_GUN_RUN,   // "Run_Shoot"     - running with it up
    CLIP_COUNT
};

struct city_character {
    idx    model;                          // index into loader.model_files
    idx    mesh;                           // skinned mesh in the renderer
    idx    materials[CITY_MAX_CHAR_TINTS]; // palette + a tint per variant
    size_t material_count;
    float  scale;                          // rig units -> target height in metres
    float  yaw_offset;                     // rig's facing vs our -Z convention
    idx    clips[CLIP_COUNT];              // index into an animator's clip list
    // Roles filled in from another rig, as indices into
    // pix_data_loader::animation_clips. city_build_character_animator appends
    // these after the rig's own clips, which is what lets clips[] above
    // address a borrowed clip exactly like an authored one.
    idx    borrowed[CITY_MAX_BORROWED];
    size_t borrowed_count;
    bool   ok;
};

enum city_set {
    SET_ROAD,          // straight, corner, tsplit, junction, crossing, curved corner
    SET_BUILDING,      // KayKit's building_A..H - every occupied lot, any zone
    // Kenney's suburban kit: twenty-one detached houses, each one a whole
    // home rather than a block face. The downtown kit above cannot do a
    // suburb - a suburb of shopfront towers is what the outer ring used to
    // look like - and these cannot do a downtown, so the generator picks by
    // zone (see city__place_buildings).
    SET_HOUSE,
    // The blocks that make a downtown: whole apartment buildings and shops,
    // one model per lot, from assets/house. These replace the KayKit
    // building_A..H terraces as the city's main building stock - those are one
    // tileable block face each, eight shapes in five colours, and a city
    // built entirely out of them reads as one building repeated however the
    // facades are shuffled. The kit is still loaded and still used, but now
    // as the infill between these rather than as the whole city.
    SET_TOWER,
    SET_YARD,          // fences and planters
    SET_PAVING,        // garden paths and driveways, flat on the ground
    SET_STREET,        // lamps, lights, bins, hydrants, benches, boxes
    SET_TREE_STREET,   // narrow enough to stand on a pavement
    SET_TREE_PARK,     // anything goes
    SET_SHRUB,         // bushes, grass tufts, flowers
    SET_ROCK,          // rocks, stones, logs, stumps
    SET_PARK_FEATURE,  // statues, pots, campfires, tents
    // Playground equipment. Its own set rather than more park features
    // because it is placed as a group - a slide, a swing and a climbing frame
    // standing together on one piece of ground is a playground, and the same
    // three scattered a block apart are litter.
    SET_PLAYGROUND,
    // Everything that runs along a line rather than standing on a spot: low
    // stone walls, park railings, yard fences. Placed by city__run_barrier,
    // which is what knows how to lay a model end to end down a cell edge.
    SET_WALL,
    // Docks, jetties and moored boats: what a waterfront has on it, as
    // opposed to what a park does.
    SET_WATERFRONT,
    // One-per-district pieces big enough to navigate by - a church, a radio
    // mast, a barn. Placed at most once per block, deliberately: a landmark
    // repeated down a street is not a landmark.
    SET_LANDMARK,
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
    // the waterfront and the bridges over it
    ONE_BRIDGE_ARCH, ONE_DOCK, ONE_BOAT,
    // park furniture, and the low wall that edges a park
    ONE_PARK_BENCH, ONE_WALL,
    ONE_QUAD, ONE_SLAB, ONE_BRIDGE, ONE_PIER, ONE_WATER_TILE,
    // house interiors - see city_house.hpp. The shell (wall/floor/ceiling) is
    // the same generated slab as the bridge deck, just re-skinned; the rest
    // are real Kenney furniture-kit pieces, sized off their own bounds like
    // every other untextured kit in this catalogue.
    ONE_INTERIOR_WALL, ONE_INTERIOR_FLOOR, ONE_INTERIOR_CEILING,
    ONE_BED, ONE_SOFA, ONE_DINING_TABLE, ONE_DINING_CHAIR, ONE_COFFEE_TABLE,
    ONE_BOOKCASE, ONE_TV_CABINET, ONE_TV, ONE_KITCHEN_COUNTER, ONE_KITCHEN_UPPER,
    ONE_FRIDGE, ONE_STOVE, ONE_SIDE_TABLE, ONE_FLOOR_LAMP, ONE_RUG, ONE_PLANT,
    ONE_COUNT
};

// How many extra textured parts one model can be built out of. A tree is two
// - bark and leaves - but the new building set is painted with as many as six
// (walls, roof, glass, door, sill, gutter), and a model is loaded once per
// material: at three, the last two materials of a building were simply
// dropped, which shows up as a hole where its windows should be.
#define CITY_MODEL_PARTS 6

struct city_model {
    idx  mesh;
    idx  material;
    // The rest of the model, when it is painted with more than one texture.
    //
    // A glTF model with two materials cannot be drawn as one textured mesh:
    // whichever texture is picked, the other half of the model wears it. That
    // is what put tiled birch bark all over every tree's canopy. So a model
    // like that is loaded once per material and the extra pieces ride along
    // here, drawn at the same transform as the first - see city__prop.
    idx  part_mesh[CITY_MODEL_PARTS];
    idx  part_material[CITY_MODEL_PARTS];
    int  part_count;
    vec3 bounds_min;
    vec3 bounds_max;
    float scale;        // kit -> world, already folded in by the generator
    // Which way this particular file was authored to face, as a turn to add
    // to the yaw the generator asks for.
    //
    // Nothing in a glTF says where a model front is, and this art set does not
    // agree with itself: the KayKit block faces and every new building put
    // their facade on +Z, the Kenney suburban kit and the church put theirs on
    // -Z, and one house is a quarter turn off both. Left to a rule at the call
    // site this is a guess that is wrong for half the catalogue and shows up
    // as blank gables facing the street with front doors opening into next
    // door garden. Measured per file with PIX_SHOWROOM and recorded here.
    float yaw_offset;
    // What this model actually is, for anything that has to show a human a
    // model rather than an index - the collider editor above all, where
    // "model 274 of 361" is not something anybody can act on.
    char name[40];
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

// ---- firearms ----
//
// Three of them, and they are deliberately not variations on one gun: a
// revolver that hits hard six times, a pistol that is the everyday one, and a
// rifle that is fully automatic. Everything that separates them - how fast
// they fire, how far they carry, how much they kick, how wide they throw - is
// a number here rather than a branch anywhere in the shooting code.
//
// Every one of these files is authored the same way - the grip at the origin,
// the barrel down +X, the sights up +Y - which is what lets one set of numbers
// place all three in a hand. `grip` is the last of the offset from the wrist
// to where the gun's origin has to sit, in metres, expressed in the gun's own
// frame (x along the barrel, y up, z to the right); `muzzle` is how far along
// +X the barrel ends, which is where the flash goes and where a shot starts.
// Both are measured off the model's own bounds at the size it is loaded at,
// not guessed - see tools and the bounds printed by dump_assets.
enum city_weapon_kind {
    WEAPON_PISTOL,
    WEAPON_REVOLVER,
    WEAPON_RIFLE,
    WEAPON_COUNT
};

struct city_weapon {
    idx    model;          // index into city_catalog::models, (idx)-1 if the file was missing
    const char* name;
    float  damage;
    float  rate;           // seconds between shots
    float  range;          // metres
    float  spread;         // radians of cone at the muzzle, hip fired
    float  aim_spread;     // and the same while aiming down the arm
    float  recoil;         // radians the camera is kicked up per shot
    float  knockback;      // metres per second imparted to what it hits
    int    magazine;       // rounds before a reload
    float  reload_time;
    bool   automatic;      // holding the trigger keeps firing
    vec3   grip;           // wrist -> gun origin, in bone space
    float  muzzle;         // gun origin -> barrel end, along the gun's own forward
    float  size;           // longest axis in metres, what the model is scaled to
};

struct city_catalog {
    city_model models[CITY_MAX_MODELS];
    size_t     model_count;

    city_set_range sets[SET_COUNT];
    // The small stuff inside SET_STREET: bins, cones, crates, a mailbox.
    //
    // SET_STREET holds two different kinds of thing. Most of it is clutter
    // that can be dropped anywhere along a kerb, but the first entries are
    // street lighting and traffic signals - things that stand three to five
    // metres tall on their own pole and are placed deliberately, one to a
    // cell, at a spacing that has to read as regular. Picking at random from
    // the whole set puts a second signal head inside the lamp post it lands
    // next to, which is exactly what it looks like.
    city_set_range street_clutter;
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
    // house interiors - plain, untextured, distinct from every outdoor
    // material so a room reads as indoors rather than as more pavement
    idx mat_interior_wall, mat_interior_floor, mat_interior_ceiling;
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

    // Every material that paints a building, in one list.
    //
    // Two things are done to all of them together and to nothing else: their
    // windows are glazed (material::glass) and their windows light up after
    // dark (material::window_glow, set per frame by city__submit_lights). One
    // atlas covers all of the KayKit blocks and one covers the whole suburban
    // kit, but each new-pack building brought its own, so this cannot be a
    // single named material any more.
    idx    building_materials[48];
    size_t building_material_count;

    // ---- what can be picked up and fired ----
    city_weapon weapons[WEAPON_COUNT];

    // the animated cast
    city_character characters[CITY_MAX_CHARACTERS];
    size_t         character_count;
    // Strays and pets - loaded through exactly the same path as the people
    // (city__load_actor), so anything that can draw a pedestrian can draw one
    // of these. See city_animals.hpp.
    city_character animals[CITY_MAX_ANIMALS];
    size_t         animal_count;

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


// The name a model is shown under. Asset filenames in the newer packs carry
// their author and an upload hash - "Pine by Quaternius - 699sFuLCN2.glb" -
// and neither half of that tail helps anybody pick a model out of a list, so
// what is kept is the part before " by ", or the bare stem for a kit piece.
static void city__model_name(city_model& m, const char* raw) {
    const char* base = raw;
    for (const char* p = raw; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;

    size_t n = 0;
    for (; base[n] && n + 1 < sizeof(m.name); n++) {
        if (base[n] == ' ' && base[n + 1] == 'b' && base[n + 2] == 'y' && base[n + 3] == ' ') break;
        if (base[n] == '.' && (base[n + 1] == 'o' || base[n + 1] == 'g')) break;   // .obj / .glb
        m.name[n] = base[n];
    }
    m.name[n] = 0;
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
    city__model_name(m, name);
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

// ---- glTF props (the non-Kenney packs: characters aside, everything under
// assets/{nature,paths,road_objects,cars}) ----
//
// These ship one real UV-mapped texture per file instead of one shared atlas
// per kit, so unlike every loader above, a model here brings its own
// material rather than being handed one. And unlike an OBJ prop, a glTF file
// always parses into the *rigged* vertex format (see gltf_load_file) even
// when nothing in it is animated - a plain instanced prop needs the ordinary
// one, so the position/normal/uv are copied across and the bone data is
// dropped on the floor here rather than carried around forever for props
// that will never read it.
// One textured piece of a glTF prop: the primitives painted with a single
// material, uploaded as a mesh plus the material's own texture. Returns false
// when that material contributes no geometry to this node.
//
// `bounds` is grown across every piece, so a two-material tree is sized by
// the whole tree rather than by whichever half happened to load first.
static bool city__add_gltf_piece(pix_data_loader& loader, pix_renderer& renderer,
                                 const char* path, int node, int material_index,
                                 float metallic, float roughness,
                                 idx* out_mesh, idx* out_material, vec3* mn, vec3* mx) {
    // Everything parsed here is thrown away again at the bottom - see
    // pix_loader_mark. A prop is a mesh handle, a material handle and a box;
    // none of that is the file it came from.
    pix_loader_mark mark = pix_loader_mark_now(loader);
    idx file = load_model_gltf_file(loader, path, node, material_index);
    if (file == (idx)-1) { pix_loader_rewind(loader, mark); return false; }

    skinned_mesh_file_data* md = get_model_mesh(loader, file);
    if (!md || !md->vertex_count || md->vertex_count > 65535) {
        pix_loader_rewind(loader, mark);
        return false;
    }

    static vertex verts[65536];
    for (size_t i = 0; i < md->vertex_count; i++) {
        const vertex_rigged& src = md->vertex_data[i];
        verts[i].position = src.position;
        verts[i].normal = src.normal;
        // glTF uv is top-left origin already; VSHDER_BASIC (unlike VSHDER_SKINNED)
        // flips V assuming OBJ's bottom-left origin, so flip here to cancel that out.
        verts[i].uv = v2(src.uv.x, 1.0f - src.uv.y);
        vec3 p = src.position;
        if (p.x < mn->x) mn->x = p.x;
        if (p.y < mn->y) mn->y = p.y;
        if (p.z < mn->z) mn->z = p.z;
        if (p.x > mx->x) mx->x = p.x;
        if (p.y > mx->y) mx->y = p.y;
        if (p.z > mx->z) mx->z = p.z;
    }

    mesh_file_data mfd = {};
    mfd.vertex_count = md->vertex_count;
    mfd.vertex_data = verts;
    mfd.index_count = md->index_count;
    mfd.index_data = md->index_data;
    idx mesh = load_mesh(renderer, mfd);
    if (mesh == (idx)-1) { pix_loader_rewind(loader, mark); return false; }

    const model_file_data& mf = loader.model_files[file];
    idx material = load_material_image(renderer,
        mf.image >= 0 ? &loader.image_files[mf.image] : 0,
        v3(1.0f, 1.0f, 1.0f), metallic, roughness, mf.image_is_palette);

    // both the mesh and the texture are on the GPU now; the parse can go
    pix_loader_rewind(loader, mark);
    *out_mesh = mesh;
    *out_material = material;
    return true;
}

// A whole glTF prop, however many textures it was painted with.
//
// Most of this art set is one material per file and comes out as a single
// mesh, exactly as it always did. The ones that are not - trees, which are
// bark and leaves, and a few props with a glass or a metal part - are loaded
// once per material and drawn together at one transform. Trying to serve
// those with one texture is what put tiled birch bark across every canopy in
// the city and made the trees look like broken geometry.
static idx city__add_model_gltf(city_catalog& cat, pix_data_loader& loader, pix_renderer& renderer,
                                const char* path, int node, float target_size,
                                float metallic = 0.02f, float roughness = 0.85f) {
    if (cat.model_count >= CITY_MAX_MODELS) return (idx)-1;

    int materials[CITY_MODEL_PARTS + 1];
    pix_loader_mark list_mark = pix_loader_mark_now(loader);
    int material_count = gltf_list_materials(loader.arena, path, node, materials,
                                             CITY_MODEL_PARTS + 1);
    pix_loader_rewind(loader, list_mark);

    vec3 mn = v3(1e30f, 1e30f, 1e30f), mx = v3(-1e30f, -1e30f, -1e30f);
    idx mesh[CITY_MODEL_PARTS + 1], material[CITY_MODEL_PARTS + 1];
    int pieces = 0;

    if (material_count <= 1) {
        // one material, or none named at all: load the file whole, which is
        // also the only path that works for a file whose primitives carry no
        // material index
        if (city__add_gltf_piece(loader, renderer, path, node, -1, metallic, roughness,
                                 &mesh[0], &material[0], &mn, &mx)) pieces = 1;
    } else {
        for (int i = 0; i < material_count && pieces <= CITY_MODEL_PARTS; i++)
            if (city__add_gltf_piece(loader, renderer, path, node, materials[i],
                                     metallic, roughness, &mesh[pieces], &material[pieces],
                                     &mn, &mx)) pieces++;
    }
    if (!pieces) return (idx)-1;

    idx id = (idx)cat.model_count++;
    city_model& m = cat.models[id];
    m.mesh = mesh[0];
    m.material = material[0];
    m.part_count = 0;
    for (int i = 1; i < pieces; i++) {
        m.part_mesh[m.part_count] = mesh[i];
        m.part_material[m.part_count] = material[i];
        m.part_count++;
    }
    m.bounds_min = mn;
    m.bounds_max = mx;
    vec3 e = v3sub(mx, mn);
    float longest = e.x > e.y ? (e.x > e.z ? e.x : e.z) : (e.y > e.z ? e.y : e.z);
    m.scale = longest > 1e-4f ? target_size / longest : 1.0f;
    city__model_name(m, path);
    return id;
}

#define GLTF_GROUP_MAX 16

// A "family" file - several related props sharing one glb, each its own
// named node (five birch trees standing in a row is the case this project
// actually ships; see gltf_load_file's `only_node` for why loading the file
// whole would merge all five into one). Every member is added to `set` at
// the same `target_size`, which is exactly right for a set of size variants
// of "the same kind of thing" and only approximately right for anything more
// varied - good enough for the tree and shrub families here, which is all
// this is used for.
static void city__add_model_gltf_group(city_catalog& cat, pix_data_loader& loader,
                                       pix_renderer& renderer, int set, const char* path,
                                       float target_size, float metallic = 0.02f,
                                       float roughness = 0.85f) {
    gltf_node_info nodes[GLTF_GROUP_MAX];
    pix_loader_mark list_mark = pix_loader_mark_now(loader);
    int n = gltf_list_nodes(loader.arena, path, nodes, GLTF_GROUP_MAX);
    pix_loader_rewind(loader, list_mark);   // names are copied out; the parse is not needed
    if (!cat.sets[set].count) cat.sets[set].first = (idx)cat.model_count;
    for (int i = 0; i < n; i++) {
        // A file with exactly one mesh node is not a family, it is a single
        // prop, and passing -1 (whole file) instead of that lone node index
        // is what lets city_load_collider_bounds/city_editor rely on every
        // model but a family member coming from a load that kept the file's
        // own root transform rather than one this function stripped for it.
        int only = (n == 1) ? -1 : nodes[i].node;
        if (city__add_model_gltf(cat, loader, renderer, path, only, target_size, metallic, roughness)
            != (idx)-1)
            cat.sets[set].count++;
    }
}

struct city_gltf_item { const char* file; float size; };

// One glTF file added to a set as a single model, however many mesh nodes it
// happens to have. The group loader above is the right default - most
// multi-node files in this art set really are families of separate props -
// but a file that is one object split into parts needs the opposite rule, and
// nothing in the file itself says which it is.
static idx city__add_set_gltf_whole(city_catalog& cat, pix_data_loader& loader,
                                    pix_renderer& renderer, int set, const char* dir,
                                    const char* file, float target_size,
                                    float facing_degrees = 0.0f) {
    char path[512];
    snprintf(path, sizeof(path), "%s%s", dir, file);
    if (!cat.sets[set].count) cat.sets[set].first = (idx)cat.model_count;
    idx id = city__add_model_gltf(cat, loader, renderer, path, -1, target_size);
    if (id == (idx)-1) return id;
    cat.sets[set].count++;
    cat.models[id].yaw_offset = facing_degrees * 0.01745329f;
    return id;
}

// every model in a set authored the same way round
static void city__face_set(city_catalog& cat, int set, float facing_degrees) {
    for (idx i = cat.sets[set].first; i < cat.sets[set].first + cat.sets[set].count; i++)
        cat.models[i].yaw_offset = facing_degrees * 0.01745329f;
}

// city__add_model_gltf_group already tracks a set's first/count correctly
// across repeated calls (see there), so replacing a whole Kenney set with a
// table of these is exactly the add_set_sized loop above, just over exact
// filenames instead of a directory + bare name, and splitting any file that
// turns out to be a family of props rather than one.
static void city__add_set_gltf(city_catalog& cat, pix_data_loader& loader, pix_renderer& renderer,
                               int set, const char* dir, const city_gltf_item* items, int count) {
    for (int i = 0; i < count; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s%s", dir, items[i].file);
        city__add_model_gltf_group(cat, loader, renderer, set, path, items[i].size);
    }
}

// Rescales a whole set so each model's *footprint* fills `tiles` cells,
// keeping its own proportions. Sizing a building by its longest axis is what
// turns a three storey townhouse into a shed standing next to a bungalow that
// covers its neighbour's garden: what has to fit on a lot is the plan, and the
// height that comes with it is the model's own business.
static void city__fit_set_to_plan(city_catalog& cat, int set, float tiles) {
    for (idx i = cat.sets[set].first; i < cat.sets[set].first + cat.sets[set].count; i++) {
        city_model& m = cat.models[i];
        vec3 e = v3sub(m.bounds_max, m.bounds_min);
        float plan = e.x > e.z ? e.x : e.z;
        if (plan > 1e-4f) m.scale = (CITY_TILE * tiles) / plan;
    }
}

// ---- what a thing is made of ----
//
// Every glTF prop is loaded with one finish - near-dielectric, fairly rough -
// because that is the safe default for a folder that is mostly painted wood,
// plastic and stone. It is wrong for the half of the street that is metal: a
// lamp post, a hydrant, a wrought iron railing and a mailbox all read as matte
// grey plastic under it, and metal is exactly the material that this renderer
// has something interesting to say about, since it mirrors the sky it stands
// under and picks up every lamp near it after dark.
//
// Applied after loading rather than at it, so the table below reads as what it
// is - a list of which props are metal - instead of being smeared through the
// catalogue as two extra numbers on every line.
//
// Safe to do per model because a glTF prop owns its material: load_material_image
// makes a new one per file. It would not be safe for the Kenney and KayKit
// kits, which share one atlas material across a whole kit - hence matching on
// the capitalised names the new packs use, and only within the sets that hold
// them.
struct city_finish { const char* name; float metallic; float roughness; };

static void city__finish_set(city_catalog& cat, pix_renderer& renderer, int set,
                             const city_finish* table, int count) {
    for (idx i = cat.sets[set].first; i < cat.sets[set].first + cat.sets[set].count; i++) {
        const city_model& m = cat.models[i];
        for (int k = 0; k < count; k++) {
            if (strcmp(m.name, table[k].name) != 0) continue;
            if (m.material < renderer.material_count) {
                renderer.materials[m.material].metallic  = table[k].metallic;
                renderer.materials[m.material].roughness = table[k].roughness;
            }
            for (int part = 0; part < m.part_count; part++)
                if (m.part_material[part] < renderer.material_count) {
                    renderer.materials[m.part_material[part]].metallic  = table[k].metallic;
                    renderer.materials[m.part_material[part]].roughness = table[k].roughness;
                }
            break;
        }
    }
}

// the same, for one model the generator holds by index rather than by name
static void city__finish_model(city_catalog& cat, pix_renderer& renderer, idx model,
                               float metallic, float roughness) {
    const city_model* m = city_get(cat, model);
    if (!m) return;
    if (m->material < renderer.material_count) {
        renderer.materials[m->material].metallic  = metallic;
        renderer.materials[m->material].roughness = roughness;
    }
    for (int part = 0; part < m->part_count; part++)
        if (m->part_material[part] < renderer.material_count) {
            renderer.materials[m->part_material[part]].metallic  = metallic;
            renderer.materials[m->part_material[part]].roughness = roughness;
        }
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
            snprintf(m.name, sizeof(m.name), "%s facade %d", names[i], f);
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

static idx city__register_generated(city_catalog& cat, idx mesh, idx material,
                                    const char* name = "generated") {
    if (cat.model_count >= CITY_MAX_MODELS) return (idx)-1;
    idx id = (idx)cat.model_count++;
    city_model& m = cat.models[id];
    m.mesh = mesh;
    m.material = material;
    snprintf(m.name, sizeof(m.name), "%s", name);
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
    // The HumanArmature rigs name every clip for whoever they were exported
    // as - "Man_Walk", "Female_Walk" - for the same eleven clips on the same
    // skeleton. Miss one of these prefixes and the rig resolves none of its
    // own clips, then borrows its whole locomotion off a 62-bone rig that
    // shares only half its bone names, which is what tears the mesh apart.
    if (strncmp(name, "Man_", 4) == 0) name += 4;
    else if (strncmp(name, "Female_", 7) == 0) name += 7;
    else if (strncmp(name, "Woman_", 6) == 0) name += 6;
    return name;
}

// What each role is actually *called*, across every rig in the folder - the
// six-strong Quaternius "CharacterArmature" cast, the older "Man" rig whose
// names all carry a "Man_" prefix, the Mixamo-style woman who spells her
// locomotion "Walking" and "Running", and the animal rigs whose run is a
// "Gallop". The first name a rig ships wins; these are all the same role, not
// a preference ladder of near-misses.
#define CLIP_PREFS 5

static const char* const CLIP_NAMES[CLIP_COUNT][CLIP_PREFS] = {
    /* IDLE  */ { "Idle", "Idle_Neutral", "Standing", "Idle_2", 0 },
    /* WALK  */ { "Walk", "Walking", 0, 0, 0 },
    /* RUN   */ { "Run", "Running", "Gallop", "Sprint", 0 },
    /* JUMP  */ { "Jump", "Jump2", "RunningJump", "Gallop_Jump", 0 },
    /* WAVE  */ { "Wave", "Clapping", "PickUp", 0, 0 },
    /* TALK  */ { "Idle_Neutral", "Standing", "Interact", "Idle_2", 0 },
    /* SIT   */ { "Sitting", "SitIdle", 0, 0, 0 },
    /* PUNCH */ { "Punch_Right", "Punch_Left", "Punch", "Attack", 0 },
    /* HIT   */ { "HitRecieve", "HitReact", "HitRecieve_2", "Idle_HitReact_Left", 0 },
    /* DEATH */ { "Death", 0, 0, 0, 0 },
    // The clip names in these files are "CharacterArmature|Idle_Gun_Point"
    // and the loader's name field is 32 characters, so what actually survives
    // the parse is the truncated "Idle_Gun_Poin". Matching the untruncated
    // name here would silently match nothing at all, which is why these two
    // are spelled the way they are.
    /* GUNIDLE */ { "Idle_Gun", 0, 0, 0, 0 },
    /* GUNAIM  */ { "Idle_Gun_Poin", "Idle_Gun_Point", 0, 0, 0 },
    /* GUNFIRE */ { "Gun_Shoot", "Idle_Gun_Shoo", "Idle_Gun_Shoot", 0, 0 },
    /* GUNRUN  */ { "Run_Shoot", 0, 0, 0, 0 },
};

// The role to stand in with when neither this rig nor any other in the cast
// can supply the real thing. -1 means "do without": a jump faked out of a
// combat roll, or a hit reaction faked out of a walk cycle, reads worse than
// the behaviour simply not playing.
static const int CLIP_FALLBACK[CLIP_COUNT] = {
    /* IDLE  */ -1,          // a rig with no standing pose at all is broken
    /* WALK  */ CLIP_RUN,
    /* RUN   */ CLIP_WALK,
    /* JUMP  */ -1,
    /* WAVE  */ CLIP_IDLE,
    /* TALK  */ CLIP_IDLE,
    /* SIT   */ CLIP_IDLE,
    /* PUNCH */ -1,
    /* HIT   */ -1,
    /* DEATH */ -1,
    // Resolved in this order, so each of the three below can lean on the one
    // above it having already been settled: a rig with no firearm poses ends
    // up holding the gun in its ordinary idle rather than not holding it.
    /* GUNIDLE */ CLIP_IDLE,
    /* GUNAIM  */ CLIP_GUN_IDLE,
    /* GUNFIRE */ CLIP_GUN_AIM,
    /* GUNRUN  */ CLIP_RUN,
};

// Outfit tints. These multiply the rig's own palette rather than replacing it,
// so a character keeps its authored skin and hair while its clothes shift - a
// flat recolour of the whole model reads as a painted statue.
//
// Eight rigs is not many to build a crowd out of, and the thing that gives a
// crowd away is not the number of faces, it is seeing the same silhouette in
// the same colour twice within a few metres. Twenty tints across eight rigs
// is a hundred and sixty visibly different people, and each one is a material
// clone over a texture that was already uploaded, so the whole set costs
// nothing but material slots.
//
// They spread much wider in hue than they used to and stay balanced in
// brightness. The old set moved every channel by five or ten percent, which
// on a stylised palette is not a different coat, it is the same coat under
// slightly different weather - a street of them still read as one outfit.
// What keeps skin from going with the clothes is that the sum of the three
// channels stays near three, so a tint pushes colour around rather than
// darkening everything it touches.
static const vec3 CHAR_TINTS[CITY_MAX_CHAR_TINTS] = {
    { 1.00f, 1.00f, 1.00f },   // as authored
    { 0.66f, 0.80f, 1.16f },   // cool blue
    { 1.20f, 0.80f, 0.64f },   // warm rust
    { 0.70f, 1.10f, 0.80f },   // sage
    { 1.18f, 1.04f, 0.66f },   // sand
    { 0.90f, 0.72f, 1.18f },   // violet
    { 1.16f, 0.70f, 0.84f },   // dusty rose
    { 0.66f, 0.90f, 1.02f },   // slate
    { 0.78f, 0.78f, 0.80f },   // muted grey
    { 1.16f, 1.12f, 0.98f },   // bright
    { 0.70f, 0.64f, 0.58f },   // dark work clothes
    { 0.88f, 1.16f, 1.06f },   // pale mint
    { 1.10f, 0.62f, 0.58f },   // brick red
    { 0.62f, 0.72f, 0.62f },   // olive
    { 1.06f, 0.94f, 1.14f },   // lilac
    { 0.58f, 0.86f, 0.90f },   // teal
    { 1.14f, 0.88f, 0.58f },   // ochre
    { 0.94f, 0.60f, 0.74f },   // magenta
    { 0.60f, 0.66f, 0.86f },   // navy
    { 1.04f, 1.10f, 0.72f }    // lime
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

// The same, plus whatever this rig borrowed off the rest of the cast. The
// borrowed clips go on the end, in the order they were taken, because
// city__share_clips numbered them that way when it filled in clips[].
static bool city_build_character_animator(animator& out, pix_data_loader& loader,
                                          const city_character& ch) {
    if (!city_build_animator(out, loader, ch.model)) return false;
    for (size_t i = 0; i < ch.borrowed_count; i++)
        animator_add_clip(out, &loader.animation_clips[ch.borrowed[i]]);
    return true;
}

// One rigged actor - a person, a dog, anything else with a skeleton later.
// `target_height` is how tall the thing should stand in metres; `tints` is how
// many recoloured copies of its material to make (a crowd wants twelve, a dog
// wants a couple).
// ---- outfit tinting ----
//
// A rig arrives as one generated palette: a handful of cells, one per material
// the file declared, holding the skin, the hair, the eyes and every part of the
// outfit side by side. A crowd is made out of that by recolouring the palette
// per pedestrian.
//
// What must not be recoloured is the person. Multiplying the whole palette by
// a tint - which is what a material colour does - dyes the face and hair along
// with the jacket, and a street of green-skinned people in teal hair is the
// result. There is no way to say "only these cells" through a material colour,
// so each variant gets its own copy of the palette with the person left alone
// and only the clothing cells multiplied.
//
// The cost is one 16 x 16 texture per variant, which is nothing; the material
// count is unchanged, since a tint was already costing one of those.

// Skin, hair and eyes, told from clothing by colour alone - there is nothing
// else to go on, since the palette says only what colour each material was.
//
// Two bands cover it. Everything from deep brown to pale pink lies in the
// orange end of the hue circle, which is where every skin tone and every
// natural hair colour except grey sits; and anything dark enough is hair or a
// shadow under it. The cost of the rule is that a tan coat keeps its colour
// too, which is a far better failure than a mint-green face.
static bool city__is_person_colour(vec3 c) {
    float h, sat, l;
    city__rgb_to_hsl(c, &h, &sat, &l);
    if (l < 0.22f) return true;                                   // dark hair
    if (h >= 8.0f && h <= 55.0f && sat <= 0.85f && l <= 0.93f) return true;
    return false;
}

// One variant's palette: the same image with its clothing cells multiplied.
static idx city__outfit_material(pix_renderer& renderer, const image_file_data* src, vec3 tint) {
    if (!src || !src->data) return (idx)-1;
    int n = src->width * src->height;
    if (n <= 0 || n > GLTF_PALETTE_MAX) return (idx)-1;

    static unsigned char px[GLTF_PALETTE_MAX * 4];
    const unsigned char* in = (const unsigned char*)src->data;
    for (int i = 0; i < n; i++) {
        vec3 c = v3(in[i * 4 + 0] / 255.0f, in[i * 4 + 1] / 255.0f, in[i * 4 + 2] / 255.0f);
        vec3 o = city__is_person_colour(c)
               ? c : v3(c.x * tint.x, c.y * tint.y, c.z * tint.z);
        for (int k = 0; k < 3; k++) {
            float v = (k == 0 ? o.x : (k == 1 ? o.y : o.z)) * 255.0f + 0.5f;
            px[i * 4 + k] = (unsigned char)(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v));
        }
        px[i * 4 + 3] = in[i * 4 + 3];
    }

    image_file_data copy = *src;
    copy.data = (char*)px;
    return load_material_image(renderer, &copy, v3(1.0f, 1.0f, 1.0f), 0.0f, 0.72f, true);
}

static bool city__load_actor(city_character& ch, pix_data_loader& loader,
                             pix_renderer& renderer, const char* dir, const char* file,
                             float target_height, int tints) {
    memset(&ch, 0, sizeof(ch));
    ch.model = (idx)-1;
    ch.mesh = (idx)-1;
    for (int i = 0; i < CLIP_COUNT; i++) ch.clips[i] = (idx)-1;
    for (int i = 0; i < CITY_MAX_BORROWED; i++) ch.borrowed[i] = (idx)-1;

    char path[512];
    snprintf(path, sizeof(path), "%s%s", dir, file);
    ch.model = load_model_gltf_file(loader, path);
    if (ch.model == (idx)-1) return false;

    skinned_mesh_file_data* md = get_model_mesh(loader, ch.model);
    if (!md) { ch.model = (idx)-1; return false; }
    ch.mesh = load_skinned_mesh(renderer, *md);
    if (ch.mesh == (idx)-1) { ch.model = (idx)-1; return false; }

    city_rig_bounds rb = city__measure_rig(loader, ch.model);
    float raw = rb.max.y - rb.min.y;
    ch.scale = (raw > 0.05f) ? target_height / raw : 1.0f;
    ch.yaw_offset = 3.14159265f;     // these rigs are modelled facing +Z

    // resolve the clip roles against what this rig actually exports
    const model_file_data& mf = loader.model_files[ch.model];
    for (int role = 0; role < CLIP_COUNT; role++) {
        for (int pref = 0; pref < CLIP_PREFS && ch.clips[role] == (idx)-1; pref++) {
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
    if (tints > CITY_MAX_CHAR_TINTS) tints = CITY_MAX_CHAR_TINTS;
    for (int i = 1; i < tints; i++) {
        // A recoloured palette where the file gave us one, so the tint lands
        // on the outfit and not on the face. Anything else - a rig with a real
        // texture rather than a palette, an animal - falls back to the whole
        // material tint it always had.
        idx variant = mf.image_is_palette ? city__outfit_material(renderer, img, CHAR_TINTS[i])
                                          : (idx)-1;
        if (variant == (idx)-1)
            variant = clone_material(renderer, base, CHAR_TINTS[i], 0.0f, 0.72f);
        ch.materials[ch.material_count++] = variant;
    }

    ch.ok = true;
    return true;
}

// ---- step 2 and 3 of clip resolution (see the city_clip comment) ----

// Fills in every role a rig does not ship from another rig that does,
// retargeted onto this skeleton. Runs once, after the whole cast is loaded,
// because a rig can only borrow from rigs that already exist.
static void city__share_clips(city_catalog& cat, pix_data_loader& loader) {
    for (size_t t = 0; t < cat.character_count; t++) {
        city_character& to_ch = cat.characters[t];
        if (!to_ch.ok) continue;
        skeleton_file_data* to_sk = get_model_skeleton(loader, to_ch.model);
        if (!to_sk) continue;
        size_t own_clips = loader.model_files[to_ch.model].animation_count;

        for (int role = 0; role < CLIP_COUNT && to_ch.borrowed_count < CITY_MAX_BORROWED; role++) {
            if (to_ch.clips[role] != (idx)-1) continue;

            for (size_t s = 0; s < cat.character_count; s++) {
                if (s == t) continue;
                const city_character& from_ch = cat.characters[s];
                if (!from_ch.ok || from_ch.clips[role] == (idx)-1) continue;
                // Only from a rig that *authored* the clip. A borrowed clip is
                // already a retarget, and retargeting a retarget compounds
                // every bind-pose difference it went through.
                const model_file_data& from_mf = loader.model_files[from_ch.model];
                if (from_ch.clips[role] >= from_mf.animation_count) continue;

                skeleton_file_data* from_sk = get_model_skeleton(loader, from_ch.model);
                if (!from_sk || !city_skeletons_compatible(*from_sk, *to_sk)) continue;

                idx made = city_retarget_clip(
                    loader, loader.animation_clips[from_mf.first_animation + from_ch.clips[role]],
                    *from_sk, *to_sk);
                if (made == (idx)-1) continue;

                to_ch.borrowed[to_ch.borrowed_count] = made;
                to_ch.clips[role] = (idx)(own_clips + to_ch.borrowed_count);
                to_ch.borrowed_count++;
                break;
            }
        }
    }
}

// Last resort: a related role this rig does have. Kept separate from the
// borrowing pass above so a rig always prefers the real clip from a
// neighbouring rig over a stand-in from its own.
static void city__resolve_clip_fallbacks(city_character& ch) {
    for (int role = 0; role < CLIP_COUNT; role++) {
        if (ch.clips[role] != (idx)-1) continue;
        int alt = CLIP_FALLBACK[role];
        // one hop only; a chain of stand-ins is how a walk ends up being a death
        if (alt >= 0 && ch.clips[alt] != (idx)-1) ch.clips[role] = ch.clips[alt];
    }
}

// Which rig the player gets: one that authored its own jump and its own
// sitting pose, since those two are the player's alone and a borrowed clip is
// never quite as good as the one the animator made for the skeleton. Every
// other rig can now jump too (see city__share_clips), so this is a preference
// rather than the hard requirement it used to be.
static idx city_pick_player(const city_catalog& cat, const pix_data_loader& loader) {
    idx best = (idx)-1;
    int best_score = -1;
    for (size_t i = 0; i < cat.character_count; i++) {
        const city_character& ch = cat.characters[i];
        if (!ch.ok) continue;
        size_t own = loader.model_files[ch.model].animation_count;
        // "Authored" means the rig's own clip, not one borrowed off a
        // neighbour and retargeted - a borrowed clip is never quite as good on
        // a skeleton it was not made for, and these three are the player's
        // alone: nobody in the crowd jumps, drives or aims anything.
        // Note that "its own" is two tests, not one. `< own` rules out a clip
        // borrowed off a neighbouring rig, but every role in CLIP_FALLBACK
        // also resolves to one of this rig's *other* clips when it ships none
        // of its own - so a rig with no firearm poses at all comes back
        // holding an index that is its own and is under `own`, and is its
        // idle. Comparing against what the role falls back to is what
        // actually separates an authored clip from a stand-in.
        bool jump = ch.clips[CLIP_JUMP] != (idx)-1 && ch.clips[CLIP_JUMP] < own;
        bool sit  = ch.clips[CLIP_SIT]  != (idx)-1 && ch.clips[CLIP_SIT]  < own
                 && ch.clips[CLIP_SIT]  != ch.clips[CLIP_IDLE];
        bool gun  = ch.clips[CLIP_GUN_AIM]   != (idx)-1 && ch.clips[CLIP_GUN_AIM]   < own
                 && ch.clips[CLIP_GUN_SHOOT] < own
                 && ch.clips[CLIP_GUN_AIM]   != ch.clips[CLIP_IDLE]
                 && ch.clips[CLIP_GUN_SHOOT] != ch.clips[CLIP_GUN_AIM];

        // Weighted rather than a hard requirement, because no rig in this cast
        // ships all three. Firearm poses outrank the other two on purpose: a
        // sit is seen from behind a windscreen and a jump lasts half a second,
        // but somebody holding a gun with their arms by their sides is the
        // player's own silhouette for as long as they are carrying one.
        int score = (gun ? 4 : 0) + (jump ? 2 : 0) + (sit ? 1 : 0);
        if (score > best_score) { best_score = score; best = (idx)i; }
    }
    return best;
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

// A glTF car, loaded whole rather than split into a body and a wheel like
// city__load_vehicle's Kenney kit is. These ship their wheels as their own
// named nodes too (usually one merged node per axle rather than the OBJ
// kit's four separate corners), but with the whole car merged into a single
// static mesh - the same "not authored in parts" fallback above already
// draws - that difference in topology stops mattering: wheel_mesh stays
// -1, city_car_draw's own check skips the per-wheel loop, and what is lost
// is wheel spin and steer, not the car itself.
//
// `length` is the target nose-to-tail size in metres; body_mesh's local +Z
// is assumed to be the nose, matching every wheel-pivot check made against
// this pack while building it, and is turned to face -Z (this project's
// forward) by the same half turn the Kenney car kit needed for the same
// reason - see KIT_VEHICLE_SCALE's own comment.
static bool city__load_vehicle_gltf(city_catalog& cat, pix_data_loader& loader, pix_renderer& renderer,
                                    const char* path, float length) {
    if (cat.vehicle_count >= CITY_MAX_VEHICLES) return false;
    pix_loader_mark mark = pix_loader_mark_now(loader);
    idx file = load_model_gltf_file(loader, path, -1);
    if (file == (idx)-1) { pix_loader_rewind(loader, mark); return false; }
    skinned_mesh_file_data* md = get_model_mesh(loader, file);
    if (!md || !md->vertex_count || md->vertex_count > 65535) {
        pix_loader_rewind(loader, mark);
        return false;
    }

    static vertex verts[65536];
    vec3 mn = v3(1e30f, 1e30f, 1e30f), mx = v3(-1e30f, -1e30f, -1e30f);
    for (size_t i = 0; i < md->vertex_count; i++) {
        const vertex_rigged& src = md->vertex_data[i];
        verts[i].position = src.position;
        verts[i].normal = src.normal;
        // glTF uv is top-left origin already; VSHDER_BASIC (unlike VSHDER_SKINNED)
        // flips V assuming OBJ's bottom-left origin, so flip here to cancel that out.
        verts[i].uv = v2(src.uv.x, 1.0f - src.uv.y);
        vec3 p = src.position;
        mn.x = p.x < mn.x ? p.x : mn.x; mn.y = p.y < mn.y ? p.y : mn.y; mn.z = p.z < mn.z ? p.z : mn.z;
        mx.x = p.x > mx.x ? p.x : mx.x; mx.y = p.y > mx.y ? p.y : mx.y; mx.z = p.z > mx.z ? p.z : mx.z;
    }
    mesh_file_data mfd = {};
    mfd.vertex_count = md->vertex_count;
    mfd.vertex_data = verts;
    mfd.index_count = md->index_count;
    mfd.index_data = md->index_data;
    idx mesh = load_mesh(renderer, mfd);
    if (mesh == (idx)-1) { pix_loader_rewind(loader, mark); return false; }

    const model_file_data& mf = loader.model_files[file];
    // Car paint, not plastic.
    //
    // Real car paint is a coloured base under a clear lacquer, and what the
    // eye reads as "car" is that lacquer: a tight bright highlight from the
    // sun, a sharp reflection of the sky along the flanks, and a hard
    // Fresnel rim at grazing angles. This BRDF has no separate clearcoat
    // lobe, so the way to get there is a low roughness with enough metal in
    // it to keep the reflection coloured and strong - at 0.35/0.35 these read
    // as matte injection-moulded toys parked on a shiny road.
    idx material = load_material_image(renderer,
        mf.image >= 0 ? &loader.image_files[mf.image] : 0,
        v3(1.0f, 1.0f, 1.0f), 0.55f, 0.14f, mf.image_is_palette);
    pix_loader_rewind(loader, mark);      // uploaded; the parse can go

    vec3 e = v3sub(mx, mn);
    float scale = e.z > 1e-4f ? length / e.z : 1.0f;

    city_vehicle_model v = {};
    v.body_mesh = mesh;
    v.wheel_mesh = (idx)-1;
    v.material = material;
    // Stand the car on the road rather than through it.
    //
    // Everything that places a vehicle - the traffic, the parked scenery -
    // puts its *origin* on the tarmac (see city_car_draw and
    // city__parked_car), which is only right if the model was authored with
    // its origin at the contact patch. These are not: their origin is around
    // the middle of the shell, so half of every car was buried in the road.
    // Lifting by the model's own lowest vertex puts the tyres on the surface
    // whatever the exporter chose, and it is measured, not guessed at.
    v.body_offset = v3(0.0f, -mn.y * scale, 0.0f);
    v.model_yaw = 3.14159265f;
    v.scale = scale;
    v.half_length = e.z * 0.5f * scale;
    v.half_width  = e.x * 0.5f * scale;
    v.height      = e.y * scale;
    v.part_count = 0;
    for (int i = 0; i < CITY_MAX_VEHICLE_PARTS; i++) v.part_mesh[i] = (idx)-1;
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
    // Sits under the whole map, past the last cell the culler kept, so it is
    // what the horizon is made of. The city is an archipelago now and what
    // runs off to the horizon is open sea, not the green field this used to
    // be - a green rim around a map of islands reads as the edge of the
    // world, which is exactly what it was.
    cat.mat_horizon  = load_material(renderer, 0, v3(0.20f, 0.33f, 0.40f), 0.0f, 0.85f);

    // ---- KayKit: roads and street furniture ----
    static const char* ROADS[] = {
        "road_straight", "road_corner", "road_tsplit", "road_junction",
        "road_straight_crossing", "road_corner_curved"
    };
    city__add_set(cat, loader, renderer, SET_ROAD, KAY_DIR, ROADS, 6, cat.mat_city, KIT_ROAD_SCALE);
    for (int i = 0; i < 6 && (idx)i < cat.sets[SET_ROAD].count; i++)
        cat.singles[ONE_ROAD_STRAIGHT + i] = cat.sets[SET_ROAD].first + i;

    // Traffic lights and the bench have no replacement in the new pack, so
    // the Kenney set is still loaded for exactly those two - everything else
    // in it (the streetlight, the hydrant, the bin, the dumpster) is
    // immediately overridden below to point at assets/road_objects instead,
    // which is what "revamp with the new assets, don't use Kenney for them"
    // means for a set two of whose six members have nothing to be revamped to.
    static const char* STREET[] = {
        "streetlight", "trafficlight_A", "bench", "firehydrant", "trash_A",
        "dumpster", "trafficlight_B", "trafficlight_C", "trash_B", "box_A", "box_B"
    };
    city__add_set(cat, loader, renderer, SET_STREET, KAY_DIR, STREET, 11, cat.mat_city, KIT_PROP_SCALE);
    if (cat.sets[SET_STREET].count >= 6) {
        idx f = cat.sets[SET_STREET].first;
        cat.singles[ONE_STREETLIGHT]  = f + 0;
        cat.singles[ONE_TRAFFICLIGHT] = f + 1;   // kept - no new traffic light
        cat.singles[ONE_BENCH]        = f + 2;   // kept - no new bench
        cat.singles[ONE_HYDRANT]      = f + 3;
        cat.singles[ONE_BIN]          = f + 4;
        cat.singles[ONE_DUMPSTER]     = f + 5;
    }

    static const city_gltf_item ROAD_OBJECTS[] = {
        { "Street Light by Quaternius - 0lxF8Dl1jU.glb",       4.5f },
        { "Fire Hydrant by Quaternius - DKkMQbEklp.glb",       0.75f },
        { "Trash Can by Zsky - PEtRDeGyg0.glb",                1.0f },
        { "Dumpster by Quaternius - PKsbolkZSr.glb",           1.8f },
        { "Barrel by Quaternius - MraIiFnpAY.glb",             0.9f },
        { "Traffic Cone by Quaternius - lAx8JytxGD.glb",       0.7f },
        { "Traffic Barrier by Quaternius - nugx3heueH.glb",    1.1f },
        { "Stop sign by Poly by Google - 60GyU9CdZ9r.glb",     2.2f },
        { "Town Sign by Quaternius - VSZubp0ru3.glb",          2.5f },
        { "Sign by Quaternius - 4MSsNFk5fc.glb",               2.0f },
        { "Crate by Quaternius - 3OEFd1AWfa.glb",              0.8f },
        { "Cardboard Boxes by Quaternius - bs6ikOeTrR.glb",    0.9f },
        { "Mailbox by J-Toastie - 9gbHlg1IlY.glb",             1.2f },
    };
    idx clutter_first = (idx)cat.model_count;
    city__add_set_gltf(cat, loader, renderer, SET_STREET, NEW_ROAD_DIR,
                       ROAD_OBJECTS, sizeof(ROAD_OBJECTS) / sizeof(ROAD_OBJECTS[0]));
    // Everything the block above added except its first entry, which is the
    // street light: that one is placed by name on its own stride, and a second
    // one turning up at random half way down a run is a lamp post nobody put
    // there.
    if (cat.model_count > clutter_first + 1) {
        cat.street_clutter.first = clutter_first + 1;
        cat.street_clutter.count = (idx)cat.model_count - clutter_first - 1;
    }
    // The streetlight, hydrant, bin and dumpster this project actually places
    // by name - see city__place_street_furniture - now point at the new
    // pack's versions, appended just above, rather than the Kenney ones the
    // block before it loaded.
    {
        idx f = cat.sets[SET_STREET].first + 6;   // 6 kept from KayKit, new ones start here
        cat.singles[ONE_STREETLIGHT] = f + 0;
        cat.singles[ONE_HYDRANT]     = f + 1;
        cat.singles[ONE_BIN]         = f + 2;
        cat.singles[ONE_DUMPSTER]    = f + 3;
    }

    static const city_gltf_item INDUSTRIAL[] = {
        { "Water Tower by Quaternius - tMK8bhapAK.glb",        9.0f },
        { "Container Green by Quaternius - h5RUr3vlcS.glb",    2.4f },
        { "Container Red by Quaternius - vzzCNUB6Zn.glb",      2.4f },
        { "Stone Wall by Quaternius - tdeAOh3LQV.glb",         1.8f },
    };
    city__add_set_gltf(cat, loader, renderer, SET_INDUSTRIAL, NEW_ROAD_DIR,
                       INDUSTRIAL, sizeof(INDUSTRIAL) / sizeof(INDUSTRIAL[0]));

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
    // These are the ones dir_to_yaw_building used to exist for: their frontage
    // is on +Z. Recorded on the models instead, so the building pass has one
    // rule for every kit rather than a branch per kit.
    city__face_set(cat, SET_BUILDING, 180.0f);

    // ---- Kenney suburban: detached houses for the outer ring ----
    //
    // Twenty-one of them, and until now not one was loaded - the kit's
    // colormap was set up as a material and then nothing was ever textured
    // with it, so every suburb in the city was built out of downtown
    // shopfronts. Sized off their own bounds like every other kit whose
    // pieces are not one uniform scale: these range from a bungalow to a
    // three-storey townhouse, and a single factor across the set would make
    // one of those wrong.
    static const city_kit_item HOUSES[] = {
        { "building-type-a", 11.0f }, { "building-type-b", 11.5f },
        { "building-type-c", 12.0f }, { "building-type-d", 10.5f },
        { "building-type-e", 11.0f }, { "building-type-f", 12.5f },
        { "building-type-g", 11.0f }, { "building-type-h", 11.5f },
        { "building-type-i", 12.0f }, { "building-type-j", 10.5f },
        { "building-type-k", 11.5f }, { "building-type-l", 12.0f },
        { "building-type-m", 11.0f }, { "building-type-n", 11.5f },
        { "building-type-o", 12.5f }, { "building-type-p", 11.0f },
        { "building-type-q", 11.5f }, { "building-type-r", 12.0f },
        { "building-type-s", 11.0f }, { "building-type-t", 11.5f },
        { "building-type-u", 12.0f },
    };
    city__add_set_sized(cat, loader, renderer, SET_HOUSE, SUB_DIR, HOUSES,
                        (int)(sizeof(HOUSES) / sizeof(HOUSES[0])), cat.mat_suburban);

    // and the new pack's houses on the end of the same set. A suburb built
    // out of one kit is a housing estate however many models the kit has -
    // every roof pitch, every window and every wall colour comes from one
    // hand. These are from five different authors and that is the point of
    // mixing them in rather than replacing one kit with another.
    //
    // Sizes here are only a first pass: the loop below rescales the whole set
    // by footprint, so what matters is that each file parses, not what number
    // is next to it. "Row House" and one of the two Town Houses are missing
    // on purpose - the first is authored as 32 loose cubes floating 3 m above
    // its own origin, the second does not parse at all.
    // The trailing number is the turn that puts each one front to the street,
    // read off PIX_SHOWROOM rather than assumed - three of these five are
    // half a turn from the kit above them and one is a quarter.
    static const struct { const char* file; float size; float facing; } NEW_HOUSES[] = {
        { "House by Quaternius - roqiHdrpgc.glb",          9.0f,   0.0f },
        { "House by jrich01 - Fr0oyQzfzk.glb",            11.0f,  90.0f },
        { "Town House by Quaternius - sDQJBImZuw.glb",    11.0f, 180.0f },
        { "Brown Building by J-Toastie - fGKIlWGDNH.glb", 10.0f, 180.0f },
        { "Building Red by J-Toastie - lbNz2dClar.glb",   10.0f, 180.0f },
    };
    for (size_t i = 0; i < sizeof(NEW_HOUSES) / sizeof(NEW_HOUSES[0]); i++)
        city__add_set_gltf_whole(cat, loader, renderer, SET_HOUSE, NEW_HOUSE_DIR,
                                 NEW_HOUSES[i].file, NEW_HOUSES[i].size, NEW_HOUSES[i].facing);
    // Sized by footprint rather than by longest axis. A house has to fit on a
    // lot, and the tall narrow ones in this kit are two storeys of the same
    // small plan - scaling those by height shrinks the plan to a shed, and
    // scaling the wide bungalow next door by the same rule spreads it across
    // its neighbour's garden.
    //
    // A whole lot across, slightly over. At four fifths of a cell these read
    // as dolls' houses next to a 1.8 m character; a house should fill its
    // plot and leave the garden to the setback, not to a ring of bare grass.
    city__fit_set_to_plan(cat, SET_HOUSE, 1.02f);

    // ---- yard fencing: assets/road_objects, sized by length the same way
    // the Kenney fence used to be, for the same reason (a fence sized like a
    // building is a wall, not a fence) ----
    // Sized off each model's own proportions, not off the cell.
    //
    // These four are authored at wildly different aspect ratios, and stating
    // "one cell long" for all of them is what produced the 7.5 m tall garden
    // fences: the J-Toastie pieces are barely wider than they are high, so
    // stretching one to span a cell stands a fence panel two storeys up. Only
    // the Quaternius fence is a long low run and only it is tiled a cell at a
    // time (it is ONE_FENCE, the one every yard boundary uses); the rest are
    // gate-and-post pieces at their own human size.
    static const city_gltf_item YARD[] = {
        { "Fence by Quaternius - U7g0Wxpt63.glb",      CITY_TILE },
        { "Fence by J-Toastie - ZSQMyIqPTz.glb",       3.0f },
        { "Fence Piece by J-Toastie - Y6D3Sbc85w.glb", 2.0f },
        { "Fence End by J-Toastie - tQ5zhPd5UC.glb",   1.5f },
    };
    city__add_set_gltf(cat, loader, renderer, SET_YARD, NEW_ROAD_DIR,
                       YARD, sizeof(YARD) / sizeof(YARD[0]));
    if (cat.sets[SET_YARD].count) cat.singles[ONE_FENCE] = cat.sets[SET_YARD].first;

    // ---- garden paths and driveways: assets/paths ----
    // Sized as what they are - a footpath and a driveway - rather than as a
    // fraction of a cell.
    //
    // Every one of these is authored long-axis-along-Z and much narrower than
    // it is long: the path piece is half a metre wide and a metre long. Sizing
    // it to half a cell stretches that one metre to four and the half metre to
    // two, which stops being a path and becomes a paving slab - and laying a
    // line of those reads as a plaza, not a walkway. So the number here is the
    // piece length, and the width that comes with it is the model own.
    static const city_gltf_item PAVING[] = {
        { "Path Straight by Quaternius - ZuRHRsKWoz.glb",         3.40f },  // 1.7 m wide
        { "Rock Path Round Small by Quaternius - yHEdadj5I0.glb", 1.50f },
        { "Rock Path Round Wide by Quaternius - mWb3XxOctl.glb",  2.20f },
        { "Rock Path Square Thin by Quaternius - HAux7BIMfm.glb", 3.20f },  // 2.5 m wide drive
    };
    city__add_set_gltf(cat, loader, renderer, SET_PAVING, NEW_PATHS_DIR,
                       PAVING, sizeof(PAVING) / sizeof(PAVING[0]));
    if (cat.sets[SET_PAVING].count >= 4) {
        cat.singles[ONE_PATH]     = cat.sets[SET_PAVING].first;
        cat.singles[ONE_DRIVEWAY] = cat.sets[SET_PAVING].first + 3;
    }

    // ---- the buildings a district is actually made of: assets/house ----
    //
    // One model per lot, whole buildings rather than block faces, sized by
    // footprint to a tile and a bit so they stand shoulder to shoulder down a
    // street without their walls intersecting. The apartment block is the
    // tall one and the bakery is the short one; between them they are what a
    // commercial street is built out of here now.
    static const city_gltf_item TOWERS[] = {
        { "Apartment building by Poly by Google - 01lqee-dZAr.glb", 18.0f },
        { "Big Building by Quaternius - AVCS8jUd2l.glb",             12.0f },
        { "Building by Quaternius - ZSYgIuHfYb.glb",                 12.0f },
        { "Bakery by Poly by Google - 6BGhNQlUzRR.glb",              12.0f },
    };
    city__add_set_gltf(cat, loader, renderer, SET_TOWER, NEW_HOUSE_DIR,
                       TOWERS, sizeof(TOWERS) / sizeof(TOWERS[0]));
    // The bakery is filed under road_objects rather than house, which is why
    // it needs its own call - it is a shopfront, and it is the one short
    // building in a set that is otherwise all apartment blocks.
    static const city_gltf_item TOWER_SHOPS[] = {
        { "Bakery by Poly by Google - 6BGhNQlUzRR.glb", 12.0f },
    };
    city__add_set_gltf(cat, loader, renderer, SET_TOWER, NEW_ROAD_DIR,
                       TOWER_SHOPS, sizeof(TOWER_SHOPS) / sizeof(TOWER_SHOPS[0]));
    // A shade over a tile, the same as the houses: a building that stops
    // short of its own lot line leaves a stripe of grass between it and its
    // neighbour, and a row of those reads as a model village.
    city__fit_set_to_plan(cat, SET_TOWER, 1.06f);
    // Every one of these four is authored facing +Z, the same way round as
    // the KayKit block faces and the opposite way from the suburban kit.
    city__face_set(cat, SET_TOWER, 180.0f);

    // ---- playgrounds: assets/park ----
    //
    // Sized in real metres off what each piece is: a slide a child can climb
    // is about 3 m to the top of its ladder, a swing frame is 3.5 m wide, a
    // climbing frame about 5. The files themselves are authored anywhere from
    // 0.6 to 150 units across, which is exactly why every entry here states a
    // size instead of the folder getting one factor.
    static const city_gltf_item PLAYGROUND[] = {
        { "Slide by Poly by Google - dDe3njWPbg0.glb",       3.4f },
        { "Swing set by Poly by Google - e-IJdcqZH4p.glb",   3.6f },
        { "Jungle gym by Poly by Google - 0-7U_RTHzKT.glb",  5.0f },
        { "Seasaw by Jarlan Perez - 1A1vpruOUhy.glb",        2.6f },
    };
    city__add_set_gltf(cat, loader, renderer, SET_PLAYGROUND, NEW_PARK_DIR,
                       PLAYGROUND, sizeof(PLAYGROUND) / sizeof(PLAYGROUND[0]));

    // The park bench is a separate model from the street bench and is placed
    // by a different pass, so it is a single rather than a set member.
    cat.singles[ONE_PARK_BENCH] = city__add_model_gltf(
        cat, loader, renderer, NEW_PARK_DIR "Bench by Ev Amitay - dOSjmdmKaxi.glb", -1, 1.7f);

    // ---- walls and railings ----
    //
    // Each one is sized so a single piece spans a whole cell edge, which is
    // what lets city__run_barrier lay a continuous run by stepping one cell
    // at a time with no per-model spacing to get wrong. The graveyard railing
    // is the tall one and edges a park; the stone wall is the low one and
    // edges a yard.
    static const city_gltf_item WALLS[] = {
        { "Stone Wall by Quaternius - tdeAOh3LQV.glb", CITY_TILE * 0.5f },
    };
    city__add_set_gltf(cat, loader, renderer, SET_WALL, NEW_ROAD_DIR,
                       WALLS, sizeof(WALLS) / sizeof(WALLS[0]));
    static const city_gltf_item RAILINGS[] = {
        { "Fence by Kay Lousberg - aE3GIx8jIH.glb", CITY_TILE * 0.5f },
    };
    city__add_set_gltf(cat, loader, renderer, SET_WALL, NEW_GRAVE_DIR,
                       RAILINGS, sizeof(RAILINGS) / sizeof(RAILINGS[0]));
    if (cat.sets[SET_WALL].count) cat.singles[ONE_WALL] = cat.sets[SET_WALL].first;

    // ---- the waterfront ----
    static const city_gltf_item WATERFRONT[] = {
        { "Dock Wide by Quaternius - XndOrGa7rN.glb", CITY_TILE },
        { "Dock by Quaternius - XViKoBh2UN.glb",      CITY_TILE * 0.6f },
        { "Boat by Poly by Google - c2SYxaiPfF3.glb", 7.0f },
        { "Boat by Poly by Google - 1ZuSXvhkRg_.glb", 6.0f },
    };
    city__add_set_gltf(cat, loader, renderer, SET_WATERFRONT, NEW_ROAD_DIR,
                       WATERFRONT, sizeof(WATERFRONT) / sizeof(WATERFRONT[0]));
    if (cat.sets[SET_WATERFRONT].count >= 3) {
        cat.singles[ONE_DOCK] = cat.sets[SET_WATERFRONT].first;
        cat.singles[ONE_BOAT] = cat.sets[SET_WATERFRONT].first + 2;
    }

    // ---- landmarks ----
    //
    // The one building on the skyline that says which district you are in.
    // Placed at most once per block by city__place_landmark, so their sizes
    // are the real thing rather than lot-fitted: a church is meant to be
    // taller than the terrace it stands in.
    static const city_gltf_item LANDMARK_HOUSE[] = {
        { "Carpenter Gothic Church by jrich01 - Vq4Gik1t7c.glb", 17.0f },
    };
    city__add_set_gltf(cat, loader, renderer, SET_LANDMARK, NEW_HOUSE_DIR,
                       LANDMARK_HOUSE, sizeof(LANDMARK_HOUSE) / sizeof(LANDMARK_HOUSE[0]));
    static const city_gltf_item LANDMARK_ROAD[] = {
        { "Radio tower by Poly by Google - d1zYJG7rJGY.glb", 34.0f },
        { "Radio tower by Poly by Google - eWEYV9ppUjv.glb", 30.0f },
        { "Stag Statue by Quaternius - cKloIsNcT8.glb",       4.6f },
    };
    city__add_set_gltf(cat, loader, renderer, SET_LANDMARK, NEW_ROAD_DIR,
                       LANDMARK_ROAD, sizeof(LANDMARK_ROAD) / sizeof(LANDMARK_ROAD[0]));
    static const city_gltf_item LANDMARK_FARM[] = {
        { "Barn by Poly by Google - 0QTh_KUZRYE.glb",  13.0f },
    };
    city__add_set_gltf(cat, loader, renderer, SET_LANDMARK, NEW_FARM_DIR,
                       LANDMARK_FARM, sizeof(LANDMARK_FARM) / sizeof(LANDMARK_FARM[0]));
    // the barn doors are on its +Z side; the church, the masts and the statue
    // ahead of it in the set are all fine as they are
    if (cat.sets[SET_LANDMARK].count)
        cat.models[cat.sets[SET_LANDMARK].first + cat.sets[SET_LANDMARK].count - 1]
            .yaw_offset = 3.14159265f;
    // Whole file, not per node. The big barn is authored as five nodes - a
    // shell and four wall panels - and the family-file rule that serves a row
    // of five birch trees so well turns it into five separate landmarks, four
    // of which are a barn wall standing on its own in a field.
    city__add_set_gltf_whole(cat, loader, renderer, SET_LANDMARK, NEW_FARM_DIR,
                             "Big Barn by Quaternius - q1N3xn2SpC.glb", 12.0f, 180.0f);

    // ---- the bridge arch ----
    //
    // The one model in the catalogue that ships no texture at all, so it is
    // handed the same concrete the deck above it is poured from rather than
    // being left the white default. Sized to two cells along its span, which
    // is what city__build_bridges steps it by.
    cat.singles[ONE_BRIDGE_ARCH] = city__add_model_gltf(
        cat, loader, renderer, NEW_BRIDGE_DIR "Bridge by CreativeTrio - orI7eNSB38.glb",
        -1, CITY_TILE * 2.0f);
    if (cat.singles[ONE_BRIDGE_ARCH] != (idx)-1) {
        city_model& arch = cat.models[cat.singles[ONE_BRIDGE_ARCH]];
        arch.material = cat.mat_concrete;
        for (int i = 0; i < arch.part_count; i++) arch.part_material[i] = cat.mat_concrete;
    }

    // ---- greenery: assets/nature, replacing the Kenney suburban trees and
    // nature kit ----
    //
    // Every entry states how big the thing is in metres along its longest
    // axis, same convention as the Kenney tables these replace and for the
    // same reason - a folder that ranges from an 8 m tree to a 0.2 m tuft of
    // grass has no one multiplier that is right for both ends of it.
    //
    // Several of these files are a family of size variants sharing one glb
    // (five birch trees standing in a row, the case this project actually
    // ships - see gltf_load_file's `only_node`), so city__add_set_gltf pulls
    // out however many of them there are, not just one, at the same target
    // size each - close enough for a set of size variants of the same tree.
    static const city_gltf_item STREET_TREES[] = {
        { "Birch Trees by Quaternius - R7qMWzb7nk.glb", 7.5f },
    };
    city__add_set_gltf(cat, loader, renderer, SET_TREE_STREET, NEW_NATURE_DIR,
                       STREET_TREES, sizeof(STREET_TREES) / sizeof(STREET_TREES[0]));

    static const city_gltf_item PARK_TREES[] = {
        { "Pine by Quaternius - 699sFuLCN2.glb",   8.0f },
        { "Pine by Quaternius - 79gmlLnweB.glb",   9.5f },
        { "Maple Trees by Quaternius - iGFtQd0PJO.glb", 8.5f },
        { "Dead Trees by Quaternius - F5I0Q7TwO5.glb",  6.5f },
        { "Trees by Quaternius - etFGNvsiFv.glb",       8.0f },
    };
    city__add_set_gltf(cat, loader, renderer, SET_TREE_PARK, NEW_NATURE_DIR,
                       PARK_TREES, sizeof(PARK_TREES) / sizeof(PARK_TREES[0]));

    static const city_gltf_item SHRUBS[] = {
        { "Bush by Quaternius - ooG6CkLyE8.glb",             1.2f },
        { "Bush with Flowers by Quaternius - U1ymDy8tbY.glb", 1.3f },
        { "Flower Bushes by Quaternius - 1X06RgvSr6.glb",     1.0f },
        { "Flower Group by Quaternius - hfPzQAedOe.glb",      0.6f },
        { "Flowers by Quaternius - NBUxHir6FJ.glb",           0.4f },
        { "Grass by Quaternius - UGTOzcO3P2.glb",             0.5f },
        { "Grass Wispy by Quaternius - Msr9zx66VU.glb",       0.6f },
        { "Tall Grass by Quaternius - JSIYtscPmP.glb",        0.8f },
    };
    city__add_set_gltf(cat, loader, renderer, SET_SHRUB, NEW_NATURE_DIR,
                       SHRUBS, sizeof(SHRUBS) / sizeof(SHRUBS[0]));

    static const city_gltf_item ROCKS[] = {
        { "Rock by Quaternius - RtLRqYjfMs.glb",        1.0f },
        { "Rock Medium by Quaternius - JQxF95498B.glb", 1.7f },
        { "Rock Medium by Quaternius - KZdEP3uUpa.glb", 1.9f },
    };
    city__add_set_gltf(cat, loader, renderer, SET_ROCK, NEW_NATURE_DIR,
                       ROCKS, sizeof(ROCKS) / sizeof(ROCKS[0]));

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
    //
    // Quaternius (and IvOfficial's Range Rover) rather than the Kenney car
    // kit - see city__load_vehicle_gltf for what that costs (wheel spin and
    // steer) against what it buys (real distinct cars instead of one kit's
    // recolours). "Tires by Quaternius" is a spare wheel, not a car, and is
    // not loaded as one.
    struct { const char* file; float length; } CARS[] = {
        { "Car by Quaternius - HQ0hvRM2XR.glb",         4.3f },
        { "Taxi by Quaternius - x43lOScTpN.glb",        4.4f },
        { "Police Car by Quaternius - BwwnUrWGmV.glb",  4.5f },
        { "SUV by Quaternius - xsMtZhBkxL.glb",         4.6f },
        { "Range Rover by IvOfficial - 8zk4o6nALW.glb", 4.8f },
        { "Broken Car by Quaternius - Y67erogmR9.glb",  4.2f },
        { "Pickup Truck by Quaternius - qn4grQgHm8.glb",5.3f },
        { "Truck by Quaternius - cXw6oiFtZ8.glb",       6.5f },
    };
    for (size_t i = 0; i < sizeof(CARS) / sizeof(CARS[0]); i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s%s", NEW_CARS_DIR, CARS[i].file);
        city__load_vehicle_gltf(cat, loader, renderer, path, CARS[i].length);
    }

    // ---- house interiors (see city_house.hpp) ----
    //
    // Furniture only: the room shell (wall/floor/ceiling) is generated
    // geometry, registered below next to the bridge deck it shares a mesh
    // with. These are Kenney's furniture kit, untextured like the nature kit,
    // so they have to load - and add their colours to the shared palette -
    // before that palette's texture is baked a little further down.
    //
    // Sized off each model's own longest axis, the same convention as every
    // other unscaled kit here (see city_kit_item): a reasonable real-world
    // estimate per piece rather than one factor for a kit that mixes a bed
    // and a lamp.
    static const city_kit_item FURNITURE[] = {
        { "bedDouble",          2.00f },
        { "table",              1.30f },
        { "chair",              0.55f },
        { "loungeSofa",         1.80f },
        { "tableCoffee",        1.00f },
        { "bookcaseClosed",     1.70f },
        { "cabinetTelevision",  1.20f },
        { "televisionModern",   0.90f },
        { "kitchenCabinet",     1.00f },
        { "kitchenCabinetUpper",1.00f },
        { "kitchenFridgeLarge", 1.70f },
        { "kitchenStove",       0.90f },
        { "sideTable",          0.50f },
        { "lampRoundFloor",     1.50f },
        { "rugRectangle",       2.40f },
        { "pottedPlant",        1.00f },
    };
    static const int FURN_SINGLE[] = {
        ONE_BED, ONE_DINING_TABLE, ONE_DINING_CHAIR, ONE_SOFA, ONE_COFFEE_TABLE,
        ONE_BOOKCASE, ONE_TV_CABINET, ONE_TV, ONE_KITCHEN_COUNTER, ONE_KITCHEN_UPPER,
        ONE_FRIDGE, ONE_STOVE, ONE_SIDE_TABLE, ONE_FLOOR_LAMP, ONE_RUG, ONE_PLANT,
    };
    for (int i = 0; i < 16; i++)
        cat.singles[FURN_SINGLE[i]] =
            city__add_model_sized(cat, loader, renderer, FURN_DIR, FURNITURE[i], cat.mat_palette);

    // ---- generated ground geometry ----
    idx quad = city__make_quad(renderer);
    idx slab = city__make_slab(renderer);
    cat.singles[ONE_QUAD] = city__register_generated(cat, quad, cat.mat_grass, "ground quad");
    cat.singles[ONE_SLAB] = city__register_generated(cat, slab, cat.mat_concrete, "slab");
    cat.singles[ONE_WATER_TILE] =
        city__register_generated(cat, city__make_water_tile(renderer), cat.mat_water, "water tile");
    // a bridge deck and its piers are the same unit box, stretched
    cat.singles[ONE_BRIDGE] = city__register_generated(cat, slab, cat.mat_concrete, "bridge deck");
    cat.singles[ONE_PIER]   = city__register_generated(cat, slab, cat.mat_dirt, "bridge pier");

    // a house's shell, the same unit box again: a wash of warm white for the
    // walls and ceiling, a plain wood tone for the floor - deliberately flat,
    // so the furniture is what a room is furnished with, not the walls
    cat.mat_interior_wall    = load_material(renderer, 0, v3(0.86f, 0.83f, 0.76f), 0.0f, 0.92f);
    cat.mat_interior_floor   = load_material(renderer, 0, v3(0.47f, 0.33f, 0.20f), 0.05f, 0.55f);
    cat.mat_interior_ceiling = load_material(renderer, 0, v3(0.95f, 0.94f, 0.90f), 0.0f, 0.95f);
    cat.singles[ONE_INTERIOR_WALL]    = city__register_generated(cat, slab, cat.mat_interior_wall, "interior wall");
    cat.singles[ONE_INTERIOR_FLOOR]   = city__register_generated(cat, slab, cat.mat_interior_floor, "interior floor");
    cat.singles[ONE_INTERIOR_CEILING] = city__register_generated(cat, slab, cat.mat_interior_ceiling, "interior ceiling");

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
    // Thirteen rigs, and each one is worth its cost: a crowd is read as a
    // crowd or as a handful of clones almost entirely on silhouette, and no
    // number of colour tints on one body shape fixes that. Five of these
    // stand in assets/park rather than assets/characters, which is why the
    // folder is per entry - they are the same kind of rig and load through
    // exactly the same path.
    static const struct { const char* dir; const char* file; } CHARACTERS[] = {
        { CHAR_DIR, "Hoodie Character by Quaternius - gKLBoRsyKe.glb" },
        { CHAR_DIR, "Business Man by Quaternius - JFrLIKqvCH.glb" },
        { CHAR_DIR, "Animated Woman by Quaternius - qJ2gsTUBHL.glb" },
        { CHAR_DIR, "Man by Quaternius - HMnuH5geEG.glb" },
        { CHAR_DIR, "Animated Woman by Quaternius - 9kF7eTDbhO.glb" },
        { CHAR_DIR, "Animated Woman by Quaternius - nIItLV9nxS.glb" },
        { CHAR_DIR, "Casual Character by Quaternius - kZ3DmIoGip.glb" },
        { CHAR_DIR, "Farmer by Quaternius - 7pn3R6hPvE.glb" },
        { CHAR_DIR, "Man in Suit by Quaternius - mQnGoME1ez.glb" },
        { NEW_PARK_DIR, "Man by Quaternius - fjHyMd5Wxw.glb" },
        { NEW_PARK_DIR, "Woman Casual by Quaternius - jpKRgGDxhk.glb" },
        { NEW_PARK_DIR, "Suit by Quaternius - sOUciDsoVV.glb" },
        { NEW_PARK_DIR, "Worker by Quaternius - Yg2bQZO6Hj.glb" },
    };
    for (size_t i = 0; i < sizeof(CHARACTERS) / sizeof(CHARACTERS[0])
                       && cat.character_count < CITY_MAX_CHARACTERS; i++) {
        city_character ch;
        if (city__load_actor(ch, loader, renderer, CHARACTERS[i].dir, CHARACTERS[i].file,
                             PLAYER_HEIGHT, CITY_MAX_CHAR_TINTS))
            cat.characters[cat.character_count++] = ch;
    }
    // Now that the whole cast exists, fill in what each rig is missing from
    // the others, and only then fall back within a rig - see the city_clip
    // comment for why that order matters.
    city__share_clips(cat, loader);
    for (size_t i = 0; i < cat.character_count; i++)
        city__resolve_clip_fallbacks(cat.characters[i]);

    // ---- the guns ----
    //
    // Quaternius again, and like every other glTF prop in this file they are
    // loaded whole with their own texture. What is not automatic is the grip
    // offset below: nothing in the file says where a hand goes on a gun, so
    // each one is placed against the rig's wrist bone by eye once and then
    // holds for every character, because every character in the cast shares
    // one skeleton layout. The component along the barrel is the one that
    // matters: the wrist bone sits at the base of the palm rather than in the
    // middle of the fist, so the grip has to be carried forward by about half
    // a hand or the gun hangs off the back of it.
    static const struct {
        const char* file;
        city_weapon w;
    } GUNS[WEAPON_COUNT] = {
        { "Pistol by Quaternius - J3i9KDQ3kt.glb",
          { (idx)-1, "Pistol",   26.0f, 0.16f, 90.0f,  0.030f, 0.010f, 0.016f, 5.5f,
            12, 1.5f, false, { 0.045f, -0.012f, 0.005f }, 0.19f, 0.24f } },
        { "Revolver by Quaternius - E7IaG9TptR.glb",
          { (idx)-1, "Revolver", 55.0f, 0.42f, 110.0f, 0.024f, 0.006f, 0.034f, 9.0f,
            6,  2.3f, false, { 0.050f, -0.012f, 0.005f }, 0.22f, 0.26f } },
        { "Assault Rifle by Quaternius - K2lXTYFSLC.glb",
          { (idx)-1, "Rifle",    22.0f, 0.09f, 150.0f, 0.045f, 0.014f, 0.011f, 4.0f,
            30, 2.6f, true,  { 0.075f, -0.022f, 0.005f }, 0.50f, 0.72f } },
    };
    for (int i = 0; i < WEAPON_COUNT; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s%s", ARMS_DIR, GUNS[i].file);
        cat.weapons[i] = GUNS[i].w;
        cat.weapons[i].model = city__add_model_gltf(cat, loader, renderer, path, -1,
                                                    GUNS[i].w.size, 0.55f, 0.42f);
    }

    // ---- the animals ----
    //
    // Dogs on the pavements and in the parks. Same loader, same clip roles,
    // same animator as a person: a shiba is a rig that happens to walk on
    // four legs, and none of the code that draws or poses one cares.
    static const struct { const char* file; float height; } ANIMALS[] = {
        { "Shiba Inu by Quaternius - y4wdQpg767.glb", 0.55f },
    };
    for (size_t i = 0; i < sizeof(ANIMALS) / sizeof(ANIMALS[0])
                       && cat.animal_count < CITY_MAX_ANIMALS; i++) {
        city_character a;
        if (city__load_actor(a, loader, renderer, ANIMAL_DIR, ANIMALS[i].file,
                             ANIMALS[i].height, 4)) {
            city__resolve_clip_fallbacks(a);
            cat.animals[cat.animal_count++] = a;
        }
    }

    // ---- the metal in the street ----
    //
    // Roughness is what separates these from each other as much as metalness
    // does: a galvanised lamp post is satin, a painted hydrant is a little
    // softer, a steel skip is scuffed, and a chromed mailbox flap is nearly a
    // mirror. One number for all of them is what makes a kit look like a kit.
    static const city_finish STREET_METAL[] = {
        { "Street Light",     0.85f, 0.26f },
        { "Streetlight",      0.85f, 0.26f },
        { "Fire Hydrant",     0.55f, 0.34f },
        { "Trash Can",        0.75f, 0.30f },
        { "Trashcan",         0.75f, 0.30f },
        { "Dumpster",         0.80f, 0.36f },
        { "Barrel",           0.75f, 0.30f },
        { "Traffic Barrier",  0.30f, 0.42f },   // plastic body, steel feet
        { "Stop sign",        0.70f, 0.24f },
        { "Sign",             0.65f, 0.28f },
        { "Mailbox",          0.80f, 0.24f },
    };
    city__finish_set(cat, renderer, SET_STREET, STREET_METAL,
                     (int)(sizeof(STREET_METAL) / sizeof(STREET_METAL[0])));

    static const city_finish YARD_METAL[] = {
        { "Container Green",  0.75f, 0.38f },
        { "Container Red",    0.75f, 0.38f },
        { "Water Tower",      0.60f, 0.40f },
    };
    city__finish_set(cat, renderer, SET_INDUSTRIAL, YARD_METAL,
                     (int)(sizeof(YARD_METAL) / sizeof(YARD_METAL[0])));

    static const city_finish PLAY_METAL[] = {
        { "Slide",            0.70f, 0.22f },   // the chute is steel
        { "Swing set",        0.60f, 0.32f },
        { "Jungle gym",       0.75f, 0.26f },
        { "Seasaw",           0.40f, 0.38f },
    };
    city__finish_set(cat, renderer, SET_PLAYGROUND, PLAY_METAL,
                     (int)(sizeof(PLAY_METAL) / sizeof(PLAY_METAL[0])));

    static const city_finish MAST_METAL[] = {
        { "Radio tower",      0.80f, 0.34f },
    };
    city__finish_set(cat, renderer, SET_LANDMARK, MAST_METAL,
                     (int)(sizeof(MAST_METAL) / sizeof(MAST_METAL[0])));

    // The park railing is wrought iron and is held by index rather than by
    // name, because "Fence" in this catalogue is also four wooden ones.
    if (cat.sets[SET_WALL].count > 1)
        city__finish_model(cat, renderer, cat.sets[SET_WALL].first + 1, 0.85f, 0.28f);

    // ---- glazing ----
    {
        idx shared[2] = { cat.mat_building, cat.mat_suburban };
        for (int i = 0; i < 2; i++)
            if (cat.building_material_count < 48)
                cat.building_materials[cat.building_material_count++] = shared[i];

        const int building_sets[2] = { SET_TOWER, SET_HOUSE };
        for (int si = 0; si < 2; si++)
            for (idx i = cat.sets[building_sets[si]].first;
                 i < cat.sets[building_sets[si]].first + cat.sets[building_sets[si]].count; i++) {
                const city_model& m = cat.models[i];
                idx mats[CITY_MODEL_PARTS + 1];
                int n = 0;
                mats[n++] = m.material;
                for (int part = 0; part < m.part_count; part++) mats[n++] = m.part_material[part];
                for (int k = 0; k < n && cat.building_material_count < 48; k++) {
                    bool seen = false;
                    for (size_t q = 0; q < cat.building_material_count; q++)
                        if (cat.building_materials[q] == mats[k]) seen = true;
                    if (!seen) cat.building_materials[cat.building_material_count++] = mats[k];
                }
            }

        for (size_t i = 0; i < cat.building_material_count; i++)
            if (cat.building_materials[i] < renderer.material_count)
                renderer.materials[cat.building_materials[i]].glass = 1.0f;
    }

    cat.ok = cat.sets[SET_ROAD].count > 0 && cat.sets[SET_BUILDING].count > 0;
    return cat.ok;
}


// ---- what actually loaded ----
//
// A model that fails to load is invisible: city__add_model returns -1, the
// set is one shorter than the table that fed it, and the generator places
// whatever is left without complaint. That is the right behaviour at runtime
// and the wrong behaviour to debug against - "the trees do not load" and "the
// generator chose not to place a tree here" look identical from inside the
// game.
//
// So every run drops a plain text report next to the executable: how many of
// each set survived, which named single models are missing, and for every
// character rig, which clip roles resolved, which were borrowed off another
// rig and which are simply not there. It costs one file write at startup and
// it is the first thing to look at when something is not on screen.
static void city_report_catalog(const city_catalog& cat, const pix_data_loader& loader,
                                const pix_renderer& renderer, const char* path) {
    FILE* f = fopen(path, "w");
    if (!f) return;

    static const char* SET_NAME[SET_COUNT] = {
        "road", "building", "house", "tower", "yard", "paving", "street",
        "tree (street)", "tree (park)", "shrub", "rock", "park feature",
        "playground", "wall", "waterfront", "landmark", "industrial",
        "railroad", "train"
    };
    static const char* ONE_NAME[ONE_COUNT] = {
        "road straight", "road corner", "road tsplit", "road junction",
        "road crossing", "road corner curved",
        "streetlight", "traffic light", "bench", "hydrant", "bin", "dumpster",
        "fence", "path", "driveway",
        "bridge arch", "dock", "boat", "park bench", "wall",
        "quad", "slab", "bridge", "pier", "water tile",
        "interior wall", "interior floor", "interior ceiling",
        "bed", "sofa", "dining table", "dining chair", "coffee table",
        "bookcase", "tv cabinet", "tv", "kitchen counter", "kitchen upper",
        "fridge", "stove", "side table", "floor lamp", "rug", "plant"
    };
    static const char* CLIP_ROLE_NAME[CLIP_COUNT] = {
        "idle", "walk", "run", "jump", "wave", "talk", "sit", "punch", "hit", "death",
        "gunidle", "gunaim", "gunfire", "gunrun"
    };

    fprintf(f, "pix city asset report\n");
    fprintf(f, "=====================\n\n");
    fprintf(f, "models    %zu / %d\n", cat.model_count, CITY_MAX_MODELS);
    fprintf(f, "meshes    %zu / %d      skinned %zu / %d\n",
            renderer.mesh_count, MAX_MESHES, renderer.skinned_mesh_count, MAX_MESHES);
    fprintf(f, "vertices  %zu / %d      indices %zu / %d\n",
            (size_t)renderer.vertex_cursor, MESH_POOL_VERTICES,
            (size_t)renderer.index_cursor, MESH_POOL_INDICES);
    fprintf(f, "materials %zu / %d\n", renderer.material_count, MAX_MATERIALS);
    fprintf(f, "clips     %zu / %d      arena %.1f / %.1f MB\n\n",
            loader.animation_clip_count, MAX_ANIMATIONS,
            (double)loader.arena.size / (double)MB, (double)loader.arena.capacity / (double)MB);

    fprintf(f, "sets\n");
    for (int i = 0; i < SET_COUNT; i++) {
        fprintf(f, "  %-14s %3d model%s%s\n", SET_NAME[i], (int)cat.sets[i].count,
                cat.sets[i].count == 1 ? " " : "s", cat.sets[i].count ? "" : "   << EMPTY");
        // Which models, by name, at what world size. A count alone cannot tell
        // "the set loaded" from "the set loaded, minus the one file that
        // mattered", and a scale alone cannot tell a house from a doll house.
        for (idx k = cat.sets[i].first; k < cat.sets[i].first + cat.sets[i].count; k++) {
            vec3 e = v3sub(cat.models[k].bounds_max, cat.models[k].bounds_min);
            float sc = cat.models[k].scale;
            fprintf(f, "      %-34s %5.2f x %5.2f x %5.2f m\n",
                    cat.models[k].name, e.x * sc, e.y * sc, e.z * sc);
        }
    }

    fprintf(f, "\nnamed models that failed to load\n");
    int missing = 0;
    for (int i = 0; i < ONE_COUNT; i++)
        if (cat.singles[i] == (idx)-1) { fprintf(f, "  %s\n", ONE_NAME[i]); missing++; }
    if (!missing) fprintf(f, "  (none)\n");

    fprintf(f, "\nvehicles  %zu\n", cat.vehicle_count);
    for (size_t i = 0; i < cat.vehicle_count; i++) {
        const city_vehicle_model& v = cat.vehicles[i];
        fprintf(f, "  %zu  %.1f x %.1f m  %.1f m tall  lifted %+.2f m onto its wheels\n",
                i, v.half_length * 2.0f, v.half_width * 2.0f, v.height, v.body_offset.y);
    }

    fprintf(f, "\ncharacters  %zu\n", cat.character_count);
    for (size_t i = 0; i < cat.character_count; i++) {
        const city_character& ch = cat.characters[i];
        size_t own = ch.model < loader.model_file_count
                   ? loader.model_files[ch.model].animation_count : 0;
        fprintf(f, "  rig %zu   %zu clips of its own, %zu borrowed, %.2f scale\n",
                i, own, ch.borrowed_count, ch.scale);
        for (int role = 0; role < CLIP_COUNT; role++) {
            const char* how = "MISSING";
            if (ch.clips[role] != (idx)-1) how = (ch.clips[role] < own) ? "own" : "borrowed";
            fprintf(f, "      %-6s %s\n", CLIP_ROLE_NAME[role], how);
        }
    }

    fprintf(f, "\nanimals  %zu\n", cat.animal_count);
    for (size_t i = 0; i < cat.animal_count; i++) {
        const city_character& an = cat.animals[i];
        fprintf(f, "  animal %zu   idle %s  walk %s  run %s\n", i,
                an.clips[CLIP_IDLE] != (idx)-1 ? "yes" : "NO",
                an.clips[CLIP_WALK] != (idx)-1 ? "yes" : "NO",
                an.clips[CLIP_RUN]  != (idx)-1 ? "yes" : "NO");
    }

    fclose(f);
}

// ---- collider overrides (see city_editor.hpp's collider mode) ----
//
// Every static collider in the city - a building, a fence, a tree trunk -
// is somebody's `ext = bounds_max - bounds_min` on the model it stands in
// for, so overriding those two vectors in place is all a collider edit is;
// nothing that reads a city_model has to know an edit ever happened. This
// just has to get the whole table to and from disk, keyed by index rather
// than by name, because that index is exactly what every prop already
// carries and what the editor lets you step through.
#define COLLIDER_SAVE_MAGIC   0x434F4C42u   // "COLB"
#define COLLIDER_SAVE_VERSION 1u
#define COLLIDER_SAVE_PATH    "collider_overrides.sav"

struct collider_save_header { uint32_t magic; uint32_t version; uint32_t count; };

static bool city_save_collider_bounds(const city_catalog& cat, const char* path = COLLIDER_SAVE_PATH) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    collider_save_header head = { COLLIDER_SAVE_MAGIC, COLLIDER_SAVE_VERSION, (uint32_t)cat.model_count };
    bool ok = fwrite(&head, sizeof(head), 1, f) == 1;
    for (size_t i = 0; ok && i < cat.model_count; i++) {
        ok = fwrite(&cat.models[i].bounds_min, sizeof(vec3), 1, f) == 1
          && fwrite(&cat.models[i].bounds_max, sizeof(vec3), 1, f) == 1;
    }
    fclose(f);
    return ok;
}

// Safe to call whether or not a save exists - a missing or unreadable file
// just leaves every model's freshly measured bounds alone, the same "no
// save yet, fall back to what generation already does" rule city_map.hpp's
// layout file follows.
static bool city_load_collider_bounds(city_catalog& cat, const char* path = COLLIDER_SAVE_PATH) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    collider_save_header head;
    bool ok = fread(&head, sizeof(head), 1, f) == 1
           && head.magic == COLLIDER_SAVE_MAGIC && head.version == COLLIDER_SAVE_VERSION;
    size_t n = ok ? (head.count < cat.model_count ? head.count : cat.model_count) : 0;
    for (size_t i = 0; i < n; i++) {
        vec3 mn, mx;
        if (fread(&mn, sizeof(vec3), 1, f) != 1 || fread(&mx, sizeof(vec3), 1, f) != 1) { ok = false; break; }
        cat.models[i].bounds_min = mn;
        cat.models[i].bounds_max = mx;
    }
    fclose(f);
    return ok;
}
