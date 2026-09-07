#pragma once
#include <stdlib.h>
#include "../core/lighting.hpp"
#include "../core/renderer.hpp"
#include "../core/random.hpp"
#include "city_config.hpp"

// The weather over the city.
//
// Weather here is one number that moves slowly - how much cloud is over the
// city - plus what that number implies. Rain is not rolled separately: it is
// what a thick enough deck does, so a downpour can never happen under a clear
// sky and the cloud always arrives before the first drop. That ordering is the
// whole reason this is a target-and-drift rather than a set of states that
// switch: the sky closing over for a minute before the rain starts is what
// makes the rain feel like it came from somewhere.
//
// The one thing that genuinely lags rather than tracking the deck is the
// ground: it takes a while to soak and much longer to dry, so the city stays
// shining after a shower has passed. See pix_weather::wetness, which the
// surface shader reads.

// how long one spell of weather lasts before a new target is drawn, seconds
#define CITY_WEATHER_MIN_SPELL   90.0f
#define CITY_WEATHER_MAX_SPELL  260.0f
// how fast the deck can move toward its target, units of overcast per second.
// Slow enough that a full clear-to-storm swing takes the better part of a
// minute, which is roughly what a sky does.
#define CITY_WEATHER_DRIFT        0.055f
// the deck a downpour needs behind it; below this it is grey but dry
#define CITY_WEATHER_RAIN_ONSET   0.55f

struct city_weather {
    pix_weather now;
    float target;          // the overcast this spell is heading for
    float spell_left;      // seconds until a new target is drawn
    rng   random;
    bool  forced;          // a key has pinned the weather; stop drawing targets
};

// Rolls the next spell. The distribution is the point: most of the time it is
// a fair day, sometimes it is grey, and rain is the tail of that - a city
// where it rains a third of the time reads as broken rather than as moody.
static float city_weather_roll_target(rng& r) {
    float roll = rng_float(r);
    if (roll < 0.45f) return rng_range(r, 0.00f, 0.18f);   // clear
    if (roll < 0.72f) return rng_range(r, 0.18f, 0.45f);   // fair, some cloud
    if (roll < 0.89f) return rng_range(r, 0.45f, 0.70f);   // gloomy
    return rng_range(r, 0.72f, 1.00f);                     // rain
}

static city_weather city_weather_init(uint32_t seed) {
    city_weather w = {};
    w.random = rng_seed(seed ? seed : 0x51ED270Bu);
    w.now.overcast = 0.10f;
    w.now.rain = 0.0f;
    w.now.wetness = 0.0f;
    w.target = w.now.overcast;
    w.spell_left = rng_range(w.random, CITY_WEATHER_MIN_SPELL, CITY_WEATHER_MAX_SPELL);

    // PIX_WEATHER=<0..1> pins the sky for a run - 0 clear, 1 a downpour - and
    // starts it there rather than drifting into it, which is what makes a
    // rainy screenshot one line to ask for.
    const char* env = getenv("PIX_WEATHER");
    if (env) {
        float deck = (float)atof(env);
        deck = deck < 0.0f ? 0.0f : (deck > 1.0f ? 1.0f : deck);
        w.forced = true;
        w.target = deck;
        w.now.overcast = deck;
        w.now.rain = deck > CITY_WEATHER_RAIN_ONSET
                   ? (deck - CITY_WEATHER_RAIN_ONSET) / (1.0f - CITY_WEATHER_RAIN_ONSET)
                   : 0.0f;
        w.now.wetness = w.now.rain;
    }
    return w;
}

// Pins the weather to a given deck until it is released. Used by the debug key
// and by anything scripted that wants a specific sky.
static void city_weather_force(city_weather& w, float overcast) {
    w.target = overcast < 0.0f ? 0.0f : (overcast > 1.0f ? 1.0f : overcast);
    w.forced = true;
}

static void city_weather_release(city_weather& w) {
    w.forced = false;
    w.spell_left = 0.0f;      // draw a fresh spell on the next update
}

static void city_weather_update(city_weather& w, pix_renderer& renderer, float dt) {
    if (!w.forced) {
        w.spell_left -= dt;
        if (w.spell_left <= 0.0f) {
            w.target = city_weather_roll_target(w.random);
            w.spell_left = rng_range(w.random, CITY_WEATHER_MIN_SPELL, CITY_WEATHER_MAX_SPELL);
        }
    }

    float step = CITY_WEATHER_DRIFT * dt;
    float delta = w.target - w.now.overcast;
    if (delta > step)       w.now.overcast += step;
    else if (delta < -step) w.now.overcast -= step;
    else                    w.now.overcast = w.target;

    // Rain follows the deck rather than being its own roll, and it lags it a
    // little on the way in so the sky is already grey when the first drop
    // falls.
    float wants_rain = (w.now.overcast - CITY_WEATHER_RAIN_ONSET) / (1.0f - CITY_WEATHER_RAIN_ONSET);
    if (wants_rain < 0.0f) wants_rain = 0.0f;
    if (wants_rain > 1.0f) wants_rain = 1.0f;
    float rain_rate = wants_rain > w.now.rain ? 0.22f : 0.45f;   // starts slower than it stops
    w.now.rain += (wants_rain - w.now.rain) * (1.0f - expf(-rain_rate * dt));

    // Soaking is quick, drying is not - a road stays wet long after the cloud
    // has gone, and that trailing shine is most of what sells that it rained.
    float soak = w.now.rain > w.now.wetness ? 0.30f : 0.035f;
    w.now.wetness += (w.now.rain - w.now.wetness) * (1.0f - expf(-soak * dt));

    pix_set_weather(renderer, w.now);
}

// For the HUD.
static const char* city_weather_name(const city_weather& w) {
    if (w.now.rain > 0.35f)     return "rain";
    if (w.now.rain > 0.05f)     return "drizzle";
    if (w.now.overcast > 0.55f) return "gloomy";
    if (w.now.overcast > 0.25f) return "cloudy";
    return "clear";
}
