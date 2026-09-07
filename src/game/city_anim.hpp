#pragma once
#include <string.h>
#include "../core/animation.hpp"
#include "../loader/data_loader.hpp"

// Borrowing one rig's animation for another rig's skeleton.
//
// The cast is three unrelated skeletons wearing the same clothes. Six of the
// rigs share Quaternius' 62-bone "CharacterArmature" and ship 24 clips each -
// but no jump and nothing to sit on a car seat with. One rig ("Man") is a
// 31-bone "HumanArmature" and is the only one in the whole folder that ships
// a real Jump and a Sitting. A third is a Mixamo-style 41-bone rig whose
// clips are simply named differently.
//
// Without this file that means one character in the city can jump and the
// rest cannot, and the player is stuck being whichever rig happens to own the
// clip. With it, a clip authored for any skeleton can be replayed on any
// other, so the roles a rig is missing are filled in from the rest of the
// cast and every character can do everything.
//
// How it works, and why it is only a few dozen lines:
//
//   * bones are matched by *name*. These rigs were built by the same author
//     from the same body plan - Hips, Abdomen, Torso, Neck, Head, Shoulder.L,
//     UpperArm.L, LowerLeg.R and so on are spelled identically across the two
//     Quaternius skeletons even though one has 62 bones and the other 31, so
//     name matching covers the whole body and misses only fingers and the odd
//     helper bone. A target bone with no counterpart simply holds its own rest
//     pose, which is exactly right for a finger the donor clip never moved.
//
//   * only *rotation* is taken. Two skeletons have different bone lengths, so
//     replaying the donor's local translations would stretch the recipient's
//     limbs to the donor's proportions. Rotation is the pose; translation is
//     the build, and the recipient keeps its own.
//
//   * rotation is taken as a *delta from the donor's own bind pose*, then
//     applied on top of the recipient's bind pose:
//
//         local = bind_to * inverse(bind_from) * key
//
//     Copying the raw local rotation instead would only work if both rigs
//     rested in exactly the same pose, which they do not - one rests in a
//     T-pose and the other slightly A-posed, and the difference shows up as
//     arms sunk into the ribcage.
//
// The result is a plain animation_clip_file_data appended to the loader, so
// nothing downstream - the animator, the pose banks, the player's crossfade -
// needs to know a clip was retargeted rather than authored.

// How many borrowed clips one character can carry on top of its own.
#define CITY_MAX_BORROWED 8

static idx city_retarget_clip(pix_data_loader& loader, const animation_clip_file_data& src,
                              const skeleton_file_data& from, const skeleton_file_data& to);

// True when `from` can usefully donate to `to`: enough of the body lines up by
// name that the result is a pose rather than a scarecrow.
static bool city_skeletons_compatible(const skeleton_file_data& from,
                                      const skeleton_file_data& to);

// ---------------- implementation ----------------

// Bone names differ across exporters in ways that carry no meaning: a
// "mixamorig:" prefix, a "Bip01 " prefix, capitalisation. Compare on what is
// left after those are taken off.
static const char* city__bone_key(const char* name) {
    const char* colon = strrchr(name, ':');
    if (colon) name = colon + 1;
    if (strncmp(name, "Bip01 ", 6) == 0) name += 6;
    return name;
}

static bool city__bone_same(const char* a, const char* b) {
    a = city__bone_key(a);
    b = city__bone_key(b);
    for (;; a++, b++) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return false;
        if (!ca) return true;
    }
}

static int city__find_bone(const skeleton_file_data& sk, const char* name) {
    for (size_t i = 0; i < sk.bone_count; i++)
        if (city__bone_same(sk.bones[i].name, name)) return (int)i;
    return -1;
}

// The bones that have to line up for a retarget to be worth doing. Fingers,
// toes and pole targets are deliberately not in the list: a clip that moves
// only those is not a clip anybody will notice missing.
static const char* const CITY_CORE_BONES[] = {
    "Hips", "Torso", "Neck", "Head",
    "UpperArm.L", "LowerArm.L", "UpperArm.R", "LowerArm.R",
    "UpperLeg.L", "LowerLeg.L", "UpperLeg.R", "LowerLeg.R",
};
#define CITY_CORE_BONE_COUNT 12

static bool city_skeletons_compatible(const skeleton_file_data& from,
                                      const skeleton_file_data& to) {
    if (!from.bone_count || !to.bone_count) return false;
    int matched = 0;
    for (int i = 0; i < CITY_CORE_BONE_COUNT; i++)
        if (city__find_bone(from, CITY_CORE_BONES[i]) >= 0
            && city__find_bone(to, CITY_CORE_BONES[i]) >= 0) matched++;
    // Two thirds of the core body, which the two Quaternius skeletons clear
    // comfortably and an unrelated rig (Mixamo's LeftArm/Spine1 naming) fails
    // outright - exactly the line that should be drawn.
    return matched * 3 >= CITY_CORE_BONE_COUNT * 2;
}

static quat city__quat_conj(quat q) {
    quat r = { -q.x, -q.y, -q.z, q.w };
    return r;
}

static idx city_retarget_clip(pix_data_loader& loader, const animation_clip_file_data& src,
                              const skeleton_file_data& from, const skeleton_file_data& to) {
    if (loader.animation_clip_count >= MAX_ANIMATIONS) return (idx)-1;
    if (!to.bone_count || !from.bone_count) return (idx)-1;

    bone_animation_track* tracks = allocate<bone_animation_track>(loader.arena, to.bone_count);
    if (!tracks) return (idx)-1;

    int filled = 0;
    for (size_t i = 0; i < to.bone_count; i++) {
        tracks[i] = bone_animation_track();
        tracks[i].bone = (int32_t)i;

        int j = city__find_bone(from, to.bones[i].name);
        if (j < 0 || (size_t)j >= src.track_count) continue;
        const bone_animation_track& s = src.tracks[j];
        if (!s.keyframe_count) continue;

        bone_keyframe* keys = allocate<bone_keyframe>(loader.arena, s.keyframe_count);
        if (!keys) break;

        // bind_to * inverse(bind_from), the fixed part of the conversion
        quat rebind = quat_mul(to.bones[i].local_rotation,
                               city__quat_conj(quat_norm(from.bones[j].local_rotation)));
        for (size_t k = 0; k < s.keyframe_count; k++) {
            keys[k].time     = s.keyframes[k].time;
            keys[k].rotation = quat_norm(quat_mul(rebind, s.keyframes[k].rotation));
            // the recipient's own build, not the donor's
            keys[k].position = to.bones[i].local_position;
            keys[k].scale    = to.bones[i].local_scale;
        }
        tracks[i].keyframes = keys;
        tracks[i].keyframe_count = s.keyframe_count;
        filled++;
    }
    if (!filled) return (idx)-1;

    idx id = (idx)loader.animation_clip_count++;
    animation_clip_file_data& out = loader.animation_clips[id];
    out = animation_clip_file_data();
    snprintf(out.name, sizeof(out.name), "%s", src.name);
    out.duration = src.duration;
    out.skeleton = -1;              // retargeted: it belongs to no loaded skeleton table entry
    out.track_count = to.bone_count;
    out.tracks = tracks;
    return id;
}
