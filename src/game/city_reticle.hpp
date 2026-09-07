#pragma once
#include "../core/sprite.hpp"
#include "../core/opengl_utils.hpp"
#include "../core/shader_sources.hpp"
#include "city_weapons.hpp"
#include "city_assets.hpp"

// ---- the reticle ----
//
// Four ticks around a gap with a dot in the middle, drawn whenever there is a
// gun in the hand rather than only while the sights are up. That is the point
// of it: hip fire is where the aim is hardest to read, so a mark that only
// appears once the gun is already up tells you nothing at the moment you
// needed telling.
//
// The gap is the weapon's own cone, not a fixed number of pixels. Each gun
// carries a hip spread and an aim spread in radians (see city_weapon), and the
// half-angle projected onto the screen through the vertical field of view is
// exactly where a round can land - so the reticle opens when a rifle is
// sprayed from the hip and closes to almost nothing when a revolver is aimed,
// without a single tuned constant of its own. It is damped rather than
// snapped, because the spread changes the instant a mouse button goes down and
// a mark that teleports reads as a bug.
//
// Same one-palette-texture, one-batch technique as the minimap: five solid
// rectangles, one draw call, no texture of its own to load.

#define RETICLE_TICK       9.0f     // length of each arm, pixels
#define RETICLE_THICK      2.0f     // and its width
#define RETICLE_MIN_GAP    4.0f     // closest the arms ever come to the centre
#define RETICLE_MAX_GAP   64.0f     // and how far they are allowed to open
#define RETICLE_DOT        2.0f
#define RETICLE_SETTLE    12.0f     // how fast the gap chases the spread

struct city_reticle {
    sprite_batch batch;
    idx          shader;
    idx          palette_tex;
    float        gap;               // current opening, pixels from centre
    bool         ready;
};

static void city_reticle_init(city_reticle& r) {
    memset(&r, 0, sizeof(r));
    r.gap = RETICLE_MIN_GAP;

    // Two entries: the ticks, and a near-black one drawn a pixel out on every
    // side as an outline. Without it a white reticle disappears against a
    // white wall, which in this city is most of the suburbs.
    static const vec4 SWATCH[2] = {
        { 1.00f, 1.00f, 1.00f, 0.92f },   // 0  the mark
        { 0.00f, 0.00f, 0.00f, 0.55f },   // 1  its shadow
    };
    unsigned char pixels[2 * 4] = {};
    for (int i = 0; i < 2; i++) {
        pixels[i * 4 + 0] = (unsigned char)(clampf(SWATCH[i].x, 0.0f, 1.0f) * 255.0f);
        pixels[i * 4 + 1] = (unsigned char)(clampf(SWATCH[i].y, 0.0f, 1.0f) * 255.0f);
        pixels[i * 4 + 2] = (unsigned char)(clampf(SWATCH[i].z, 0.0f, 1.0f) * 255.0f);
        pixels[i * 4 + 3] = (unsigned char)(clampf(SWATCH[i].w, 0.0f, 1.0f) * 255.0f);
    }
    r.palette_tex = opengl_create_texture2d(2, 1, 4, pixels, TEXTURE_PIXELATED);

    r.shader = opengl_create_shader(VSHDER_SPRITE, FSHDER_SPRITE);
    r.batch = pix_create_sprite_batch(16);
    pix_batch_texture(r.batch, r.palette_tex);
    r.ready = true;
}

static void city_reticle_shutdown(city_reticle& r) {
    if (!r.ready) return;
    pix_destroy_sprite_batch(r.batch);
    r.ready = false;
}

static void city_reticle__fill(sprite* buf, size_t& n, size_t cap,
                               float x, float y, float w, float h, int swatch) {
    if (n + 1 >= cap) return;
    // the shadow first, one pixel proud on every side, then the mark on top
    sprite& s0 = buf[n++];
    s0.box = v4(x - 1.0f, y - 1.0f, w + 2.0f, h + 2.0f);
    s0.crop = v4(1.0f, 0.0f, 1.0f, 1.0f);
    s0.texture = 0;

    sprite& s1 = buf[n++];
    s1.box = v4(x, y, w, h);
    s1.crop = v4((float)swatch, 0.0f, 1.0f, 1.0f);
    s1.texture = 0;
}

// `dt` is here because the gap is a damped value; drawing and updating are one
// call because there is nothing else in this file to update.
static void city_reticle_draw(city_reticle& r, const city_player& p, const city_catalog& cat,
                              int screen_w, int screen_h, float fov_y, float dt,
                              const mat4& ui_projection) {
    if (!r.ready || !p.gun_drawn) return;
    const city_weapon* w = city_arms_current(p.arms, cat);
    if (!w) return;

    // The cone, in screen pixels. tan of the half-angle over tan of half the
    // field of view is the fraction of a half-screen the spread covers, which
    // is the same projection the shot itself goes through.
    float spread = p.arms.aiming ? w->aim_spread : w->spread;
    float target = tanf(spread) / tanf(fov_y * 0.5f) * ((float)screen_h * 0.5f);
    target = clampf(target + RETICLE_MIN_GAP, RETICLE_MIN_GAP, RETICLE_MAX_GAP);
    // Recoil pushes it open on top of that, so a burst visibly costs accuracy
    // even though the kick is really the camera moving.
    target += p.arms.recoil * 240.0f;
    r.gap = damp(r.gap, target, RETICLE_SETTLE, dt);

    float cx = (float)screen_w * 0.5f;
    float cy = (float)screen_h * 0.5f;
    float g = r.gap;
    float t = RETICLE_THICK;
    float len = RETICLE_TICK;

    sprite buf[16];
    size_t n = 0;
    const size_t cap = sizeof(buf) / sizeof(buf[0]);

    city_reticle__fill(buf, n, cap, cx - t * 0.5f, cy - g - len, t, len, 0);   // up
    city_reticle__fill(buf, n, cap, cx - t * 0.5f, cy + g,       t, len, 0);   // down
    city_reticle__fill(buf, n, cap, cx - g - len, cy - t * 0.5f, len, t, 0);   // left
    city_reticle__fill(buf, n, cap, cx + g,       cy - t * 0.5f, len, t, 0);   // right
    // The centre dot only while aiming - it is the "this is the exact line"
    // mark, and hip fire has no exact line to promise.
    if (p.arms.aiming)
        city_reticle__fill(buf, n, cap, cx - RETICLE_DOT * 0.5f, cy - RETICLE_DOT * 0.5f,
                           RETICLE_DOT, RETICLE_DOT, 0);

    replace_sprites(r.batch, buf, n, 0);
    r.batch.size = n;
    draw_sprites(r.batch, r.shader, ui_projection);
}
