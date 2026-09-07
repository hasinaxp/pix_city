#pragma once
#include <stdio.h>
#include <time.h>
#include "../core/platform.hpp"
#include "../core/opengl_api.hpp"
#include "../core/opengl_utils.hpp"
#include "../core/shader_sources.hpp"
#include "../core/renderer.hpp"
#include "../core/physics.hpp"
#include "../core/sound.hpp"
#include "../core/font.hpp"
#include "../core/text.hpp"
#include "../core/profile.hpp"
#include "../loader/data_loader.hpp"
#include "../loader/png_writer.hpp"

#include "city_config.hpp"
#include "city_assets.hpp"
#include "city_map.hpp"
#include "city_traffic.hpp"
#include "city_peds.hpp"
#include "city_fx.hpp"
#include "city_weapons.hpp"
#include "city_animals.hpp"
#include "city_player.hpp"
#include "city_dialogue.hpp"
#include "city_house.hpp"
#include "city_editor.hpp"
#include "city_weather.hpp"
#include "city_minimap.hpp"
#include "city_reticle.hpp"

// Assembly and the frame loop.
//
// The systems below do not know about each other: the map is data, traffic and
// pedestrians read it, the physics world is the only thing they share, and this
// file is where they are wired together and drawn. Adding a system means adding
// an update call and a draw call here, not touching anything else.

#define CITY_WIDTH   1600
#define CITY_HEIGHT   900
// Vertical field of view, radians. Named because the reticle has to project a
// weapon's spread cone through the same lens the shot goes through.
#define CITY_FOV_Y   1.02f

// How long a full day takes when the clock is running, in real seconds.
#define CITY_DAY_SECONDS 900.0f

// How far a horn and a set of locked tyres carry, in metres.
#define HORN_EARSHOT  70.0f
#define SKID_EARSHOT  45.0f
// A gunshot carries much further than either, which is the whole point of one.
#define SHOT_EARSHOT 120.0f

// How far a levelled gun frightens people, and how often the crowd is asked
// to notice it - see city_peds_menace.
#define MENACE_RANGE  26.0f
#define MENACE_PERIOD  0.35f

// ---- what the frame is spent on ----
//
// PIX_PROF=1 prints these once a second. The split is by what would be changed
// to make one of them smaller, not by call depth: culling is the CPU walk that
// decides what to draw, submit is every GL call the frame makes, and present
// is the swap - which is where a frame waiting on the GPU or on vsync shows
// up, and telling that apart from CPU work is the first thing anybody wants
// to know.
enum {
    PROF_SIM,        // physics, traffic, the crowd, the player
    PROF_CULL,       // frustum walk and instance push
    PROF_SUBMIT,     // end_frame: sort, shadow, reflection, colour, post
    PROF_HUD,        // text, reticle, minimap
    PROF_PRESENT,    // buffer swap and window messages
    PROF_COUNT
};
static const char* const CITY_PROF_NAMES[PROF_COUNT] = {
    "sim", "cull", "submit", "hud", "present"
};

struct city_game {
    pix_window      window;
    pix_renderer    renderer;
    pix_data_loader loader;
    pix_audio       audio;

    city_catalog catalog;
    city_world   world;
    phys_world   physics;
    city_traffic traffic;
    city_peds    peds;
    city_dogs    dogs;
    city_player  player;
    city_dialogue dialogue;
    city_house_system house;
    city_editor  editor;
    city_fx      fx;
    city_pickups pickups;

    idx      text_shader;
    font_data hud_font;
    pixi_text hud;
    // The two pieces of HUD that are not text: a reticle in the middle of the
    // screen whenever a gun is in the hand, and a map of the streets around
    // the player in the bottom left. Both are sprite batches over a palette
    // texture rather than glyphs, because neither is made of letters.
    city_reticle reticle;
    city_minimap minimap;
    mat4     ui_projection;

    idx snd_step, snd_chime, snd_horn, snd_skid;
    // One synthesised report per weapon - see city_make_gunshot. A pistol, a
    // revolver and a rifle are the same envelope at three lengths and three
    // pitches, which is as much as the ear needs to tell them apart.
    idx snd_shot[WEAPON_COUNT];
    idx snd_dry;
    float step_timer;
    // How long since the crowd was last told there is a gun pointed at them.
    // Aiming is a state that lasts, and telling them every frame would burn a
    // full scan of the crowd on something that changes nobody mind that fast.
    float menace_timer;

    float time;
    float fps;
    bool  show_help;
    bool  mouse_look;
    bool  clock_running;
    city_weather weather;
    idx   ground_plane_mesh;

    // screenshots: F12 saves one, PIX_SHOT=<seconds> saves one and quits
    int   shot_count;
    float shot_deadline;
    // PIX_STRESS=<n> kills n pedestrians at once, a couple of seconds in.
    // "What happens when forty people die in the same moment" is exactly the
    // case that broke before - the animation pool ran dry and every later
    // corpse stood bolt upright - and it is not a case anybody can set up by
    // hand often enough to trust it stays fixed.
    int   stress_kills;
    bool  stress_done;
    // PIX_FIRE=<seconds> turns the player toward the nearest pedestrian and
    // holds the trigger down from then on. Same reason as PIX_STRESS above:
    // what a street does when somebody opens fire in it is not something that
    // can be set up by hand often enough to trust it, and it is certainly not
    // something a screenshot taken while both hands are on the keyboard can
    // catch.
    float auto_fire;

    // One per car, so a crash throws its debris once rather than on every
    // frame the two bodies are still resolving out of each other. It lives
    // here rather than on city_car because it is not something a driver
    // knows or a car is - it is how often this file is allowed to make a
    // noise about one.
    float crash_cooldown[MAX_CARS];

    pix_profiler profile;
};

void run_city_game();

// ---------------- implementation ----------------

// One flat plane under the whole map so the horizon never shows a void where
// the per-cell ground has been culled away.
//
// Its height matters, and it has to clear the *lowest* thing in the world, not
// just the roads. The roads are sunk so their kerbs line up with the pavement,
// which puts the tarmac below y = 0 - but the river bed is 6 m below that
// again, and a filler plane tucked just under the tarmac was being drawn
// straight across the top of the whole river. That is why the water could not
// be seen at all: not a shader problem, an opaque sheet over it. Below the bed
// it is out of the way of everything, and at the distance it is actually
// visible (past the culled cells, in fog) the extra drop cannot be told apart.
#define BASE_PLANE_Y (RIVER_BED - 1.0f)

static void city__draw_base_plane(const city_game& g, pix_renderer& renderer) {
    if (g.ground_plane_mesh == (idx)-1) return;
    mat4 m = mat4_trs_y2(v3(0.0f, BASE_PLANE_Y, 0.0f), 0.0f, CITY_EXTENT * 1.6f, 1.0f);
    push_instance(renderer, g.ground_plane_mesh, g.catalog.mat_horizon, m);
}

// Culling. Props were bucketed by cell at generation time, so a frame only
// walks the square of cells that could be on screen, rejects each cell's
// column against the frustum, and then pushes whatever survives. The per-prop
// work is a distance compare - the cell test has already done the hard part.
static void city__draw_world(const city_world& w, pix_renderer& renderer,
                             const frustum& view, vec3 eye) {
    int centre_x = world_to_cell(eye.x);
    int centre_z = world_to_cell(eye.z);
    int reach = (int)(VIEW_DISTANCE / CITY_TILE) + 1;

    int x0 = centre_x - reach, x1 = centre_x + reach;
    int z0 = centre_z - reach, z1 = centre_z + reach;
    if (x0 < 0) x0 = 0;
    if (z0 < 0) z0 = 0;
    if (x1 >= CITY_CELLS) x1 = CITY_CELLS - 1;
    if (z1 >= CITY_CELLS) z1 = CITY_CELLS - 1;

    const float half = CITY_TILE * 0.5f;
    const float column_top = 130.0f;        // taller than the tallest tower

    for (int z = z0; z <= z1; z++) {
        for (int x = x0; x <= x1; x++) {
            size_t cell = (size_t)z * CITY_CELLS + x;
            uint32_t first = w.cell_prop_start[cell];
            uint32_t last  = w.cell_prop_start[cell + 1];
            if (first == last) continue;

            float cx = cell_to_world(x), cz = cell_to_world(z);
            float dx = cx - eye.x, dz = cz - eye.z;
            float dist2 = dx * dx + dz * dz;
            if (dist2 > (VIEW_DISTANCE + CITY_TILE) * (VIEW_DISTANCE + CITY_TILE)) continue;

            if (!frustum_test_aabb(view, v3(cx - half, -2.0f, cz - half),
                                         v3(cx + half, column_top, cz + half))) continue;

            float dist = sqrtf(dist2);
            bool far_away = dist > LOD_DISTANCE;

            for (uint32_t i = first; i < last; i++) {
                const city_prop& p = w.props[i];
                if (dist > p.fade) continue;

                idx mesh = (far_away && p.lod_mesh != (idx)-1) ? p.lod_mesh : p.mesh;
                float sy = p.scale_y > 0.0f ? p.scale_y : p.scale;
                float sx = p.scale_x > 0.0f ? p.scale_x : p.scale;
                push_instance(renderer, mesh, p.material,
                              mat4_trs_y3(p.position, p.yaw, sx, sy, p.scale));
            }
        }
    }
}

// Water goes through its own queue: it is transparent, so the renderer draws it
// after everything opaque and with the wave shader instead of the surface one.
static void city__draw_water(const city_world& w, pix_renderer& renderer,
                             const frustum& view, vec3 eye) {
    for (size_t i = 0; i < w.water_count; i++) {
        const city_prop& s = w.water[i];
        float dx = s.position.x - eye.x, dz = s.position.z - eye.z;
        if (dx * dx + dz * dz > VIEW_DISTANCE * VIEW_DISTANCE) continue;
        if (!frustum_test_sphere(view, s.position, s.radius)) continue;
        push_water(renderer, s.mesh, s.material,
                   mat4_trs_y2(s.position, s.yaw, s.scale, s.scale_y));
    }
}

// ---- what is lit, and when ----
//
// Lights are submitted per frame, so "the street lamps come on at dusk" is this
// function and nothing else: by day it hands over none, after dusk it hands
// over the ones near the camera, and the renderer keeps the closest few. There
// is no lamp state to switch and no lights to create or destroy.
//
// Colours are the ones the real fittings have: sodium street lighting is warm
// and orange, headlights are a cool white, and having the two disagree is most
// of what makes a night street read as a night street.
static void city__submit_lights(city_game& g, pix_renderer& renderer, vec3 eye) {
    float night = renderer.night;

    // A muzzle flash is a light, not a sprite. It lasts two frames, so by day
    // it is barely there and after dark it throws the whole street into relief
    // for exactly as long as the bang - which is most of what makes firing a
    // gun at night feel like firing a gun.
    if (g.player.arms.flash > 0.0f)
        push_light(renderer, pix_point_light(
            g.player.arms.flash_at, MUZZLE_LIGHT_RANGE,
            v3scale(v3(1.00f, 0.72f, 0.34f),
                    MUZZLE_INTENSITY * (MUZZLE_DAY_FLOOR
                                        + (1.0f - MUZZLE_DAY_FLOOR) * night))));

    // Lit windows. One material carries every building in the city, so this is
    // one assignment a frame rather than anything per building - and because it
    // rides on the same night factor the lamps do, the whole city comes on
    // together over the same few minutes of dusk.
    // Every building material, not just the KayKit atlas: the new packs each
    // brought their own, and a downtown where half the blocks light up and the
    // other half stay dark reads as broken rather than as varied.
    vec3 glow = v3scale(v3(1.00f, 0.86f, 0.58f), night * 0.85f);
    for (size_t i = 0; i < g.catalog.building_material_count; i++)
        if (g.catalog.building_materials[i] < renderer.material_count)
            renderer.materials[g.catalog.building_materials[i]].window_glow = glow;

    if (night <= 0.01f) return;

    const float reach = 140.0f;
    vec3 lamp_color = v3scale(v3(1.00f, 0.60f, 0.24f), LAMP_INTENSITY * night);
    for (size_t i = 0; i < g.world.lamp_count; i++) {
        vec3 p = g.world.lamps[i];
        float dx = p.x - eye.x, dz = p.z - eye.z;
        if (dx * dx + dz * dz > reach * reach) continue;
        push_light(renderer, pix_point_light(p, LAMP_RADIUS, lamp_color));
    }

    // Headlights. A cone out of the nose of the car, aimed slightly down the
    // road rather than straight ahead, plus a small red bulb at the back so a
    // queue of traffic reads from behind.
    vec3 beam_color = v3scale(v3(1.00f, 0.97f, 0.88f), HEADLIGHT_INTENSITY * night);
    for (size_t i = 0; i < MAX_CARS; i++) {
        const city_car& c = g.traffic.cars[i];
        if (!c.active) continue;
        float dx = c.position.x - eye.x, dz = c.position.z - eye.z;
        if (dx * dx + dz * dz > reach * reach) continue;

        const city_vehicle_model& vm = g.catalog.vehicles[c.model];
        vec3 fwd = forward_from_yaw(c.yaw);
        vec3 nose = v3add(c.position, v3scale(fwd, vm.half_length));
        nose.y += 0.75f;
        vec3 beam = v3norm(v3(fwd.x, -0.22f, fwd.z));
        push_light(renderer, pix_spot_light(nose, beam, HEADLIGHT_RANGE,
                                            0.30f, 0.62f, beam_color));

        if (c.brake_light > 0.5f) {
            vec3 tail = v3sub(c.position, v3scale(fwd, vm.half_length));
            tail.y += 0.75f;
            push_light(renderer, pix_point_light(tail, 6.0f,
                v3scale(v3(1.00f, 0.07f, 0.04f), BRAKE_INTENSITY * night)));
        }
    }
}

static vec3 city__find_spawn(const city_world& w) {
    // walk outward from the middle of the map until a pavement cell turns up
    int cx = CITY_CELLS / 2, cz = CITY_CELLS / 2;
    for (int radius = 0; radius < CITY_CELLS / 2; radius++)
        for (int dz = -radius; dz <= radius; dz++)
            for (int dx = -radius; dx <= radius; dx++) {
                if (dx * dx + dz * dz < (radius - 1) * (radius - 1)) continue;
                int x = cx + dx, z = cz + dz;
                if (!city_in_bounds(x, z)) continue;
                const city_cell& c = city_at(w, x, z);
                if (c.kind != CELL_SIDEWALK || c.kerb == DIR_NONE) continue;
                // on the walking strip, not the cell centre - a building now
                // takes the back half of every pavement cell
                return city_stand_point(w, x, z);
            }
    return v3(0.0f, 0.0f, 0.0f);
}

// Saves the map editor's grid and rebuilds the whole city from it on the
// spot, rather than only on the next launch. city_generate rebuilds every
// static collider from scratch, which is exactly why this cannot just call
// it again on top of the running world: the old buildings' colliders would
// still be standing, over whatever the edit put in their place, and nothing
// would ever have told the phys_world their building was gone. Resetting
// the static side first is what makes calling city_generate a second time
// safe - see phys_reset_statics.
//
// Traffic and the crowd are torn down and reseeded rather than salvaged for
// the same reason: every car and pedestrian is mid-route between two cells
// of a road graph that may not exist any more.
static void city__editor_apply_map(city_game& g) {
    city_save_layout(g.world);
    g.editor.map_dirty = false;

    phys_reset_statics(g.physics);
    city_generate(g.world, g.catalog, g.physics, g.world.seed);
    city_house_add_colliders(g.physics);

    city_traffic_init(g.traffic, 0xC0FFEEu);
    city_peds_init(g.peds, g.loader, g.catalog, 0xBADCAFEu);
    city_dogs_init(g.dogs, g.loader, g.catalog, 0xD0655EEDu);
    // The guns lying in the street were placed on pavement cells that the
    // edit may have just turned into road or river.
    city_pickups_scatter(g.pickups, g.world, 0x6D5D5u);
    g.dialogue.active = false;         // the ped it referenced no longer exists
    g.dialogue.ped_index = -1;
    g.house.indoors = false;

    vec3 spawn = city__find_spawn(g.world);
    phys_body* b = phys_get_body(g.physics, g.player.body);
    if (b) { b->position = spawn; b->velocity = v3(0.0f, 0.0f, 0.0f); }
    g.player.position = spawn;
    g.player.car = -1;
    // After the respawn, not before it - the point of these three is that they
    // are near where the player is about to be standing.
    city_pickups_seed_near(g.pickups, g.world, spawn, 3);
}

// One frame's worth of submission, in the order the renderer wants it: the
// camera, then what lights the world, then what is in it.
//
// Everything here is a submission - no system draws itself and none of them
// know about each other. Adding a system to the world means adding one line to
// this function and nothing else, which is the property worth protecting as
// more of them arrive.
static void city__render_frame(city_game& g) {
    camera cam;
    { PIX_PROFILE(g.profile, PROF_CULL);
    city_player_camera(g.player, g.physics, &cam);
    begin_frame(g.renderer, cam);

    pix_set_time(g.renderer, g.time);
    mat4 view_proj = mat4_mul(g.renderer.projection_matrix, g.renderer.view_matrix);
    frustum view = frustum_from_viewproj(view_proj);

    if (g.editor.mode == EDITOR_COLLIDER) {
        city_editor_draw_collider(g.editor, g.catalog, g.renderer);
    } else if (g.house.indoors) {
        city_house_draw(g.house, g.catalog, g.renderer);
    } else {
        city__submit_lights(g, g.renderer, cam.position);
        city__draw_base_plane(g, g.renderer);
        city__draw_water(g.world, g.renderer, view, cam.position);
        city__draw_world(g.world, g.renderer, view, cam.position);
        city_traffic_draw(g.traffic, g.catalog, g.renderer, view, cam.position);
        if (!city_player_on_foot(g.player))
            city_car_draw(g.traffic.cars[g.player.car], g.catalog, g.renderer);
        city_peds_draw(g.peds, g.catalog, g.renderer, view, cam.position);
        city_dogs_draw(g.dogs, g.catalog, g.renderer, view, cam.position);
        city_pickups_draw(g.pickups, g.catalog, g.renderer, view, cam.position, g.time);
    }
    city_player_draw(g.player, g.catalog, g.renderer);
    // Last, and outside the indoor/outdoor split: blood follows the player
    // through a front door, and the renderer draws the particle queue after
    // everything opaque whatever order it was filled in.
    city_fx_draw(g.fx, g.renderer);

    }
    { PIX_PROFILE(g.profile, PROF_SUBMIT);
      end_frame(g.renderer);
    }
}

// ---- combat ----
//
// Two very different kinds of hit share one landing point, city_ped_hit: a
// car doing real speed is what actually kills someone, a fist is a stagger
// that only kills if it happens to finish off someone already hurt. Neither
// system needs to know the other exists - each just calls the same function
// with numbers sized for what it is.

// A body pressed against a fast car's bumper, not just standing near it - the
// same oriented-box distance test city__nearest_car uses to find a car to get
// into, run the other way to find a pedestrian actually in the way of one.
// Below walking-into-someone speed nothing happens here at all: the ordinary
// physics solver already keeps the two apart, and turning that into a
// "hit" as well would make every pedestrian on a crowded pavement stagger
// every time a car eased past them.
#define VEHICLE_HIT_MIN_KPH   8.0f
#define VEHICLE_HIT_RADIUS    0.55f

static void city__vehicle_ped_impacts(city_game& g) {
    for (size_t i = 0; i < MAX_CARS; i++) {
        city_car& c = g.traffic.cars[i];
        if (!c.active) continue;
        float speed_kph = fabsf(c.speed) * 3.6f;
        if (speed_kph < VEHICLE_HIT_MIN_KPH) continue;

        const city_vehicle_model& vm = g.catalog.vehicles[c.model];
        obb2 box = { v2(c.position.x, c.position.z),
                     v2(vm.half_width, vm.half_length), c.yaw + vm.model_yaw };
        vec3 fwd = forward_from_yaw(c.yaw);
        // A gentle nudge at the threshold speed, a genuine launch well above
        // it - the same shape of curve LAMP_INTENSITY and friends use
        // elsewhere in this file for "the number that makes the effect read
        // right at both ends", just for a punch instead of a light.
        float dmg    = clampf(speed_kph * 1.8f, 15.0f, 150.0f);
        float launch = clampf(speed_kph * 0.12f, 2.0f, 9.0f);
        vec3 impulse = v3(fwd.x * launch, launch * 0.55f, fwd.z * launch);

        for (size_t j = 0; j < MAX_PEDS; j++) {
            city_ped& ped = g.peds.people[j];
            if (!ped.active || ped.state == PED_STATE_DEAD || ped.invuln > 0.0f) continue;
            vec2 pp = v2(ped.position.x, ped.position.z);
            float gap = v2len(v2sub(pp, closest_point_obb(box, pp)));
            if (gap > VEHICLE_HIT_RADIUS) continue;
            city_fx_blood(g.fx, v3(ped.position.x, ped.position.y + 1.0f, ped.position.z), fwd);
            city_ped_hit(g.peds, g.catalog, g.physics, (int)j, impulse, dmg, c.position);
            // A car ploughing through a crowd is the loudest thing that can
            // happen on a street; everyone near it gets out of the way.
            city_peds_alarm(g.peds, ped.position, PED_ALARM_RADIUS);
        }
    }
}

// The player is unkillable by design - no health, no game over, just a fist -
// so this only ever looks for someone else to land on. `city_player_punch_frame`
// gates this to the one frame in the swing the fist is actually out, and to
// once per swing.
#define PUNCH_RANGE       1.9f
#define PUNCH_ARC_COS     0.55f   // roughly a 66 degree half-angle in front of the player
#define PUNCH_DAMAGE      34.0f
#define PUNCH_LAUNCH      3.2f

static void city__player_melee(city_game& g) {
    if (!city_player_punch_frame(g.player)) return;

    vec3 fwd = forward_from_yaw(g.player.yaw);
    int best = -1;
    float best_d2 = PUNCH_RANGE * PUNCH_RANGE;
    for (size_t i = 0; i < MAX_PEDS; i++) {
        city_ped& ped = g.peds.people[i];
        if (!ped.active || ped.state == PED_STATE_DEAD) continue;
        vec3 to = v3sub(ped.position, g.player.position);
        to.y = 0.0f;
        float d2 = v3dot(to, to);
        if (d2 > best_d2) continue;
        if (d2 > 1e-4f && v3dot(v3scale(to, 1.0f / sqrtf(d2)), fwd) < PUNCH_ARC_COS) continue;
        best_d2 = d2;
        best = (int)i;
    }
    if (best < 0) return;

    vec3 impulse = v3(fwd.x * PUNCH_LAUNCH, 1.6f, fwd.z * PUNCH_LAUNCH);
    vec3 hit_at = g.peds.people[best].position;
    city_fx_sparks(g.fx, v3(hit_at.x, hit_at.y + 1.4f, hit_at.z), v3(fwd.x, 0.5f, fwd.z), 3);
    bool was_alive = g.peds.people[best].state != PED_STATE_DEAD;
    city_ped_hit(g.peds, g.catalog, g.physics, best, impulse, PUNCH_DAMAGE, g.player.position);
    // and the street sees it happen
    city_peds_alarm(g.peds, hit_at, PED_ALARM_RADIUS);

    bool killed = was_alive && g.peds.people[best].state == PED_STATE_DEAD;
    g.peds.reputation -= killed ? PED_REPUTATION_KILL : PED_REPUTATION_PUNCH;
    if (g.peds.reputation < -1.0f) g.peds.reputation = -1.0f;
}

// Distance and pan for something happening out in the world - defined with the
// rest of the audio further down, and used up here by the crash below.
static bool city__world_sound(const city_game& g, vec3 at, float reach,
                              float* out_volume, float* out_pan);

// ---- crashes ----
//
// The solver already records the largest impulse each body took this step, so
// a crash needs no collision callback and no contact list: a car whose body
// changed velocity by several metres per second in one step has hit something,
// and how hard is the same number. Everything below - how much glass, how much
// smoke, whether the street turns to look - comes off it.
#define CRASH_IMPACT_MIN  4.0f     // m/s of velocity change; below this it is a nudge
#define CRASH_SERIOUS     9.0f     // and above this, something the pavement notices
#define CRASH_COOLDOWN    0.8f

static void city__crash_fx(city_game& g, float dt) {
    for (size_t i = 0; i < MAX_CARS; i++) {
        if (g.crash_cooldown[i] > 0.0f) g.crash_cooldown[i] -= dt;
        const city_car& c = g.traffic.cars[i];
        if (!c.active) continue;
        const phys_body* b = phys_get_body(g.physics, c.body);
        if (!b || b->impact < CRASH_IMPACT_MIN || g.crash_cooldown[i] > 0.0f) continue;
        g.crash_cooldown[i] = CRASH_COOLDOWN;

        const city_vehicle_model& vm = g.catalog.vehicles[c.model];
        vec3 fwd = forward_from_yaw(c.yaw);
        // Off the end of the car that was doing the travelling: a car reversing
        // into something throws its debris off the boot, not the bonnet.
        float along = c.speed >= 0.0f ? vm.half_length : -vm.half_length;
        vec3 nose = v3add(c.position, v3scale(fwd, along));
        nose.y += 0.55f;

        float force = clampf(b->impact / (CRASH_SERIOUS * 1.5f), 0.2f, 1.0f);
        city_fx_sparks(g.fx, nose, v3(-fwd.x * 0.4f, 0.7f, -fwd.z * 0.4f),
                       6 + (int)(force * 16.0f));
        city_fx_dust(g.fx, nose, 0.9f, 3 + (int)(force * 8.0f));
        city_fx_smoke(g.fx, nose, 1.4f);

        if (b->impact < CRASH_SERIOUS) continue;

        // A real crash is the loudest thing on an ordinary street, and a
        // street stops to look at one. The same call a punch and a gunshot
        // make - what differs is only how far it carries.
        city_peds_alarm(g.peds, c.position, PED_ALARM_RADIUS * 1.5f);
        float vol, pan;
        if (g.snd_skid != SOUND_INVALID
            && city__world_sound(g, c.position, SKID_EARSHOT * 1.6f, &vol, &pan))
            play_sound(g.audio, g.snd_skid, vol * 0.7f, 0.45f, false, pan);
    }
}

// ---- shooting ----
//
// The player's own code raised a flag and knows nothing else (see the trigger
// section of city_player_update); this is the half that can reach the crowd,
// the traffic and the particle pool, so this is where a pulled trigger becomes
// a round going somewhere.
//
// The one thing worth being careful about is where a shot is aimed. Starting
// it at the muzzle and firing along the *barrel* means the player shoots at
// whatever their character's hand happens to be pointing at, which in a third
// person camera is never what they were looking at. So the line is found from
// the camera first - what is under the middle of the screen - and the round
// then travels from the muzzle to that point. The gun and the crosshair agree
// because they are made to meet, not because the arm was solved to aim.
static void city__player_shoot(city_game& g) {
    city_arms& a = g.player.arms;
    a.fired = false;
    if (!a.trigger) return;
    a.trigger = false;

    const city_weapon* w = city_arms_current(a, g.catalog);
    if (!w || !a.muzzle_valid) return;

    camera cam;
    city_player_camera(g.player, g.physics, &cam);
    vec3 look = city_player_aim(g.player);

    float reach = w->range, hit = 0.0f;
    if (phys_raycast(g.physics, cam.position, look, reach, &hit)) reach = hit;
    if (phys_raycast_bodies(g.physics, cam.position, look, reach,
                            PHYS_LAYER_PED | PHYS_LAYER_VEHICLE, g.player.body,
                            &hit) != (idx)-1) reach = hit;
    // Anything nearer than this is behind or beside the muzzle, and aiming at
    // it would fire the round backwards past the player's own ear.
    if (reach < 3.0f) reach = 3.0f;

    vec3 target = v3add(cam.position, v3scale(look, reach));
    vec3 line = v3norm(v3sub(target, a.muzzle));
    city_weapon_fire(a, g.catalog, g.peds, g.physics, g.fx, a.muzzle, line, g.player.body);
}

// Walking over a gun picks it up. No key, deliberately: 'F' is already a car
// and 'E' is already a door and a conversation, and a third meaning stacked
// onto either is a press that does the wrong one of three things.
static void city__player_pickups(city_game& g) {
    if (!city_player_on_foot(g.player) || g.house.indoors) return;
    int which = city_pickups_nearest(g.pickups, g.player.position, PICKUP_RANGE);
    if (which < 0) return;

    city_pickup& it = g.pickups.items[which];
    const city_weapon& w = g.catalog.weapons[it.weapon];
    if (!city_arms_take(g.player.arms, g.catalog, it.weapon,
                        w.magazine * (PICKUP_MAGS - 1))) return;
    it.active = false;
    play_sound(g.audio, g.snd_chime, 0.5f, 1.25f);
}

// ---- being hit back ----
//
// A pedestrian who decided to fight rather than run closes on the player and
// swings; city_peds_update flags the frame a fist lands and knows nothing
// about who it landed on. This is the other half of that: the player is the
// thing being punched, so the player's own code is what turns a flag into a
// shove.
//
// There is no player health in this game by design, so a landed punch is felt
// as being knocked about rather than as a number coming down - which is also
// what keeps a crowd of fighters dangerous without being lethal.
static void city__ped_melee(city_game& g) {
    phys_body* pb = phys_get_body(g.physics, g.player.body);
    for (size_t i = 0; i < MAX_PEDS; i++) {
        city_ped& ped = g.peds.people[i];
        if (!ped.struck) continue;
        ped.struck = false;
        if (!pb || !city_player_on_foot(g.player)) continue;

        vec3 push = v3sub(g.player.position, ped.position);
        push.y = 0.0f;
        float len = v3len(push);
        if (len < 1e-3f) continue;
        push = v3scale(push, PED_PUNCH_SHOVE / len);
        pb->velocity = v3add(pb->velocity, v3(push.x, 1.2f, push.z));
        play_sound(g.audio, g.snd_chime, 0.35f, 0.6f);
    }
}

// One frame's worth of simulation. Fixed order: the player moves first because
// everything else takes the player's position as the centre of the world it
// bothers to simulate, then the solver runs once for all of them, then each
// system reads back what the solver decided.
static void city__simulate(city_game& g, float dt) {
    // Checked before the player moves, so a teleport this frame is what the
    // rest of the frame - movement, the camera, the animation - actually
    // sees, rather than catching up a frame late.
    city_house_update(g.house, g.world, g.player, g.physics, g.window, dt);

    // A conversation only starts on foot, outdoors, and when the same 'E'
    // press would not rather walk the player through a front door -
    // house.near_house already answers that question for this frame.
    bool talk_allowed = !g.house.indoors && g.house.near_house < 0
                       && city_player_on_foot(g.player) && g.editor.mode == EDITOR_OFF;
    // What a pedestrian can talk about that isn't about them: the weather they
    // are standing in, the hour, and the district they happen to be in.
    city_dialogue_context talk_ctx;
    talk_ctx.hour     = g.renderer.hour;
    talk_ctx.rain     = g.weather.now.rain;
    talk_ctx.overcast = g.weather.now.overcast;
    {
        const city_cell& here = city_at(g.world, world_to_cell(g.player.position.x),
                                                 world_to_cell(g.player.position.z));
        talk_ctx.zone = here.zone < ZONE_COUNT ? here.zone : 0;
    }
    city_dialogue_update(g.dialogue, g.peds, g.player, g.physics, g.window, dt,
                         talk_allowed, talk_ctx);

    // While a conversation is running the player is frozen mid-scene - Pokemon
    // style, the camera stops with them - so ordinary movement, look and
    // punch input are all skipped rather than fought against every frame.
    if (!g.dialogue.active)
        city_player_update(g.player, g.window, g.traffic, g.catalog, g.physics, dt);

    city_traffic_update(g.traffic, g.world, g.catalog, g.physics, g.player.position, g.time, dt);
    city_peds_update(g.peds, g.world, g.catalog, g.physics, g.player.position, g.time,
                     g.renderer.hour, dt, g.physics.jobs);
    city_dogs_update(g.dogs, g.world, g.catalog, g.physics, g.player.position, g.time, dt);
    // Fills the shared pose banks the crowd is drawn from. Without it every
    // bank pose stays zeroed, and a zero-bone pose uploads no bone matrices at
    // all - so each pedestrian skins itself with whatever rig was drawn before
    // it, which is what turned the 62-bone rigs into giant torn shapes.
    city_peds_animate(g.peds, g.catalog, g.time);
    city__vehicle_ped_impacts(g);
    city__ped_melee(g);
    if (!g.dialogue.active) {
        city__player_melee(g);
        city__player_shoot(g);
        city__player_pickups(g);
    }

    // A gun that is up is a threat whether or not it is fired, and the crowd
    // in front of it reacts to that on its own clock rather than every frame -
    // see city_peds_menace.
    g.menace_timer -= dt;
    if (g.player.arms.aiming && g.menace_timer <= 0.0f) {
        g.menace_timer = MENACE_PERIOD;
        vec3 facing = city_player_aim(g.player);
        facing.y = 0.0f;
        city_peds_menace(g.peds, g.player.position, v3norm(facing), MENACE_RANGE);
    }

    city_fx_update(g.fx, dt, g.player.position);

    phys_step(g.physics, dt);

    city__read_back_all(g.traffic, g.physics);
    // After the solver, because the impulses it recorded are what a crash is.
    city__crash_fx(g, dt);
    if (city_player_on_foot(g.player)) {
        phys_body* pb = phys_get_body(g.physics, g.player.body);
        if (pb) g.player.position = pb->position;
    } else {
        g.player.position = g.traffic.cars[g.player.car].position;
        g.player.yaw = g.traffic.cars[g.player.car].yaw;
    }
}

static void city__build_hud(city_game& g, char* buffer, size_t capacity) {
    const city_player& p = g.player;
    static const char* ZONE_NAME[ZONE_COUNT] = {
        "Downtown", "Commercial", "Residential", "Suburb", "Industrial", "Parkland"
    };

    if (g.editor.mode == EDITOR_COLLIDER) {
        city_editor_collider_hud(g.editor, g.catalog, buffer, capacity);
        return;
    }
    // Indoors, the player's position is the shared interior far outside the
    // generated grid - a cell/zone lookup there would just read whatever the
    // clamp at the grid's edge happens to be, which is not a place, so this
    // is its own line instead of falling through to the outdoor one below.
    const char* where = "Home";
    if (!g.house.indoors) {
        int cx = world_to_cell(p.position.x), cz = world_to_cell(p.position.z);
        const city_cell& cell = city_at(g.world, cx, cz);
        where = ZONE_NAME[cell.zone < ZONE_COUNT ? cell.zone : 0];
    }

    // What pressing the interact key would do right now, if anything - the
    // same question both HUD layouts below answer, so it is worked out once.
    int car = city_player_on_foot(p)
        ? city__nearest_car(g.traffic, g.catalog, p.position, ENTER_CAR_RANGE) : -1;
    // What is in the player's hand, if anything. Rounds in the gun, then
    // rounds in the pocket - the two numbers every shooter shows, because
    // "can I keep firing" and "can I reload" are different questions.
    char gun[64] = "";
    {
        const city_weapon* w = city_arms_current(p.arms, g.catalog);
        if (w) {
            if (p.arms.reload > 0.0f)
                snprintf(gun, sizeof(gun), "   -   %s  reloading", w->name);
            else
                snprintf(gun, sizeof(gun), "   -   %s  %d/%d", w->name,
                         p.arms.magazine[p.arms.weapon], p.arms.reserve[p.arms.weapon]);
        }
    }

    const char* action = 0;
    if (g.house.indoors)              action = "[E] leave the house";
    else if (car >= 0)                action = "[F] get in";
    else if (g.house.near_house >= 0) action = "[E] go inside";
    else if (car < 0 && city_player_on_foot(p) && !g.dialogue.active
             && city_dialogue_target_available(g.peds, p)) action = "[E] talk";

    if (g.show_help) {
        char extra[64] = "";
        if (action) snprintf(extra, sizeof(extra), "   -   %s", action);
        snprintf(buffer, capacity,
            "%.0f fps   %s   %s%s%s\n"
            "\n"
            "  W A S D   walk / drive        mouse   look\n"
            "  shift     run                 space   jump  (handbrake in a car)\n"
            "  F         get in / out of the nearest car\n"
            "  E         go through a house's front door / talk to someone\n"
            "  click     throw a punch / fire   right click   aim\n"
            "  Q         next gun / holster  R       reload   1 2 3   pick a gun\n"
            "  [ ]       wind the clock      N       run the day / night cycle\n"
            "  J         cycle the weather\n"
            "  TAB       hide this panel     M       release the mouse\n"
            "  F5        map editor          F6      collider editor\n"
            "  ESC       quit\n"
            "\n"
            "%02d:%02d   %s   %zu cars   %zu people   %zu props   %zu colliders   "
            "%zu lamps   %d lights   %.0f MB",
            g.fps, city_player_on_foot(p) ? "on foot" : "driving", where, gun, extra,
            (int)g.renderer.hour, (int)((g.renderer.hour - floorf(g.renderer.hour)) * 60.0f),
            city_weather_name(g.weather),
            g.traffic.count, g.peds.count, g.world.prop_count, g.physics.static_count,
            g.world.lamp_count, g.renderer.lights.packed_count,
            (double)g.loader.arena.size / (double)MB);
        return;
    }

    if (g.house.indoors) {
        snprintf(buffer, capacity, "%.0f fps   %s   %s   [TAB] help", g.fps, where, action);
    } else if (city_player_on_foot(p)) {
        snprintf(buffer, capacity, "%.0f fps   %s   on foot%s%s%s   [TAB] help",
                 g.fps, where, gun, action ? "   -   " : "", action ? action : "");
    } else {
        float kph = fabsf(g.traffic.cars[p.car].speed) * 3.6f;
        snprintf(buffer, capacity, "%.0f fps   %s   %3.0f km/h   [F] get out   [TAB] help",
                 g.fps, where, kph);
    }
}

// How loud and how far to which ear something happening out in the world
// should be. The mixer is not positional, so distance and pan are worked out
// here against the camera the player is actually listening from.
static bool city__world_sound(const city_game& g, vec3 at, float reach,
                              float* out_volume, float* out_pan) {
    vec3 to = v3sub(at, g.player.position);
    to.y = 0.0f;
    float d = v3len(to);
    if (d > reach) return false;

    float fall = 1.0f - d / reach;
    *out_volume = fall * fall;              // inverse-square-ish, and silent at the edge
    if (*out_volume < 0.02f) return false;

    vec3 right = dir_right(forward_from_yaw(g.player.yaw));
    *out_pan = d > 0.5f ? clampf(v3dot(v3scale(to, 1.0f / d), right), -1.0f, 1.0f) : 0.0f;
    return true;
}

// Horns and tyres. city__drive_ai raises a flag for the one frame a driver
// reaches for either, and this is what turns those flags into something the
// player can hear - the traffic system itself has no idea where the listener
// is, which is the whole reason the flags exist rather than a play_sound call
// down in the AI.
static void city__traffic_audio(city_game& g) {
    int horns = 0;
    for (size_t i = 0; i < MAX_CARS; i++) {
        city_car& c = g.traffic.cars[i];
        if (!c.active) continue;

        if (c.honking) {
            c.honking = false;
            float vol, pan;
            // Two horns going at once is a busy street; six is a wall of noise
            // that drowns out everything else the frame had to say.
            if (horns < 2 && g.snd_horn != SOUND_INVALID
                && city__world_sound(g, c.position, HORN_EARSHOT, &vol, &pan)) {
                play_sound(g.audio, g.snd_horn, vol * 0.5f,
                           0.92f + (float)(i % 7) * 0.03f, false, pan);
                horns++;
            }
        }
        if (c.skidding) {
            c.skidding = false;
            float vol, pan;
            if (g.snd_skid != SOUND_INVALID
                && city__world_sound(g, c.position, SKID_EARSHOT, &vol, &pan))
                play_sound(g.audio, g.snd_skid, vol * 0.42f, 0.96f, false, pan);
            // and the tyres leave something behind, off the back axle rather
            // than out of the middle of the car
            const city_vehicle_model& vm = g.catalog.vehicles[c.model];
            vec3 back = v3sub(c.position, v3scale(forward_from_yaw(c.yaw), vm.half_length * 0.8f));
            city_fx_dust(g.fx, v3(back.x, back.y + 0.1f, back.z), vm.half_width, 5);
        }
    }
}

// footsteps, and a chime on getting in or out of a car
static void city__update_audio(city_game& g, float dt, bool was_on_foot) {
    if (was_on_foot != city_player_on_foot(g.player))
        play_sound(g.audio, g.snd_chime, 0.55f, city_player_on_foot(g.player) ? 0.9f : 1.15f);

    city__traffic_audio(g);

    // The shot. Fired straight rather than through city__world_sound - the
    // player is the one holding it, so it is not a thing happening somewhere
    // in the world that has to be placed against the listener.
    city_arms& arms = g.player.arms;
    if (arms.fired) {
        arms.fired = false;
        idx shot = arms.weapon >= 0 && arms.weapon < WEAPON_COUNT
                 ? g.snd_shot[arms.weapon] : SOUND_INVALID;
        if (shot != SOUND_INVALID)
            play_sound(g.audio, shot, 0.8f, 0.94f + (float)(rand() % 100) * 0.0012f);
    }
    if (arms.clicked) {
        arms.clicked = false;
        if (g.snd_dry != SOUND_INVALID) play_sound(g.audio, g.snd_dry, 0.4f, 1.0f);
    }

    if (!city_player_on_foot(g.player) || g.player.speed < 0.4f) {
        g.step_timer = 0.0f;
        return;
    }
    // stride rate follows walking speed, so running is a faster patter
    g.step_timer -= dt * g.player.speed;
    if (g.step_timer <= 0.0f) {
        g.step_timer = 1.55f;
        play_sound(g.audio, g.snd_step, 0.30f, 0.92f + (float)(rand() % 100) * 0.0016f);
    }
}

// Whatever is on screen, to a PNG next to the executable.
//
// It reads the back buffer, so what it captures is exactly the finished
// frame - post processing, HUD and all - rather than a second render of the
// scene that might differ from it. glReadPixels hands back rows bottom-up,
// which is why they go out in reverse.
static void city__screenshot(const char* path) {
    static unsigned char rgb[CITY_WIDTH * CITY_HEIGHT * 3];
    static unsigned char flip[CITY_WIDTH * CITY_HEIGHT * 3];
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, CITY_WIDTH, CITY_HEIGHT, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    for (int y = 0; y < CITY_HEIGHT; y++)
        memcpy(flip + (size_t)y * CITY_WIDTH * 3,
               rgb + (size_t)(CITY_HEIGHT - 1 - y) * CITY_WIDTH * 3, CITY_WIDTH * 3);
    png_write_rgb(path, flip, CITY_WIDTH, CITY_HEIGHT);
}

static city_game g_city;   // several megabytes of world; not a stack citizen

void run_city_game() {
    city_game& g = g_city;

    g.window = pix_create_window("pix city", CITY_WIDTH, CITY_HEIGHT);
    if (!opengl_load_functions()) {
        MessageBoxA(0, "failed to load OpenGL 4.4 functions", "pix city", MB_OK);
        return;
    }

    // Which adapter actually gave out the context. Worth a line of output on
    // every run: a laptop has two, they are an order of magnitude apart in
    // per-draw driver cost, and "why is it suddenly half the frame rate" has
    // exactly this answer often enough to be worth never having to guess.
    {
        const char* gpu = (const char*)glGetString(GL_RENDERER);
        printf("gpu: %s\n", gpu ? gpu : "unknown");
        fflush(stdout);
    }

    idx shader = opengl_create_shader(VSHDER_BASIC, shader_with_pbr(FSHDER_BASIC));
    g.renderer = pix_create_renderer(CITY_WIDTH, CITY_HEIGHT, shader, v3(0.55f, 0.70f, 0.86f));
    g.renderer.projection_matrix =
        mat4_perspective(CITY_FOV_Y, (float)CITY_WIDTH / (float)CITY_HEIGHT, 0.25f, 1400.0f);
    // Per cascade, not for the whole atlas: three of these sit side by side, so
    // the sun is drawing 4608 x 1536 of depth every frame.
    pix_enable_effects(g.renderer, 1024);

    // Character rigs resampling every bone onto a merged timeline used to be
    // by far the biggest thing in here; the nature/road-object/vehicle packs
    // added after them are individually textured glTF files rather than a
    // few shared atlases, and there are a lot of them.
    g.loader = pix_create_data_loader(768 * MB);

    if (!city_load_catalog(g.catalog, g.loader, g.renderer)) {
        MessageBoxA(0,
            "Could not load the city art.\n\n"
            "Expected assets/city/{commercial,suburban,vehicles,nature,rail} and\n"
            "assets/KayKit_City_Builder - run the project from its root folder.",
            "pix city", MB_OK);
        return;
    }
    // A collider edit from a previous session, if there was one - has to
    // land before city_generate ever measures a bounding box off a model,
    // which is what every static prop's collider comes from.
    city_load_collider_bounds(g.catalog);
    city_editor_init(g.editor, g.renderer);

    // What survived the load, in a file, every run - see city_report_catalog.
    city_report_catalog(g.catalog, g.loader, g.renderer, "asset_report.txt");

    // one oversized quad standing in for the ground past the culled cells
    g.ground_plane_mesh = g.catalog.singles[ONE_QUAD] != (idx)-1
        ? g.catalog.models[g.catalog.singles[ONE_QUAD]].mesh : (idx)-1;

    phys_create_world(g.physics);
    city_generate(g.world, g.catalog, g.physics, 20260905u);
    city_house_add_colliders(g.physics);

    city_traffic_init(g.traffic, 0xC0FFEEu);
    city_peds_init(g.peds, g.loader, g.catalog, 0xBADCAFEu);
    city_dogs_init(g.dogs, g.loader, g.catalog, 0xD0655EEDu);
    g.weather = city_weather_init(0x5EA50Eu);

    // PIX_SPAWN=<cell x>,<cell z> starts the player somewhere specific. The
    // city is a kilometre across and the interesting thing to look at is
    // rarely where the default spawn puts you - this is how a change to the
    // waterfront gets checked without walking there first.
    vec3 spawn = city__find_spawn(g.world);
    {
        const char* at = getenv("PIX_SPAWN");
        int sx = 0, sz = 0;
        if (at && sscanf(at, "%d,%d", &sx, &sz) == 2 && city_in_bounds(sx, sz)) {
            // A bridge deck is road, and road is not walkable ground as far as
            // the search below is concerned - so asking for a bridge cell used
            // to put you on the nearest bank, which is the one place a bridge
            // cannot be seen from. Asking for one by cell means standing on it.
            if (city_at(g.world, sx, sz).flags & CELLF_BRIDGE) {
                spawn = cell_centre(sx, sz, ROAD_Y);
            } else {
                // nearest cell somebody can actually stand on, so asking for a
                // spot in the sea puts you on the beach beside it
                for (int radius = 0; radius < 24; radius++) {
                    bool done = false;
                    for (int dz = -radius; dz <= radius && !done; dz++)
                        for (int dx = -radius; dx <= radius && !done; dx++) {
                            if (!city_is_walkable(g.world, sx + dx, sz + dz)) continue;
                            spawn = city_stand_point(g.world, sx + dx, sz + dz);
                            done = true;
                        }
                    if (done) break;
                }
            }
        }
    }
    // PIX_SHOWROOM=<set>[,<yaw degrees>] stands every model of one catalogue
    // set in a row in front of the spawn, all turned the same way.
    //
    // It exists because nothing in a glTF file says which way the thing it
    // holds is meant to face, and a building placed a quarter turn out is not
    // subtly wrong - it presents a blank gable to the street and puts its
    // front door in next door's garden. Four runs of this at 0/90/180/270
    // settle the whole set at once, and the answers go in CITY_MODEL_FACING.
    {
        const char* show = getenv("PIX_SHOWROOM");
        int set = 0, from = 0, howmany = 0;
        float deg = 0.0f;
        if (show && sscanf(show, "%d,%f,%d,%d", &set, &deg, &from, &howmany) >= 1
            && set >= 0 && set < SET_COUNT && g.catalog.sets[set].count) {
            // A slice of the set, for the sets too long to fit one screen -
            // the house set is twenty-six models, which at any spacing wide
            // enough to see one is half a kilometre of row.
            if (from < 0 || from >= (int)g.catalog.sets[set].count) from = 0;
            if (howmany <= 0 || from + howmany > (int)g.catalog.sets[set].count)
                howmany = (int)g.catalog.sets[set].count - from;
            idx first = g.catalog.sets[set].first + (idx)from;
            int n = howmany;
            // Clear the stage. Standing the row in the middle of whatever the
            // generator built there leaves it behind a park railing and under
            // somebody's tree, which is exactly as useful as not having the
            // mode at all.
            vec3 stage = v3(spawn.x, 0.0f, spawn.z - 45.0f);
            // The ground stays. It is emitted as props like everything else,
            // and clearing it along with the scenery drops the whole stage
            // into the sea - which is memorable, and useless.
            const city_model* ground_q = city_get(g.catalog, g.catalog.singles[ONE_QUAD]);
            const city_model* water_q  = city_get(g.catalog, g.catalog.singles[ONE_WATER_TILE]);
            idx ground_mesh = ground_q ? ground_q->mesh : (idx)-1;
            idx water_mesh  = water_q  ? water_q->mesh  : (idx)-1;
            size_t kept = 0;
            for (size_t i = 0; i < g.world.prop_count; i++) {
                const city_prop& q = g.world.props[i];
                float dx = q.position.x - stage.x, dz = q.position.z - stage.z;
                bool near_stage = dx * dx + dz * dz < 55.0f * 55.0f;
                bool is_ground = (q.mesh == ground_mesh || q.mesh == water_mesh);
                if (near_stage && !is_ground) continue;
                g.world.props[kept++] = q;
            }
            g.world.prop_count = kept;

            // Facing the camera, which looks down -Z from behind the player,
            // plus whatever turn is being tested.
            float yaw = 3.14159265f + deg * 0.01745329f;
            for (int i = 0; i < n; i++) {
                const city_model* m = city_get(g.catalog, first + (idx)i);
                if (!m) continue;
                vec3 at = v3(stage.x + (float)(i - n / 2) * 18.0f, 0.0f, stage.z);
                city__prop(g.world, m, at, yaw, m->scale, VIEW_DISTANCE);
            }
            // props are drawn out of per-cell buckets, and these arrived after
            // the generator built them
            city__bucket_props(g.world);
        }
    }

    city_player_init(g.player, g.loader, g.catalog, g.physics, spawn);
    city_fx_init(g.fx, 0xB1EDu);
    city_pickups_scatter(g.pickups, g.world, 0x6D5D5u);
    city_pickups_seed_near(g.pickups, g.world, spawn, 3);

    // ---- hud ----
    g.text_shader = opengl_create_shader(VSHDER_TEXT, FSHDER_TEXT);
    g.hud_font = pix_load_font_ttf("C:/Windows/Fonts/arial.ttf");
    text_style style = pix_default_style(v4(1.0f, 1.0f, 1.0f, 1.0f));
    style.size = 19.0f;
    style.outline_width = 0.14f;
    style.outline_color = v4(0.0f, 0.0f, 0.0f, 0.85f);
    g.hud = pix_create_text(&g.hud_font, "", { 0.0f, 0.0f }, style, 512);
    city_reticle_init(g.reticle);
    city_minimap_init(g.minimap);
    g.ui_projection = mat4_ortho(0.0f, (float)CITY_WIDTH, (float)CITY_HEIGHT, 0.0f, -1.0f, 1.0f);
    city_dialogue_init(g.dialogue, &g.hud_font);

    // ---- audio ----
    pix_create_audio(g.audio, 8 * MB);
    set_master_volume(g.audio, 0.65f);
    g.snd_step = g.snd_chime = g.snd_horn = g.snd_skid = SOUND_INVALID;
    idx wav_step  = load_sound_wav_file(g.loader, "assets/audio/step.wav");
    idx wav_chime = load_sound_wav_file(g.loader, "assets/audio/chime.wav");
    idx wav_horn  = load_sound_wav_file(g.loader, "assets/audio/horn.wav");
    idx wav_skid  = load_sound_wav_file(g.loader, "assets/audio/skid.wav");
    if (wav_step  != (idx)-1) g.snd_step  = load_sound(g.audio, g.loader.sound_files[wav_step]);
    if (wav_chime != (idx)-1) g.snd_chime = load_sound(g.audio, g.loader.sound_files[wav_chime]);
    if (wav_horn  != (idx)-1) g.snd_horn  = load_sound(g.audio, g.loader.sound_files[wav_horn]);
    if (wav_skid  != (idx)-1) g.snd_skid  = load_sound(g.audio, g.loader.sound_files[wav_skid]);

    // The guns, synthesised on the spot - see city_make_gunshot. Longer and
    // lower is bigger: the rifle's report is the shortest and sharpest of the
    // three because a rifle's is, and the revolver's is the one that rolls.
    static const struct { float length, pitch; } SHOTS[WEAPON_COUNT] = {
        { 0.26f, 1.15f },   // pistol
        { 0.40f, 0.80f },   // revolver
        { 0.22f, 1.35f },   // rifle
    };
    for (int i = 0; i < WEAPON_COUNT; i++)
        g.snd_shot[i] = city_make_gunshot(g.audio, SHOTS[i].length, SHOTS[i].pitch,
                                          0x9E3779B9u * (uint32_t)(i + 1));
    // An empty chamber: the same synth with almost no envelope left, which is
    // a click rather than a bang.
    g.snd_dry = city_make_gunshot(g.audio, 0.03f, 2.4f, 0x51ED27u);

    {
        // PIX_HOUR=<0..24> starts the clock somewhere other than mid morning.
        // Half of what the lighting does only happens after dark - the lamps,
        // the headlights, the shop windows, a wet road turning into a mirror
        // under them - and none of it can be checked from a screenshot taken
        // at ten in the morning.
        const char* hour = getenv("PIX_HOUR");
        if (hour) pix_set_time_of_day(g.renderer, (float)atof(hour));

        const char* shot = getenv("PIX_SHOT");
        if (shot) g.shot_deadline = (float)atof(shot);

        // PIX_ARM=<0 pistol|1 revolver|2 rifle> starts the player holding one.
        // Guns are scattered across the whole map (see city_pickups_scatter),
        // so checking how one sits in a hand otherwise means walking until a
        // pavement happens to have one on it.
        const char* arm = getenv("PIX_ARM");
        if (arm) {
            int which = atoi(arm);
            if (which >= 0 && which < WEAPON_COUNT)
                city_arms_take(g.player.arms, g.catalog, which,
                               g.catalog.weapons[which].magazine * PICKUP_MAGS);
        }
        const char* fire = getenv("PIX_FIRE");
        if (fire) g.auto_fire = (float)atof(fire);
    }
    // PIX_SELFTEST loads the art, builds the city and shuts down again
    // without ever running a frame - a build check that needs no window and
    // no one watching it. It goes here rather than straight after the
    // catalogue so that world generation is covered by it too.
    bool selftest = getenv("PIX_SELFTEST") != 0;
    {
        const char* stress = getenv("PIX_STRESS");
        if (stress) g.stress_kills = atoi(stress);
    }

    g.show_help = true;
    g.mouse_look = true;
    pix_set_mouse_capture(g.window, true);

    LARGE_INTEGER frequency, previous, now;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&previous);

    pix_profiler_init(g.profile, CITY_PROF_NAMES, PROF_COUNT);
    // The CPU breakdown alone cannot say why a frame is slow when the answer
    // is the GPU - it puts every one of those milliseconds in `present`. With
    // PIX_PROF on, the per-pass GPU times go up alongside it.
    pix_enable_gpu_timing(g.renderer, g.profile.enabled);

    while (!selftest && !g.window.should_close) {
        // The swap for the *previous* frame happens inside here, which is why
        // this is the section a frame that is really waiting on the GPU or on
        // vsync piles up in rather than in submit.
        { PIX_PROFILE(g.profile, PROF_PRESENT);
          pix_update_window(g.window);
        }
        // Esc backs out of an editor mode first, and only quits the game
        // once neither is open - the same key, but "close this tool" takes
        // priority over "close the app" any time there is a tool open to close.
        if (g.window.keystates[KEY_ESC].pressed) {
            if (g.editor.mode == EDITOR_MAP) {
                g.editor.mode = EDITOR_OFF;
                pix_set_mouse_capture(g.window, g.mouse_look);
            } else if (g.editor.mode == EDITOR_COLLIDER) {
                city_editor_exit_collider(g.editor, g.player, g.physics);
            } else if (g.dialogue.active) {
                city_dialogue_end(g.dialogue, g.peds);
            } else {
                break;
            }
        }

        QueryPerformanceCounter(&now);
        float dt = (float)(now.QuadPart - previous.QuadPart) / (float)frequency.QuadPart;
        previous = now;
        if (dt > 0.1f) dt = 0.1f;         // a stall must not teleport anything
        g.time += dt;
        g.fps += ((dt > 0.0f ? 1.0f / dt : g.fps) - g.fps) * 0.08f;

        if (g.window.keystates[KEY_TAB].pressed) g.show_help = !g.show_help;

        // ---- map editor (F5) / collider editor (F6) ----
        if (g.window.keystates[KEY_F5].pressed) {
            if (g.editor.mode == EDITOR_MAP) {
                g.editor.mode = EDITOR_OFF;
                pix_set_mouse_capture(g.window, g.mouse_look);
            } else if (g.editor.mode == EDITOR_OFF) {
                g.editor.mode = EDITOR_MAP;
                city_editor_sync_grid(g.editor, g.world, CITY_WIDTH, CITY_HEIGHT);
                pix_set_mouse_capture(g.window, false);   // the cursor has to be visible to click a cell
            }
        }
        if (g.window.keystates[KEY_F6].pressed) {
            if (g.editor.mode == EDITOR_COLLIDER) city_editor_exit_collider(g.editor, g.player, g.physics);
            else if (g.editor.mode == EDITOR_OFF) city_editor_enter_collider(g.editor, g.player, g.physics);
        }

        if (g.editor.mode == EDITOR_MAP) {
            // so the tool can show where in the city the player is standing
            g.editor.player_cell_x = world_to_cell(g.player.position.x);
            g.editor.player_cell_z = world_to_cell(g.player.position.z);
            city_editor_update_map(g.editor, g.world, g.window, CITY_WIDTH, CITY_HEIGHT);
            // Enter both saves the grid and rebuilds the city from it right
            // away - see city__editor_apply_map for why a plain second call
            // to city_generate is not safe on its own.
            if (g.window.keystates[KEY_RETURN].pressed) {
                city__editor_apply_map(g);
                g.editor.mode = EDITOR_OFF;
                pix_set_mouse_capture(g.window, g.mouse_look);
            }
            city_editor_draw_map(g.editor, CITY_WIDTH, CITY_HEIGHT);
            char hud[640];
            city_editor_map_hud(g.editor, g.world, hud, sizeof(hud));
            // clear of the palette chips the tool draws down the left edge
            g.hud.position = { 52.0f, 32.0f };
            pix_update_text(g.hud, hud);
            draw_text(g.hud, g.text_shader, g.ui_projection);
            continue;
        }

        if (g.editor.mode == EDITOR_COLLIDER) {
            city_editor_update_collider(g.editor, g.catalog, g.window);
            if (g.window.keystates[KEY_RETURN].pressed) {
                city_save_collider_bounds(g.catalog);
                g.editor.collider_changed = false;
            }
        }

        // Time of day. Held paused by default so the world looks the same every
        // run, but the clock is real and everything - sun angle, sky, fog,
        // whether the street lighting is on - hangs off it. The bracket keys
        // step through pedestals instead while the collider editor has them.
        if (g.window.keystates['N'].pressed) g.clock_running = !g.clock_running;
        if (g.editor.mode == EDITOR_OFF) {
            float hour = g.renderer.hour;
            if (g.window.keystates[KEY_LEFT_BRACKET].held)  hour -= dt * 3.0f;
            if (g.window.keystates[KEY_RIGHT_BRACKET].held) hour += dt * 3.0f;
            if (g.clock_running) hour += dt * (24.0f / CITY_DAY_SECONDS);
            if (hour != g.renderer.hour) pix_set_time_of_day(g.renderer, hour);
        } else if (g.clock_running) {
            pix_set_time_of_day(g.renderer, g.renderer.hour + dt * (24.0f / CITY_DAY_SECONDS));
        }
        // Weather. It drifts on its own clock rather than the day's, so a
        // paused sun still gets a changing sky; J cycles it by hand.
        if (g.window.keystates['J'].pressed) {
            if (!g.weather.forced)            city_weather_force(g.weather, 0.0f);
            else if (g.weather.target < 0.3f) city_weather_force(g.weather, 0.62f);
            else if (g.weather.target < 0.8f) city_weather_force(g.weather, 1.0f);
            else                              city_weather_release(g.weather);
        }
        city_weather_update(g.weather, g.renderer, dt);

        if (g.editor.mode == EDITOR_OFF && g.window.keystates['M'].pressed) {
            g.mouse_look = !g.mouse_look;
            pix_set_mouse_capture(g.window, g.mouse_look);
        }
        bool was_on_foot = city_player_on_foot(g.player);

        // ---- simulate ----
        // A positive count kills that many outright; a negative one lands that
        // many ordinary punches instead, which is the case that exercises
        // fleeing and fighting rather than dying.
        if (g.stress_kills != 0 && !g.stress_done && g.time > 4.0f) {
            g.stress_done = true;
            bool lethal = g.stress_kills > 0;
            int want = lethal ? g.stress_kills : -g.stress_kills, hit = 0;
            // Nearest first, in two sweeps. Hitting whoever happens to be
            // early in the array reaches people a hundred metres away, and
            // somebody punched from a hundred metres cannot fight back
            // however brave they are - which made the test say nobody ever
            // does.
            for (int pass = 0; pass < 2 && hit < want; pass++)
                for (size_t i = 0; i < MAX_PEDS && hit < want; i++) {
                    city_ped& ped = g.peds.people[i];
                    if (!ped.active || ped.state == PED_STATE_DEAD) continue;
                    if (ped.state == PED_STATE_HIT) continue;
                    float dx = ped.position.x - g.player.position.x;
                    float dz = ped.position.z - g.player.position.z;
                    // Nearest first, and inside the distance a fighter will
                    // actually close over. Note that the crowd never spawns
                    // within 30 m of the player (see city_peds_update), so a
                    // tighter first sweep than that reaches nobody at all -
                    // which is exactly why an earlier version of this test
                    // reported that nobody ever fights back.
                    // The two modes want different reach. Killing is a
                    // stress test of the animation pool, so it takes anybody
                    // on the map; punching is a test of how people react, and
                    // somebody punched from across the city cannot react to
                    // it however brave they are - so that one stays inside
                    // the distance a fighter will actually close over. Note
                    // the crowd never spawns within 30 m of the player (see
                    // city_peds_update), so a tighter sweep than that reaches
                    // nobody at all.
                    float reach = lethal ? 1e9f : (pass == 0 ? 36.0f : PED_CHASE_RANGE);
                    if (dx * dx + dz * dz > reach * reach) continue;
                    city_ped_hit(g.peds, g.catalog, g.physics, (int)i, v3(0.0f, 2.0f, 0.0f),
                                 lethal ? PED_MAX_HEALTH * 2.0f : PUNCH_DAMAGE,
                                 g.player.position);
                    hit++;
                }
        }
        // PIX_FIRE, and nothing else in the game, drives the player from
        // code - see city_game::auto_fire. It writes the mouse buttons rather
        // than reaching into the weapon state, so what it exercises is the
        // real path: aim, trigger, animation, recoil and all.
        if (g.auto_fire > 0.0f && g.time >= g.auto_fire) {
            int nearest = -1;
            float best = 40.0f * 40.0f;
            for (size_t i = 0; i < MAX_PEDS; i++) {
                const city_ped& ped = g.peds.people[i];
                if (!ped.active || ped.state == PED_STATE_DEAD) continue;
                float dx = ped.position.x - g.player.position.x;
                float dz = ped.position.z - g.player.position.z;
                if (dx * dx + dz * dz < best) { best = dx * dx + dz * dz; nearest = (int)i; }
            }
            if (nearest >= 0) {
                // Chest height, and pitched to actually reach it. The crowd
                // never spawns within thirty metres of the player, so a flat
                // aim puts every round into the tarmac ten metres short -
                // which is exactly what the first run of this test did.
                vec3 to = v3sub(v3add(g.peds.people[nearest].position, v3(0.0f, 1.1f, 0.0f)),
                                v3add(g.player.position, v3(0.0f, CAM_HEIGHT, 0.0f)));
                g.player.cam_yaw = yaw_from_forward(v3norm(v3(to.x, 0.0f, to.z)));
                g.player.cam_pitch = -asinf(clampf(v3norm(to).y, -0.9f, 0.9f));
            }
            g.window.keystates[MOUSE_BUTTON_RIGHT].held = true;
            g.window.keystates[MOUSE_BUTTON_LEFT].held = true;
            g.window.keystates[MOUSE_BUTTON_LEFT].pressed = true;
        }
        { PIX_PROFILE(g.profile, PROF_SIM);
          city__simulate(g, dt);
          city__update_audio(g, dt, was_on_foot);
        }

        // ---- draw ----
        city__render_frame(g);

        { PIX_PROFILE(g.profile, PROF_HUD);
        char hud[640];
        city__build_hud(g, hud, sizeof(hud));
        g.hud.position = { 18.0f, 32.0f };
        pix_update_text(g.hud, hud);
        draw_text(g.hud, g.text_shader, g.ui_projection);
        city_reticle_draw(g.reticle, g.player, g.catalog, CITY_WIDTH, CITY_HEIGHT,
                          CITY_FOV_Y, dt, g.ui_projection);
        city_minimap_draw(g.minimap, g.world, g.traffic, g.peds,
                          g.player.position, g.player.yaw,
                          CITY_WIDTH, CITY_HEIGHT, g.ui_projection);
        city_dialogue_draw(g.dialogue, CITY_WIDTH, CITY_HEIGHT, g.text_shader, g.ui_projection);
        }
        if (pix_profile_frame(g.profile) && g.renderer.gpu_timing) {
            char line[512];
            int n = snprintf(line, sizeof(line), "pass cpu/gpu ");
            double total = 0.0;
            for (int i = 0; i < PIX_GPU_PASSES && n < (int)sizeof(line); i++) {
                total += pix_gpu_pass_ms(g.renderer, i);
                n += snprintf(line + n, sizeof(line) - (size_t)n, " %s %5.2f/%5.2f",
                              PIX_GPU_PASS_NAMES[i], pix_cpu_pass_ms(g.renderer, i),
                              pix_gpu_pass_ms(g.renderer, i));
            }
            n += snprintf(line + n, sizeof(line) - (size_t)n, "  |  %d draws, %d instances",
                          g.renderer.frame_draws, g.renderer.frame_instances);
            printf("%s  |  total %5.2f\n", line, total);
            fflush(stdout);
        }

        // The frame is finished and still in the back buffer - the swap does
        // not happen until the top of the next pix_update_window - so this is
        // the one place a capture gets exactly what was on screen.
        if (g.window.keystates[KEY_F12].pressed) {
            char path[64];
            snprintf(path, sizeof(path), "screenshot_%03d.png", g.shot_count++);
            city__screenshot(path);
        }
        // PIX_SHOT=<seconds> takes one and quits, which is how a change gets
        // looked at without anybody having to sit and watch the window.
        if (g.shot_deadline > 0.0f && g.time >= g.shot_deadline) {
            city__screenshot("screenshot_auto.png");
            // What the crowd is actually doing, in numbers. A body that is
            // dead, has no animation slot and has not toppled is one standing
            // bolt upright - the exact failure the reactor recycling exists
            // to prevent, and one that no screenshot taken from the pavement
            // would reliably show.
            if (g.stress_kills != 0 || g.auto_fire > 0.0f) {
                int dead = 0, animated = 0, toppled = 0, upright = 0;
                int fleeing = 0, fighting = 0, brave = 0, staggering = 0, watching = 0;
                for (size_t i = 0; i < MAX_PEDS; i++) {
                    const city_ped& ped = g.peds.people[i];
                    if (!ped.active) continue;
                    if (ped.state == PED_STATE_FLEE)  fleeing++;
                    if (ped.state == PED_STATE_FIGHT) fighting++;
                    if (ped.state == PED_STATE_HIT)   staggering++;
                    if (ped.state == PED_STATE_IDLE && ped.loiter > 0.0f
                        && ped.curiosity * 0.6f + ped.bravery * 0.4f > PED_ONLOOKER_NERVE)
                        watching++;
                    if (ped.brave && ped.alarm > 0.0f) brave++;
                    if (ped.state != PED_STATE_DEAD) continue;
                    dead++;
                    if (ped.reactor >= 0)        animated++;
                    else if (ped.topple > 0.0f)  toppled++;
                    else                         upright++;
                }
                printf("stress: %d dead (%d on a death clip, %d toppled, %d STANDING), "
                       "%d fleeing, %d fighting, %d staggering, %d watching, %d still brave, "
                       "reputation %.2f\n",
                       dead, animated, toppled, upright, fleeing, fighting, staggering,
                       watching, brave, g.peds.reputation);
                fflush(stdout);
            }
            break;
        }
    }

    pix_set_mouse_capture(g.window, false);
    pix_destroy_audio(g.audio);
    pix_destroy_text(g.hud);
    city_reticle_shutdown(g.reticle);
    city_minimap_shutdown(g.minimap);
    pix_free_font(g.hud_font);
    phys_destroy_world(g.physics);
    pix_destroy_data_loader(g.loader);
    pix_destory_renderer(g.renderer);
}
