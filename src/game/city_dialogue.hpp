#pragma once
#include "../core/random.hpp"
#include "../core/text.hpp"
#include "../core/sprite.hpp"
#include "../core/shader_sources.hpp"
#include "city_peds.hpp"
#include "city_player.hpp"

// Pokemon-style triggered conversation with a nearby pedestrian.
//
// Walk up to someone, face them, press E: the world stops for the two of
// you. The pedestrian is pulled out of the walking crowd's AI entirely (its
// own PED_STATE_TALK - see city_peds.hpp) and turned to face the player, the
// player's own movement is frozen by city_game.hpp skipping city_player_update
// while a conversation is active, and a text box plays out over the top of the
// world exactly the way the HUD text does.
//
// A conversation is four beats:
//
//   opening   they say something, coloured by who they are and what has been
//             going on - the weather, the hour, the district, whether they
//             have met the player before, and what the street currently
//             thinks of the player (city_peds::reputation)
//   choice    the player picks one of three replies: warm, nosy, or rude
//   reply     they answer that reply, again in their own voice
//   closing   they sign off, warmly or coldly depending on how it went
//
// Nothing here is a dialogue tree in the authored sense - there is no script
// to walk. Every line is picked from a pool indexed by the speaker's traits
// and the world's current state, so the same pedestrian says different things
// in the rain than in the sun, and a timid one and a belligerent one answer
// the same insult in opposite ways.

#define DIALOGUE_RANGE     3.2f
#define DIALOGUE_ARC_COS   0.5f   // roughly a 60 degree half-angle in front of the player
#define DIALOGUE_LINE_LEN  120
#define DIALOGUE_NAME_LEN  32
#define DIALOGUE_MAX_CHOICES 3
#define DIALOGUE_CHOICE_LEN  72

// beats, in the order they play
#define DLG_OPENING 0
#define DLG_CHOICE  1
#define DLG_REPLY   2
#define DLG_CLOSING 3

// which reply the player picked
#define DLG_PICK_WARM 0
#define DLG_PICK_NOSY 1
#define DLG_PICK_RUDE 2

// Reputation the player is above/below before the crowd's manner changes.
#define DIALOGUE_LIKED_AT   0.25f
#define DIALOGUE_FEARED_AT (-0.35f)

struct city_dialogue {
    bool  active;
    int   ped_index;         // into city_peds::people, while active
    int   stage;             // DLG_*
    char  speech[DIALOGUE_LINE_LEN];
    char  name[DIALOGUE_NAME_LEN];
    char  choices[DIALOGUE_MAX_CHOICES][DIALOGUE_CHOICE_LEN];
    int   choice_count;
    int   hover;             // which reply is highlighted
    int   picked;            // DLG_PICK_*, valid from DLG_REPLY on
    float cooldown;          // guards against the same keypress re-opening on close
    // Banked until the conversation actually finishes, so walking away
    // half way through neither earns credit nor costs any.
    float pending_reputation;
    // Set when a reply went badly enough that the pedestrian squares up. Read
    // and cleared by city_dialogue_update as it closes the box.
    bool  provoked;

    // ---- rendering ----
    idx          shader;     // VSHDER_SPRITE / FSHDER_SPRITE, for the panel behind the text
    sprite_batch panel;
    idx          panel_tex;
    pixi_text    name_text;
    pixi_text    body_text;
    pixi_text    prompt_text;
    pixi_text    choice_text[DIALOGUE_MAX_CHOICES];
    bool         ready;
};

// Everything a line can be about that does not live on the pedestrian. Passed
// in rather than reached for, because this file knows about people and the
// player and deliberately nothing else.
struct city_dialogue_context {
    float hour;        // 0..24
    float rain;        // 0..1
    float overcast;    // 0..1
    int   zone;        // ZONE_*
};

static void city_dialogue_init(city_dialogue& d, font_data* font);
static void city_dialogue_update(city_dialogue& d, city_peds& peds, city_player& p,
                                 phys_world& phys, const pix_window& window, float dt,
                                 bool allow_trigger, const city_dialogue_context& ctx);
static void city_dialogue_end(city_dialogue& d, city_peds& peds);
static void city_dialogue_draw(city_dialogue& d, int screen_w, int screen_h,
                               idx text_shader, const mat4& ui_projection);
// Whether an "[E] talk" hint should show right now - the HUD's own question,
// answered with the same search city_dialogue_update triggers off.
static bool city_dialogue_target_available(const city_peds& peds, const city_player& p);

// ---------------- implementation ----------------

static const char* DIALOGUE_NAMES[] = {
    "Alex", "Sam", "Jordan", "Casey", "Riley", "Morgan", "Taylor", "Jamie",
    "Drew", "Quinn", "Avery", "Reese", "Skyler", "Rowan", "Emerson", "Blair",
    "Nadia", "Marcus", "Priya", "Otto", "Lena", "Cyrus", "Wren", "Bo",
};

// ---- openings ----
//
// Split by manner rather than by topic: which pool a pedestrian draws from is
// decided by their friendliness and by how the city currently feels about the
// player, and only then is a line taken out of it.
static const char* DLG_OPEN_WARM[] = {
    "Hey there! Nice to see a friendly face.",
    "Oh - hello! Don't mind me, I'm just wandering.",
    "Morning! Or afternoon. I've honestly lost track.",
    "Hi! You look like you're headed somewhere interesting.",
};
static const char* DLG_OPEN_NEUTRAL[] = {
    "Yeah? What is it?",
    "Hm? Oh, hello.",
    "Can I help you with something?",
    "You need directions or something?",
};
static const char* DLG_OPEN_CURT[] = {
    "I'm in a hurry, make it quick.",
    "What.",
    "I've got somewhere to be, you know.",
    "Look, I really don't have time for this.",
};
static const char* DLG_OPEN_AFRAID[] = {
    "Please - I don't want any trouble.",
    "I saw what you did. Just... stay back.",
    "You're the one they're all talking about, aren't you?",
    "Whatever you want, just take it and go.",
};
static const char* DLG_OPEN_ADMIRING[] = {
    "Hey, it's you! People have been saying good things.",
    "You're that one everybody's been on about. Good to meet you!",
    "Oh - hi! You've got a bit of a reputation, you know. The good kind.",
};
static const char* DLG_OPEN_AGAIN[] = {
    "You again! Twice in one day, what are the odds.",
    "Back so soon? I'm flattered.",
    "Oh, hello again. Still wandering?",
};

// ---- small talk, keyed off the world rather than the speaker ----
static const char* DLG_TOPIC_RAIN[] = {
    "Of course it starts raining the minute I leave the house.",
    "You'd think I'd learn to carry an umbrella by now.",
    "Roads get slick like this. Watch the crossings.",
};
static const char* DLG_TOPIC_GREY[] = {
    "Grey old day, isn't it. Feels like it can't decide.",
    "Sky's been sat like that since breakfast.",
};
static const char* DLG_TOPIC_FINE[] = {
    "Lovely bit of weather, finally.",
    "Days like this I take the long way round.",
};
static const char* DLG_TOPIC_NIGHT[] = {
    "Bit late to be out, isn't it? For both of us.",
    "Quiet at this hour. I like it, mostly.",
    "Streetlights make everything look nicer than it is.",
};
static const char* DLG_TOPIC_MORNING[] = {
    "Too early. Far too early.",
    "Whole city's still waking up.",
};

static const char* DLG_ZONE_TALK[ZONE_COUNT] = {
    /* DOWNTOWN    */ "Everything downtown costs double and takes twice as long.",
    /* COMMERCIAL  */ "Half these shopfronts turn over every year. Hard to keep track.",
    /* RESIDENTIAL */ "It's quiet round here. That's why I stay.",
    /* SUBURB      */ "Nothing ever happens out this way. Suits me fine.",
    /* INDUSTRIAL  */ "You can hear the works going day and night from here.",
    /* PARK        */ "Best bit of green in the whole city, this.",
};

// ---- replies to the player's three options ----
static const char* DLG_REPLY_WARM_OPEN[] = {
    "That's kind of you to say. Made my day, that.",
    "Ha! Likewise. Don't get much of that around here.",
    "Well, aren't you pleasant. Good to meet you properly.",
};
static const char* DLG_REPLY_WARM_CLOSED[] = {
    "...Right. Yes. Thanks, I suppose.",
    "That's - fine. Yes. Anyway.",
};
static const char* DLG_REPLY_NOSY_OPEN[] = {
    "Around here? Depends who's asking, but it's not bad.",
    "Been here years. You get used to the noise.",
    "Honestly? It's better than where I came from.",
};
static const char* DLG_REPLY_NOSY_CLOSED[] = {
    "That's really none of your business.",
    "Why do you want to know that?",
};
static const char* DLG_REPLY_RUDE_MEEK[] = {
    "Alright, alright - I'm going. No need for that.",
    "Sorry! Sorry. I'll get out of your way.",
    "Right. Yes. Sorry to have bothered you.",
};
static const char* DLG_REPLY_RUDE_FIRM[] = {
    "Charming. And here I was being polite.",
    "You want to try saying that again?",
    "There's no call for that, is there.",
};
static const char* DLG_REPLY_RUDE_ANGRY[] = {
    "Say that again. Go on. I dare you.",
    "Right - that's it. You've picked the wrong one today.",
};

// ---- sign-offs ----
static const char* DLG_CLOSE_WARM[] = {
    "Anyway - take care of yourself out there!",
    "Right, I'll let you get on. See you around!",
    "Good talking to you. Mind how you go.",
};
static const char* DLG_CLOSE_COLD[] = {
    "We're done here.",
    "Right. Goodbye.",
    "I'm going now.",
};

#define DLG_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define DLG_PICK_FROM(r, a) ((a)[rng_int((r), 0, DLG_COUNT(a) - 1)])

// The small talk a pedestrian tacks on after their opening. Weather first,
// because it is what anybody actually opens with, then the hour, then where
// they happen to be standing.
static const char* city_dialogue__topic(rng& r, const city_dialogue_context& ctx) {
    if (ctx.rain > 0.25f) return DLG_PICK_FROM(r, DLG_TOPIC_RAIN);
    if (ctx.hour >= 21.0f || ctx.hour < 5.5f) return DLG_PICK_FROM(r, DLG_TOPIC_NIGHT);
    if (ctx.hour < 8.0f) return DLG_PICK_FROM(r, DLG_TOPIC_MORNING);
    if (ctx.overcast > 0.55f) return DLG_PICK_FROM(r, DLG_TOPIC_GREY);
    if (ctx.zone >= 0 && ctx.zone < ZONE_COUNT && rng_chance(r, 0.45f))
        return DLG_ZONE_TALK[ctx.zone];
    return DLG_PICK_FROM(r, DLG_TOPIC_FINE);
}

static void city_dialogue__open_line(city_dialogue& d, rng& r, const city_ped& ped,
                                     float reputation, const city_dialogue_context& ctx) {
    const char* opener;
    if (reputation <= DIALOGUE_FEARED_AT) {
        // Frightened of the player specifically. A brave one still fronts it
        // out, which is why this reads off bravery and not only reputation.
        opener = (ped.bravery > 0.72f) ? DLG_PICK_FROM(r, DLG_OPEN_CURT)
                                       : DLG_PICK_FROM(r, DLG_OPEN_AFRAID);
    } else if (ped.met > 0) {
        opener = DLG_PICK_FROM(r, DLG_OPEN_AGAIN);
    } else if (reputation >= DIALOGUE_LIKED_AT && ped.friendliness > 0.35f) {
        opener = DLG_PICK_FROM(r, DLG_OPEN_ADMIRING);
    } else if (ped.friendliness > 0.62f) {
        opener = DLG_PICK_FROM(r, DLG_OPEN_WARM);
    } else if (ped.friendliness < 0.3f) {
        opener = DLG_PICK_FROM(r, DLG_OPEN_CURT);
    } else {
        opener = DLG_PICK_FROM(r, DLG_OPEN_NEUTRAL);
    }

    // Somebody warm enough to keep talking adds the small talk; somebody curt
    // leaves it at the opener, which is most of what makes them read as curt.
    bool chatty = ped.friendliness > 0.45f && reputation > DIALOGUE_FEARED_AT;
    if (chatty && rng_chance(r, 0.75f))
        snprintf(d.speech, DIALOGUE_LINE_LEN, "%s %s", opener, city_dialogue__topic(r, ctx));
    else
        snprintf(d.speech, DIALOGUE_LINE_LEN, "%s", opener);
}

static void city_dialogue__offer_choices(city_dialogue& d, const city_ped& ped) {
    snprintf(d.choices[DLG_PICK_WARM], DIALOGUE_CHOICE_LEN,
             "Good to meet you.");
    snprintf(d.choices[DLG_PICK_NOSY], DIALOGUE_CHOICE_LEN,
             ped.met > 0 ? "So what do you actually do around here?"
                         : "What's this part of town like?");
    snprintf(d.choices[DLG_PICK_RUDE], DIALOGUE_CHOICE_LEN,
             "Out of my way.");
    d.choice_count = DIALOGUE_MAX_CHOICES;
    d.hover = 0;
}

// How they take each of the three replies, and what it costs or earns.
static void city_dialogue__answer(city_dialogue& d, rng& r, const city_ped& ped, int pick) {
    d.provoked = false;
    if (pick == DLG_PICK_WARM) {
        bool open = ped.friendliness > 0.4f;
        snprintf(d.speech, DIALOGUE_LINE_LEN, "%s",
                 open ? DLG_PICK_FROM(r, DLG_REPLY_WARM_OPEN)
                      : DLG_PICK_FROM(r, DLG_REPLY_WARM_CLOSED));
        d.pending_reputation += PED_REPUTATION_CHAT * (open ? 2.0f : 1.0f);
    } else if (pick == DLG_PICK_NOSY) {
        // Being asked about themselves is a friendliness question for most
        // people and a lawfulness one for the guarded - somebody who keeps to
        // the rules also keeps to themselves about strangers.
        bool open = ped.friendliness * 0.7f + (1.0f - ped.lawfulness) * 0.3f > 0.42f;
        snprintf(d.speech, DIALOGUE_LINE_LEN, "%s",
                 open ? DLG_PICK_FROM(r, DLG_REPLY_NOSY_OPEN)
                      : DLG_PICK_FROM(r, DLG_REPLY_NOSY_CLOSED));
        d.pending_reputation += open ? PED_REPUTATION_CHAT : 0.0f;
    } else {
        float temper = ped.aggression * 0.6f + ped.bravery * 0.4f;
        if (temper > 0.74f) {
            snprintf(d.speech, DIALOGUE_LINE_LEN, "%s", DLG_PICK_FROM(r, DLG_REPLY_RUDE_ANGRY));
            d.provoked = true;
        } else if (temper > 0.42f) {
            snprintf(d.speech, DIALOGUE_LINE_LEN, "%s", DLG_PICK_FROM(r, DLG_REPLY_RUDE_FIRM));
        } else {
            snprintf(d.speech, DIALOGUE_LINE_LEN, "%s", DLG_PICK_FROM(r, DLG_REPLY_RUDE_MEEK));
        }
        d.pending_reputation -= PED_REPUTATION_CHAT * 3.0f;
    }
}

static void city_dialogue__close_line(city_dialogue& d, rng& r) {
    bool warm = d.picked != DLG_PICK_RUDE && !d.provoked;
    snprintf(d.speech, DIALOGUE_LINE_LEN, "%s",
             warm ? DLG_PICK_FROM(r, DLG_CLOSE_WARM) : DLG_PICK_FROM(r, DLG_CLOSE_COLD));
}

static void city_dialogue_init(city_dialogue& d, font_data* font) {
    memset(&d, 0, sizeof(d));
    d.ped_index = -1;

    d.shader = opengl_create_shader(VSHDER_SPRITE, FSHDER_SPRITE);
    d.panel = pix_create_sprite_batch(1);
    unsigned char pixel[4] = { 14, 12, 20, 232 };   // near-black, near-opaque
    d.panel_tex = opengl_create_texture2d(1, 1, 4, pixel, TEXTURE_PIXELATED);
    pix_batch_texture(d.panel, d.panel_tex);

    text_style name_style = pix_default_style(v4(1.00f, 0.86f, 0.40f, 1.0f));
    name_style.size = 20.0f;
    name_style.outline_width = 0.16f;
    name_style.outline_color = v4(0.0f, 0.0f, 0.0f, 0.9f);
    d.name_text = pix_create_text(font, "", { 0.0f, 0.0f }, name_style, 32);

    text_style body_style = pix_default_style(v4(1.0f, 1.0f, 1.0f, 1.0f));
    body_style.size = 19.0f;
    body_style.outline_width = 0.14f;
    body_style.outline_color = v4(0.0f, 0.0f, 0.0f, 0.85f);
    d.body_text = pix_create_text(font, "", { 0.0f, 0.0f }, body_style, 256);

    text_style prompt_style = pix_default_style(v4(0.82f, 0.82f, 0.82f, 0.9f));
    prompt_style.size = 15.0f;
    prompt_style.outline_width = 0.12f;
    prompt_style.outline_color = v4(0.0f, 0.0f, 0.0f, 0.85f);
    d.prompt_text = pix_create_text(font, "", { 0.0f, 0.0f }, prompt_style, 64);

    text_style choice_style = pix_default_style(v4(0.86f, 0.86f, 0.90f, 1.0f));
    choice_style.size = 17.0f;
    choice_style.outline_width = 0.12f;
    choice_style.outline_color = v4(0.0f, 0.0f, 0.0f, 0.85f);
    for (int i = 0; i < DIALOGUE_MAX_CHOICES; i++)
        d.choice_text[i] = pix_create_text(font, "", { 0.0f, 0.0f }, choice_style, 128);

    d.ready = true;
}

// Nearest talkable pedestrian in front of the player - the same shape of
// search city__player_melee uses to find a punch target, just walking the
// crowd instead of gated on state HIT/DEAD, gated here on WALK/WAIT so a
// mid-crossing or already-reacting pedestrian can't be grabbed into a chat.
static int city_dialogue__find_target(const city_peds& peds, const city_player& p) {
    vec3 fwd = forward_from_yaw(p.yaw);
    int best = -1;
    float best_d2 = DIALOGUE_RANGE * DIALOGUE_RANGE;
    for (size_t i = 0; i < MAX_PEDS; i++) {
        const city_ped& ped = peds.people[i];
        if (!ped.active) continue;
        if (ped.state != PED_STATE_WALK && ped.state != PED_STATE_WAIT
            && ped.state != PED_STATE_IDLE) continue;

        vec3 to = v3sub(ped.position, p.position);
        to.y = 0.0f;
        float d2 = v3dot(to, to);
        if (d2 > best_d2) continue;
        if (d2 > 1e-4f && v3dot(v3scale(to, 1.0f / sqrtf(d2)), fwd) < DIALOGUE_ARC_COS) continue;
        best_d2 = d2;
        best = (int)i;
    }
    return best;
}

static bool city_dialogue_target_available(const city_peds& peds, const city_player& p) {
    return city_dialogue__find_target(peds, p) >= 0;
}

static void city_dialogue__begin(city_dialogue& d, city_peds& peds, city_player& p,
                                 phys_world& phys, int index,
                                 const city_dialogue_context& ctx) {
    city_ped& ped = peds.people[index];

    d.active = true;
    d.ped_index = index;
    d.stage = DLG_OPENING;
    d.picked = DLG_PICK_WARM;
    d.provoked = false;
    d.pending_reputation = 0.0f;
    d.choice_count = 0;

    // A name that stays the same for as long as this pedestrian is alive
    // rather than being redrawn every time they are spoken to - being greeted
    // by "Casey" and then by "Rowan" a minute later is the sort of thing that
    // reads as a bug even when nobody can say why.
    snprintf(d.name, DIALOGUE_NAME_LEN, "%s",
             DIALOGUE_NAMES[(index * 2654435761u >> 4) % DLG_COUNT(DIALOGUE_NAMES)]);
    city_dialogue__open_line(d, peds.random, ped, peds.reputation, ctx);

    // Turn to face each other, the way an NPC in a Pokemon game spins to
    // look straight at you the moment the conversation starts.
    vec3 to_ped = v3sub(ped.position, p.position);
    to_ped.y = 0.0f;
    if (v3len(to_ped) > 1e-3f) {
        vec3 dir = v3norm(to_ped);
        p.yaw = yaw_from_forward(dir);
        ped.yaw = yaw_from_forward(v3scale(dir, -1.0f));
    }
    ped.state = PED_STATE_TALK;

    phys_body* pb = phys_get_body(phys, p.body);
    if (pb) pb->velocity = v3(0.0f, 0.0f, 0.0f);
}

static void city_dialogue_end(city_dialogue& d, city_peds& peds) {
    if (d.ped_index >= 0 && (size_t)d.ped_index < MAX_PEDS) {
        city_ped& ped = peds.people[d.ped_index];
        // Only hand it back if it is still this conversation's ped and still
        // ours to hand back - a hit or death mid-chat has already moved it
        // on to its own state, and stepping on that here would cut a
        // stagger or a death short.
        if (ped.active && ped.state == PED_STATE_TALK) {
            if (ped.met < 255) ped.met++;
            if (d.provoked) {
                // Insulted by somebody standing right in front of them. They
                // square up on their own account rather than waiting to be
                // hit first - the one route into a fight that starts with
                // words instead of a punch.
                ped.threat = ped.position;
                ped.brave = true;
                ped.alarm = PED_FIGHT_TIME;
                ped.swing = PED_SWING_PERIOD * 0.5f;
                ped.state = PED_STATE_FIGHT;
            } else {
                ped.state = PED_STATE_WALK;
            }
        }
    }
    peds.reputation += d.pending_reputation;
    if (peds.reputation >  1.0f) peds.reputation =  1.0f;
    if (peds.reputation < -1.0f) peds.reputation = -1.0f;

    d.pending_reputation = 0.0f;
    d.active = false;
    d.ped_index = -1;
}

static void city_dialogue_update(city_dialogue& d, city_peds& peds, city_player& p,
                                 phys_world& phys, const pix_window& window, float dt,
                                 bool allow_trigger, const city_dialogue_context& ctx) {
    if (d.cooldown > 0.0f) d.cooldown -= dt;

    if (!d.active) {
        if (!allow_trigger || d.cooldown > 0.0f) return;
        if (!window.keystates['E'].pressed) return;
        int target = city_dialogue__find_target(peds, p);
        if (target < 0) return;
        city_dialogue__begin(d, peds, p, phys, target, ctx);
        return;
    }

    // Something else got to the pedestrian first - a car, a punch, the
    // crowd cull despawning it out from under us - so let the chat drop
    // rather than fight whatever state it is in now.
    if (d.ped_index < 0 || (size_t)d.ped_index >= MAX_PEDS
        || !peds.people[d.ped_index].active
        || peds.people[d.ped_index].state != PED_STATE_TALK) {
        d.pending_reputation = 0.0f;
        d.active = false;
        d.ped_index = -1;
        return;
    }

    // Hold the player still for the duration - city_game.hpp already skips
    // city_player_update while a conversation is active, so nothing else is
    // driving this body's velocity, but a shove from a car or a pedestrian
    // squeezing past should not send the player wandering off mid-sentence.
    phys_body* pb = phys_get_body(phys, p.body);
    if (pb) { pb->velocity.x = 0.0f; pb->velocity.z = 0.0f; }

    if (window.keystates[KEY_ESC].pressed) {
        d.pending_reputation = 0.0f;    // walking out earns nothing either way
        city_dialogue_end(d, peds);
        d.cooldown = 0.35f;
        return;
    }

    if (d.stage == DLG_CHOICE) {
        if (window.keystates[KEY_UP].pressed && d.hover > 0) d.hover--;
        if (window.keystates[KEY_DOWN].pressed && d.hover + 1 < d.choice_count) d.hover++;

        int pick = -1;
        for (int i = 0; i < d.choice_count; i++)
            if (window.keystates['1' + i].pressed) pick = i;
        if (pick < 0 && (window.keystates['E'].pressed
                         || window.keystates[KEY_RETURN].pressed
                         || window.keystates[MOUSE_BUTTON_LEFT].pressed))
            pick = d.hover;
        if (pick < 0) return;

        d.picked = pick;
        city_dialogue__answer(d, peds.random, peds.people[d.ped_index], pick);
        d.stage = DLG_REPLY;
        return;
    }

    bool advance = window.keystates['E'].pressed || window.keystates[KEY_RETURN].pressed
                || window.keystates[MOUSE_BUTTON_LEFT].pressed;
    if (!advance) return;

    if (d.stage == DLG_OPENING) {
        city_dialogue__offer_choices(d, peds.people[d.ped_index]);
        d.stage = DLG_CHOICE;
    } else if (d.stage == DLG_REPLY) {
        // Somebody who has just squared up does not then wish you a nice day.
        if (d.provoked) {
            city_dialogue_end(d, peds);
            d.cooldown = 0.35f;
            return;
        }
        city_dialogue__close_line(d, peds.random);
        d.stage = DLG_CLOSING;
    } else {
        city_dialogue_end(d, peds);
        d.cooldown = 0.35f;
    }
}

static void city_dialogue_draw(city_dialogue& d, int screen_w, int screen_h,
                               idx text_shader, const mat4& ui_projection) {
    if (!d.active || !d.ready) return;

    bool choosing = (d.stage == DLG_CHOICE);
    float box_w = (float)screen_w * 0.60f;
    float box_h = choosing ? 190.0f : 116.0f;
    float box_x = ((float)screen_w - box_w) * 0.5f;
    float box_y = (float)screen_h - box_h - 34.0f;

    sprite panel = {};
    panel.box = v4(box_x, box_y, box_w, box_h);
    panel.crop = v4(0.0f, 0.0f, 1.0f, 1.0f);
    panel.texture = 0;
    replace_sprites(d.panel, &panel, 1, 0);
    draw_sprites(d.panel, d.shader, ui_projection);

    d.name_text.position = { box_x + 22.0f, box_y + 28.0f };
    pix_update_text(d.name_text, d.name);
    draw_text(d.name_text, text_shader, ui_projection);

    d.body_text.position = { box_x + 22.0f, box_y + 62.0f };
    pix_update_text(d.body_text, d.speech);
    draw_text(d.body_text, text_shader, ui_projection);

    const char* prompt;
    if (choosing) {
        // The player's three replies, the highlighted one carrying the arrow.
        // Numbers as well as the arrow because reaching for 1/2/3 is faster
        // than paging down to the option you already read.
        for (int i = 0; i < d.choice_count; i++) {
            char row[DIALOGUE_CHOICE_LEN + 8];
            snprintf(row, sizeof(row), "%s %d. %s",
                     i == d.hover ? ">" : " ", i + 1, d.choices[i]);
            d.choice_text[i].style.color = (i == d.hover)
                                         ? v4(1.00f, 0.88f, 0.45f, 1.0f)
                                         : v4(0.78f, 0.78f, 0.82f, 1.0f);
            d.choice_text[i].position = { box_x + 30.0f, box_y + 96.0f + (float)i * 26.0f };
            pix_update_text(d.choice_text[i], row);
            draw_text(d.choice_text[i], text_shader, ui_projection);
        }
        prompt = "[1-3] or arrows + [E]";
    } else {
        prompt = (d.stage == DLG_CLOSING) ? "[E] close" : "[E] continue";
    }

    d.prompt_text.position = { box_x + box_w - pix_text_width(d.name_text.font, prompt,
                                                              d.prompt_text.style) - 20.0f,
                               box_y + box_h - 16.0f };
    pix_update_text(d.prompt_text, prompt);
    draw_text(d.prompt_text, text_shader, ui_projection);
}
