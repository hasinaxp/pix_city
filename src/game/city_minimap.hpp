#pragma once
#include "../core/sprite.hpp"
#include "../core/opengl_utils.hpp"
#include "../core/shader_sources.hpp"
#include "city_config.hpp"
#include "city_map.hpp"
#include "city_traffic.hpp"
#include "city_peds.hpp"

// ---- the minimap ----
//
// A window on the same grid the map editor paints: small, bottom left, and it
// follows the player. It is the editor's technique cut down - one palette
// texture of flat colours, one sprite batch, and a crop that picks a swatch
// out of that palette per cell. Nothing is rasterised into an image and
// nothing but the batch itself is uploaded per frame, so the whole panel costs
// one draw call.
//
// It is north-up rather than turning with the camera, and not only by taste: a
// sprite here is an axis-aligned rect and cannot be rotated, so a heading-up
// map would need either a rotated-quad path in the sprite shader or a
// render-to-texture pass. North-up also keeps the city's own grid square on
// screen, which is what makes a block layout readable at a glance. Which way
// the player is facing is carried by the marker instead.
//
// MINIMAP_CELLS is odd on purpose: the player then sits in the middle cell of
// the window rather than on the seam between two.

#define MINIMAP_CELLS   31                       // window width, in world cells
#define MINIMAP_PX      196.0f                   // on-screen size, in pixels
#define MINIMAP_MARGIN  18.0f                    // gap to the screen corner
#define MINIMAP_BLIPS   64                       // cars and people drawn at most
#define MINIMAP_CHROME  (4 + 1 + 2)              // frame, backdrop, player marker

// The palette, in the order a crop index picks out of the strip. The first ten
// are the cell kinds and match the editor's swatches, so two views of one city
// do not disagree about what colour a park is; the rest is this file's chrome.
#define MINIMAP_SWATCH_BACK    10
#define MINIMAP_SWATCH_FRAME   11
#define MINIMAP_SWATCH_PLAYER  12
#define MINIMAP_SWATCH_NOSE    13
#define MINIMAP_SWATCH_CAR     14
#define MINIMAP_SWATCH_PED     15

struct city_minimap {
    sprite_batch batch;
    idx          shader;
    idx          palette_tex;
    bool         ready;
    bool         visible;
};

static int city_minimap__swatch(uint8_t kind, uint8_t zone) {
    switch (kind) {
        case CELL_GROUND:   return 0;
        case CELL_ROAD:     return 1;
        case CELL_SIDEWALK: return 2;
        case CELL_PARK:     return 3;
        case CELL_WATER:    return 4;
        case CELL_LOT:
            switch (zone) {
                case ZONE_DOWNTOWN:    return 5;
                case ZONE_COMMERCIAL:  return 6;
                case ZONE_RESIDENTIAL: return 7;
                case ZONE_INDUSTRIAL:  return 9;
                default:               return 8;    // suburb, and any stray zone
            }
        default: return 0;
    }
}

static void city_minimap_init(city_minimap& m) {
    memset(&m, 0, sizeof(m));
    m.visible = true;

    // rgba, so the backdrop can be the one translucent entry - the sprite
    // shader takes its alpha straight from the texture
    static const vec4 SWATCH[16] = {
        { 0.30f, 0.30f, 0.30f, 1.00f },   // 0  ground
        { 0.13f, 0.13f, 0.15f, 1.00f },   // 1  road
        { 0.74f, 0.74f, 0.70f, 1.00f },   // 2  pavement
        { 0.24f, 0.55f, 0.24f, 1.00f },   // 3  park
        { 0.18f, 0.40f, 0.72f, 1.00f },   // 4  water
        { 0.52f, 0.30f, 0.62f, 1.00f },   // 5  downtown
        { 0.80f, 0.52f, 0.20f, 1.00f },   // 6  commercial
        { 0.76f, 0.72f, 0.28f, 1.00f },   // 7  residential
        { 0.50f, 0.70f, 0.34f, 1.00f },   // 8  suburb
        { 0.52f, 0.40f, 0.30f, 1.00f },   // 9  industrial
        { 0.05f, 0.06f, 0.08f, 0.62f },   // 10 backdrop, under everything
        { 0.92f, 0.92f, 0.90f, 0.85f },   // 11 frame
        { 1.00f, 0.20f, 0.30f, 1.00f },   // 12 the player
        { 1.00f, 1.00f, 1.00f, 1.00f },   // 13 which way they are facing
        { 0.35f, 0.75f, 1.00f, 1.00f },   // 14 a car
        { 1.00f, 0.85f, 0.35f, 1.00f },   // 15 a person
    };
    unsigned char pixels[16 * 4] = {};
    for (int i = 0; i < 16; i++) {
        pixels[i * 4 + 0] = (unsigned char)(clampf(SWATCH[i].x, 0.0f, 1.0f) * 255.0f);
        pixels[i * 4 + 1] = (unsigned char)(clampf(SWATCH[i].y, 0.0f, 1.0f) * 255.0f);
        pixels[i * 4 + 2] = (unsigned char)(clampf(SWATCH[i].z, 0.0f, 1.0f) * 255.0f);
        pixels[i * 4 + 3] = (unsigned char)(clampf(SWATCH[i].w, 0.0f, 1.0f) * 255.0f);
    }
    m.palette_tex = opengl_create_texture2d(16, 1, 4, pixels, TEXTURE_PIXELATED);

    m.shader = opengl_create_shader(VSHDER_SPRITE, FSHDER_SPRITE);
    m.batch = pix_create_sprite_batch(MINIMAP_CELLS * MINIMAP_CELLS +
                                      MINIMAP_BLIPS + MINIMAP_CHROME);
    pix_batch_texture(m.batch, m.palette_tex);
    m.ready = true;
}

static void city_minimap_shutdown(city_minimap& m) {
    if (!m.ready) return;
    pix_destroy_sprite_batch(m.batch);
    m.ready = false;
}

// A solid rectangle in one palette colour. Every piece of this panel is one of
// these, which is why there is no shape code anywhere below.
static void city_minimap__fill(sprite* buf, size_t& n, size_t cap,
                               float x, float y, float w, float h, int swatch) {
    if (n >= cap) return;
    sprite& s = buf[n++];
    s.box = v4(x, y, w, h);
    s.crop = v4((float)swatch, 0.0f, 1.0f, 1.0f);
    s.texture = 0;
}

// Everything on the map is placed by this: a world position becomes a point in
// the panel, north up, with the player at the centre. Positions outside the
// window come back too - callers clip by comparing against the panel rect.
static void city_minimap__project(float wx, float wz, vec3 centre,
                                  float x0, float y0, float px,
                                  float* out_x, float* out_y) {
    float half = MINIMAP_CELLS * 0.5f;
    float cx = (wx - centre.x) / CITY_TILE;         // cells east of the player
    float cz = (wz - centre.z) / CITY_TILE;         // cells north of the player
    *out_x = x0 + (half + cx) * px;
    *out_y = y0 + (half - cz) * px;                 // +Z is north, screen y is down
}

// Rebuilt every frame. That is MINIMAP_CELLS^2 sprites - a thousand or so -
// written into a buffer that is already resident, which comes to one
// glBufferSubData and one instanced draw. A dirty flag to skip the rebuild
// would save less than its own bookkeeping and would have to be invalidated by
// the editor, the traffic and the crowd alike.
static void city_minimap_draw(city_minimap& m, const city_world& w,
                              const city_traffic& traffic, const city_peds& peds,
                              vec3 player_pos, float player_yaw,
                              int screen_w, int screen_h, const mat4& ui_projection) {
    if (!m.ready || !m.visible) return;
    (void)screen_w;

    const float px = MINIMAP_PX / (float)MINIMAP_CELLS;
    const float x0 = MINIMAP_MARGIN;
    const float y0 = (float)screen_h - MINIMAP_MARGIN - MINIMAP_PX;

    static sprite buf[MINIMAP_CELLS * MINIMAP_CELLS + MINIMAP_BLIPS + MINIMAP_CHROME];
    const size_t cap = sizeof(buf) / sizeof(buf[0]);
    size_t n = 0;

    // The backdrop goes down first: it shows through wherever the window runs
    // off the edge of the world, which is what gives the coastline an edge.
    const float pad = 3.0f;
    city_minimap__fill(buf, n, cap, x0 - pad, y0 - pad,
                       MINIMAP_PX + pad * 2.0f, MINIMAP_PX + pad * 2.0f,
                       MINIMAP_SWATCH_BACK);

    int pcx = world_to_cell(player_pos.x);
    int pcz = world_to_cell(player_pos.z);
    int half = MINIMAP_CELLS / 2;
    for (int dz = -half; dz <= half; dz++)
        for (int dx = -half; dx <= half; dx++) {
            int cx = pcx + dx, cz = pcz + dz;
            if (!city_in_bounds(cx, cz)) continue;       // backdrop stands in
            const city_cell& c = w.cells[(size_t)cz * CITY_CELLS + cx];
            city_minimap__fill(buf, n, cap,
                               x0 + (float)(dx + half) * px,
                               y0 + (float)(half - dz) * px,
                               px + 0.5f, px + 0.5f,     // overlap, so no seams
                               city_minimap__swatch(c.kind, c.zone));
        }

    // ---- blips ----
    //
    // Traffic and the crowd, clipped to the panel and capped: past a few dozen
    // dots a map stops saying anything, and the cap is what stops a busy
    // junction costing more than the streets behind it.
    const float reach = MINIMAP_PX - 4.0f;
    size_t blips = 0;
    for (int i = 0; i < MAX_CARS && blips < MINIMAP_BLIPS; i++) {
        const city_car& c = traffic.cars[i];
        if (!c.active || c.player_driven) continue;
        float bx, by;
        city_minimap__project(c.position.x, c.position.z, player_pos, x0, y0, px, &bx, &by);
        if (bx < x0 || by < y0 || bx > x0 + reach || by > y0 + reach) continue;
        city_minimap__fill(buf, n, cap, bx, by, 3.5f, 3.5f, MINIMAP_SWATCH_CAR);
        blips++;
    }
    for (int i = 0; i < MAX_PEDS && blips < MINIMAP_BLIPS; i++) {
        const city_ped& p = peds.people[i];
        if (!p.active) continue;
        float bx, by;
        city_minimap__project(p.position.x, p.position.z, player_pos, x0, y0, px, &bx, &by);
        if (bx < x0 || by < y0 || bx > x0 + reach || by > y0 + reach) continue;
        city_minimap__fill(buf, n, cap, bx, by, 2.5f, 2.5f, MINIMAP_SWATCH_PED);
        blips++;
    }

    // ---- the player ----
    //
    // Placed by the same projection as everything else rather than pinned to
    // the middle of the panel, so the dot slides within its cell as they walk
    // instead of the whole map jumping a cell at a time. The nose is a second
    // dot a little way along the facing: not an arrow, but the one thing a dot
    // cannot say by itself.
    float mx, my;
    city_minimap__project(player_pos.x, player_pos.z, player_pos, x0, y0, px, &mx, &my);
    vec3 fwd = forward_from_yaw(player_yaw);
    city_minimap__fill(buf, n, cap, mx + fwd.x * 7.0f - 2.0f, my - fwd.z * 7.0f - 2.0f,
                       4.0f, 4.0f, MINIMAP_SWATCH_NOSE);
    city_minimap__fill(buf, n, cap, mx - 3.0f, my - 3.0f, 6.0f, 6.0f, MINIMAP_SWATCH_PLAYER);

    // ---- the frame ----
    const float t = 2.0f;
    city_minimap__fill(buf, n, cap, x0 - t, y0 - t,
                       MINIMAP_PX + t * 2.0f, t, MINIMAP_SWATCH_FRAME);
    city_minimap__fill(buf, n, cap, x0 - t, y0 + MINIMAP_PX,
                       MINIMAP_PX + t * 2.0f, t, MINIMAP_SWATCH_FRAME);
    city_minimap__fill(buf, n, cap, x0 - t, y0, t, MINIMAP_PX, MINIMAP_SWATCH_FRAME);
    city_minimap__fill(buf, n, cap, x0 + MINIMAP_PX, y0, t, MINIMAP_PX, MINIMAP_SWATCH_FRAME);

    replace_sprites(m.batch, buf, n, 0);
    m.batch.size = n;
    draw_sprites(m.batch, m.shader, ui_projection);
}
