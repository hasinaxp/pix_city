#pragma once
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include "dtype.hpp"
#include "math.hpp"
#include "../loader/data_loader.hpp"
#include "../loader/png_loader.hpp"
#include "opengl_utils.hpp"
#include "lighting.hpp"
#include "framebuffer.hpp"
#include "shader_sources.hpp"
#include "animation.hpp"

#define MAX_RENDERABLE 24576           // a city block's worth of props survives culling
#define MAX_ANIMATED   96              // animated instances drawn per frame
#define MAX_MESHES     512
#define MAX_WATER_INSTANCES 4096       // surface tiles visible at once
#define MAX_MATERIALS  128
#define MAX_SHADERS    32
#define MESH_POOL_VERTICES (1 << 20)   // shared vertex buffer capacity
#define MESH_POOL_INDICES  (1 << 20)   // shared index buffer capacity
#define SKIN_POOL_VERTICES (1 << 16)   // shared skinned vertex buffer (uint16 indices cap it here anyway)
#define SKIN_POOL_INDICES  (1 << 17)

struct mesh {
    idx vbo;
    idx vertex_offset;   // first vertex in the shared buffer
    idx vertex_size;     // stride, bytes
    idx vertex_count;
    idx index_offset;    // first index in the shared buffer
    idx index_count;
};

struct material {
    idx shader;
    idx texture;
    vec3 color;
    float metallic;
    float roughness;
    // Linear radiance the surface gives off by itself. Zero for almost
    // everything; it is what a lit window, a brake light or a lamp head is,
    // and it is the channel the bloom pass exists to serve.
    vec3 emissive;
};

struct pix_render_instance {
    idx mesh;
    idx material;
    mat4 transform;
};

struct camera {
    vec3 position;
    vec3 direction;
    vec3 up;
};

struct pix_renderer {
    int width;
    int height;
    vec3 clear_color;

    pix_render_instance instances[MAX_RENDERABLE];
    mesh     meshes[MAX_MESHES];
    material materials[MAX_MATERIALS];
    idx      shaders[MAX_SHADERS];
    idx      shader;

    // animated pass: each instance carries the pose it should be drawn with
    pix_render_instance animated_instances[MAX_ANIMATED];
    const animation*    animated_poses[MAX_ANIMATED];
    mesh   skinned_meshes[MAX_MESHES];
    size_t skinned_mesh_count;
    size_t animated_count;
    idx    skinned_shader;

    size_t mesh_count;
    size_t material_count;
    size_t shader_count;
    size_t renderable_count;

    idx vertex_cursor;  // bump allocator into the shared buffers
    idx index_cursor;
    idx skin_vertex_cursor;
    idx skin_index_cursor;

    mat4 view_matrix;
    mat4 projection_matrix;

    idx vao;
    idx models_vbo;      // every mesh's vertices
    idx instances_vbo;   // per-frame transforms, streamed with glBufferSubData
    idx ebo;             // every mesh's indices

    idx vao_animated;
    idx models_vbo_animated;  // every skinned mesh's vertices
    idx ebo_animated;

    idx white_texture;   // stand-in for untextured materials
    mem_arena tex_arena; // scratch for image decoding

    // ---- lighting, shadows and post processing ----
    // Off until pix_enable_effects is called, in which case end_frame draws
    // straight to the back buffer exactly as it always did.
    bool        effects;
    framebuffer scene_target;   // HDR colour the post pass reads
    framebuffer shadow_map;     // depth only, from the sun
    idx         depth_shader;
    idx         depth_skinned_shader;
    idx         post_shader;
    idx         water_shader;
    idx         sky_shader;    // analytic sky drawn behind the world

    // bloom: quarter resolution, ping-ponged between two targets
    framebuffer bloom_a, bloom_b;
    idx         bloom_prefilter_shader;
    idx         bloom_blur_shader;
    float       bloom_threshold;
    float       bloom_knee;
    float       bloom_strength;

    // The sun and the sky are described by lighting.hpp and mirrored into the
    // flat fields the shader uniforms are fed from, so a caller can set an hour
    // of the day and everything downstream follows.
    pix_sun sun;
    pix_sky sky;
    float   hour;          // time of day, 0..24
    float   night;         // 0 by day, 1 after dusk; what artificial light rides on

    pix_light_list lights;

    float shadow_strength; // 0 turns shadows off without unbinding anything
    float shadow_extent;   // half width of the furthest cascade, metres
    float shadow_depth;    // how far along the sun direction the box reaches
    float exposure;
    float saturation;
    float vignette;
    float sharpen;

    // one shadow projection per cascade, near to far, plus where each ends
    mat4  light_space[SHADOW_CASCADES];
    float cascade_far[SHADOW_CASCADES];
    float cascade_texel[SHADOW_CASCADES];   // one shadow texel in world metres
    vec3 camera_position;

    // Water is drawn after the opaque pass, blended, from its own list. It has
    // to be big enough for every water cell that can be on screen at once: a
    // river six cells wide running the length of a VIEW_DISTANCE draw is well
    // over a thousand tiles, and a queue that silently stops accepting them
    // ends the river in a straight line across the middle of the view.
    pix_render_instance water_instances[MAX_WATER_INSTANCES];
    size_t water_count;
    vec3   water_shallow;
    vec3   water_deep;
    float  water_depth;    // metres from the surface to the bed, for the murk term
    float  time;
};


static pix_renderer pix_create_renderer(
    int width, int height, idx shader, vec3 clear_color = { 0.0f, 0.5f, 0.8f });
static void pix_destory_renderer(pix_renderer& renderer);

// Sets the sun, the sky, the fog and how much artificial light the world should
// be running, all from one hour of the day. See lighting.hpp for the keyframes.
static void pix_set_time_of_day(pix_renderer& renderer, float hour);

// Lights are submitted per frame the way draw calls are: cleared in
// begin_frame, added by whoever owns them, and reduced to the nearest few in
// end_frame. A light that stops being submitted stops existing, which is what
// makes a street of lamps that fade up at dusk one line of gameplay code
// rather than a resource to manage.
static void push_light(pix_renderer& renderer, const pix_light& light);

static idx load_mesh(pix_renderer& renderer, mesh_file_data& mesh_data);
static idx load_skinned_mesh(pix_renderer& renderer, skinned_mesh_file_data& mesh_data);
static idx load_material(pix_renderer& renderer,
    const char* texture_path = nullptr, vec3 color = { 1.0f, 1.0f, 1.0f },
    float metalic = 0.1f, float roughness = 0.5f);
// same, for a texture already decoded in memory (glTF embeds its textures).
// `pixelated` is for generated colour palettes, where a linear filter would
// blend one material's cell into the next.
static idx load_material_image(pix_renderer& renderer, image_file_data* image,
    vec3 color = { 1.0f, 1.0f, 1.0f }, float metalic = 0.1f, float roughness = 0.5f,
    bool pixelated = false);
// A second material over the same GL texture, differing only in tint and
// finish. Outfit variants and per-building shades want this rather than
// load_material, which would decode and upload the same image again.
static idx clone_material(pix_renderer& renderer, idx source, vec3 color,
                          float metallic, float roughness);
static idx get_default_material(pix_renderer& renderer);
static idx load_shader(pix_renderer& renderer, const char* vsrc, const char* fsrc);

// Allocates the scene target and shadow map and switches end_frame over to the
// three pass path. Safe to skip entirely; everything below degrades to the
// plain forward draw when it has not been called.
static void pix_enable_effects(pix_renderer& renderer, int shadow_resolution = 2048);
static void pix_set_time(pix_renderer& renderer, float seconds);
// queued separately from push_instance: water is transparent, so it has to be
// drawn after everything opaque and with its own shader
static void push_water(pix_renderer& renderer, idx mesh, idx material, const mat4& transform);

static void begin_frame(pix_renderer& renderer, camera& cam);
static void push_instance(pix_renderer& renderer, pix_render_instance& instance);
static void push_instance(pix_renderer& renderer, idx mesh, idx material, const mat4& transform);
// `pose` must stay alive until end_frame; it is referenced, not copied
static void push_animated_instance(pix_renderer& renderer, pix_render_instance& instance,
    const animation& pose);
static void end_frame(pix_renderer& renderer, bool clear_instances = true);


// ------------------- implementation ---------------------

static void pix_set_time_of_day(pix_renderer& r, float hour) {
    r.hour = hour;
    pix_daylight_at(hour, &r.sun, &r.sky);
    r.night = pix_night_factor(r.sun);
}

static void push_light(pix_renderer& r, const pix_light& light) {
    pix_lights_add(r.lights, light);
}

static pix_renderer pix_create_renderer(int width, int height, idx shader, vec3 clear_color) {
    pix_renderer r = {};
    r.width = width;
    r.height = height;
    r.shader = shader;
    r.clear_color = clear_color;
    r.tex_arena.capacity = 32 * MB;
    r.tex_arena.data = (char*)malloc(r.tex_arena.capacity);

    glGenVertexArrays(1, &r.vao);
    glBindVertexArray(r.vao);

    glGenBuffers(1, &r.models_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, r.models_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(MESH_POOL_VERTICES * sizeof(vertex)), 0, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vertex), (void*)offsetof(vertex, position));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(vertex), (void*)offsetof(vertex, normal));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(vertex), (void*)offsetof(vertex, uv));
    glEnableVertexAttribArray(2);

    glGenBuffers(1, &r.ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(MESH_POOL_INDICES * sizeof(uint16_t)), 0, GL_STATIC_DRAW);

    glGenBuffers(1, &r.instances_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, r.instances_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(MAX_RENDERABLE * sizeof(mat4)), 0, GL_DYNAMIC_DRAW);
    for (int c = 0; c < 4; c++) {
        glVertexAttribPointer(3 + c, 4, GL_FLOAT, GL_FALSE, sizeof(mat4), (void*)(size_t)(c * 4 * sizeof(float)));
        glEnableVertexAttribArray(3 + c);
        glVertexAttribDivisor(3 + c, 1);
    }

    // ---- skinned pass: its own vao/pools, vertex_rigged layout ----
    glGenVertexArrays(1, &r.vao_animated);
    glBindVertexArray(r.vao_animated);

    glGenBuffers(1, &r.models_vbo_animated);
    glBindBuffer(GL_ARRAY_BUFFER, r.models_vbo_animated);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(SKIN_POOL_VERTICES * sizeof(vertex_rigged)), 0, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vertex_rigged), (void*)offsetof(vertex_rigged, position));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(vertex_rigged), (void*)offsetof(vertex_rigged, normal));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(vertex_rigged), (void*)offsetof(vertex_rigged, uv));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(vertex_rigged), (void*)offsetof(vertex_rigged, bone_ids));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(vertex_rigged), (void*)offsetof(vertex_rigged, bone_weights));
    glEnableVertexAttribArray(4);

    glGenBuffers(1, &r.ebo_animated);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r.ebo_animated);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(SKIN_POOL_INDICES * sizeof(uint16_t)), 0, GL_STATIC_DRAW);

    glBindVertexArray(0);

    r.skinned_shader = opengl_create_shader(VSHDER_SKINNED, shader_with_pbr(FSHDER_BASIC));

    unsigned char white[4] = { 255, 255, 255, 255 };
    r.white_texture = opengl_create_texture2d(1, 1, 4, white, TEXTURE_PIXELATED);

    r.projection_matrix = mat4_perspective(1.05f, (float)width / (float)height, 0.1f, 800.0f);
    r.view_matrix = mat4_identity();

    // A late morning sun: high enough that shadows are short and legible from
    // a third person camera, off-axis enough that every face of a box gets a
    // different amount of light.
    // Lighting is done in linear space now (the post pass owns the tonemap and
    // the single sRGB encode), so these are radiances, not 0..1 screen colours:
    // the sun sits well above 1, the sky/ground terms are pre-integrated diffuse
    // irradiance. Ambient still has to stay well under the sun or shadowed faces
    // wash out and the shadow map stops being visible - the point of casting it.
    pix_set_time_of_day(r, 10.0f);      // a late morning, shadows short and legible
    pix_lights_clear(r.lights);
    r.shadow_strength = 1.0f;
    r.shadow_extent = 220.0f;
    r.shadow_depth  = 620.0f;
    r.exposure      = 0.95f;
    r.saturation    = 1.10f;
    // threshold just above diffuse white, so only genuine speculars glare
    r.bloom_threshold = 1.05f;
    r.bloom_knee      = 0.55f;
    r.bloom_strength  = 0.55f;
    r.vignette      = 0.24f;
    r.sharpen       = 0.25f;
    // A real river is a green-brown that the sky sits on top of, not a blue.
    // The blue comes from the reflection; making the body blue as well gives
    // the flat cyan slab that reads as a swimming pool.
    r.water_shallow = v3(0.13f, 0.22f, 0.19f);
    r.water_deep    = v3(0.03f, 0.07f, 0.08f);
    r.water_depth   = 2.1f;
    for (int c = 0; c < SHADOW_CASCADES; c++) r.light_space[c] = mat4_identity();
    return r;
}

static void pix_enable_effects(pix_renderer& r, int shadow_resolution) {
    if (r.effects) return;
    r.scene_target = pix_create_render_target(r.width, r.height, true);
    // One atlas, the cascades side by side. Three separate maps would need
    // three samplers and three binds per draw; one texture needs neither, and
    // the only cost is remembering to inset each tile so a filter tap cannot
    // read into its neighbour.
    r.shadow_map   = pix_create_shadow_map(shadow_resolution * SHADOW_CASCADES,
                                           shadow_resolution);
    // quarter res is plenty: the result is about to be blurred anyway, and it
    // makes the two Gaussian passes essentially free
    int bw = r.width / 4 > 1 ? r.width / 4 : 1;
    int bh = r.height / 4 > 1 ? r.height / 4 : 1;
    r.bloom_a = pix_create_render_target(bw, bh, true);
    r.bloom_b = pix_create_render_target(bw, bh, true);
    r.bloom_prefilter_shader = opengl_create_shader(VSHDER_POST, FSHDER_BLOOM_PREFILTER);
    r.bloom_blur_shader      = opengl_create_shader(VSHDER_POST, FSHDER_BLOOM_BLUR);
    r.depth_shader         = opengl_create_shader(VSHDER_DEPTH, FSHDER_DEPTH);
    r.depth_skinned_shader = opengl_create_shader(VSHDER_DEPTH_SKINNED, FSHDER_DEPTH);
    r.post_shader          = opengl_create_shader(VSHDER_POST, shader_with_common(FSHDER_POST));
    // both stages evaluate the same wave field, so both get it spliced in
    {
        const char* wvs = shader_with_waves(VSHDER_WATER);
        const char* wfs = shader_with_waves(FSHDER_WATER);
        r.water_shader = opengl_create_shader(wvs, wfs);
    }
    r.sky_shader           = opengl_create_shader(VSHDER_SKY,  shader_with_common(FSHDER_SKY));
    r.effects = true;
}

static void pix_set_time(pix_renderer& r, float seconds) { r.time = seconds; }

static void push_water(pix_renderer& r, idx mesh, idx material, const mat4& transform) {
    if (mesh == (idx)-1 || r.water_count >= MAX_WATER_INSTANCES) return;
    pix_render_instance& it = r.water_instances[r.water_count++];
    it.mesh = mesh;
    it.material = material;
    it.transform = transform;
}

static void pix_destory_renderer(pix_renderer& renderer) {
    if (renderer.effects) {
        pix_destroy_framebuffer(renderer.scene_target);
        pix_destroy_framebuffer(renderer.shadow_map);
        renderer.effects = false;
    }
    free(renderer.tex_arena.data);
    renderer.tex_arena.data = 0;
    renderer.tex_arena.capacity = 0;
    renderer.tex_arena.size = 0;
}

static idx load_mesh(pix_renderer& r, mesh_file_data& md) {
    // Overrunning the shared pool used to fail silently: glBufferSubData past
    // the end of a buffer is simply ignored, so the last models loaded came out
    // as invisible geometry rather than as an error. Refuse instead.
    if (r.mesh_count >= MAX_MESHES) return (idx)-1;
    if (r.vertex_cursor + md.vertex_count > MESH_POOL_VERTICES) return (idx)-1;
    if (r.index_cursor  + md.index_count  > MESH_POOL_INDICES)  return (idx)-1;
    if (md.vertex_count > 65536) return (idx)-1;   // indices are uint16 per mesh

    idx id = (idx)r.mesh_count++;
    mesh& m = r.meshes[id];
    m.vbo = r.models_vbo;
    m.vertex_offset = r.vertex_cursor;
    m.vertex_size = sizeof(vertex);
    m.vertex_count = (idx)md.vertex_count;
    m.index_offset = r.index_cursor;
    m.index_count = (idx)md.index_count;

    glBindVertexArray(r.vao);
    glBindBuffer(GL_ARRAY_BUFFER, r.models_vbo);
    glBufferSubData(GL_ARRAY_BUFFER,
        (GLintptr)(r.vertex_cursor * sizeof(vertex)),
        (GLsizeiptr)(md.vertex_count * sizeof(vertex)), md.vertex_data);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r.ebo);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER,
        (GLintptr)(r.index_cursor * sizeof(uint16_t)),
        (GLsizeiptr)(md.index_count * sizeof(uint16_t)), md.index_data);
    glBindVertexArray(0);

    r.vertex_cursor += (idx)md.vertex_count;
    r.index_cursor += (idx)md.index_count;
    return id;
}

static idx load_skinned_mesh(pix_renderer& r, skinned_mesh_file_data& md) {
    if (r.skinned_mesh_count >= MAX_MESHES) return (idx)-1;
    if (r.skin_vertex_cursor + md.vertex_count > SKIN_POOL_VERTICES) return (idx)-1;
    if (r.skin_index_cursor  + md.index_count  > SKIN_POOL_INDICES)  return (idx)-1;
    idx id = (idx)r.skinned_mesh_count++;
    mesh& m = r.skinned_meshes[id];
    m.vbo = r.models_vbo_animated;
    m.vertex_offset = r.skin_vertex_cursor;
    m.vertex_size = sizeof(vertex_rigged);
    m.vertex_count = (idx)md.vertex_count;
    m.index_offset = r.skin_index_cursor;
    m.index_count = (idx)md.index_count;

    glBindVertexArray(r.vao_animated);
    glBindBuffer(GL_ARRAY_BUFFER, r.models_vbo_animated);
    glBufferSubData(GL_ARRAY_BUFFER,
        (GLintptr)(r.skin_vertex_cursor * sizeof(vertex_rigged)),
        (GLsizeiptr)(md.vertex_count * sizeof(vertex_rigged)), md.vertex_data);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r.ebo_animated);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER,
        (GLintptr)(r.skin_index_cursor * sizeof(uint16_t)),
        (GLsizeiptr)(md.index_count * sizeof(uint16_t)), md.index_data);
    glBindVertexArray(0);

    r.skin_vertex_cursor += (idx)md.vertex_count;
    r.skin_index_cursor += (idx)md.index_count;
    return id;
}

static idx load_material(pix_renderer& r, const char* texture_path, vec3 color, float metalic, float roughness) {
    idx id = (idx)r.material_count++;
    material& mat = r.materials[id];
    mat.shader = r.shader;
    mat.texture = 0;
    mat.color = color;
    mat.metallic = metalic;
    mat.roughness = roughness;
    mat.emissive = v3(0.0f, 0.0f, 0.0f);

    if (texture_path) {
        png_result png = {};
        if (png_load_file(r.tex_arena, texture_path, &png)) {
            // the KayKit atlas is a grid of flat colour swatches - mipmaps would
            // bleed neighbouring swatches together and wash everything brown.
            mat.texture = opengl_create_texture2d(png.width, png.height, 4, png.pixels, TEXTURE_LINEAR);
        }
        arena_reset(r.tex_arena); // decoded pixels now live on the GPU
    }
    return id;
}

static idx load_material_image(pix_renderer& r, image_file_data* image, vec3 color,
                               float metalic, float roughness, bool pixelated) {
    idx id = (idx)r.material_count++;
    material& mat = r.materials[id];
    mat.shader = r.shader;
    mat.texture = 0;
    mat.color = color;
    mat.metallic = metalic;
    mat.roughness = roughness;
    mat.emissive = v3(0.0f, 0.0f, 0.0f);
    if (image && image->data)
        mat.texture = opengl_create_texture2d(image->width, image->height, 4, image->data,
                                              pixelated ? TEXTURE_PIXELATED : TEXTURE_LINEAR);
    return id;
}

static idx clone_material(pix_renderer& r, idx source, vec3 color,
                          float metallic, float roughness) {
    if (source >= r.material_count || r.material_count >= MAX_MATERIALS) return source;
    idx id = (idx)r.material_count++;
    material& mat = r.materials[id];
    mat.shader = r.materials[source].shader;
    mat.texture = r.materials[source].texture;   // shared, not re-uploaded
    mat.color = color;
    mat.metallic = metallic;
    mat.roughness = roughness;
    mat.emissive = r.materials[source].emissive;
    return id;
}

static idx get_default_material(pix_renderer& r) {
    return load_material(r, nullptr, v3(1.0f, 1.0f, 1.0f), 0.1f, 0.5f);
}

static idx load_shader(pix_renderer& r, const char* vsrc, const char* fsrc) {
    idx prog = opengl_create_shader(vsrc, fsrc);
    r.shaders[r.shader_count++] = prog;
    return prog;
}

// Fits one orthographic box per cascade around the slice of the view it covers,
// and snaps each to whole shadow-map texels.
//
// Without the snap the box slides continuously as the player walks and every
// shadow edge crawls and shimmers; with it the box only ever moves in whole
// texel steps and the edges sit still. The splits are geometric rather than
// even, because perspective means the near half of the view occupies most of
// the screen and deserves most of the resolution.
static void pix__fit_cascades(pix_renderer& r, const camera& cam) {
    const float near_plane = 6.0f;
    int tile = r.shadow_map.height;                 // the atlas is square per tile

    for (int c = 0; c < SHADOW_CASCADES; c++) {
        // 6 m, 40 m, 220 m for three cascades over a 220 m extent
        float t = (float)(c + 1) / (float)SHADOW_CASCADES;
        float far_d = near_plane * powf(r.shadow_extent / near_plane, t);
        r.cascade_far[c] = far_d;

        // The box has to cover the slice from whatever angle the camera is at,
        // so it is sized by the slice's diagonal rather than its length - which
        // is also what makes its size independent of where the camera looks,
        // and therefore stable under rotation.
        float near_d = (c == 0) ? 0.0f
                                : near_plane * powf(r.shadow_extent / near_plane,
                                                    (float)c / (float)SHADOW_CASCADES);
        float extent = (far_d - near_d) * 0.75f + far_d * 0.35f;

        vec3 focus = v3add(cam.position, v3scale(cam.direction, (near_d + far_d) * 0.5f));
        focus.y = 0.0f;

        float texel = (extent * 2.0f) / (float)tile;
        focus.x = floorf(focus.x / texel) * texel;
        focus.z = floorf(focus.z / texel) * texel;
        r.cascade_texel[c] = texel;

        vec3 eye = v3add(focus, v3scale(r.sun.direction, r.shadow_depth * 0.5f));
        mat4 light_view = mat4_lookat(eye, focus, v3(0.0f, 1.0f, 0.0f));
        mat4 light_proj = mat4_ortho(-extent, extent, -extent, extent, 1.0f, r.shadow_depth);
        r.light_space[c] = mat4_mul(light_proj, light_view);
    }
}

static void begin_frame(pix_renderer& r, camera& cam) {
    r.view_matrix = mat4_lookat(cam.position, v3add(cam.position, cam.direction), cam.up);
    r.camera_position = cam.position;
    r.renderable_count = 0;
    r.animated_count = 0;
    r.water_count = 0;
    pix_lights_clear(r.lights);
    if (r.effects) pix__fit_cascades(r, cam);
}

static void push_instance(pix_renderer& r, pix_render_instance& instance) {
    if (r.renderable_count < MAX_RENDERABLE)
        r.instances[r.renderable_count++] = instance;
}

// the form world building actually uses: no temporary to fill in first
static void push_instance(pix_renderer& r, idx mesh, idx material, const mat4& transform) {
    if (mesh == (idx)-1 || material == (idx)-1) return;
    if (r.renderable_count >= MAX_RENDERABLE) return;
    pix_render_instance& it = r.instances[r.renderable_count++];
    it.mesh = mesh;
    it.material = material;
    it.transform = transform;
}

static void push_animated_instance(pix_renderer& r, pix_render_instance& instance, const animation& pose) {
    if (r.animated_count >= MAX_ANIMATED) return;
    r.animated_instances[r.animated_count] = instance;
    r.animated_poses[r.animated_count] = &pose;   // referenced, not copied
    r.animated_count++;
}

// Grouping instances by (mesh, material) is what turns the frame into a handful
// of instanced draws, and with tens of thousands of them a comparison sort
// shows up in the profile. The key space is small and known - MAX_MESHES *
// MAX_MATERIALS - so a counting sort does it in one linear pass instead.
#define PIX__SORT_KEYS (MAX_MESHES * MAX_MATERIALS)

static void pix__sort_instances(pix_render_instance* items, size_t count) {
    static uint32_t counts[PIX__SORT_KEYS + 1];
    static pix_render_instance scratch[MAX_RENDERABLE];
    static uint32_t keys[MAX_RENDERABLE];
    if (count < 2) return;

    uint32_t lo = PIX__SORT_KEYS, hi = 0;
    for (size_t i = 0; i < count; i++) {
        uint32_t k = (uint32_t)items[i].mesh * MAX_MATERIALS + (uint32_t)items[i].material;
        if (k >= PIX__SORT_KEYS) k = PIX__SORT_KEYS - 1;   // malformed instance, keep it in range
        keys[i] = k;
        if (k < lo) lo = k;
        if (k > hi) hi = k;
    }
    if (lo == hi) return;                                  // already one batch

    memset(counts + lo, 0, (hi - lo + 2) * sizeof(uint32_t));
    for (size_t i = 0; i < count; i++) counts[keys[i] + 1]++;
    for (uint32_t k = lo; k <= hi; k++) counts[k + 1] += counts[k];
    for (size_t i = 0; i < count; i++) scratch[counts[keys[i]]++] = items[i];
    memcpy(items, scratch, count * sizeof(pix_render_instance));
}

// Walks the sorted instance list once, issuing one instanced draw per
// (mesh, material) run. Shared by the shadow pass and the colour pass; only the
// shadow pass skips the material work, since depth does not care about it.
static void pix__draw_instanced(pix_renderer& r, idx program, bool with_material) {
    static mat4 batch[MAX_RENDERABLE];

    GLint u_color    = with_material ? glGetUniformLocation(program, "uColor")     : -1;
    GLint u_metallic = with_material ? glGetUniformLocation(program, "uMetallic")  : -1;
    GLint u_rough    = with_material ? glGetUniformLocation(program, "uRoughness") : -1;
    GLint u_emissive = with_material ? glGetUniformLocation(program, "uEmissive")  : -1;

    glBindVertexArray(r.vao);
    glBindBuffer(GL_ARRAY_BUFFER, r.instances_vbo);

    size_t i = 0;
    while (i < r.renderable_count) {
        idx mi = r.instances[i].mesh;
        idx ma = r.instances[i].material;
        size_t j = i;
        while (j < r.renderable_count && r.instances[j].mesh == mi && r.instances[j].material == ma) {
            batch[j - i] = r.instances[j].transform;
            j++;
        }
        int n = (int)(j - i);
        glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(n * sizeof(mat4)), batch);

        if (with_material) {
            material& mat = r.materials[ma];
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, mat.texture ? mat.texture : r.white_texture);
            glUniform3f(u_color, mat.color.x, mat.color.y, mat.color.z);
            glUniform1f(u_metallic, mat.metallic);
            glUniform1f(u_rough, mat.roughness);
            glUniform3f(u_emissive, mat.emissive.x, mat.emissive.y, mat.emissive.z);
        }

        mesh& me = r.meshes[mi];
        glDrawElementsInstancedBaseVertex(
            GL_TRIANGLES, (GLsizei)me.index_count, GL_UNSIGNED_SHORT,
            (void*)(size_t)(me.index_offset * sizeof(uint16_t)), n, (GLint)me.vertex_offset);

        i = j;
    }
}

// One draw per animated instance: each needs its own uBones upload, so there is
// nothing instancing could save here.
static void pix__draw_skinned(pix_renderer& r, idx program, bool with_material) {
    GLint u_color    = with_material ? glGetUniformLocation(program, "uColor")     : -1;
    GLint u_metallic = with_material ? glGetUniformLocation(program, "uMetallic")  : -1;
    GLint u_rough    = with_material ? glGetUniformLocation(program, "uRoughness") : -1;
    GLint u_emissive = with_material ? glGetUniformLocation(program, "uEmissive")  : -1;
    GLint u_model = glGetUniformLocation(program, "uModel");
    GLint u_bones = glGetUniformLocation(program, "uBones[0]");
    if (u_bones < 0) u_bones = glGetUniformLocation(program, "uBones");

    glBindVertexArray(r.vao_animated);
    for (size_t k = 0; k < r.animated_count; k++) {
        pix_render_instance& inst = r.animated_instances[k];
        const animation* pose = r.animated_poses[k];
        if (!pose || inst.mesh >= r.skinned_mesh_count) continue;

        if (with_material) {
            material& mat = r.materials[inst.material];
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, mat.texture ? mat.texture : r.white_texture);
            glUniform3f(u_color, mat.color.x, mat.color.y, mat.color.z);
            glUniform1f(u_metallic, mat.metallic);
            glUniform1f(u_rough, mat.roughness);
            glUniform3f(u_emissive, mat.emissive.x, mat.emissive.y, mat.emissive.z);
        }
        glUniformMatrix4fv(u_model, 1, GL_FALSE, inst.transform.data);

        size_t nb = pose->bone_count < MAX_ANIM_BONES ? pose->bone_count : MAX_ANIM_BONES;
        if (nb) glUniformMatrix4fv(u_bones, (GLsizei)nb, GL_FALSE, pose->bones[0].data);

        mesh& me = r.skinned_meshes[inst.mesh];
        glDrawElementsInstancedBaseVertex(
            GL_TRIANGLES, (GLsizei)me.index_count, GL_UNSIGNED_SHORT,
            (void*)(size_t)(me.index_offset * sizeof(uint16_t)), 1, (GLint)me.vertex_offset);
    }
}

static void pix__set_lighting(pix_renderer& r, idx program) {
    glUniform3f(glGetUniformLocation(program, "uSunDir"),
                r.sun.direction.x, r.sun.direction.y, r.sun.direction.z);
    glUniform3f(glGetUniformLocation(program, "uSunColor"),
                r.sun.color.x, r.sun.color.y, r.sun.color.z);
    glUniform3f(glGetUniformLocation(program, "uSkyColor"),
                r.sky.diffuse_up.x, r.sky.diffuse_up.y, r.sky.diffuse_up.z);
    glUniform3f(glGetUniformLocation(program, "uGroundColor"),
                r.sky.diffuse_down.x, r.sky.diffuse_down.y, r.sky.diffuse_down.z);
    glUniform3f(glGetUniformLocation(program, "uSkyZenith"),
                r.sky.zenith.x, r.sky.zenith.y, r.sky.zenith.z);
    glUniform3f(glGetUniformLocation(program, "uSkyHorizon"),
                r.sky.horizon.x, r.sky.horizon.y, r.sky.horizon.z);
    glUniform3f(glGetUniformLocation(program, "uFogColor"),
                r.sky.fog.x, r.sky.fog.y, r.sky.fog.z);
    glUniform1f(glGetUniformLocation(program, "uFogDensity"), r.sky.fog_density);
    glUniform3f(glGetUniformLocation(program, "uCameraPos"),
                r.camera_position.x, r.camera_position.y, r.camera_position.z);
}

// The frame's punctual lights, already reduced to the nearest few. Three array
// uploads and a count - there is no per-light work here, which is the whole
// reason the list is packed the way it is.
static void pix__bind_lights(pix_renderer& r, idx program) {
    int n = r.lights.packed_count;
    glUniform1i(glGetUniformLocation(program, "uLightCount"), n);
    if (!n) return;
    glUniform4fv(glGetUniformLocation(program, "uLightPosRadius[0]"),  n, r.lights.pos_radius);
    glUniform4fv(glGetUniformLocation(program, "uLightColorInner[0]"), n, r.lights.color_inner);
    glUniform4fv(glGetUniformLocation(program, "uLightDirOuter[0]"),   n, r.lights.dir_outer);
}

static void pix__bind_shadow(pix_renderer& r, idx program) {
    glUniformMatrix4fv(glGetUniformLocation(program, "uLightSpace[0]"), SHADOW_CASCADES,
                       GL_FALSE, r.light_space[0].data);
    glUniform4f(glGetUniformLocation(program, "uCascadeFar"),
                r.cascade_far[0], r.cascade_far[1], r.cascade_far[2], 0.0f);
    glUniform4f(glGetUniformLocation(program, "uCascadeTexel"),
                r.cascade_texel[0], r.cascade_texel[1], r.cascade_texel[2], 0.0f);
    glUniform2f(glGetUniformLocation(program, "uShadowTexel"),
                1.0f / (float)r.shadow_map.width, 1.0f / (float)r.shadow_map.height);
    glUniform1f(glGetUniformLocation(program, "uShadowStrength"),
                r.effects ? r.shadow_strength : 0.0f);
    glUniform1i(glGetUniformLocation(program, "uShadowMap"), 1);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, r.effects ? r.shadow_map.depth : r.white_texture);
    glActiveTexture(GL_TEXTURE0);
}

// Renders the world from the sun into the depth-only target. Front faces are
// culled so the depth recorded is the *back* of each object, which moves the
// self-shadowing artefact to surfaces the camera cannot see anyway.
static void pix__shadow_pass(pix_renderer& r) {
    pix_bind_framebuffer(r.shadow_map);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);

    int tile = r.shadow_map.height;
    for (int c = 0; c < SHADOW_CASCADES; c++) {
        glViewport(c * tile, 0, tile, tile);

        glUseProgram(r.depth_shader);
        glUniformMatrix4fv(glGetUniformLocation(r.depth_shader, "uViewProj"), 1, GL_FALSE,
                           r.light_space[c].data);
        pix__draw_instanced(r, r.depth_shader, false);

        // Characters only cast into the near cascades. A person is under two
        // metres across; by the far cascade one is smaller than a shadow texel,
        // so the draw costs a full skinned pass and puts nothing on screen.
        if (r.animated_count && c < SHADOW_CASCADES - 1) {
            glUseProgram(r.depth_skinned_shader);
            glUniformMatrix4fv(glGetUniformLocation(r.depth_skinned_shader, "uViewProj"), 1,
                               GL_FALSE, r.light_space[c].data);
            pix__draw_skinned(r, r.depth_skinned_shader, false);
        }
    }
    glViewport(0, 0, r.shadow_map.width, r.shadow_map.height);

    glCullFace(GL_BACK);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(0);
}

// ---- bloom ----
//
// The scene target is real HDR, so a sun glint off a car roof, a lamp head or a
// wave crest is genuinely brighter than 1 while the tonemap squashes it back to
// near white. This is what puts that energy back on screen as glare, and it is
// the single thing that makes a bright highlight read as *bright* rather than
// as a pale patch. Threshold with a soft knee, then two separable Gaussians at
// quarter resolution.
static void pix__bloom_pass(pix_renderer& r) {
    if (r.bloom_strength <= 0.0f || !r.bloom_prefilter_shader) return;

    pix_bind_framebuffer(r.bloom_a);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(r.bloom_prefilter_shader);
    glUniform1i(glGetUniformLocation(r.bloom_prefilter_shader, "uScene"), 0);
    glUniform2f(glGetUniformLocation(r.bloom_prefilter_shader, "uTexel"),
                1.0f / (float)r.width, 1.0f / (float)r.height);
    glUniform1f(glGetUniformLocation(r.bloom_prefilter_shader, "uThreshold"), r.bloom_threshold);
    glUniform1f(glGetUniformLocation(r.bloom_prefilter_shader, "uKnee"), r.bloom_knee);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r.scene_target.color);
    pix_draw_fullscreen();

    float tx = 1.0f / (float)r.bloom_a.width;
    float ty = 1.0f / (float)r.bloom_a.height;
    glUseProgram(r.bloom_blur_shader);
    glUniform1i(glGetUniformLocation(r.bloom_blur_shader, "uSource"), 0);

    pix_bind_framebuffer(r.bloom_b);
    glUniform2f(glGetUniformLocation(r.bloom_blur_shader, "uDirection"), tx, 0.0f);
    glBindTexture(GL_TEXTURE_2D, r.bloom_a.color);
    pix_draw_fullscreen();

    pix_bind_framebuffer(r.bloom_a);
    glUniform2f(glGetUniformLocation(r.bloom_blur_shader, "uDirection"), 0.0f, ty);
    glBindTexture(GL_TEXTURE_2D, r.bloom_b.color);
    pix_draw_fullscreen();

    glEnable(GL_DEPTH_TEST);
}

static void pix__water_pass(pix_renderer& r, const mat4& view_proj) {
    if (!r.water_count || !r.water_shader) return;
    static mat4 batch[MAX_WATER_INSTANCES];

    glUseProgram(r.water_shader);
    glUniformMatrix4fv(glGetUniformLocation(r.water_shader, "uViewProj"), 1, GL_FALSE, view_proj.data);
    glUniform1f(glGetUniformLocation(r.water_shader, "uTime"), r.time);
    glUniform1f(glGetUniformLocation(r.water_shader, "uBedDepth"), r.water_depth);
    glUniform3f(glGetUniformLocation(r.water_shader, "uShallowColor"),
                r.water_shallow.x, r.water_shallow.y, r.water_shallow.z);
    glUniform3f(glGetUniformLocation(r.water_shader, "uDeepColor"),
                r.water_deep.x, r.water_deep.y, r.water_deep.z);
    pix__set_lighting(r, r.water_shader);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);          // water does not occlude the water behind it

    glBindVertexArray(r.vao);
    glBindBuffer(GL_ARRAY_BUFFER, r.instances_vbo);

    size_t i = 0;
    while (i < r.water_count) {
        idx mi = r.water_instances[i].mesh;
        size_t j = i;
        while (j < r.water_count && r.water_instances[j].mesh == mi) {
            batch[j - i] = r.water_instances[j].transform;
            j++;
        }
        int n = (int)(j - i);
        glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(n * sizeof(mat4)), batch);
        mesh& me = r.meshes[mi];
        glDrawElementsInstancedBaseVertex(
            GL_TRIANGLES, (GLsizei)me.index_count, GL_UNSIGNED_SHORT,
            (void*)(size_t)(me.index_offset * sizeof(uint16_t)), n, (GLint)me.vertex_offset);
        i = j;
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}

// The backdrop. A full-screen triangle turned back into world-space view rays
// by the inverse view-projection, shaded by the same analytic sky the surface
// and water shaders sample - so what fills the horizon and what a car reflects
// are the same function. Drawn straight after the clear with depth writes off,
// so every later opaque pixel paints over it for free.
static void pix__sky_pass(pix_renderer& r, const mat4& view_proj) {
    if (!r.sky_shader) return;
    mat4 inv = mat4_inverse(view_proj);

    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(r.sky_shader);
    glUniformMatrix4fv(glGetUniformLocation(r.sky_shader, "uInvViewProj"), 1, GL_FALSE, inv.data);
    glUniform3f(glGetUniformLocation(r.sky_shader, "uCameraPos"),
                r.camera_position.x, r.camera_position.y, r.camera_position.z);
    glUniform3f(glGetUniformLocation(r.sky_shader, "uSunDir"),
                r.sun.direction.x, r.sun.direction.y, r.sun.direction.z);
    glUniform3f(glGetUniformLocation(r.sky_shader, "uSunColor"),
                r.sun.color.x, r.sun.color.y, r.sun.color.z);
    glUniform3f(glGetUniformLocation(r.sky_shader, "uSkyZenith"),
                r.sky.zenith.x, r.sky.zenith.y, r.sky.zenith.z);
    glUniform3f(glGetUniformLocation(r.sky_shader, "uSkyHorizon"),
                r.sky.horizon.x, r.sky.horizon.y, r.sky.horizon.z);
    glUniform3f(glGetUniformLocation(r.sky_shader, "uGroundColor"),
                r.sky.diffuse_down.x, r.sky.diffuse_down.y, r.sky.diffuse_down.z);
    glUniform3f(glGetUniformLocation(r.sky_shader, "uFogColor"),
                r.sky.fog.x, r.sky.fog.y, r.sky.fog.z);
    pix_draw_fullscreen();

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
}

static void pix__post_pass(pix_renderer& r) {
    pix_bind_backbuffer(r.width, r.height);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(r.post_shader);
    glUniform1i(glGetUniformLocation(r.post_shader, "uScene"), 0);
    glUniform2f(glGetUniformLocation(r.post_shader, "uTexel"),
                1.0f / (float)r.width, 1.0f / (float)r.height);
    glUniform1f(glGetUniformLocation(r.post_shader, "uExposure"), r.exposure);
    glUniform1f(glGetUniformLocation(r.post_shader, "uSaturation"), r.saturation);
    glUniform1f(glGetUniformLocation(r.post_shader, "uVignette"), r.vignette);
    glUniform1f(glGetUniformLocation(r.post_shader, "uSharpen"), r.sharpen);
    glUniform1f(glGetUniformLocation(r.post_shader, "uBloomStrength"), r.bloom_strength);
    glUniform1i(glGetUniformLocation(r.post_shader, "uBloom"), 1);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, r.bloom_a.color);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r.scene_target.color);
    pix_draw_fullscreen();

    glEnable(GL_DEPTH_TEST);
}

static void end_frame(pix_renderer& r, bool clear_instances) {
    mat4 vp = mat4_mul(r.projection_matrix, r.view_matrix);

    // group instances so each (mesh, material) run is one instanced draw
    pix__sort_instances(r.instances, r.renderable_count);
    // and reduce this frame's lights to the handful a fragment will evaluate
    pix_lights_pack(r.lights, r.camera_position);

    if (r.effects) pix__shadow_pass(r);

    if (r.effects) pix_bind_framebuffer(r.scene_target);
    else           pix_bind_backbuffer(r.width, r.height);

    glEnable(GL_DEPTH_TEST);
    glClearColor(r.clear_color.x, r.clear_color.y, r.clear_color.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (r.effects) pix__sky_pass(r, vp);

    glUseProgram(r.shader);
    glUniformMatrix4fv(glGetUniformLocation(r.shader, "uViewProj"), 1, GL_FALSE, vp.data);
    glUniform1i(glGetUniformLocation(r.shader, "uTex"), 0);
    pix__set_lighting(r, r.shader);
    pix__bind_lights(r, r.shader);
    pix__bind_shadow(r, r.shader);
    pix__draw_instanced(r, r.shader, true);

    if (r.animated_count && r.skinned_shader) {
        glUseProgram(r.skinned_shader);
        glUniformMatrix4fv(glGetUniformLocation(r.skinned_shader, "uViewProj"), 1, GL_FALSE, vp.data);
        glUniform1i(glGetUniformLocation(r.skinned_shader, "uTex"), 0);
        pix__set_lighting(r, r.skinned_shader);
        pix__bind_lights(r, r.skinned_shader);
        pix__bind_shadow(r, r.skinned_shader);
        pix__draw_skinned(r, r.skinned_shader, true);
    }

    pix__water_pass(r, vp);

    glBindVertexArray(0);
    if (r.effects) {
        pix__bloom_pass(r);
        pix__post_pass(r);
    }

    if (clear_instances) { r.renderable_count = 0; r.animated_count = 0; r.water_count = 0; }
}
