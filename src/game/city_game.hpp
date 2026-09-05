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
#include "../loader/data_loader.hpp"

#include "city_config.hpp"
#include "city_assets.hpp"
#include "city_map.hpp"
#include "city_traffic.hpp"
#include "city_peds.hpp"
#include "city_player.hpp"

// Assembly and the frame loop.
//
// The systems below do not know about each other: the map is data, traffic and
// pedestrians read it, the physics world is the only thing they share, and this
// file is where they are wired together and drawn. Adding a system means adding
// an update call and a draw call here, not touching anything else.

#define CITY_WIDTH   1600
#define CITY_HEIGHT   900

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
    city_player  player;

    idx      text_shader;
    font_data hud_font;
    pixi_text hud;
    mat4     ui_projection;

    idx snd_step, snd_chime;
    float step_timer;

    float time;
    float fps;
    bool  show_help;
    bool  mouse_look;
    idx   ground_plane_mesh;
};

void run_city_game();

// ---------------- implementation ----------------

// One flat plane under the whole map so the horizon never shows a void where
// the per-cell ground has been culled away.
//
// Its height matters: the roads are sunk so their kerbs line up with the
// pavement, which puts the tarmac below y = 0. A filler plane at y = 0 would
// therefore be drawn straight over every street in the city.
#define BASE_PLANE_Y (ROAD_Y - 0.4f)

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
                push_instance(renderer, mesh, p.material,
                              mat4_trs_y2(p.position, p.yaw, p.scale, sy));
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

static vec3 city__find_spawn(const city_world& w) {
    // walk outward from the middle of the map until a pavement cell turns up
    int cx = CITY_CELLS / 2, cz = CITY_CELLS / 2;
    for (int radius = 0; radius < CITY_CELLS / 2; radius++)
        for (int dz = -radius; dz <= radius; dz++)
            for (int dx = -radius; dx <= radius; dx++) {
                if (dx * dx + dz * dz < (radius - 1) * (radius - 1)) continue;
                int x = cx + dx, z = cz + dz;
                if (city_in_bounds(x, z) && city_at(w, x, z).kind == CELL_SIDEWALK)
                    return cell_centre(x, z);
            }
    return v3(0.0f, 0.0f, 0.0f);
}

static void city__build_hud(city_game& g, char* buffer, size_t capacity) {
    const city_player& p = g.player;
    int cx = world_to_cell(p.position.x), cz = world_to_cell(p.position.z);
    const city_cell& cell = city_at(g.world, cx, cz);
    static const char* ZONE_NAME[ZONE_COUNT] = {
        "Downtown", "Commercial", "Residential", "Suburb", "Industrial", "Parkland"
    };

    if (g.show_help) {
        snprintf(buffer, capacity,
            "%.0f fps   %s   %s\n"
            "\n"
            "  W A S D   walk / drive        mouse   look\n"
            "  shift     run                 space   jump  (handbrake in a car)\n"
            "  F         get in / out of the nearest car\n"
            "  TAB       hide this panel     M       release the mouse\n"
            "  ESC       quit\n"
            "\n"
            "%zu cars   %zu people   %zu props   %zu colliders   %zu water   %zu casts   %.0f MB",
            g.fps, city_player_on_foot(p) ? "on foot" : "driving",
            ZONE_NAME[cell.zone < ZONE_COUNT ? cell.zone : 0],
            g.traffic.count, g.peds.count, g.world.prop_count, g.physics.static_count,
            g.world.water_count, g.catalog.character_count,
            (double)g.loader.arena.size / (double)MB);
        return;
    }

    if (city_player_on_foot(p)) {
        int car = city__nearest_car(g.traffic, g.catalog, p.position, ENTER_CAR_RANGE);
        snprintf(buffer, capacity, "%.0f fps   %s   on foot%s   [TAB] help",
                 g.fps, ZONE_NAME[cell.zone < ZONE_COUNT ? cell.zone : 0],
                 car >= 0 ? "   -   [F] get in" : "");
    } else {
        float kph = fabsf(g.traffic.cars[p.car].speed) * 3.6f;
        snprintf(buffer, capacity, "%.0f fps   %s   %3.0f km/h   [F] get out   [TAB] help",
                 g.fps, ZONE_NAME[cell.zone < ZONE_COUNT ? cell.zone : 0], kph);
    }
}

// footsteps, and a chime on getting in or out of a car
static void city__update_audio(city_game& g, float dt, bool was_on_foot) {
    if (was_on_foot != city_player_on_foot(g.player))
        play_sound(g.audio, g.snd_chime, 0.55f, city_player_on_foot(g.player) ? 0.9f : 1.15f);

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

static city_game g_city;   // several megabytes of world; not a stack citizen

void run_city_game() {
    city_game& g = g_city;

    g.window = pix_create_window("pix city", CITY_WIDTH, CITY_HEIGHT);
    if (!opengl_load_functions()) {
        MessageBoxA(0, "failed to load OpenGL 4.4 functions", "pix city", MB_OK);
        return;
    }

    idx shader = opengl_create_shader(VSHDER_BASIC, shader_with_common(FSHDER_BASIC));
    g.renderer = pix_create_renderer(CITY_WIDTH, CITY_HEIGHT, shader, v3(0.55f, 0.70f, 0.86f));
    g.renderer.projection_matrix =
        mat4_perspective(1.02f, (float)CITY_WIDTH / (float)CITY_HEIGHT, 0.25f, 1400.0f);
    pix_enable_effects(g.renderer, 2048);

    // four 24-clip character rigs resample every bone onto a merged timeline,
    // which is by far the biggest thing in here
    g.loader = pix_create_data_loader(384 * MB);

    if (!city_load_catalog(g.catalog, g.loader, g.renderer)) {
        MessageBoxA(0,
            "Could not load the city art.\n\n"
            "Expected assets/city/{commercial,suburban,vehicles,nature,rail} and\n"
            "assets/KayKit_City_Builder - run the project from its root folder.",
            "pix city", MB_OK);
        return;
    }

    // one oversized quad standing in for the ground past the culled cells
    g.ground_plane_mesh = g.catalog.singles[ONE_QUAD] != (idx)-1
        ? g.catalog.models[g.catalog.singles[ONE_QUAD]].mesh : (idx)-1;

    phys_create_world(g.physics);
    city_generate(g.world, g.catalog, g.physics, 20260905u);

    city_traffic_init(g.traffic, 0xC0FFEEu);
    city_peds_init(g.peds, g.loader, g.catalog, 0xBADCAFEu);
    city_player_init(g.player, g.loader, g.catalog, g.physics, city__find_spawn(g.world));

    // ---- hud ----
    g.text_shader = opengl_create_shader(VSHDER_TEXT, FSHDER_TEXT);
    g.hud_font = pix_load_font_ttf("C:/Windows/Fonts/arial.ttf");
    text_style style = pix_default_style(v4(1.0f, 1.0f, 1.0f, 1.0f));
    style.size = 19.0f;
    style.outline_width = 0.14f;
    style.outline_color = v4(0.0f, 0.0f, 0.0f, 0.85f);
    g.hud = pix_create_text(&g.hud_font, "", { 0.0f, 0.0f }, style, 512);
    g.ui_projection = mat4_ortho(0.0f, (float)CITY_WIDTH, (float)CITY_HEIGHT, 0.0f, -1.0f, 1.0f);

    // ---- audio ----
    pix_create_audio(g.audio, 8 * MB);
    set_master_volume(g.audio, 0.65f);
    g.snd_step = g.snd_chime = SOUND_INVALID;
    idx wav_step  = load_sound_wav_file(g.loader, "assets/audio/step.wav");
    idx wav_chime = load_sound_wav_file(g.loader, "assets/audio/chime.wav");
    if (wav_step  != (idx)-1) g.snd_step  = load_sound(g.audio, g.loader.sound_files[wav_step]);
    if (wav_chime != (idx)-1) g.snd_chime = load_sound(g.audio, g.loader.sound_files[wav_chime]);

    g.show_help = true;
    g.mouse_look = true;
    pix_set_mouse_capture(g.window, true);

    LARGE_INTEGER frequency, previous, now;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&previous);

    while (!g.window.should_close) {
        pix_update_window(g.window);
        if (g.window.keystates[KEY_ESC].pressed) break;

        QueryPerformanceCounter(&now);
        float dt = (float)(now.QuadPart - previous.QuadPart) / (float)frequency.QuadPart;
        previous = now;
        if (dt > 0.1f) dt = 0.1f;         // a stall must not teleport anything
        g.time += dt;
        g.fps += ((dt > 0.0f ? 1.0f / dt : g.fps) - g.fps) * 0.08f;

        if (g.window.keystates[KEY_TAB].pressed) g.show_help = !g.show_help;
        if (g.window.keystates['M'].pressed) {
            g.mouse_look = !g.mouse_look;
            pix_set_mouse_capture(g.window, g.mouse_look);
        }

        bool was_on_foot = city_player_on_foot(g.player);

        // ---- simulate ----
        city_player_update(g.player, g.window, g.traffic, g.catalog, g.physics, dt);
        city_traffic_update(g.traffic, g.world, g.catalog, g.physics, g.player.position, g.time, dt);
        city_peds_update(g.peds, g.world, g.catalog, g.physics, g.player.position, g.time, dt);

        phys_step(g.physics, dt);

        // ---- pull the solved positions back into the game state ----
        city__read_back_all(g.traffic, g.physics);
        if (city_player_on_foot(g.player)) {
            phys_body* pb = phys_get_body(g.physics, g.player.body);
            if (pb) g.player.position = pb->position;
        } else {
            g.player.position = g.traffic.cars[g.player.car].position;
            g.player.yaw = g.traffic.cars[g.player.car].yaw;
        }

        city__update_audio(g, dt, was_on_foot);

        // ---- draw ----
        camera cam;
        city_player_camera(g.player, g.physics, &cam);
        begin_frame(g.renderer, cam);

        pix_set_time(g.renderer, g.time);
        mat4 view_proj = mat4_mul(g.renderer.projection_matrix, g.renderer.view_matrix);
        frustum view = frustum_from_viewproj(view_proj);

        city__draw_base_plane(g, g.renderer);
        city__draw_water(g.world, g.renderer, view, cam.position);
        city__draw_world(g.world, g.renderer, view, cam.position);
        city_traffic_draw(g.traffic, g.catalog, g.renderer, view, cam.position);
        if (!city_player_on_foot(g.player))
            city_car_draw(g.traffic.cars[g.player.car], g.catalog, g.renderer);
        city_peds_draw(g.peds, g.catalog, g.renderer, view, cam.position);
        city_player_draw(g.player, g.catalog, g.renderer);

        end_frame(g.renderer);

        char hud[640];
        city__build_hud(g, hud, sizeof(hud));
        g.hud.position = { 18.0f, 32.0f };
        pix_update_text(g.hud, hud);
        draw_text(g.hud, g.text_shader, g.ui_projection);
    }

    pix_set_mouse_capture(g.window, false);
    pix_destroy_audio(g.audio);
    pix_destroy_text(g.hud);
    pix_free_font(g.hud_font);
    phys_destroy_world(g.physics);
    pix_destroy_data_loader(g.loader);
    pix_destory_renderer(g.renderer);
}
