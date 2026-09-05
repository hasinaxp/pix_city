#pragma once
#include <string.h>
#include <stdio.h>

// instanced: per-vertex pos/normal/uv (loc 0..2), per-instance model matrix (loc 3..6)
const char* VSHDER_BASIC = R"(#version 440 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
layout(location=3) in mat4 aModel;   // consumes 3..6
uniform mat4 uViewProj;
out vec3 vNormal;
out vec2 vUV;
out vec3 vWorld;
void main() {
    vec4 world = aModel * vec4(aPos, 1.0);
    vWorld = world.xyz;
    gl_Position = uViewProj * world;
    vNormal = mat3(aModel) * aNormal;
    vUV = vec2(aUV.x, 1.0 - aUV.y); // OBJ/Blender V origin is bottom, GL texture V is top
}
)";

// Shared GLSL: an analytic daytime sky and the sRGB<->linear helpers. Prepended
// to every shader that needs to sample the environment (surface reflections,
// water reflections, the sky pass itself) so there is exactly one sky in the
// scene and reflections always agree with the backdrop.
//
// sky_env returns *linear* radiance for a view direction: a horizon-to-zenith
// gradient, a ground half below the horizon, and the sun as a sharp disc with
// two softer glow lobes around it. Everything downstream tonemaps and gamma
// encodes in the post pass, so nothing here clamps.
const char* GLSL_COMMON = R"(
vec3 srgb_to_linear(vec3 c) { return pow(max(c, 0.0), vec3(2.2)); }
vec3 linear_to_srgb(vec3 c) { return pow(max(c, 0.0), vec3(1.0 / 2.2)); }

vec3 sky_env(vec3 dir, vec3 sunDir, vec3 sunColor, vec3 zenith, vec3 horizon, vec3 ground) {
    float up = dir.y;
    vec3 sky;
    if (up >= 0.0) {
        float t = pow(1.0 - up, 5.0);                 // haze bunched near the horizon
        sky = mix(zenith, horizon, t);
    } else {
        sky = mix(horizon, ground, clamp(-up * 3.0, 0.0, 1.0));
    }
    float sd = max(dot(dir, normalize(sunDir)), 0.0);
    sky += sunColor * pow(sd, 1800.0) * 16.0;         // disc
    sky += sunColor * pow(sd, 28.0)   * 0.30;         // tight glow
    sky += sunColor * pow(sd, 5.0)    * 0.06;         // wide forward scatter
    return sky;
}
)";

// Splices GLSL_COMMON in immediately after a shader's #version line, so every
// fragment shader that samples the sky (surfaces, water, the sky pass) shares
// one definition. Returns a pointer into a static buffer - compile it before
// calling again.
static const char* shader_with_common(const char* src) {
    static char buf[16384];
    const char* nl = strchr(src, '\n');
    if (!nl) return src;
    size_t head = (size_t)(nl - src) + 1;
    memcpy(buf, src, head);
    int n = (int)head;
    n += sprintf(buf + n, "%s\n", GLSL_COMMON);
    sprintf(buf + n, "%s", src + head);
    return buf;
}

// ---- shared BRDF ----
//
// Split out of the surface shader so the skinned pass, the water and anything
// added later evaluate exactly the same material model. The pieces are the
// standard ones and are named after the papers they come from, because the
// point of a physically based renderer is that a surface behaves the way the
// published model says it does rather than the way it was tuned to.
const char* GLSL_BRDF = R"(
const float PI = 3.14159265359;

// GGX / Trowbridge-Reitz normal distribution (Walter et al. 2007)
float d_ggx(float ndh, float a) {
    float a2 = a * a;
    float d = ndh * ndh * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-8);
}
// height-correlated Smith visibility (Heitz 2014) - folds in the 1/(4 ndl ndv)
float v_smith(float ndv, float ndl, float a) {
    float a2 = a * a;
    float gv = ndl * sqrt(ndv * ndv * (1.0 - a2) + a2);
    float gl = ndv * sqrt(ndl * ndl * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-5);
}
vec3 f_schlick(float u, vec3 f0) {
    float m = clamp(1.0 - u, 0.0, 1.0);
    return f0 + (1.0 - f0) * (m * m * m * m * m);
}
// Karis' analytic environment BRDF fit (the "mobile" split-sum approximation),
// so the ambient specular carries the right Fresnel and roughness weight
// without a precomputed lookup table
vec3 env_brdf(vec3 f0, float rough, float ndv) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572,  0.022);
    const vec4 c1 = vec4( 1.0,  0.0425,  1.04,  -0.04);
    vec4 r = rough * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * ndv)) * r.x + r.y;
    vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
    return f0 * ab.x + ab.y;
}

// One evaluation of the full material against one light direction. `radiance`
// is what arrives at the surface; everything above it is geometry.
vec3 surface_brdf(vec3 n, vec3 v, vec3 l, vec3 diff_albedo, vec3 f0, float a, vec3 radiance) {
    vec3  h = normalize(l + v);
    float ndl = max(dot(n, l), 0.0);
    if (ndl <= 0.0) return vec3(0.0);
    float ndv = max(dot(n, v), 1e-4);
    float ndh = max(dot(n, h), 0.0);
    float vdh = max(dot(v, h), 0.0);

    vec3 F = f_schlick(vdh, f0);
    vec3 spec = d_ggx(ndh, a) * v_smith(ndv, ndl, a) * F;
    vec3 kd = vec3(1.0) - F;
    return (kd * diff_albedo / PI + spec) * radiance * ndl;
}

// The direction a rough surface's specular lobe actually points. A perfect
// mirror reflects along reflect(-v, n); as roughness climbs the lobe leans back
// toward the normal, and using the mirror direction anyway is what makes rough
// metal look like a warped mirror instead of like brushed metal.
// (Frostbite's getSpecularDominantDir, Lagarde & de Rousiers 2014.)
vec3 specular_dominant_dir(vec3 n, vec3 r, float rough) {
    float smoothness = clamp(1.0 - rough, 0.0, 1.0);
    float lerp_factor = smoothness * (sqrt(smoothness) + rough);
    return normalize(mix(n, r, lerp_factor));
}

// Windowed inverse-square falloff (Karis 2013). Physically an inverse square
// never reaches zero, which would mean every light in the world touches every
// pixel; the window pulls it to exactly zero at `radius` so a light can be
// culled at a known distance without a visible edge where it stops.
float light_falloff(float dist2, float radius) {
    float r2 = radius * radius;
    float f = clamp(1.0 - (dist2 * dist2) / max(r2 * r2, 1e-6), 0.0, 1.0);
    return (f * f) / max(dist2, 0.01);
}
)";

// ---- punctual lights ----
//
// One uniform block shared by the surface and skinned shaders. Lights arrive
// already reduced to the ones nearest the camera and packed into three vec4
// arrays, so binding a frame's lighting is three uploads and no per-light work
// in the draw loop.
const char* GLSL_LIGHTS = R"(
uniform int  uLightCount;
uniform vec4 uLightPosRadius[32];    // xyz position, w radius
uniform vec4 uLightColorInner[32];   // rgb linear radiance, w cos(inner cone)
uniform vec4 uLightDirOuter[32];     // xyz direction, w cos(outer cone), < -1 = point

vec3 punctual_lights(vec3 world, vec3 n, vec3 v, vec3 diff_albedo, vec3 f0, float a) {
    vec3 sum = vec3(0.0);
    for (int i = 0; i < uLightCount; ++i) {
        vec3  to = uLightPosRadius[i].xyz - world;
        float d2 = dot(to, to);
        float radius = uLightPosRadius[i].w;
        if (d2 > radius * radius) continue;

        vec3  l = to * inversesqrt(max(d2, 1e-8));
        float atten = light_falloff(d2, radius);

        float cos_outer = uLightDirOuter[i].w;
        if (cos_outer > -1.0) {                       // a cone, not a bulb
            float cd = dot(-l, uLightDirOuter[i].xyz);
            float t = clamp((cd - cos_outer)
                            / max(uLightColorInner[i].w - cos_outer, 1e-4), 0.0, 1.0);
            atten *= t * t;
        }
        if (atten <= 0.0) continue;

        sum += surface_brdf(n, v, l, diff_albedo, f0, a, uLightColorInner[i].rgb * atten);
    }
    return sum;
}
)";

// ---- cascaded shadow maps ----
//
// One directional light over a whole city cannot be served by one shadow map:
// a single 2048 map stretched over the view distance gives texels metres across,
// and stretched over the near field it stops casting anything past the end of
// the street. So the view is split by distance into SHADOW_CASCADES slices, each
// gets its own tightly fitted map, and a fragment picks the nearest slice it
// falls inside.
//
// The three maps live side by side in one texture so the lookup needs one
// sampler and one bind. Two details do most of the work in making the result
// look solid rather than crawling:
//
//   normal offset  the sample position is pushed along the surface normal by
//                  about a texel's worth of world space before it is projected.
//                  A depth-only bias has to grow with the slope until it
//                  detaches the shadow from whatever cast it; moving sideways
//                  along the surface instead does not.
//   cascade blend  the last stretch of each slice cross-fades into the next, so
//                  the resolution change is a soft band rather than a visible
//                  line drawn across the ground.
const char* GLSL_SHADOW = R"(
uniform mat4      uLightSpace[3];
uniform vec4      uCascadeFar;        // xyz: view distance each cascade ends at
uniform vec4      uCascadeTexel;      // xyz: one shadow texel in world metres
uniform sampler2DShadow uShadowMap;
uniform vec2      uShadowTexel;       // one texel of the whole atlas
uniform float     uShadowStrength;

float shadow_cascade(int c, vec3 world, vec3 n, float ndl) {
    // normal offset, scaled by how oblique the light is to this surface
    float slope = clamp(1.0 - ndl, 0.0, 1.0);
    vec3 p_world = world + n * (uCascadeTexel[c] * (1.0 + 2.0 * slope) * 1.4);

    vec4 lp = uLightSpace[c] * vec4(p_world, 1.0);
    vec3 p = lp.xyz / lp.w * 0.5 + 0.5;
    if (p.z > 1.0 || p.z < 0.0) return 1.0;

    // the cascade's own tile inside the atlas, inset so a filter tap can never
    // read across into its neighbour
    float inset = uShadowTexel.x * 2.0;
    p.x = clamp(p.x, inset, 1.0 - inset) / 3.0 + float(c) / 3.0;
    p.y = clamp(p.y, uShadowTexel.y * 2.0, 1.0 - uShadowTexel.y * 2.0);

    float bias = 0.0006;
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            sum += texture(uShadowMap, vec3(p.xy + vec2(float(x) * uShadowTexel.x,
                                                        float(y) * uShadowTexel.y),
                                            p.z - bias));
    return sum * (1.0 / 9.0);
}

float sun_shadow(vec3 world, vec3 n, float ndl, float view_depth) {
    if (uShadowStrength <= 0.0) return 1.0;

    int c = 2;
    if (view_depth < uCascadeFar.x)      c = 0;
    else if (view_depth < uCascadeFar.y) c = 1;

    float s = shadow_cascade(c, world, n, ndl);

    // cross-fade the last tenth of a cascade into the next one
    if (c < 2) {
        float far_edge = (c == 0) ? uCascadeFar.x : uCascadeFar.y;
        float band = far_edge * 0.12;
        float t = clamp((view_depth - (far_edge - band)) / band, 0.0, 1.0);
        if (t > 0.0) s = mix(s, shadow_cascade(c + 1, world, n, ndl), t);
    }
    return mix(1.0, s, uShadowStrength);
}
)";

// Splices the sky, the BRDF, the light loop and the shadow lookup into a
// shader in that order - each depends on the one before it.
static const char* shader_with_pbr(const char* src) {
    static char buf[3][32768];
    static int  slot = 0;
    char* out = buf[slot];
    slot = (slot + 1) % 3;

    const char* nl = strchr(src, '\n');
    if (!nl) return src;
    size_t head = (size_t)(nl - src) + 1;
    memcpy(out, src, head);
    int n = (int)head;
    n += sprintf(out + n, "%s\n%s\n%s\n%s\n", GLSL_COMMON, GLSL_BRDF, GLSL_LIGHTS, GLSL_SHADOW);
    sprintf(out + n, "%s", src + head);
    return out;
}

// The surface shader every solid thing in the world is drawn with. Metallic /
// roughness PBR: a Cook-Torrance specular lobe over a Lambertian diffuse, lit by
//
//   * one shadowed directional sun, through the cascades above
//   * an image based ambient that samples the same analytic sky the backdrop
//     uses, along the roughness-corrected dominant reflection direction - so a
//     car roof mirrors the real sky gradient and a brick wall picks up only its
//     broad tint
//   * every punctual light near enough to matter, through the same BRDF
//
// Albedo and tint arrive in sRGB and are linearised on read; all lighting is
// done in linear space and the post pass owns the tonemap and the single gamma
// encode.
const char* FSHDER_BASIC = R"(#version 440 core
in vec3 vNormal;
in vec2 vUV;
in vec3 vWorld;
out vec4 frag;

uniform sampler2D uTex;
uniform vec3  uColor;
uniform float uMetallic;
uniform float uRoughness;
uniform vec3  uEmissive;      // linear radiance added straight to the result

uniform vec3  uSunDir;        // normalised, pointing toward the sun
uniform vec3  uSunColor;      // linear radiance
uniform vec3  uSkyColor;      // hemisphere diffuse irradiance, from above
uniform vec3  uGroundColor;   // hemisphere diffuse irradiance, bounced from below
uniform vec3  uSkyZenith;     // sky-dome colour straight up   (reflections)
uniform vec3  uSkyHorizon;    // sky-dome colour at the horizon (reflections)

uniform vec3  uCameraPos;
uniform vec3  uFogColor;
uniform float uFogDensity;

void main() {
    vec3 n = normalize(vNormal);
    vec3 v = normalize(uCameraPos - vWorld);
    float ndv = max(dot(n, v), 1e-4);

    vec3  albedo = srgb_to_linear(texture(uTex, vUV).rgb * uColor);
    float rough  = clamp(uRoughness, 0.045, 1.0);
    float metal  = clamp(uMetallic, 0.0, 1.0);
    float a      = rough * rough;

    vec3 f0 = mix(vec3(0.04), albedo, metal);
    vec3 diff_albedo = albedo * (1.0 - metal);

    // ---- direct sun ----
    vec3  l = normalize(uSunDir);
    float ndl = max(dot(n, l), 0.0);
    float view_depth = length(uCameraPos - vWorld);
    float shadow = sun_shadow(vWorld, n, ndl, view_depth);
    vec3  direct = surface_brdf(n, v, l, diff_albedo, f0, a, uSunColor) * shadow;

    // ---- punctual lights ----
    direct += punctual_lights(vWorld, n, v, diff_albedo, f0, a);

    // ---- ambient, off the analytic sky ----
    float hemi = n.y * 0.5 + 0.5;
    vec3  amb_diffuse = mix(uGroundColor, uSkyColor, hemi) * diff_albedo;

    vec3 refl = specular_dominant_dir(n, reflect(-v, n), rough);
    vec3 mirror = sky_env(refl, uSunDir, uSunColor, uSkyZenith, uSkyHorizon, uGroundColor);
    vec3 flat_dome = mix(uSkyHorizon, uSkyZenith, clamp(refl.y * 0.5 + 0.5, 0.0, 1.0));
    // the mirror image survives longer on a smooth surface than a straight lerp
    // on roughness would allow, which is what keeps car paint glossy
    vec3 env = mix(mirror, flat_dome, rough * rough);

    // a reflection that has bent below the surface it came off cannot be seen
    // (Frostbite's horizon occlusion)
    float horizon = clamp(1.0 + dot(reflect(-v, n), n), 0.0, 1.0);
    vec3 amb_spec = env * env_brdf(f0, rough, ndv) * (horizon * horizon);

    // down-facing crevices see less of the sky
    float ao = mix(0.45, 1.0, hemi);

    vec3 color = direct + (amb_diffuse + amb_spec) * ao + uEmissive;

    float fog = 1.0 - exp(-view_depth * view_depth * uFogDensity * uFogDensity);
    frag = vec4(mix(color, srgb_to_linear(uFogColor), clamp(fog, 0.0, 1.0)), 1.0);
}
)";

// ---- sky pass ----
//
// A full-screen triangle drawn before the world, at the far plane, with depth
// writes off. The fragment shader turns each pixel back into a world-space view
// ray through the inverse view-projection and asks sky_env what colour that
// direction is - the same function the surface and water shaders sample for
// reflections, so the backdrop and every reflection of it are consistent.
const char* VSHDER_SKY = R"(#version 440 core
out vec2 vNDC;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
    vNDC = p;
    gl_Position = vec4(p, 1.0, 1.0);   // z = w -> sits on the far plane
}
)";

const char* FSHDER_SKY = R"(#version 440 core
in vec2 vNDC;
out vec4 frag;
uniform mat4  uInvViewProj;
uniform vec3  uCameraPos;
uniform vec3  uSunDir;
uniform vec3  uSunColor;
uniform vec3  uSkyZenith;
uniform vec3  uSkyHorizon;
uniform vec3  uGroundColor;
uniform vec3  uFogColor;
void main() {
    vec4 far = uInvViewProj * vec4(vNDC, 1.0, 1.0);
    vec3 dir = normalize(far.xyz / far.w - uCameraPos);
    vec3 col = sky_env(dir, uSunDir, uSunColor, uSkyZenith, uSkyHorizon, uGroundColor);
    // let the lower sky settle into the same fog the world fades into, so the
    // horizon line does not read as a hard seam
    float haze = smoothstep(0.10, -0.05, dir.y);
    col = mix(col, srgb_to_linear(uFogColor), haze * 0.85);
    frag = vec4(col, 1.0);
}
)";

// skinned: per-vertex pos/normal/uv (loc 0..2) + bone ids/weights (loc 3..4).
// One draw per animated instance, since every instance needs its own uBones,
// so the model matrix rides along as a uniform rather than an instance buffer.
// Pairs with FSHDER_BASIC. uBones[] must match MAX_ANIM_BONES in animation.hpp.
// Note glTF's uv origin is already top-left, so unlike VSHDER_BASIC there is no V flip.
const char* VSHDER_SKINNED = R"(#version 440 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
layout(location=3) in vec4 aBoneIds;
layout(location=4) in vec4 aWeights;
uniform mat4 uViewProj;
uniform mat4 uModel;
uniform mat4 uBones[128];
out vec3 vNormal;
out vec2 vUV;
out vec3 vWorld;
void main() {
    mat4 skin = uBones[int(aBoneIds.x)] * aWeights.x
              + uBones[int(aBoneIds.y)] * aWeights.y
              + uBones[int(aBoneIds.z)] * aWeights.z
              + uBones[int(aBoneIds.w)] * aWeights.w;
    vec4 world = uModel * (skin * vec4(aPos, 1.0));
    vWorld = world.xyz;
    gl_Position = uViewProj * world;
    vNormal = mat3(uModel) * (mat3(skin) * aNormal);
    vUV = aUV;
}
)";

// ---- depth only, for the shadow pass ----
//
// The shadow map records how far the sun can see and nothing else, so these
// carry no varyings and write no colour. They share the instancing and skinning
// layouts of the shaders above so the same vertex arrays draw both passes.

const char* VSHDER_DEPTH = R"(#version 440 core
layout(location=0) in vec3 aPos;
layout(location=3) in mat4 aModel;
uniform mat4 uViewProj;   // the light's view-projection
void main() { gl_Position = uViewProj * (aModel * vec4(aPos, 1.0)); }
)";

const char* VSHDER_DEPTH_SKINNED = R"(#version 440 core
layout(location=0) in vec3 aPos;
layout(location=3) in vec4 aBoneIds;
layout(location=4) in vec4 aWeights;
uniform mat4 uViewProj;
uniform mat4 uModel;
uniform mat4 uBones[128];
void main() {
    mat4 skin = uBones[int(aBoneIds.x)] * aWeights.x
              + uBones[int(aBoneIds.y)] * aWeights.y
              + uBones[int(aBoneIds.z)] * aWeights.z
              + uBones[int(aBoneIds.w)] * aWeights.w;
    gl_Position = uViewProj * (uModel * (skin * vec4(aPos, 1.0)));
}
)";

const char* FSHDER_DEPTH = R"(#version 440 core
void main() { }
)";

// ---- water ----
//
// The wave field is one function, GLSL_WAVE, shared by the vertex and fragment
// stages: the vertex stage uses its height to actually move the surface, the
// fragment stage uses its gradient for a normal at full pixel resolution. It is
// evaluated from world position, so neighbouring tiles agree exactly along
// their shared edge and the river is one continuous sheet rather than a grid of
// independently rippling squares.
//
// Everything that makes water read as water is here:
//
//   * the surface moves. A flat plane with a clever shader on it is a painted
//     floor; a surface whose silhouette rises and falls against the far bank is
//     immediately liquid. This is the single biggest cue and it needs real
//     geometry, which is why the tile is subdivided.
//   * four octaves - two slow swells crossing, two fast chops - because one
//     sine reads as corrugated metal
//   * Fresnel drives the mix from body colour to sky reflection: near
//     transparent underfoot, a mirror toward the horizon
//   * the body colour is a green-brown that deepens with distance below the
//     surface, so shallows near a bank read differently from midstream
//   * a tight specular for sun glitter over a broad one for sheen
const char* GLSL_WAVE = R"(
// .xyz is the surface normal, .w the height offset in metres.
//
// Six directional waves over three scales. The scales matter as much as the
// count: the swell moves the silhouette, the chop is what the eye reads as
// "liquid" from a few metres away, and the ripple is fine enough to keep
// breaking up the sky reflection out to the far bank. Drop the ripple and a
// river seen at a glancing angle - which is nearly always, from a camera
// standing on the bank - turns into one unbroken mirror of a smooth sky
// gradient, and a mirror that clean reads as sheet metal, not water.
vec4 wave_field(vec2 p, float time) {
    // direction, spatial frequency, amplitude (m), speed
    const vec2  D[6] = vec2[6](vec2( 0.80,  0.60), vec2(-0.45,  0.89),
                               vec2( 0.99, -0.14), vec2( 0.20,  0.98),
                               vec2( 0.71, -0.71), vec2(-0.92, -0.39));
    const float F[6] = float[6](0.085, 0.130, 0.480, 0.730, 1.850, 2.410);
    const float A[6] = float[6](0.300, 0.190, 0.070, 0.045, 0.016, 0.011);
    const float S[6] = float[6](0.550, 0.410, 1.700, 2.300, 3.400, 4.100);

    float h = 0.0, dx = 0.0, dz = 0.0;
    for (int i = 0; i < 6; ++i) {
        float phase = dot(p, D[i]) * F[i] + time * S[i];
        h  += A[i] * sin(phase);
        float c = cos(phase) * A[i] * F[i];
        dx += c * D[i].x;
        dz += c * D[i].y;
    }

    // The gradient of a field this gentle is nearly flat, so the normal is
    // exaggerated - the ripple has to be visible in the lighting, not merely
    // present in the geometry.
    return vec4(normalize(vec3(-dx * 14.0, 1.0, -dz * 14.0)), h);
}
)";

// Splices both GLSL_COMMON and GLSL_WAVE in after the #version line.
//
// Unlike shader_with_common this alternates between two buffers, because a
// vertex and a fragment source have to be live at the same moment to be handed
// to one opengl_create_shader call - and the water pass is the one place where
// both stages need the same helper spliced in.
static const char* shader_with_waves(const char* src) {
    static char buf[2][16384];
    static int  slot = 0;
    char* out = buf[slot];
    slot ^= 1;

    const char* nl = strchr(src, '\n');
    if (!nl) return src;
    size_t head = (size_t)(nl - src) + 1;
    memcpy(out, src, head);
    int n = (int)head;
    n += sprintf(out + n, "%s\n%s\n", GLSL_COMMON, GLSL_WAVE);
    sprintf(out + n, "%s", src + head);
    return out;
}

const char* VSHDER_WATER = R"(#version 440 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
layout(location=3) in mat4 aModel;
uniform mat4  uViewProj;
uniform float uTime;
out vec3 vWorld;
void main() {
    vec4 world = aModel * vec4(aPos, 1.0);
    // Displace in world space, not model space: the tile is scaled by its cell
    // size, and lifting before that scale would make the waves taller on a
    // bigger tile.
    world.y += wave_field(world.xz, uTime).w;
    vWorld = world.xyz;
    gl_Position = uViewProj * world;
}
)";

const char* FSHDER_WATER = R"(#version 440 core
in vec3 vWorld;
out vec4 frag;
uniform vec3  uCameraPos;
uniform vec3  uSunDir;
uniform vec3  uSunColor;
uniform vec3  uShallowColor;
uniform vec3  uDeepColor;
uniform vec3  uSkyZenith;
uniform vec3  uSkyHorizon;
uniform vec3  uGroundColor;
uniform vec3  uFogColor;
uniform float uFogDensity;
uniform float uTime;
uniform float uBedDepth;      // metres from the surface down to the bed

void main() {
    vec4 wave = wave_field(vWorld.xz, uTime);
    vec3 n = wave.xyz;
    vec3 view = normalize(uCameraPos - vWorld);

    // Schlick. The 0.02 base is water's real normal-incidence reflectance; the
    // swing to ~1 at grazing angles is what sells it as a liquid surface.
    float fresnel = 0.02 + 0.98 * pow(1.0 - clamp(dot(n, view), 0.0, 1.0), 5.0);

    // How far the eye is looking through the water: straight down it is the
    // depth to the bed, at a glancing angle it is far further, and that is what
    // makes the far side of a river read darker than the near edge. Wave height
    // rides on top of it, so a crest is lighter than the trough beside it.
    float through = uBedDepth / max(abs(view.y), 0.06);
    float murk = 1.0 - exp(-through * 0.42);
    float crest = clamp(wave.w * 1.3 + 0.5, 0.0, 1.0);
    vec3 body = mix(srgb_to_linear(uShallowColor), srgb_to_linear(uDeepColor),
                    clamp(murk - crest * 0.25, 0.0, 1.0));

    // The reflection is a real mirror sample of the same analytic sky the
    // backdrop and every other surface use, taken along the wave-perturbed
    // reflection vector - so the water reflects the actual horizon gradient and
    // catches the sun where the geometry says it should.
    //
    // Scaled down on the way in, because a surface that returns the sky at full
    // strength is a mirror, and water is not: about four fifths of it comes
    // back and the rest goes into the body of the water.
    vec3 refl = reflect(-view, n);
    refl.y = abs(refl.y);   // waves that tilt the normal below horizontal still see sky
    vec3 sky = sky_env(refl, uSunDir, uSunColor, uSkyZenith, uSkyHorizon, uGroundColor) * 0.78;

    vec3 half_v = normalize(uSunDir + view);
    // Two lobes, both kept modest. A very tight, very bright highlight on a
    // surface whose waves are smaller than a pixel at any distance does not
    // sparkle, it smears into a chrome band across the whole river.
    float ndh = max(dot(n, half_v), 0.0);
    float spec = pow(ndh, 200.0) * 1.6;      // sun glitter
    spec += pow(ndh, 18.0) * 0.06;           // broad sheen

    // Never a pure reflection even edge on. Some of what comes back off water at
    // a glancing angle is still the water, and holding a little of the body
    // colour in is what keeps a river reading as a river rather than as a strip
    // of polished metal laid in the ground.
    vec3 color = mix(body, sky, fresnel * 0.88) + uSunColor * spec;

    // atmospheric fog only at real distance - the near-field colour above is
    // left alone so nearby water still reads as water, not haze
    float dist = length(uCameraPos - vWorld);
    float fog = 1.0 - exp(-dist * dist * uFogDensity * uFogDensity);
    color = mix(color, srgb_to_linear(uFogColor), clamp(fog, 0.0, 1.0) * 0.85);

    // Opacity tracks the same view-through-the-water term as the colour: you
    // can see the bed at your feet and cannot see it across the river.
    frag = vec4(color, clamp(mix(0.55, 0.98, max(murk, fresnel)), 0.0, 1.0));
}
)";

// ---- post processing ----
//
// One triangle larger than the screen, built from gl_VertexID with no vertex
// buffer bound at all. A quad made of two triangles would shade the pixels
// along its shared diagonal twice and can show a seam; this cannot.
const char* VSHDER_POST = R"(#version 440 core
out vec2 vUV;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
    vUV = p * 0.5 + 0.5;
    gl_Position = vec4(p, 0.0, 1.0);
}
)";

// ---- bloom ----
//
// The scene target is real HDR, so a sun glint off a car roof or a wave crest
// is genuinely brighter than 1 while the tonemap squashes it back to near
// white. Bloom is what puts that energy back on screen as glare, and it is the
// single thing that makes a specular highlight read as *bright* rather than as
// a pale patch. Threshold with a soft knee (a hard cutoff bands visibly as
// something crosses it), then two separable Gaussian passes at quarter res.
const char* FSHDER_BLOOM_PREFILTER = R"(#version 440 core
in vec2 vUV;
out vec4 frag;
uniform sampler2D uScene;
uniform vec2  uTexel;      // one source texel
uniform float uThreshold;
uniform float uKnee;
void main() {
    // 4-tap box downsample first: a one-pixel spark left to alias would crawl
    vec3 c = texture(uScene, vUV + vec2(-uTexel.x, -uTexel.y)).rgb
           + texture(uScene, vUV + vec2( uTexel.x, -uTexel.y)).rgb
           + texture(uScene, vUV + vec2(-uTexel.x,  uTexel.y)).rgb
           + texture(uScene, vUV + vec2( uTexel.x,  uTexel.y)).rgb;
    c *= 0.25;

    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float soft = clamp(lum - uThreshold + uKnee, 0.0, 2.0 * uKnee);
    soft = soft * soft / (4.0 * uKnee + 1e-5);
    float w = max(soft, lum - uThreshold) / max(lum, 1e-5);
    frag = vec4(c * w, 1.0);
}
)";

// five taps standing in for a nine-tap Gaussian: the hardware's linear filter
// fetches each outer pair in one sample at a weighted offset
const char* FSHDER_BLOOM_BLUR = R"(#version 440 core
in vec2 vUV;
out vec4 frag;
uniform sampler2D uSource;
uniform vec2 uDirection;   // one texel along the axis being blurred
void main() {
    vec3 c = texture(uSource, vUV).rgb * 0.2270270270;
    c += (texture(uSource, vUV + uDirection * 1.3846153846).rgb
        + texture(uSource, vUV - uDirection * 1.3846153846).rgb) * 0.3162162162;
    c += (texture(uSource, vUV + uDirection * 3.2307692308).rgb
        + texture(uSource, vUV - uDirection * 3.2307692308).rgb) * 0.0702702703;
    frag = vec4(c, 1.0);
}
)";

const char* FSHDER_POST = R"(#version 440 core
in vec2 vUV;
out vec4 frag;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform vec2  uTexel;
uniform float uExposure;
uniform float uSaturation;
uniform float uVignette;
uniform float uSharpen;
uniform float uBloomStrength;

// Narkowicz's fit of the ACES filmic curve: cheap, and it rolls highlights off
// instead of clipping them flat white the way a plain clamp does
vec3 tonemap(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 color = texture(uScene, vUV).rgb;

    // a light unsharp mask puts the edges back that the resolve softened
    if (uSharpen > 0.0) {
        vec3 blur = texture(uScene, vUV + vec2( uTexel.x, 0.0)).rgb
                  + texture(uScene, vUV + vec2(-uTexel.x, 0.0)).rgb
                  + texture(uScene, vUV + vec2(0.0,  uTexel.y)).rgb
                  + texture(uScene, vUV + vec2(0.0, -uTexel.y)).rgb;
        color += (color - blur * 0.25) * uSharpen;
    }

    // glare is added before the tonemap, in linear light, so it rolls off with
    // everything else instead of clipping on top of an already-mapped image
    if (uBloomStrength > 0.0) color += texture(uBloom, vUV).rgb * uBloomStrength;

    // the scene target holds linear HDR radiance; tonemap in linear, then this
    // is the one place the whole pipeline encodes back to sRGB for the 8-bit
    // backbuffer
    color = tonemap(color * uExposure);

    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luma), color, uSaturation);

    vec2 d = vUV - 0.5;
    color *= 1.0 - uVignette * dot(d, d);

    frag = vec4(linear_to_srgb(color), 1.0);
}
)";

// instanced: unit quad corner (loc 0), per-instance box + atlas crop + texture
// index (loc 1..3). aTexIndex selects both the sampler and its uTexSize entry,
// so one batch draws in a single call across up to MAX_SPRITE_TEXTURES(32)
// distinct textures - keep the array sizes below in sync with sprite.hpp.
const char* VSHDER_SPRITE = R"(#version 440 core
layout(location=0) in vec2 aCorner;   // unit quad, 0..1
layout(location=1) in vec4 aBox;      // x, y, w, h
layout(location=2) in vec4 aCrop;     // atlas pixel rect x, y, w, h
layout(location=3) in uint aTexIndex; // index into uTex/uTexSize
uniform mat4 uViewProj;
uniform vec2 uTexSize[32];
out vec2 vUV;
flat out uint vTexIndex;
void main() {
    vec2 pos = aBox.xy + aCorner * aBox.zw;
    gl_Position = uViewProj * vec4(pos, 0.0, 1.0);
    vUV = (aCrop.xy + aCorner * aCrop.zw) / uTexSize[aTexIndex];
    vTexIndex = aTexIndex;
}
)";

const char* FSHDER_SPRITE = R"(#version 440 core
in vec2 vUV;
flat in uint vTexIndex;
out vec4 frag;
uniform sampler2D uTex[32];
void main() {
    frag = texture(uTex[vTexIndex], vUV);
}
)";


// uItalic shears the quad in place (no extra per-instance data): the top
// edge of each glyph's own box shifts right, the bottom edge doesn't move -
// a cheap approximation of a true glyph-shape slant, good enough for UI text
const char* VSHDER_TEXT = R"(#version 440 core
layout(location=0) in vec2 aCorner;
layout(location=1) in vec4 aBox;
layout(location=2) in vec4 aCrop;
layout(location=3) in uint aTexIndex;
uniform mat4 uViewProj;
uniform vec2 uTexSize[32];
uniform float uItalic;
out vec2 vUV;
flat out uint vTexIndex;
void main() {
    vec2 local = aCorner * aBox.zw;
    local.x += uItalic * (aBox.w - local.y);
    gl_Position = uViewProj * vec4(aBox.xy + local, 0.0, 1.0);
    vUV = (aCrop.xy + aCorner * aCrop.zw) / uTexSize[aTexIndex];
    vTexIndex = aTexIndex;
}
)";

// atlas holds a signed distance field (0.5 = outline); fwidth() adapts the
// smoothstep band to the glyph's current on-screen scale, so edges stay crisp
// whether the text is shrunk or magnified. uWeight biases the fill edge to
// synthesize bold(+)/thin(-); uOutlineWidth carves a second band below the
// fill edge, in the same normalized sdf units, filled with uOutlineColor.
const char* FSHDER_TEXT = R"(#version 440 core
in vec2 vUV;
flat in uint vTexIndex;
out vec4 frag;
uniform sampler2D uTex[32];
uniform vec4 uColor;
uniform vec4 uOutlineColor;
uniform float uWeight;
uniform float uOutlineWidth;
void main() {
    float sdf = texture(uTex[vTexIndex], vUV).r;
    float w = max(fwidth(sdf), 1e-4);
    float fillEdge = 0.5 - uWeight;
    float outlineEdge = fillEdge - uOutlineWidth;
    float fillAlpha = smoothstep(fillEdge - w, fillEdge + w, sdf);
    float outlineAlpha = smoothstep(outlineEdge - w, outlineEdge + w, sdf);
    // fillAlpha/outlineAlpha is 0 in the ring-only band and ramps to 1 exactly
    // at the fill edge; when outlineWidth is 0 the two edges coincide, the
    // ratio collapses to 1 everywhere, and uOutlineColor drops out entirely -
    // a plain mix(outline, color, fillAlpha) would instead tint every glyph's
    // antialiased rim toward outline colour even with no outline requested
    float t = outlineAlpha > 1e-4 ? clamp(fillAlpha / outlineAlpha, 0.0, 1.0) : 0.0;
    vec3 rgb = mix(uOutlineColor.rgb, uColor.rgb, t);
    float alpha = outlineAlpha * mix(uOutlineColor.a, uColor.a, t);
    frag = vec4(rgb, alpha);
}
)";