#pragma once
#include "../core/sprite.hpp"
#include "../core/framebuffer.hpp"
#include "city_map.hpp"
#include "city_player.hpp"

// Two editing tools, sharing nothing but the name.
//
// Map mode is a 2D paint tool over the same kind/zone grid city_generate
// itself works from - see city_save_layout and the load branch in
// city_generate. It replaces the whole frame with a flat top-down view
// instead of drawing the 3D world at all, the same way a level editor's
// tile view does not also render the game running underneath it.
//
// Collider mode stays in the 3D world: the player is stood in front of one
// model at a time on an empty pedestal far from the city, and every drag
// edits that model's own bounds_min/bounds_max - the exact numbers every
// static collider built from that model already reads (see city__place_buildings
// and the like). Nothing downstream has to know an edit happened.

#define EDITOR_OFF       0
#define EDITOR_MAP       1
#define EDITOR_COLLIDER  2

#define EDITOR_CELL_PX   6.0f
// Its own remote corner, well clear of both the city and the house
// interior's pocket at (1500, 0, 1500) - see city_house.hpp.
#define EDITOR_PEDESTAL  v3(-1500.0f, 0.0f, -1500.0f)

// One step of map history: exactly what the map file itself stores, which is
// also exactly what a paint stroke can change.
struct city_editor_undo {
    uint8_t kind[CITY_CELLS * CITY_CELLS];
    uint8_t zone[CITY_CELLS * CITY_CELLS];
};

struct city_editor {
    int mode;

    // ---- map paint ----
    uint8_t      brush_kind;
    uint8_t      brush_zone;
    int          brush_size;    // cells across, always odd
    bool         map_dirty;     // painted since the last save
    sprite_batch grid;
    idx          grid_shader;
    idx          palette_tex;
    bool         grid_ready;

    // view: pixels per cell, and the offset from a centred map
    float view_px;
    float pan_x, pan_y;
    bool  view_dirty;           // the batch on screen no longer matches
    int   hover_x, hover_z;     // cell under the cursor, for the readout
    int   player_cell_x, player_cell_z;   // where the player is, -1 when unknown
    bool  stroke_open;          // mid-drag, so one stroke is one undo step

    city_editor_undo undo[16];
    int              undo_head;   // next slot to write
    int              undo_depth;  // how many steps are actually in there

    // ---- collider ----
    int  collider_model;
    // The first bounds seen for a model this session, so 'R' can put it
    // back - editing happens in place on the catalogue's own numbers, and
    // nothing else keeps a copy of what they used to be.
    vec3 original_min[CITY_MAX_MODELS];
    vec3 original_max[CITY_MAX_MODELS];
    bool has_original[CITY_MAX_MODELS];
    bool collider_changed;      // edited since the last save
    idx  box_material;          // bright, unmissable - drawn over the model, not instead of it

    // where to put the player back when either mode ends
    vec3  saved_pos;
    float saved_yaw, saved_cam_yaw, saved_cam_pitch;
};

static void city_editor_init(city_editor& ed, pix_renderer& renderer);
static void city_editor_sync_grid(city_editor& ed, const city_world& w, int screen_w, int screen_h);
static void city_editor_update_map(city_editor& ed, city_world& w, const pix_window& window,
                                   int screen_w, int screen_h);
static void city_editor_draw_map(const city_editor& ed, int screen_w, int screen_h);
// the tool's own state as text, so the caller does not have to know what a brush is
static void city_editor_map_hud(const city_editor& ed, const city_world& w,
                                char* out, size_t capacity);
static void city_editor_collider_hud(const city_editor& ed, const city_catalog& cat,
                                     char* out, size_t capacity);

static void city_editor_enter_collider(city_editor& ed, city_player& p, phys_world& phys);
static void city_editor_exit_collider(city_editor& ed, city_player& p, phys_world& phys);
static void city_editor_update_collider(city_editor& ed, city_catalog& cat, const pix_window& window);
static void city_editor_draw_collider(const city_editor& ed, const city_catalog& cat, pix_renderer& renderer);

// ---------------- implementation ----------------

// index into the small palette texture city_editor_init builds - see there
// for the actual colours
static int city_editor__swatch(uint8_t kind, uint8_t zone) {
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
                default:               return 8;    // suburb, and any stray zone value
            }
        default: return 0;
    }
}

// ---- the map paint tool ----
//
// What it is for: painting the shape of the city - where the water is, where
// the streets run, what each block is zoned as - and then rebuilding the
// whole world from it without leaving the game. Everything below exists
// because painting a 112 x 112 grid one cell at a time with a fixed camera
// is not something anybody can actually do:
//
//   zoom and pan       a coastline needs a close look; a district needs the
//                      whole map. Wheel zooms about the cursor, middle-drag
//                      or the arrow keys pan.
//   brush size         an island is hundreds of cells. 1 to 15 across.
//   flood fill         the only sane way to re-zone a block or drop a bay
//                      into a landmass.
//   undo               sixteen deep, because a mis-aimed fill is otherwise
//                      the end of an afternoon.
//   a legend           ten brushes, each its own colour on screen; a key
//                      number by itself is not a palette.

#define EDITOR_MIN_PX     2.0f
#define EDITOR_MAX_PX    16.0f
#define EDITOR_MAX_BRUSH 15
#define EDITOR_UNDO      16

// Extra sprite slots after the grid itself: the hover cursor, the legend
// swatches, the brush preview and the marker showing where the player is
// standing in the world being painted.
#define EDITOR_CHROME    32
#define EDITOR_LEGEND_PX 18.0f

// One brush per key, kind and zone together - painting is "what should this
// cell be", never the two picked separately, which is what keeps the tool to
// one click per cell instead of a kind pass and a zone pass over the same grid.
static const uint8_t EDITOR_BRUSH_KIND[10] = {
    CELL_GROUND, CELL_ROAD, CELL_SIDEWALK, CELL_PARK, CELL_WATER,
    CELL_LOT, CELL_LOT, CELL_LOT, CELL_LOT, CELL_LOT
};
static const uint8_t EDITOR_BRUSH_ZONE[10] = {
    ZONE_SUBURB, ZONE_SUBURB, ZONE_SUBURB, ZONE_PARK, ZONE_SUBURB,
    ZONE_DOWNTOWN, ZONE_COMMERCIAL, ZONE_RESIDENTIAL, ZONE_SUBURB, ZONE_INDUSTRIAL
};
static const char* const EDITOR_BRUSH_NAME[10] = {
    "open ground", "road", "pavement", "park", "water",
    "downtown", "commercial", "residential", "suburb", "industrial"
};

static void city_editor_init(city_editor& ed, pix_renderer& renderer) {
    memset(&ed, 0, sizeof(ed));
    ed.brush_kind = CELL_LOT;
    ed.brush_zone = ZONE_SUBURB;
    ed.brush_size = 1;
    ed.view_px = 6.0f;

    ed.grid = pix_create_sprite_batch(CITY_CELLS * CITY_CELLS + EDITOR_CHROME);
    ed.grid_shader = opengl_create_shader(VSHDER_SPRITE, FSHDER_SPRITE);

    static const vec3 SWATCH[13] = {
        { 0.30f, 0.30f, 0.30f },   // 0  ground
        { 0.10f, 0.10f, 0.12f },   // 1  road
        { 0.78f, 0.78f, 0.74f },   // 2  sidewalk
        { 0.24f, 0.55f, 0.24f },   // 3  park
        { 0.20f, 0.45f, 0.80f },   // 4  water
        { 0.62f, 0.30f, 0.70f },   // 5  lot: downtown
        { 0.90f, 0.55f, 0.15f },   // 6  lot: commercial
        { 0.85f, 0.80f, 0.25f },   // 7  lot: residential
        { 0.55f, 0.80f, 0.35f },   // 8  lot: suburb
        { 0.55f, 0.40f, 0.28f },   // 9  lot: industrial
        { 1.00f, 0.95f, 0.20f },   // 10 the hover cursor
        { 1.00f, 1.00f, 1.00f },   // 11 the selected brush's frame
        { 1.00f, 0.15f, 0.35f },   // 12 where the player is standing
    };
    unsigned char pixels[16 * 4] = {};
    for (int i = 0; i < 13; i++) {
        pixels[i * 4 + 0] = (unsigned char)(clampf(SWATCH[i].x, 0.0f, 1.0f) * 255.0f);
        pixels[i * 4 + 1] = (unsigned char)(clampf(SWATCH[i].y, 0.0f, 1.0f) * 255.0f);
        pixels[i * 4 + 2] = (unsigned char)(clampf(SWATCH[i].z, 0.0f, 1.0f) * 255.0f);
        pixels[i * 4 + 3] = 255;
    }
    ed.palette_tex = opengl_create_texture2d(16, 1, 4, pixels, TEXTURE_PIXELATED);
    pix_batch_texture(ed.grid, ed.palette_tex);
    ed.grid_ready = true;

    // Fully unlit-bright and fairly rough, so it reads the same by day or
    // night rather than going dark on the side away from the sun the way a
    // normally-lit material would - it has to stay legible as "this is a
    // tool", not become one more shaded surface in the scene.
    ed.box_material = load_material(renderer, 0, v3(1.0f, 0.12f, 0.85f), 0.0f, 1.0f);
    renderer.materials[ed.box_material].emissive = v3(0.7f, 0.05f, 0.6f);
}

// Where cell (x, z) lands on screen, and back again. Everything about the
// view - zoom, pan, centring - is these two functions and nothing else.
static float city_editor__origin_x(const city_editor& ed, int screen_w) {
    return (float)screen_w * 0.5f - CITY_CELLS * ed.view_px * 0.5f + ed.pan_x;
}
static float city_editor__origin_y(const city_editor& ed, int screen_h) {
    return (float)screen_h * 0.5f - CITY_CELLS * ed.view_px * 0.5f + ed.pan_y;
}

// Rebuilds every cell's sprite from the world. Called on opening map mode and
// whenever the view moves - painting since the last time it was open (or a
// fresh regeneration, or a pan) leaves the batch on screen stale.
static void city_editor_sync_grid(city_editor& ed, const city_world& w, int screen_w, int screen_h) {
    if (!ed.grid_ready) return;
    float gx0 = city_editor__origin_x(ed, screen_w);
    float gy0 = city_editor__origin_y(ed, screen_h);
    float gap = ed.view_px > 4.0f ? 1.0f : 0.0f;   // no grout line once cells are tiny

    static sprite buf[CITY_CELLS * CITY_CELLS];
    for (int z = 0; z < CITY_CELLS; z++)
        for (int x = 0; x < CITY_CELLS; x++) {
            const city_cell& c = w.cells[(size_t)z * CITY_CELLS + x];
            sprite& s = buf[(size_t)z * CITY_CELLS + x];
            s.box = v4(gx0 + x * ed.view_px, gy0 + z * ed.view_px,
                       ed.view_px - gap, ed.view_px - gap);
            s.crop = v4((float)city_editor__swatch(c.kind, c.zone), 0.0f, 1.0f, 1.0f);
            s.texture = 0;
        }
    replace_sprites(ed.grid, buf, CITY_CELLS * CITY_CELLS, 0);

    sprite hidden = {};
    hidden.box = v4(-100.0f, -100.0f, 0.0f, 0.0f);
    hidden.crop = v4(0.0f, 0.0f, 1.0f, 1.0f);
    for (int i = 0; i < EDITOR_CHROME; i++)
        replace_sprites(ed.grid, &hidden, 1, CITY_CELLS * CITY_CELLS + i);
    ed.view_dirty = false;
}

// ---- undo ----
//
// A snapshot is the kind and zone of every cell, which is exactly what the
// map file itself holds - 25 KB a step, so sixteen of them cost less than one
// texture and make the fill tool safe to actually use.
static void city_editor__push_undo(city_editor& ed, const city_world& w) {
    city_editor_undo& u = ed.undo[ed.undo_head];
    for (size_t i = 0; i < CITY_CELLS * CITY_CELLS; i++) {
        u.kind[i] = w.cells[i].kind;
        u.zone[i] = w.cells[i].zone;
    }
    ed.undo_head = (ed.undo_head + 1) % EDITOR_UNDO;
    if (ed.undo_depth < EDITOR_UNDO) ed.undo_depth++;
}

static bool city_editor__pop_undo(city_editor& ed, city_world& w) {
    if (!ed.undo_depth) return false;
    ed.undo_head = (ed.undo_head + EDITOR_UNDO - 1) % EDITOR_UNDO;
    ed.undo_depth--;
    const city_editor_undo& u = ed.undo[ed.undo_head];
    for (size_t i = 0; i < CITY_CELLS * CITY_CELLS; i++) {
        w.cells[i].kind = u.kind[i];
        w.cells[i].zone = u.zone[i];
    }
    return true;
}

// ---- painting ----

static bool city_editor__paint_cell(city_editor& ed, city_world& w, int x, int z) {
    if (!city_in_bounds(x, z)) return false;
    city_cell& c = w.cells[(size_t)z * CITY_CELLS + x];
    if (c.kind == ed.brush_kind && c.zone == ed.brush_zone) return false;
    c.kind = ed.brush_kind;
    c.zone = ed.brush_zone;
    return true;
}

// The brush is a square, because a square is what a grid wants and a round
// one only looks round at sizes where you would be filling anyway.
static bool city_editor__paint(city_editor& ed, city_world& w, int cx, int cz) {
    int half = ed.brush_size / 2;
    bool any = false;
    for (int dz = -half; dz <= half; dz++)
        for (int dx = -half; dx <= half; dx++)
            if (city_editor__paint_cell(ed, w, cx + dx, cz + dz)) any = true;
    return any;
}

// Flood fill over cells that match what is under the cursor now, four-way.
// Matching on both kind and zone is what makes it useful for re-zoning: fill
// a residential block and only that block changes, not the whole island.
static bool city_editor__fill(city_editor& ed, city_world& w, int cx, int cz) {
    if (!city_in_bounds(cx, cz)) return false;
    uint8_t from_kind = w.cells[(size_t)cz * CITY_CELLS + cx].kind;
    uint8_t from_zone = w.cells[(size_t)cz * CITY_CELLS + cx].zone;
    if (from_kind == ed.brush_kind && from_zone == ed.brush_zone) return false;

    static int stack[CITY_CELLS * CITY_CELLS];
    int sp = 0;
    stack[sp++] = cz * CITY_CELLS + cx;
    while (sp > 0) {
        int cell = stack[--sp];
        int x = cell % CITY_CELLS, z = cell / CITY_CELLS;
        city_cell& c = w.cells[(size_t)cell];
        if (c.kind != from_kind || c.zone != from_zone) continue;
        c.kind = ed.brush_kind;
        c.zone = ed.brush_zone;
        for (int d = 0; d < 4; d++) {
            int nx = x + DIR_DX[d], nz = z + DIR_DZ[d];
            if (!city_in_bounds(nx, nz)) continue;
            const city_cell& n = w.cells[(size_t)nz * CITY_CELLS + nx];
            if (n.kind == from_kind && n.zone == from_zone) stack[sp++] = nz * CITY_CELLS + nx;
        }
    }
    return true;
}

static void city_editor_update_map(city_editor& ed, city_world& w, const pix_window& window,
                                   int screen_w, int screen_h) {
    for (int k = 0; k < 10; k++) {
        int key = (k == 9) ? '0' : ('1' + k);
        if (window.keystates[key].pressed) {
            ed.brush_kind = EDITOR_BRUSH_KIND[k];
            ed.brush_zone = EDITOR_BRUSH_ZONE[k];
        }
    }
    if (window.keystates[KEY_LEFT_BRACKET].pressed && ed.brush_size > 1) ed.brush_size -= 2;
    if (window.keystates[KEY_RIGHT_BRACKET].pressed && ed.brush_size < EDITOR_MAX_BRUSH)
        ed.brush_size += 2;

    float gx0 = city_editor__origin_x(ed, screen_w);
    float gy0 = city_editor__origin_y(ed, screen_h);
    int cx = (int)floorf(((float)window.mouse_x - gx0) / ed.view_px);
    int cz = (int)floorf(((float)window.mouse_y - gy0) / ed.view_px);
    bool over = cx >= 0 && cz >= 0 && cx < CITY_CELLS && cz < CITY_CELLS;
    ed.hover_x = cx;
    ed.hover_z = cz;

    // ---- the view ----
    // Zoom keeps whatever is under the cursor under the cursor, which is the
    // only zoom that is any use when the thing being edited is bigger than
    // the window.
    if (window.mouse_wheel) {
        float want = ed.view_px * (window.mouse_wheel > 0 ? 1.25f : 0.8f);
        want = clampf(want, EDITOR_MIN_PX, EDITOR_MAX_PX);
        if (want != ed.view_px) {
            float fx = ((float)window.mouse_x - gx0) / ed.view_px;
            float fz = ((float)window.mouse_y - gy0) / ed.view_px;
            ed.view_px = want;
            ed.pan_x += ((float)window.mouse_x - city_editor__origin_x(ed, screen_w)) - fx * want;
            ed.pan_y += ((float)window.mouse_y - city_editor__origin_y(ed, screen_h)) - fz * want;
            ed.view_dirty = true;
        }
    }
    if (window.keystates[MOUSE_BUTTON_MID].held
        && (window.mouse_rel_x || window.mouse_rel_y)) {
        ed.pan_x += (float)window.mouse_rel_x;
        ed.pan_y += (float)window.mouse_rel_y;
        ed.view_dirty = true;
    }
    float nudge = 24.0f;
    if (window.keystates[KEY_LEFT].held)  { ed.pan_x += nudge; ed.view_dirty = true; }
    if (window.keystates[KEY_RIGHT].held) { ed.pan_x -= nudge; ed.view_dirty = true; }
    if (window.keystates[KEY_UP].held)    { ed.pan_y += nudge; ed.view_dirty = true; }
    if (window.keystates[KEY_DOWN].held)  { ed.pan_y -= nudge; ed.view_dirty = true; }
    if (window.keystates['C'].pressed) {          // recentre
        ed.pan_x = ed.pan_y = 0.0f;
        ed.view_px = 6.0f;
        ed.view_dirty = true;
    }

    // ---- edits ----
    // One undo step per stroke, not per cell: a drag across fifty cells is
    // one thing the user did and should come back in one press.
    bool painting = window.keystates[MOUSE_BUTTON_LEFT].held;
    if (painting && !ed.stroke_open) {
        city_editor__push_undo(ed, w);
        ed.stroke_open = true;
    }
    if (!painting) ed.stroke_open = false;

    bool touched = false;
    if (over && painting)                       touched = city_editor__paint(ed, w, cx, cz);
    if (over && window.keystates['F'].pressed) {
        city_editor__push_undo(ed, w);
        touched = city_editor__fill(ed, w, cx, cz);
    }
    if (window.keystates['U'].pressed && city_editor__pop_undo(ed, w)) touched = true;

    // Right click samples the cell under the cursor into the brush, the
    // usual paint-tool eyedropper - the fastest way to "more of this".
    if (over && window.keystates[MOUSE_BUTTON_RIGHT].pressed) {
        const city_cell& c = w.cells[(size_t)cz * CITY_CELLS + cx];
        ed.brush_kind = c.kind;
        ed.brush_zone = c.zone;
    }

    if (touched) {
        ed.map_dirty = true;
        ed.view_dirty = true;     // cheaper to re-lay the batch than to track cells
    }
    if (ed.view_dirty) city_editor_sync_grid(ed, w, screen_w, screen_h);

    // ---- chrome ----
    size_t slot = CITY_CELLS * CITY_CELLS;
    sprite s = {};
    s.texture = 0;

    // the brush footprint under the cursor, at its real size
    float span = ed.view_px * (float)ed.brush_size;
    if (over) {
        int half = ed.brush_size / 2;
        s.box = v4(gx0 + (float)(cx - half) * ed.view_px - 1.0f,
                   gy0 + (float)(cz - half) * ed.view_px - 1.0f, span + 2.0f, span + 2.0f);
        s.crop = v4(10.0f, 0.0f, 1.0f, 1.0f);
    } else {
        s.box = v4(-100.0f, -100.0f, 0.0f, 0.0f);
        s.crop = v4(0.0f, 0.0f, 1.0f, 1.0f);
    }
    replace_sprites(ed.grid, &s, 1, slot++);

    // the palette down the left edge, selected one framed in white
    for (int i = 0; i < 10; i++) {
        float py = 96.0f + (float)i * (EDITOR_LEGEND_PX + 4.0f);
        bool selected = EDITOR_BRUSH_KIND[i] == ed.brush_kind
                     && EDITOR_BRUSH_ZONE[i] == ed.brush_zone;
        sprite frame = {};
        frame.texture = 0;
        frame.box = v4(18.0f - 2.0f, py - 2.0f, EDITOR_LEGEND_PX + 4.0f, EDITOR_LEGEND_PX + 4.0f);
        frame.crop = v4(selected ? 11.0f : 0.0f, 0.0f, 1.0f, 1.0f);
        replace_sprites(ed.grid, &frame, 1, slot++);

        sprite chip = {};
        chip.texture = 0;
        chip.box = v4(18.0f, py, EDITOR_LEGEND_PX, EDITOR_LEGEND_PX);
        chip.crop = v4((float)city_editor__swatch(EDITOR_BRUSH_KIND[i], EDITOR_BRUSH_ZONE[i]),
                       0.0f, 1.0f, 1.0f);
        replace_sprites(ed.grid, &chip, 1, slot++);
    }

    // where the player is standing, so an edit can be aimed at the place you
    // just walked away from
    sprite you = {};
    you.texture = 0;
    if (ed.player_cell_x >= 0) {
        you.box = v4(gx0 + (float)ed.player_cell_x * ed.view_px - 2.0f,
                     gy0 + (float)ed.player_cell_z * ed.view_px - 2.0f,
                     ed.view_px + 4.0f, ed.view_px + 4.0f);
        you.crop = v4(12.0f, 0.0f, 1.0f, 1.0f);
    } else {
        you.box = v4(-100.0f, -100.0f, 0.0f, 0.0f);
        you.crop = v4(0.0f, 0.0f, 1.0f, 1.0f);
    }
    replace_sprites(ed.grid, &you, 1, slot++);
}

// The whole tool's state as one block of text, so city_game.hpp does not have
// to know what a brush is to be able to show one.
static void city_editor_map_hud(const city_editor& ed, const city_world& w,
                                char* out, size_t capacity) {
    const char* brush = "custom";
    for (int i = 0; i < 10; i++)
        if (EDITOR_BRUSH_KIND[i] == ed.brush_kind && EDITOR_BRUSH_ZONE[i] == ed.brush_zone)
            brush = EDITOR_BRUSH_NAME[i];

    char under[64] = "off the map";
    if (city_in_bounds(ed.hover_x, ed.hover_z)) {
        const city_cell& c = w.cells[(size_t)ed.hover_z * CITY_CELLS + ed.hover_x];
        const char* kind = "?";
        switch (c.kind) {
            case CELL_GROUND:   kind = "open ground"; break;
            case CELL_ROAD:     kind = "road";        break;
            case CELL_SIDEWALK: kind = "pavement";    break;
            case CELL_PARK:     kind = "park";        break;
            case CELL_WATER:    kind = "water";       break;
            case CELL_LOT:      kind = "lot";         break;
            default: break;
        }
        snprintf(under, sizeof(under), "cell %d,%d  %s", ed.hover_x, ed.hover_z, kind);
    }

    snprintf(out, capacity,
        "MAP EDITOR%s        brush: %s   size %d\n"
        "%s\n"
        "\n"
        "  1-0     pick a brush          [ ]   brush size\n"
        "  drag    paint                 F     flood fill under the cursor\n"
        "  right   sample a cell         U     undo (%d step%s)\n"
        "  wheel   zoom                  mid-drag / arrows  pan      C  recentre\n"
        "  ENTER   save and rebuild the city\n"
        "  F5 / ESC  leave without rebuilding",
        ed.map_dirty ? "   -   unsaved" : "", brush, ed.brush_size, under,
        ed.undo_depth, ed.undo_depth == 1 ? "" : "s");
}

static void city_editor_draw_map(const city_editor& ed, int screen_w, int screen_h) {
    pix_bind_backbuffer(screen_w, screen_h);
    glClearColor(0.07f, 0.07f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    mat4 proj = mat4_ortho(0.0f, (float)screen_w, (float)screen_h, 0.0f, -1.0f, 1.0f);
    draw_sprites(ed.grid, ed.grid_shader, proj);
}

// ---- collider editing ----

static void city_editor__teleport(city_player& p, phys_world& phys, vec3 pos, float yaw) {
    phys_body* b = phys_get_body(phys, p.body);
    if (b) { b->position = pos; b->velocity = v3(0.0f, 0.0f, 0.0f); }
    p.position = pos;
    p.yaw = yaw;
    p.cam_yaw = yaw;
}

static void city_editor_enter_collider(city_editor& ed, city_player& p, phys_world& phys) {
    ed.saved_pos = p.position;
    ed.saved_yaw = p.yaw;
    ed.saved_cam_yaw = p.cam_yaw;
    ed.saved_cam_pitch = p.cam_pitch;
    ed.mode = EDITOR_COLLIDER;

    vec3 spot = v3add(EDITOR_PEDESTAL, v3(0.0f, 0.0f, 4.0f));
    city_editor__teleport(p, phys, spot, 0.0f);   // faces -Z, back toward the pedestal at the origin
    p.cam_pitch = 0.15f;
}

static void city_editor_exit_collider(city_editor& ed, city_player& p, phys_world& phys) {
    city_editor__teleport(p, phys, ed.saved_pos, ed.saved_yaw);
    p.cam_yaw = ed.saved_cam_yaw;
    p.cam_pitch = ed.saved_cam_pitch;
    ed.mode = EDITOR_OFF;
}

// Left-drag resizes the footprint, right-drag (vertical only) slides the
// box's floor up or down, the wheel stretches its height - three drags for
// three things a collider actually needs, rather than one gizmo with
// handles this engine has nothing to pick them with.
//
// Holding shift divides every rate by five. Fitting a box to a tree trunk is
// a coarse drag followed by a fine one, and without the second the tool can
// only ever get within a few centimetres of what you meant.
#define COLLIDER_DRAG_RATE  0.01f
#define COLLIDER_WHEEL_RATE 0.08f
#define COLLIDER_MIN_SIZE   0.05f
#define COLLIDER_FINE       0.2f
// How far a page step jumps. A hundred and eighty models one bracket press at
// a time is not a way to reach the one you want.
#define COLLIDER_PAGE       10

static void city_editor_update_collider(city_editor& ed, city_catalog& cat, const pix_window& window) {
    if (!cat.model_count) return;
    int n = (int)cat.model_count;
    if (window.keystates[KEY_LEFT_BRACKET].pressed)  ed.collider_model = (ed.collider_model + n - 1) % n;
    if (window.keystates[KEY_RIGHT_BRACKET].pressed) ed.collider_model = (ed.collider_model + 1) % n;
    if (window.keystates[KEY_DOWN].pressed)
        ed.collider_model = (ed.collider_model + n - COLLIDER_PAGE) % n;
    if (window.keystates[KEY_UP].pressed)
        ed.collider_model = (ed.collider_model + COLLIDER_PAGE) % n;
    // Straight to the head of a set: the models are stored set by set, so
    // this is "show me the first tree" without knowing any index.
    for (int k = 0; k < 10 && k < SET_COUNT; k++) {
        int key = (k == 9) ? '0' : ('1' + k);
        if (window.keystates[key].pressed && cat.sets[k].count)
            ed.collider_model = (int)cat.sets[k].first;
    }

    float rate = window.keystates[KEY_SHIFT].held ? COLLIDER_FINE : 1.0f;

    city_model& m = cat.models[ed.collider_model];
    if (!ed.has_original[ed.collider_model]) {
        ed.original_min[ed.collider_model] = m.bounds_min;
        ed.original_max[ed.collider_model] = m.bounds_max;
        ed.has_original[ed.collider_model] = true;
    }

    bool changed = false;
    if (window.keystates[MOUSE_BUTTON_LEFT].held && (window.mouse_rel_x || window.mouse_rel_y)) {
        m.bounds_max.x += (float)window.mouse_rel_x * COLLIDER_DRAG_RATE * rate;
        m.bounds_min.x -= (float)window.mouse_rel_x * COLLIDER_DRAG_RATE * rate;
        m.bounds_max.z += (float)window.mouse_rel_y * COLLIDER_DRAG_RATE * rate;
        m.bounds_min.z -= (float)window.mouse_rel_y * COLLIDER_DRAG_RATE * rate;
        changed = true;
    }
    if (window.keystates[MOUSE_BUTTON_RIGHT].held && window.mouse_rel_y) {
        m.bounds_min.y -= (float)window.mouse_rel_y * COLLIDER_DRAG_RATE * rate;
        changed = true;
    }
    if (window.mouse_wheel) {
        m.bounds_max.y += (float)window.mouse_wheel * COLLIDER_WHEEL_RATE * rate;
        changed = true;
    }
    if (window.keystates['R'].pressed && ed.has_original[ed.collider_model]) {
        m.bounds_min = ed.original_min[ed.collider_model];
        m.bounds_max = ed.original_max[ed.collider_model];
        changed = true;
    }

    if (m.bounds_max.x - m.bounds_min.x < COLLIDER_MIN_SIZE) {
        float c = (m.bounds_max.x + m.bounds_min.x) * 0.5f;
        m.bounds_min.x = c - COLLIDER_MIN_SIZE * 0.5f;
        m.bounds_max.x = c + COLLIDER_MIN_SIZE * 0.5f;
    }
    if (m.bounds_max.z - m.bounds_min.z < COLLIDER_MIN_SIZE) {
        float c = (m.bounds_max.z + m.bounds_min.z) * 0.5f;
        m.bounds_min.z = c - COLLIDER_MIN_SIZE * 0.5f;
        m.bounds_max.z = c + COLLIDER_MIN_SIZE * 0.5f;
    }
    if (m.bounds_max.y - m.bounds_min.y < COLLIDER_MIN_SIZE) m.bounds_max.y = m.bounds_min.y + COLLIDER_MIN_SIZE;

    if (changed) ed.collider_changed = true;
}

// Which set a model belongs to, by name - the collider editor's own
// "where am I in this list". Answered from the set ranges the catalogue
// already keeps rather than from a second table that could disagree with them.
static const char* city_editor__set_name(const city_catalog& cat, int model) {
    static const char* NAMES[SET_COUNT] = {
        "road", "building", "house", "yard", "paving", "street", "street tree",
        "park tree", "shrub", "rock", "park feature", "industrial", "railroad", "train"
    };
    for (int i = 0; i < SET_COUNT; i++)
        if (cat.sets[i].count && (idx)model >= cat.sets[i].first
            && (idx)model < cat.sets[i].first + cat.sets[i].count)
            return NAMES[i];
    return "unsorted";
}

// The collider box in metres, which is the number that actually matters: a
// collider is judged against the thing it stands in for, and "1.8 m across"
// is checkable against a doorway in a way that "model 274" never was.
static void city_editor_collider_hud(const city_editor& ed, const city_catalog& cat,
                                     char* out, size_t capacity) {
    if (!cat.model_count) {
        snprintf(out, capacity, "COLLIDER EDITOR   -   no models loaded");
        return;
    }
    const city_model& m = cat.models[ed.collider_model];
    vec3 e = v3sub(m.bounds_max, m.bounds_min);
    float s = m.scale;

    snprintf(out, capacity,
        "COLLIDER EDITOR%s      %s   [%s]   model %d / %d\n"
        "box  %.2f x %.2f m footprint   %.2f m tall   floor at %+.2f m\n"
        "\n"
        "  [ ]     step one model        up/down  step ten\n"
        "  1-0     jump to a set         shift    fine control\n"
        "  left-drag   resize the footprint       wheel  height\n"
        "  right-drag  raise / lower the floor    R      back to measured\n"
        "  ENTER save every edit         F6 / ESC  done",
        ed.collider_changed ? "   -   unsaved" : "",
        m.name[0] ? m.name : "(unnamed)", city_editor__set_name(cat, ed.collider_model),
        ed.collider_model + 1, (int)cat.model_count,
        e.x * s, e.z * s, e.y * s, m.bounds_min.y * s);
}

// The model itself, drawn normally, and its collider as a solid box standing
// where the physics box built from these exact bounds would - see
// city__place_buildings' `ext = bounds_max - bounds_min` for the reader on
// the other end of this same struct.
//
// Solid rather than a wireframe outline: every draw in this renderer goes
// through push_instance, which only queues a transform for end_frame's
// batched instanced draw - there is no point in this function where the box
// is actually rasterized, so there is no moment here a polygon-mode toggle
// could bracket. A wireframe would need its own immediate, non-instanced
// draw call, which is a real render path this engine does not have - this
// is the honest fallback: wherever the collider is smaller than the model's
// own mesh, the mesh still shows past its edges, which is enough to judge
// the box against the shape it is meant to stand in for.
static void city_editor_draw_collider(const city_editor& ed, const city_catalog& cat, pix_renderer& renderer) {
    if (!cat.model_count) return;
    const city_model& m = cat.models[ed.collider_model];
    vec3 pos = EDITOR_PEDESTAL;

    if (m.mesh != (idx)-1)
        push_instance(renderer, m.mesh, m.material, mat4_trs_y(pos, 0.0f, m.scale));
    // and whatever else it is painted with - see city_model::part_mesh
    for (int i = 0; i < m.part_count; i++)
        push_instance(renderer, m.part_mesh[i], m.part_material[i],
                      mat4_trs_y(pos, 0.0f, m.scale));

    const city_model* box = city_get(cat, cat.singles[ONE_SLAB]);
    if (!box || ed.box_material == (idx)-1) return;
    float s = m.scale;
    vec3 centre_xz = v3((m.bounds_min.x + m.bounds_max.x) * 0.5f * s, m.bounds_min.y * s,
                        (m.bounds_min.z + m.bounds_max.z) * 0.5f * s);
    vec3 size = v3((m.bounds_max.x - m.bounds_min.x) * s, (m.bounds_max.y - m.bounds_min.y) * s,
                  (m.bounds_max.z - m.bounds_min.z) * s);
    vec3 wp = v3add(pos, centre_xz);
    mat4 t = mat4_mul(mat4_translate(wp.x, wp.y, wp.z), mat4_scale(size.x, size.y, size.z));
    push_instance(renderer, box->mesh, ed.box_material, t);
}
