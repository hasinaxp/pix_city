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

// The surface shader every solid thing in the world is drawn with. Metallic /
// roughness PBR: a Cook-Torrance specular lobe (GGX distribution, height
// correlated Smith visibility, Schlick Fresnel) over a Lambertian diffuse, lit
// by one shadowed directional sun plus an image based ambient that samples the
// same analytic sky the backdrop uses - so every glossy surface reflects the
// real sky gradient and picks up a sun glint at the right angle.
//
// Albedo and tint arrive in sRGB and are linearised on read; all lighting is
// done in linear space and the post pass does the tonemap and gamma encode.
//
// The shadow lookup is a sampler2DShadow, so the hardware does the depth
// compare and its linear filter blends four taps for free; the 3x3 loop on top
// of that gives a soft edge rather than a staircase. Bias is slope scaled -
// a surface nearly edge-on to the sun needs far more of it than one facing it,
// and a single constant either acnes the flat ground or detaches every shadow.
const char* FSHDER_BASIC = R"(#version 440 core
in vec3 vNormal;
in vec2 vUV;
in vec3 vWorld;
out vec4 frag;

uniform sampler2D uTex;
uniform vec3  uColor;
uniform float uMetallic;
uniform float uRoughness;

uniform vec3  uSunDir;        // normalised, pointing toward the sun
uniform vec3  uSunColor;      // linear radiance
uniform vec3  uSkyColor;      // hemisphere diffuse irradiance, from above
uniform vec3  uGroundColor;   // hemisphere diffuse irradiance, bounced from below
uniform vec3  uSkyZenith;     // sky-dome colour straight up   (reflections)
uniform vec3  uSkyHorizon;    // sky-dome colour at the horizon (reflections)

uniform mat4      uLightSpace;
uniform sampler2DShadow uShadowMap;
uniform float     uShadowTexel;
uniform float     uShadowStrength;

uniform vec3  uCameraPos;
uniform vec3  uFogColor;
uniform float uFogDensity;

const float PI = 3.14159265359;

float sun_shadow(vec3 world, float ndl) {
    vec4 light_pos = uLightSpace * vec4(world, 1.0);
    vec3 p = light_pos.xyz / light_pos.w * 0.5 + 0.5;
    if (p.z > 1.0 || p.z < 0.0) return 1.0;          // beyond the light's range

    float bias = max(0.0035 * (1.0 - ndl), 0.0008);
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            sum += texture(uShadowMap, vec3(p.xy + vec2(x, y) * uShadowTexel, p.z - bias));
    return sum * (1.0 / 9.0);
}

// GGX / Trowbridge-Reitz normal distribution
float d_ggx(float ndh, float a) {
    float a2 = a * a;
    float d = ndh * ndh * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-8);
}
// height-correlated Smith visibility - already folds in the 1/(4 ndl ndv)
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
// so the ambient specular carries the right Fresnel + roughness weight without
// a precomputed LUT
vec3 env_brdf(vec3 f0, float rough, float ndv) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572,  0.022);
    const vec4 c1 = vec4( 1.0,  0.0425,  1.04,  -0.04);
    vec4 r = rough * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * ndv)) * r.x + r.y;
    vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
    return f0 * ab.x + ab.y;
}

void main() {
    vec3 n = normalize(vNormal);
    vec3 v = normalize(uCameraPos - vWorld);
    float ndv = max(dot(n, v), 1e-4);

    vec3 albedo = srgb_to_linear(texture(uTex, vUV).rgb * uColor);
    float rough = clamp(uRoughness, 0.045, 1.0);
    float metal = clamp(uMetallic, 0.0, 1.0);
    float a = rough * rough;

    vec3 f0 = mix(vec3(0.04), albedo, metal);
    vec3 diff_albedo = albedo * (1.0 - metal);

    // ---- direct sun ----
    vec3 l = normalize(uSunDir);
    vec3 h = normalize(l + v);
    float ndl = max(dot(n, l), 0.0);
    float ndh = max(dot(n, h), 0.0);
    float vdh = max(dot(v, h), 0.0);

    float shadow = mix(1.0, sun_shadow(vWorld, ndl), uShadowStrength);
    vec3  F = f_schlick(vdh, f0);
    vec3  spec = d_ggx(ndh, a) * v_smith(ndv, ndl, a) * F;
    vec3  kd = vec3(1.0) - F;
    vec3  direct = (kd * diff_albedo / PI + spec) * uSunColor * (ndl * shadow);

    // ---- ambient / image-based lighting off the analytic sky ----
    float hemi = n.y * 0.5 + 0.5;
    vec3  irradiance = mix(uGroundColor, uSkyColor, hemi);
    vec3  amb_diffuse = irradiance * diff_albedo;

    // one mirror sample of the sky along the reflection vector is the whole
    // reflection: sharp for a polished car, blurred toward the flat sky tint as
    // roughness climbs (no prefiltered mips here, so fake the blur by lerping)
    vec3  refl = reflect(-v, n);
    vec3  mirror = sky_env(refl, uSunDir, uSunColor, uSkyZenith, uSkyHorizon, uGroundColor);
    vec3  rough_sky = mix(uSkyHorizon, uSkyZenith, clamp(refl.y * 0.5 + 0.5, 0.0, 1.0));
    vec3  env = mix(mirror, rough_sky, rough);
    vec3  amb_spec = env * env_brdf(f0, rough, ndv);

    // down-facing crevices see less of the sky
    float ao = mix(0.45, 1.0, hemi);

    vec3 color = direct + (amb_diffuse + amb_spec) * ao;

    float dist = length(uCameraPos - vWorld);
    float fog = 1.0 - exp(-dist * dist * uFogDensity * uFogDensity);
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
// A water body is one flat quad per cell, so it has four vertices and nothing
// to displace. Everything that makes it read as water therefore has to happen
// per *pixel*, not per vertex:
//
//   * the wave field is evaluated in the fragment shader from the world
//     position, and its analytic derivative gives a normal that ripples at full
//     resolution however coarse the geometry is
//   * two octaves - a slow swell and a fast chop - because a single sine reads
//     as corrugated metal
//   * Fresnel drives the mix from body colour to sky reflection, which is the
//     single strongest cue: water is near transparent underfoot and a mirror
//     toward the horizon
//   * a wide, bright specular lobe gives the sun glitter
const char* VSHDER_WATER = R"(#version 440 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
layout(location=3) in mat4 aModel;
uniform mat4 uViewProj;
out vec3 vWorld;
void main() {
    vec4 world = aModel * vec4(aPos, 1.0);
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

// height of the wave field at p, packed into .w, and its slope-derived normal
// in .xyz - one evaluation gets both, since the normal is just the height
// field's own gradient.
vec4 wave_field(vec2 p) {
    float h = 0.0, dx = 0.0, dz = 0.0;

    // swell: long, slow, two crossing directions
    vec2  d0 = vec2( 0.80,  0.60); float f0 = 0.085, a0 = 0.42, s0 = 0.55;
    vec2  d1 = vec2(-0.45,  0.89); float f1 = 0.130, a1 = 0.30, s1 = 0.41;
    // chop: short and quick, this is what actually sparkles
    vec2  d2 = vec2( 0.99, -0.14); float f2 = 0.480, a2 = 0.055, s2 = 1.70;
    vec2  d3 = vec2( 0.20,  0.98); float f3 = 0.730, a3 = 0.035, s3 = 2.30;

    float p0 = dot(p, d0) * f0 + uTime * s0;
    float p1 = dot(p, d1) * f1 + uTime * s1;
    float p2 = dot(p, d2) * f2 + uTime * s2;
    float p3 = dot(p, d3) * f3 + uTime * s3;

    h = a0 * sin(p0) + a1 * sin(p1) + a2 * sin(p2) + a3 * sin(p3);
    dx = cos(p0) * a0 * f0 * d0.x + cos(p1) * a1 * f1 * d1.x
       + cos(p2) * a2 * f2 * d2.x + cos(p3) * a3 * f3 * d3.x;
    dz = cos(p0) * a0 * f0 * d0.y + cos(p1) * a1 * f1 * d1.y
       + cos(p2) * a2 * f2 * d2.y + cos(p3) * a3 * f3 * d3.y;

    vec3 n = normalize(vec3(-dx * 6.0, 1.0, -dz * 6.0));
    return vec4(n, h);
}

void main() {
    vec4 wave = wave_field(vWorld.xz);
    vec3 n = wave.xyz;
    vec3 view = normalize(uCameraPos - vWorld);

    // Schlick. The 0.02 base is water's real normal-incidence reflectance; the
    // swing to ~1 at grazing angles is what sells it as a liquid surface.
    float fresnel = 0.02 + 0.98 * pow(1.0 - clamp(dot(n, view), 0.0, 1.0), 5.0);

    // Body colour tracks the wave crest, not the viewing angle: a crest
    // catching the light reads lighter, a trough sits darker, and because that
    // tracks uTime the whole surface visibly churns instead of holding one
    // flat tint - the single biggest thing a static screenshot cannot sell but
    // motion does. Mixing on view angle instead (the previous approach) made
    // the body colour swap for the reflection colour at exactly the angles a
    // third-person camera actually uses, so the water read as a flat grey slab.
    float shimmer = clamp(wave.w / 0.35 * 0.5 + 0.5, 0.0, 1.0);
    vec3 body = mix(srgb_to_linear(uDeepColor), srgb_to_linear(uShallowColor), shimmer);

    // The reflection is now a real mirror sample of the same analytic sky the
    // backdrop and every other surface use, taken along the wave-perturbed
    // reflection vector - so the water reflects the actual horizon gradient and
    // catches the sun where the geometry says it should, instead of a flat
    // hand-picked blue.
    vec3 refl = reflect(-view, n);
    refl.y = abs(refl.y);   // waves that tilt the normal below horizontal still see sky
    vec3 sky = sky_env(refl, uSunDir, uSunColor, uSkyZenith, uSkyHorizon, uGroundColor);

    vec3 half_v = normalize(uSunDir + view);
    float spec = pow(max(dot(n, half_v), 0.0), 260.0) * 4.0;      // sun glitter
    spec += pow(max(dot(n, half_v), 0.0), 20.0) * 0.12;            // broad sheen

    vec3 color = mix(body, sky, fresnel) + uSunColor * spec;

    // atmospheric fog only at real distance - the near-field colour above is
    // left alone so nearby water still reads as water, not haze
    float dist = length(uCameraPos - vWorld);
    float fog = 1.0 - exp(-dist * dist * uFogDensity * uFogDensity);
    color = mix(color, srgb_to_linear(uFogColor), clamp(fog, 0.0, 1.0) * 0.85);

    // more opaque at a glancing angle, clearer looking straight down
    frag = vec4(color, mix(0.75, 0.97, fresnel));
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