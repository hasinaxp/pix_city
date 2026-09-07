// Headless asset report: parses every glTF the catalogue references without
// touching OpenGL, and prints what actually came out of each file - node
// names, vertex counts, whether a texture or a generated palette was found,
// skeleton size and the clip names a rig ships.
//
//   cl /nologo /std:c++14 /EHsc /O2 /Fe:build\dump_assets.exe /Fo:build\ tools\dump_assets.cpp
//   build\dump_assets.exe [dir ...]
//
// The point of it is that "the trees do not load" is otherwise invisible: the
// game drops a model that fails to parse and carries on, so a broken file
// looks exactly like a model the generator chose not to place.
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "../src/loader/data_loader.hpp"
#include "../src/loader/png_writer.hpp"

static pix_data_loader loader;

static void report_gltf(const char* path) {
    const char* base = strrchr(path, '\\');
    base = base ? base + 1 : path;

    gltf_node_info nodes[32];
    size_t mark = loader.arena.size;
    int n = gltf_list_nodes(loader.arena, path, nodes, 32);
    loader.arena.size = mark;

    if (n <= 0) {
        printf("  %-56s  FAILED to open / no mesh nodes\n", base);
        return;
    }

    // whole-file load, the way a single prop is loaded
    mark = loader.arena.size;
    gltf_result whole = {};
    bool ok_whole = gltf_load_file(loader.arena, path, &whole, -1);
    size_t whole_verts = whole.mesh.vertex_count;
    size_t whole_bones = whole.skeleton.bone_count;
    size_t whole_clips = whole.clip_count;
    bool   whole_img   = whole.image.data != 0;
    bool   whole_pal   = whole.image_is_palette;

    printf("  %-56s  nodes %2d   %s", base, n,
           ok_whole ? "whole:ok " : "whole:FAIL");
    if (ok_whole)
        printf(" verts %5zu  bones %3zu  clips %2zu  img %s%s",
               whole_verts, whole_bones, whole_clips,
               whole_img ? "yes" : "NO ", whole_pal ? " (palette)" : "");
    printf("\n");

    // Geometry sanity. An index that points past the vertex array, a uv far
    // outside 0..1 and a texture that is mostly transparent are each
    // invisible in a vertex count, and each one wrecks the model on screen in
    // its own recognisable way - so each one is worth naming here rather than
    // being guessed at from a screenshot.
    if (ok_whole) {
        uint32_t max_index = 0;
        for (size_t i = 0; i < whole.mesh.index_count; i++)
            if (whole.mesh.index_data[i] > max_index) max_index = whole.mesh.index_data[i];
        float uvmin = 1e30f, uvmax = -1e30f;
        for (size_t i = 0; i < whole.mesh.vertex_count; i++) {
            vec2 uv = whole.mesh.vertex_data[i].uv;
            if (uv.x < uvmin) uvmin = uv.x;
            if (uv.y < uvmin) uvmin = uv.y;
            if (uv.x > uvmax) uvmax = uv.x;
            if (uv.y > uvmax) uvmax = uv.y;
        }
        int cut = 0, total = 0;
        if (whole.image.data) {
            total = whole.image.width * whole.image.height;
            for (int i = 0; i < total; i++)
                if ((unsigned char)whole.image.data[i * 4 + 3] < 128) cut++;
        }
        // Where the mass sits up the model Y axis, in ten bands.
        //
        // A model is placed by its origin, and for anything that has to line
        // up with something else in the world - a bridge deck with a road, a
        // lamp head with a light - the origin is the wrong reference and the
        // bounding box is not much better. What is wanted is the height the
        // *deck* is at, and that shows up here as the band the vertices pile
        // into: a truss bridge is piers, then a spike at the deck, then the
        // open lattice above it.
        {
            vec3 bmn = v3(1e30f, 1e30f, 1e30f), bmx = v3(-1e30f, -1e30f, -1e30f);
            for (size_t i2 = 0; i2 < whole.mesh.vertex_count; i2++) {
                vec3 q = whole.mesh.vertex_data[i2].position;
                if (q.y < bmn.y) bmn.y = q.y;  if (q.y > bmx.y) bmx.y = q.y;
            }
            float span = bmx.y - bmn.y;
            if (span > 1e-6f && whole.mesh.vertex_count) {
                int bins[10] = {0};
                for (size_t i2 = 0; i2 < whole.mesh.vertex_count; i2++) {
                    int b = (int)((whole.mesh.vertex_data[i2].position.y - bmn.y) / span * 9.999f);
                    if (b >= 0 && b < 10) bins[b]++;
                }
                printf("      height bands (low to high, %% of verts):");
                for (int b = 0; b < 10; b++)
                    printf(" %d", bins[b] * 100 / (int)whole.mesh.vertex_count);
                printf("\n");
            }
        }

        // How many distinct materials the file paints itself with, and how
        // much geometry each one actually carries. A prop is loaded once per
        // material (see city__add_gltf_piece), so a file whose materials are
        // all declared but unused loads as nothing at all - which looks
        // exactly like a missing file from the catalogue side.
        {
            int mats[8];
            size_t mm = loader.arena.size;
            int nm = gltf_list_materials(loader.arena, path, -1, mats, 8);
            loader.arena.size = mm;
            printf("      materials: %d", nm);
            for (int k = 0; k < nm; k++) {
                mm = loader.arena.size;
                gltf_result one = {};
                bool ok = gltf_load_file(loader.arena, path, &one, -1, mats[k]);
                printf("   [%d]%s%zu verts", mats[k], ok ? " " : " FAIL ",
                       ok ? one.mesh.vertex_count : (size_t)0);
                loader.arena.size = mm;
            }
            printf("\n");
        }

        // Extents, in the file own units. This is what a catalogue entry target
        // size has to be chosen against - a kit authored in centimetres and one
        // authored in metres look identical in a vertex count and are a hundred
        // times apart on screen - and it is also the box every static collider
        // for the model is derived from.
        vec3 mn = v3(1e30f, 1e30f, 1e30f), mx = v3(-1e30f, -1e30f, -1e30f);
        for (size_t i = 0; i < whole.mesh.vertex_count; i++) {
            vec3 q = whole.mesh.vertex_data[i].position;
            if (q.x < mn.x) mn.x = q.x;  if (q.x > mx.x) mx.x = q.x;
            if (q.y < mn.y) mn.y = q.y;  if (q.y > mx.y) mx.y = q.y;
            if (q.z < mn.z) mn.z = q.z;  if (q.z > mx.z) mx.z = q.z;
        }
        if (whole.mesh.vertex_count)
            printf("      size: %.2f x %.2f x %.2f   base y %.2f   centre (%.2f, %.2f)\n",
                   mx.x - mn.x, mx.y - mn.y, mx.z - mn.z, mn.y,
                   (mn.x + mx.x) * 0.5f, (mn.z + mx.z) * 0.5f);

        printf("      geom: inds %6zu  max index %5u%s  uv %.2f..%.2f%s  tex %dx%d  "
               "alpha<0.5 %d%%\n",
               whole.mesh.index_count, max_index,
               max_index >= whole.mesh.vertex_count ? " << PAST THE VERTICES" : "",
               uvmin, uvmax, (uvmin < -0.01f || uvmax > 1.01f) ? " << OUTSIDE 0..1" : "",
               whole.image.width, whole.image.height, total ? cut * 100 / total : 0);
    }
    loader.arena.size = mark;

    // per-node, the way a family file is loaded
    if (n > 1) {
        for (int i = 0; i < n; i++) {
            mark = loader.arena.size;
            gltf_result one = {};
            bool ok = gltf_load_file(loader.arena, path, &one, nodes[i].node);
            printf("      node %2d %-32s %s", nodes[i].node, nodes[i].name,
                   ok ? "ok " : "FAIL");
            if (ok) printf(" verts %5zu", one.mesh.vertex_count);
            printf("\n");
            loader.arena.size = mark;
        }
    }

    if (whole_clips) {
        mark = loader.arena.size;
        gltf_result again = {};
        if (gltf_load_file(loader.arena, path, &again, -1)) {
            printf("      clips:");
            for (size_t i = 0; i < again.clip_count; i++)
                printf(" %s", again.clips[i].name);
            printf("\n");
            printf("      bones:");
            for (size_t i = 0; i < again.skeleton.bone_count && i < 128; i++)
                printf(" %s", again.skeleton.bones[i].name);
            
            printf("\n");
        }
        loader.arena.size = mark;
    }
}

static void report_obj(const char* path) {
    const char* base = strrchr(path, '\\');
    base = base ? base + 1 : path;
    size_t mark = loader.arena.size;
    mesh_file_data m = {};
    bool ok = obj_load_file(loader.arena, path, &m, &loader.palette);
    printf("  %-56s  %s", base, ok ? "ok  " : "FAIL");
    if (ok) printf(" verts %5zu  inds %6zu  groups %zu", m.vertex_count, m.index_count, m.group_count);
    printf("\n");
    loader.arena.size = mark;
}

static void scan(const char* dir) {
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) { printf("(no such directory: %s)\n", dir); return; }

    printf("\n=== %s ===\n", dir);
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const char* dot = strrchr(fd.cFileName, '.');
        if (!dot) continue;
        char path[512];
        snprintf(path, sizeof(path), "%s\\%s", dir, fd.cFileName);
        if (_stricmp(dot, ".glb") == 0 || _stricmp(dot, ".gltf") == 0) report_gltf(path);
        else if (_stricmp(dot, ".obj") == 0) report_obj(path);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

// Writes one file's embedded texture out as a PNG, so it can actually be
// looked at. A uv range and an image size say nothing about whether the model
// is being painted with the right part of the right picture.
static void dump_texture(const char* path, const char* out) {
    size_t mark = loader.arena.size;
    gltf_result g = {};
    if (gltf_load_file(loader.arena, path, &g, -1) && g.image.data) {
        int n = g.image.width * g.image.height;
        unsigned char* rgb = (unsigned char*)malloc((size_t)n * 3);
        for (int i = 0; i < n; i++) {
            rgb[i * 3 + 0] = (unsigned char)g.image.data[i * 4 + 0];
            rgb[i * 3 + 1] = (unsigned char)g.image.data[i * 4 + 1];
            rgb[i * 3 + 2] = (unsigned char)g.image.data[i * 4 + 2];
        }
        printf("wrote %s  (%d x %d)\n", out, g.image.width, g.image.height);
        png_write_rgb(out, rgb, g.image.width, g.image.height);
        free(rgb);
    } else {
        printf("no texture in %s\n", path);
    }
    loader.arena.size = mark;
}

int main(int argc, char** argv) {
    loader = pix_create_data_loader(512 * MB);

    if (argc == 4 && strcmp(argv[1], "--tex") == 0) {
        dump_texture(argv[2], argv[3]);
        return 0;
    }

    static const char* DEFAULTS[] = {
        "assets\\nature", "assets\\road_objects", "assets\\paths", "assets\\cars",
        "assets\\characters", "assets\\animals", "assets\\arms",
        "assets\\interior\\furniture", "assets\\city\\kaykit", "assets\\city\\nature",
        "assets\\city\\suburban", "assets\\city\\rail",
    };
    if (argc > 1) for (int i = 1; i < argc; i++) scan(argv[i]);
    else for (size_t i = 0; i < sizeof(DEFAULTS) / sizeof(DEFAULTS[0]); i++) scan(DEFAULTS[i]);

    printf("\narena used %.1f MB\n", (double)loader.arena.size / (double)MB);
    return 0;
}
