#pragma once
#include "../core/random.hpp"
#include "../core/physics.hpp"
#include "../core/animation.hpp"
#include "../core/jobs.hpp"
#include "city_map.hpp"

// Pedestrians.
//
// People walk the pavement graph the same way cars walk the road graph: cell to
// cell, preferring to keep going the way they were headed. Roads are crossed
// only at the marked crossings beside junctions, and only when the lights say
// the traffic on that street has stopped - so a crowd builds up on the kerb and
// then goes over together, which is most of what makes a street look alive.
//
// On top of that skeleton sit the things that separate a crowd from a hundred
// random walks, and each one is here because without it the street reads wrong
// in a specific way:
//
//   somewhere to be   everyone holds a destination cell and picks the turn
//                     that gets them nearer it. A pure random walk is the
//                     single most obvious tell there is: nobody in a real
//                     street is equally happy to go either way at a corner.
//   keeping right     the lateral offset inside a cell is always to the right
//                     of travel, so two flows down one pavement separate into
//                     two lanes instead of walking through each other.
//   giving way        each person steers around whoever is close and ahead of
//                     them. This is what turns shoulder-barging into weaving.
//   looking first     nobody steps off a kerb the instant the light changes;
//                     they check the road, and they will not step in front of
//                     a car that is close and moving whatever the light says.
//   losing patience   and after long enough on a kerb with the road empty,
//                     they jaywalk, because people do.
//   stopping          arriving somewhere means standing there a while, and two
//                     people standing near each other fall into conversation.
//   the clock         how many people are out, and what they are out for,
//                     follows the hour of the day.
//
// ---- what runs where ----
//
// The per-person work splits cleanly in two, and the split is by what it
// writes rather than by what it costs:
//
//   maintenance   timers, despawns, spawns, animation-slot bookkeeping and
//                 pairing people off into conversations. All of it touches
//                 shared pools or two people at once, so it stays on the
//                 calling thread.
//   steering      choosing a route, avoiding neighbours, deciding whether to
//                 cross, and setting one body's velocity and yaw. Person i
//                 writes person i and nothing else, so this goes across every
//                 core through pix_parallel_for.
//
// The skinning is a third piece again and does not belong to either: sampling
// the pose banks is pure maths over data the solver never touches, so
// city_peds_animate runs on its own thread *alongside* the physics step - see
// city_game.hpp. That is why the banks are updated by their own entry point
// rather than at the top of the update the way they used to be.
//
// The expensive part of a crowd is not the walking, it is the skinning: one
// pose is a full skeleton evaluation and a bone matrix upload. So poses are
// pooled. A handful of copies of each cycle are advanced every frame at
// staggered phases and each pedestrian just points at one of them, which keeps
// the per-frame animation cost flat no matter how many people are on screen and
// still avoids a street of people marching in lockstep.
//
// There is one bank per character rig, and each bank carries a walk cycle *and*
// an idle. Somebody waiting at a crossing standing still while their legs keep
// walking is the single most obvious tell that a crowd is faked, and an idle
// clip costs one more pose per rig to fix.

#define PED_WALK_POSES   8
#define PED_IDLE_POSES   3
// A crowd that is running or fighting is a small fraction of one that is
// walking, so these two banks are much shallower than the walk.
#define PED_RUN_POSES    5
#define PED_PUNCH_POSES  3
// Faster than a real pedestrian for the same reason the player is, and kept
// well under the player's walk so that overtaking a crowd still reads as
// overtaking it.
#define PED_WALK_SPEED   1.85f
#define PED_CROSS_SPEED  2.75f
// Frightened. Faster than the player's walk and slower than their run, so
// somebody fleeing gets away from a walk and is run down by a sprint.
#define PED_FLEE_SPEED   6.0f

#define PED_STATE_WALK   0
#define PED_STATE_WAIT   1   // on the kerb, waiting for the traffic to stop
#define PED_STATE_CROSS  2
// Hit and dead both suspend the walking AI and hand the pedestrian its own
// reactor slot (see city_ped_reactor) instead of the shared walk/idle bank -
// a stagger or a death has to play once, on its own clock, not as one more
// staggered copy of a looping cycle.
#define PED_STATE_HIT    3   // staggering; returns to PED_STATE_WALK on its own
#define PED_STATE_DEAD   4   // plays once, then holds its last frame for good
// Held by city_dialogue while the player is talking to this pedestrian - see
// city_dialogue.hpp. Like WAIT, it suspends the walking AI outright, but
// nothing hands it back on its own; only city_dialogue ending the
// conversation returns the ped to PED_STATE_WALK.
#define PED_STATE_TALK   5
// What being attacked does. Most people run; a few square up. Which one this
// person is, is decided once, on the first hit that lands on them, and then
// stuck to - somebody who alternates between fleeing and fighting every time
// they are touched reads as broken rather than as frightened.
#define PED_STATE_FLEE   6
#define PED_STATE_FIGHT  7
// Arrived somewhere and stopped. A street where everybody is in transit is
// not a street, it is a corridor - the people standing still outside a shop
// are what give the ones walking past something to walk past.
#define PED_STATE_IDLE   8
// Two of those who ended up standing near each other. Both face each other
// and hold a talking pose until the clock runs out - see city__pair_chats,
// which is the one piece of pedestrian logic that has to touch two people at
// once and is therefore the one piece that cannot be threaded.
#define PED_STATE_CHAT   9

#define PED_MAX_HEALTH   100.0f
// Odds that somebody hit fights back instead of running. Kept low on purpose:
// a street where everybody turns and squares up is not a city, it is an
// arena, and the one in five who does fight is far more striking against
// four who run.
#define PED_FIGHT_CHANCE 0.22f
// How long a scare lasts, in seconds, before the walking AI takes over again.
#define PED_FLEE_TIME    9.0f
#define PED_FIGHT_TIME   11.0f
// How close a fighter closes to, and how often they swing once they are there.
#define PED_FIGHT_RANGE  2.0f
#define PED_SWING_PERIOD 1.5f
// The push a landed pedestrian punch gives, in metres per second. There is no
// player health in this game by design, so a fist that connects is felt as
// being shoved rather than as a number going down.
#define PED_PUNCH_SHOVE  4.2f
// How far away a hit is noticed. Everybody inside this scatters, which is
// most of what makes a street react to violence rather than one person.
// Nerve a bystander needs before they stand and watch trouble rather than
// run from it, and how long they stay to look. See city_peds_alarm.
#define PED_ONLOOKER_NERVE 0.58f
#define PED_WATCH_MIN      4.0f
#define PED_WATCH_MAX     10.0f

#define PED_ALARM_RADIUS 16.0f
// A gunshot carries a great deal further than a scuffle does, which is the
// whole difference between a fist fight on a pavement and a shooting on one.
#define PED_GUNSHOT_RADIUS 55.0f
// How far somebody who decided to fight will follow before giving it up and
// running instead. About a block, and deliberately well past the radius
// above: a fighter who quits the moment you take a step back is not
// fighting, and one that never quits chases you across the map.
#define PED_CHASE_RANGE  48.0f
// How long a body lies there before it is taken away. Corpses are not free:
// each one holds an animation slot and a physics body, and a city that never
// clears them ends up with every later death standing bolt upright because
// there was no slot left to fall over with.
#define PED_CORPSE_LIFETIME 26.0f
// Nothing can re-hit a body that is still resolving out of the last impact -
// without this, a car idling on top of a corpse (or a fist thrown twice in
// one swing's frames) would re-trigger the reaction every single step.
#define PED_HIT_INVULN   0.5f

// ---- what the city thinks of the player - see city_peds::reputation ----
#define PED_REPUTATION_DECAY   0.006f   // per second, back towards neutral
#define PED_REPUTATION_PUNCH   0.34f    // charged per person the player hits
#define PED_REPUTATION_KILL    0.75f
#define PED_REPUTATION_CHAT    0.06f    // earned per conversation seen through

// ---- having somewhere to be ----
//
// Why this person is out. It changes three things and nothing else: how far
// away their destination is allowed to be, how fast they walk to it, and how
// long they stand about once they get there. That is enough to tell a
// commuter crossing the city from somebody out for a walk in the park, and
// anything more elaborate would not survive being watched from six metres
// behind a running player.
#define PED_ERRAND   0   // a few blocks, unhurried, a long stop at the end
#define PED_COMMUTE  1   // right across the district, brisk, barely stops
#define PED_STROLL   2   // nowhere in particular, slow, stops often

// How far from where they are now a new destination is picked, per purpose.
#define PED_GOAL_NEAR    22    // cells
#define PED_GOAL_FAR     70

// Seconds of standing about on arrival.
#define PED_IDLE_MIN     3.0f
#define PED_IDLE_MAX     14.0f
// How long two people who fall into conversation stay in it.
#define PED_CHAT_MIN     6.0f
#define PED_CHAT_MAX     18.0f
// How near two idlers have to be to start one.
#define PED_CHAT_RANGE   2.4f

// ---- crossing a road ----
//
// Nobody steps off a kerb the instant the light changes. This is the pause
// while they look, and it is deliberately long enough to see: a queue that
// all steps off on the same frame is exactly as wrong as a queue that never
// waits at all.
#define PED_LOOK_TIME     0.55f
// A car nearer than this to the crossing, and still moving, holds people on
// the kerb whatever the lights say.
#define PED_CAR_WATCH     16.0f
#define PED_CAR_MOVING     1.5f   // m/s below which a car is not a threat
// How long somebody will stand at a kerb before crossing against the light.
// People jaywalk; a city where nobody ever does reads as a rule being obeyed
// rather than as a place.
#define PED_PATIENCE      14.0f

// ---- giving way ----
//
// Steering around whoever is close and in front. Kept short and weak on
// purpose: this is a nudge that makes two people weave past each other, not a
// navigation system, and turned up any further a crowd starts to swirl.
#define PED_AVOID_RANGE   2.3f
#define PED_AVOID_FORCE   1.5f
#define PED_AVOID_MAX     8       // neighbours considered; the nearest are what matter

struct city_ped {
    bool    active;
    vec3    position;
    float   yaw;
    float   speed;

    int     node_x, node_z;
    int     next_x, next_z;
    int     cross_dir;       // direction being crossed in, DIR_NONE otherwise

    idx     body;
    uint8_t character;       // which rig, index into city_catalog::characters
    uint8_t material;        // which outfit tint of that rig
    uint8_t pose;
    uint8_t state;
    uint8_t bank_pose;       // which standing pose a waiting one holds
    float   height;          // multiplier on the rig's own scale
    float   lateral;         // offset from the cell centreline, so they do not single-file
    float   stuck;

    // ---- where they are going ----
    int     goal_x, goal_z;  // destination cell
    uint8_t purpose;         // PED_*, see above
    float   loiter;          // seconds left standing at the destination
    float   patience;        // seconds spent waiting at this kerb
    float   look;            // seconds left of checking the road before stepping off
    float   chat;            // seconds left of a pavement conversation
    int     partner;         // who that conversation is with, -1 for nobody

    // ---- health and reactions - see city_ped_hit ----
    float   health;          // hidden; PED_MAX_HEALTH down to 0, never shown in the HUD
    float   invuln;          // seconds left before another impact can register
    int     reactor;         // slot into city_peds::reactor_pose while HIT or DEAD, else -1
    float   react_time;      // seconds into the reactor's clip

    // ---- being attacked ----
    vec3    threat;          // where whatever hit them was, for running the other way
    float   alarm;           // seconds left of fleeing or fighting
    float   swing;           // seconds until this fighter's next punch
    float   dead_time;       // seconds spent dead, for clearing the body away
    // ---- who this person is ----
    //
    // Five traits, 0..1, rolled once at spawn and never changed. They are not
    // an extra behaviour layer bolted on the side: every one of them feeds a
    // decision the crowd was already making with a coin flip. Two people given
    // the same shove now do different things, and keep doing the same
    // different things for as long as they are alive, which is what makes them
    // read as people rather than as one AI wearing twelve outfits.
    float   friendliness;    // how warmly they greet the player
    float   bravery;         // stand and fight, or run
    float   curiosity;       // drawn towards trouble instead of away from it
    float   aggression;      // starts something rather than waiting to be hit
    float   lawfulness;      // waits for the lights, disapproves of violence

    // Times this pedestrian has talked with the player. A second conversation
    // opens differently from the first, which is the whole of "NPCs remember
    // encounters" that survives inside one pedestrian's lifetime - the city's
    // longer memory of the player is city_peds::reputation.
    uint8_t met;

    bool    brave;           // fights back rather than running; decided on the first hit
    bool    struck;          // landed a punch this frame - read and cleared by the owner
                             // of whatever it is punching; the crowd does not know the
                             // player exists, so it cannot shove one itself
    // Set by the maintenance pass for everybody the threaded steering pass is
    // allowed to touch. Anybody dead, staggering, in conversation with the
    // player or about to be despawned is deliberately not in that set.
    bool    steerable;
    // Lying down without a death clip to do it with. Only used when a rig
    // ships no death animation, or when every reactor slot was already busy -
    // see city__reactor_take. Runs 0 to 1 and tips the body over on its side.
    float   topple;
};

// A handful of pedestrians can be reacting to a hit or lying dead at once
// near the player; the other hundred-odd on screen are still walking off the
// cheap shared bank below. Sized for "a car just drove through a crowd", not
// for everyone at once.
#define MAX_PED_REACTORS 24

// Staggered copies of one rig's cycles, shared by everyone using that rig.
struct city_pose_bank {
    animator  source;
    animation walk[PED_WALK_POSES];
    animation idle[PED_IDLE_POSES];
    // Running away, and throwing a punch. Both are shared staggered copies
    // like the walk rather than a reactor each: a scattering crowd is dozens
    // of people at once, and dozens of reactors is exactly what the pool
    // below is too small for.
    animation run[PED_RUN_POSES];
    animation punch[PED_PUNCH_POSES];
    // A second standing cycle. Everyone waiting at a crossing holding the same
    // pose is as obvious as everyone walking in step, and it is more obvious,
    // because a queue at a kerb is exactly where the eye stops to look.
    animation stand[PED_IDLE_POSES];
    float     walk_duration;
    float     idle_duration;
    float     stand_duration;
    float     run_duration;
    float     punch_duration;
    idx       walk_clip;
    idx       idle_clip;
    idx       stand_clip;
    idx       run_clip;
    idx       punch_clip;
    bool      ready;
};

// How many worker threads the steering pass can be handed to. One rng each,
// because a shared one is a data race and, worse, a *silent* one: xorshift
// keeps producing plausible numbers however badly it is torn.
#define PED_MAX_THREADS (PIX_MAX_WORKERS + 1)

struct city_peds {
    city_ped       people[MAX_PEDS];
    size_t         count;
    city_pose_bank banks[CITY_MAX_CHARACTERS];
    size_t         bank_count;
    rng            random;                    // maintenance and spawning, calling thread only
    rng            worker_random[PED_MAX_THREADS];   // one per steering thread

    // What the city as a whole thinks of the player, -1 (feared) to +1 (liked).
    // Individual pedestrians come and go every few hundred metres, so nothing
    // the player does could be remembered at all if it were only remembered by
    // whoever it happened to. This is where "they saw what you did" lives:
    // punching somebody in the street drops it, talking somebody through a
    // whole conversation lifts it, and it decays back to nothing over a few
    // minutes so the city forgives eventually.
    float          reputation;

    // One pose each, sampled fresh every frame from the owning ped's own bank
    // source at the ped's own reactor clock - see city_ped_hit and the
    // PED_STATE_HIT / PED_STATE_DEAD handling in city__peds_maintain.
    animation reactor_pose[MAX_PED_REACTORS];
    bool      reactor_used[MAX_PED_REACTORS];
    // Which pedestrian owns each slot, so a slot can be taken back off the
    // oldest corpse when a fresh death needs one - see city__reactor_take.
    int       reactor_owner[MAX_PED_REACTORS];

    // What the steering pass needs and does not own. Filled in by
    // city_peds_update before the parallel dispatch and read-only inside it.
    const city_world*  job_world;
    const phys_world*  job_phys;
    phys_world*        job_phys_rw;
    float              job_time;
    float              job_dt;
    vec3               job_focus;
};

static void city_ped_hit(city_peds& p, const city_catalog& cat, phys_world& phys,
                         int index, vec3 impulse, float damage, vec3 from);
// Everybody within `radius` of `at` saw it happen and scatters. What makes a
// street react to violence rather than one unlucky person.
static void city_peds_alarm(city_peds& p, vec3 at, float radius);
// A gun pointed at a street, before a shot is ever fired. Everybody inside
// `radius` who is in front of `facing` can see it; what they do about it is
// their own nerve. This is deliberately not city_peds_alarm with a different
// number: an alarm is a thing that already happened and everybody reacts to
// it at once, and a levelled weapon is a threat that only the people looking
// down the barrel of it can see.
static void city_peds_menace(city_peds& p, vec3 at, vec3 facing, float radius);

static void city_peds_init(city_peds& p, pix_data_loader& loader, const city_catalog& cat,
                           uint32_t seed);
// `hour` is the city clock, 0..24: it decides how many people are out and what
// they are out for. `jobs` may be null, in which case the steering pass runs
// on the calling thread and nothing else changes.
static void city_peds_update(city_peds& p, const city_world& w, const city_catalog& cat,
                             phys_world& phys, vec3 focus, float time, float hour, float dt,
                             pix_jobs* jobs);
// The skinning, split out so it can be run on its own thread beside the
// physics step. Touches only the pose banks and the reactor poses.
static void city_peds_animate(city_peds& p, const city_catalog& cat, float time);
static void city_peds_draw(const city_peds& p, const city_catalog& cat, pix_renderer& renderer,
                           const frustum& view, vec3 eye);

// ---------------- implementation ----------------

static void city_peds_init(city_peds& p, pix_data_loader& loader, const city_catalog& cat,
                           uint32_t seed) {
    memset(&p, 0, sizeof(p));
    p.random = rng_seed(seed);
    for (int i = 0; i < PED_MAX_THREADS; i++)
        p.worker_random[i] = rng_seed(seed * 2654435761u + (uint32_t)i * 0x9E3779B9u + 1u);

    for (size_t c = 0; c < cat.character_count; c++) {
        const city_character& ch = cat.characters[c];
        city_pose_bank& bank = p.banks[p.bank_count];
        memset(&bank, 0, sizeof(bank));
        if (!ch.ok || !city_build_character_animator(bank.source, loader, ch)) {
            p.bank_count++;                    // keep bank indices aligned with characters
            continue;
        }
        // Clip 0 is never a safe stand-in. It is whatever the exporter wrote
        // first, which across this whole cast is "Death" - a pedestrian rig
        // whose walk failed to resolve used to spend its life strolling
        // around the city in its death pose. A rig with no walk and no idle
        // is simply not given a bank.
        bank.walk_clip  = ch.clips[CLIP_WALK] != (idx)-1 ? ch.clips[CLIP_WALK] : ch.clips[CLIP_IDLE];
        bank.idle_clip  = ch.clips[CLIP_IDLE] != (idx)-1 ? ch.clips[CLIP_IDLE] : bank.walk_clip;
        bank.stand_clip = ch.clips[CLIP_TALK] != (idx)-1 ? ch.clips[CLIP_TALK] : bank.idle_clip;
        bank.run_clip   = ch.clips[CLIP_RUN] != (idx)-1 ? ch.clips[CLIP_RUN] : bank.walk_clip;
        bank.punch_clip = ch.clips[CLIP_PUNCH] != (idx)-1 ? ch.clips[CLIP_PUNCH] : bank.idle_clip;
        if (bank.walk_clip == (idx)-1 || bank.idle_clip == (idx)-1) {
            p.bank_count++;
            continue;                          // leaves bank.ready false: this rig is skipped
        }
        bank.walk_duration  = animator_duration(bank.source, bank.walk_clip);
        bank.idle_duration  = animator_duration(bank.source, bank.idle_clip);
        bank.stand_duration = animator_duration(bank.source, bank.stand_clip);
        bank.run_duration   = animator_duration(bank.source, bank.run_clip);
        bank.punch_duration = animator_duration(bank.source, bank.punch_clip);
        bank.ready = true;
        p.bank_count++;
    }
}

static void city__bank_update(city_pose_bank& bank, float time) {
    if (!bank.ready) return;

    float wd = bank.walk_duration > 0.01f ? bank.walk_duration : 1.0f;
    for (int i = 0; i < PED_WALK_POSES; i++) {
        // evenly spread around the cycle, so no two neighbours are in step
        float phase = time + wd * ((float)i / (float)PED_WALK_POSES);
        animator_sample(bank.source, bank.walk_clip, phase, &bank.walk[i]);
    }

    float idur = bank.idle_duration > 0.01f ? bank.idle_duration : 1.0f;
    for (int i = 0; i < PED_IDLE_POSES; i++) {
        float phase = time + idur * ((float)i / (float)PED_IDLE_POSES);
        animator_sample(bank.source, bank.idle_clip, phase, &bank.idle[i]);
    }

    float sdur = bank.stand_duration > 0.01f ? bank.stand_duration : 1.0f;
    for (int i = 0; i < PED_IDLE_POSES; i++) {
        float phase = time + sdur * ((float)i / (float)PED_IDLE_POSES);
        animator_sample(bank.source, bank.stand_clip, phase, &bank.stand[i]);
    }

    float rdur = bank.run_duration > 0.01f ? bank.run_duration : 1.0f;
    for (int i = 0; i < PED_RUN_POSES; i++) {
        float phase = time + rdur * ((float)i / (float)PED_RUN_POSES);
        animator_sample(bank.source, bank.run_clip, phase, &bank.run[i]);
    }

    float pdur = bank.punch_duration > 0.01f ? bank.punch_duration : 1.0f;
    for (int i = 0; i < PED_PUNCH_POSES; i++) {
        float phase = time + pdur * ((float)i / (float)PED_PUNCH_POSES);
        animator_sample(bank.source, bank.punch_clip, phase, &bank.punch[i]);
    }
}

// Every skeleton the crowd needs this frame, in one call.
//
// This is the whole reason the pose banks exist and it is by some distance
// the most expensive thing pedestrians do - thirteen rigs times twenty-two
// poses times sixty-odd bones. It reads `time` and the reactor clocks the
// maintenance pass has already advanced, and writes only into the banks and
// the reactor poses, which is what makes it safe to run beside the solver.
static void city_peds_animate(city_peds& p, const city_catalog& cat, float time) {
    for (size_t b = 0; b < p.bank_count; b++) city__bank_update(p.banks[b], time);

    for (int slot = 0; slot < MAX_PED_REACTORS; slot++) {
        if (!p.reactor_used[slot]) continue;
        int who = p.reactor_owner[slot];
        if (who < 0 || (size_t)who >= MAX_PEDS) continue;
        const city_ped& ped = p.people[who];
        if (!ped.active || ped.character >= p.bank_count) continue;
        const city_pose_bank& bank = p.banks[ped.character];
        if (!bank.ready || ped.character >= cat.character_count) continue;

        const city_character& ch = cat.characters[ped.character];
        bool dead = ped.state == PED_STATE_DEAD;
        idx clip = ch.clips[dead ? CLIP_DEATH : CLIP_HIT];
        if (clip == (idx)-1 && dead) clip = ch.clips[CLIP_HIT];
        if (clip == (idx)-1) continue;

        // Dead holds the final pose rather than looping the clip - a corpse
        // that keeps performing its death animation on a loop is not lying
        // still, it is dancing.
        float duration = animator_duration(bank.source, clip);
        float t = ped.react_time;
        if (dead && duration > 0.0f && t > duration - 1e-3f) t = duration - 1e-3f;
        animator_sample(bank.source, clip, t, &p.reactor_pose[slot]);
    }
}

// ---- how busy the street is, by the clock ----
//
// A city with the same hundred and fifty people on its pavements at four in
// the morning as at nine is not a city with a day in it. This is the whole
// of that: a multiplier on the spawn cap, floored well above zero so the
// small hours are quiet rather than deserted.
static float city_crowd_factor(float hour) {
    if (hour < 5.0f)  return 0.16f + hour * 0.02f;              // the small hours
    if (hour < 8.0f)  return 0.26f + (hour - 5.0f) * 0.25f;     // the city getting up
    if (hour < 10.0f) return 1.00f;                             // the morning peak
    if (hour < 16.0f) return 0.78f;                             // the working day
    if (hour < 19.0f) return 1.00f;                             // and coming home again
    if (hour < 22.0f) return 0.62f;                             // the evening
    return 0.62f - (hour - 22.0f) * 0.20f;
}

// Why somebody who is out at this hour is out. Rush hour is people going
// somewhere; a Sunday afternoon is people walking about; the small hours are
// mostly the latter with nobody in a hurry.
static uint8_t city__ped_purpose(float hour, rng& r) {
    bool rush = (hour > 7.0f && hour < 10.0f) || (hour > 16.0f && hour < 19.5f);
    float roll = rng_float(r);
    if (rush)             return roll < 0.62f ? PED_COMMUTE : (roll < 0.88f ? PED_ERRAND : PED_STROLL);
    if (hour < 6.0f)      return roll < 0.30f ? PED_COMMUTE : PED_STROLL;
    return roll < 0.20f ? PED_COMMUTE : (roll < 0.68f ? PED_ERRAND : PED_STROLL);
}

// Somewhere to head for. Picked off the map's own list of walkable cells so
// it is always somewhere a person can actually stand, and at a distance that
// follows what they are out for.
static void city__ped_goal(city_ped& ped, const city_world& w, rng& r) {
    ped.goal_x = ped.node_x;
    ped.goal_z = ped.node_z;
    if (!w.walk_cell_count) return;

    int reach = (ped.purpose == PED_COMMUTE) ? PED_GOAL_FAR
              : (ped.purpose == PED_ERRAND)  ? PED_GOAL_NEAR : PED_GOAL_NEAR / 2;
    // A handful of draws rather than a search. The walkable list is thousands
    // of cells long and any of them at roughly the right distance will do -
    // what matters is that it is a fixed cost, since this runs inside the
    // threaded steering pass.
    for (int attempt = 0; attempt < 8; attempt++) {
        uint32_t cell = w.walk_cells[rng_u32(r) % (uint32_t)w.walk_cell_count];
        int cx = (int)(cell % CITY_CELLS), cz = (int)(cell / CITY_CELLS);
        int dx = cx - ped.node_x, dz = cz - ped.node_z;
        int d2 = dx * dx + dz * dz;
        if (d2 < 36 || d2 > reach * reach) continue;
        ped.goal_x = cx;
        ped.goal_z = cz;
        return;
    }
}

// How fast this person walks, before their own build is taken into account.
static float city__ped_pace(const city_ped& ped) {
    switch (ped.purpose) {
        case PED_COMMUTE: return 1.24f;
        case PED_STROLL:  return 0.78f;
        default:          return 1.0f;
    }
}

// Where inside a cell a person walks.
//
// Always to the *right* of the way they are going, never to a fixed side of
// the cell. That one sign is what separates a pavement carrying two flows
// into two lanes: everybody going one way hugs one edge and everybody coming
// back hugs the other, and the two streams stop walking through each other.
static vec3 city__ped_target(const city_world& w, const city_ped& ped) {
    int dx = ped.next_x - ped.node_x, dz = ped.next_z - ped.node_z;
    vec3 travel = v3norm(v3((float)dx, 0.0f, (float)dz));
    vec3 centre = city_stand_point(w, ped.next_x, ped.next_z);
    return v3add(centre, v3scale(dir_right(travel), ped.lateral));
}

// Is there a marked crossing leading out of this pavement cell in `dir`, and
// somewhere to land on the other side?
static bool city__crossing_available(const city_world& w, int x, int z, int dir, int* out_span) {
    int rx = x + DIR_DX[dir], rz = z + DIR_DZ[dir];
    if (!city_is_road(w, rx, rz)) return false;
    const city_cell& road = city_at(w, rx, rz);
    if (!(road.flags & (CELLF_CROSSING | CELLF_JUNCTION))) return false;

    // roads here are one cell wide, but a junction can be two cells across on
    // the diagonal, so walk out until pavement reappears
    for (int span = 2; span <= 3; span++) {
        int tx = x + DIR_DX[dir] * span, tz = z + DIR_DZ[dir] * span;
        if (city_is_walkable(w, tx, tz)) { *out_span = span; return true; }
        if (!city_is_road(w, tx, tz)) return false;
    }
    return false;
}

// The next cell, chosen to get nearer wherever this person is going.
//
// The weighting is the whole of the pedestrian's "intent": carrying straight
// on is cheap, doubling back is expensive, and every option is scored by how
// much nearer the destination it leaves them. The noise on top is what stops
// two people with the same destination walking the identical route.
static void city__ped_choose(city_ped& ped, const city_world& w, rng& r) {
    int back = -1;
    for (int d = 0; d < 4; d++)
        if (ped.node_x + DIR_DX[d] == ped.next_x && ped.node_z + DIR_DZ[d] == ped.next_z) {
            // `back` is where we came from relative to the cell we now stand on
            back = (d + 2) & 3;
        }

    ped.node_x = ped.next_x;
    ped.node_z = ped.next_z;
    ped.cross_dir = DIR_NONE;
    ped.state = PED_STATE_WALK;

    // Arrived. Stand about for a while, then find somewhere else to be.
    int gdx = ped.goal_x - ped.node_x, gdz = ped.goal_z - ped.node_z;
    if (gdx * gdx + gdz * gdz <= 2) {
        ped.state = PED_STATE_IDLE;
        ped.loiter = rng_range(r, PED_IDLE_MIN, PED_IDLE_MAX)
                   * (ped.purpose == PED_COMMUTE ? 0.35f : 1.0f);
        ped.next_x = ped.node_x;
        ped.next_z = ped.node_z;
        return;
    }

    // Stepping off the kerb, but only where there is a crossing to use and
    // only when the far side is actually the way they want to go - people do
    // not cross a road for the sake of crossing it.
    {
        int order = rng_int(r, 0, 3);
        for (int i = 0; i < 4; i++) {
            int d = (order + i) & 3;
            int span;
            if (DIR_DX[d] * gdx + DIR_DZ[d] * gdz <= 0) continue;    // wrong way
            if (!city__crossing_available(w, ped.node_x, ped.node_z, d, &span)) continue;
            ped.next_x = ped.node_x + DIR_DX[d] * span;
            ped.next_z = ped.node_z + DIR_DZ[d] * span;
            ped.cross_dir = d;
            ped.state = PED_STATE_WAIT;
            ped.look = PED_LOOK_TIME;
            ped.patience = 0.0f;
            return;
        }
    }

    int best = -1;
    float best_score = -1e30f;
    for (int d = 0; d < 4; d++) {
        int nx = ped.node_x + DIR_DX[d], nz = ped.node_z + DIR_DZ[d];
        if (!city_is_walkable(w, nx, nz)) continue;
        int ndx = ped.goal_x - nx, ndz = ped.goal_z - nz;
        // Manhattan distance, negated: the grid is four-connected, so it is
        // the honest metric here and it costs no square root.
        float score = -(float)(abs(ndx) + abs(ndz));
        if (d == back) score -= 6.0f;                    // turning back is a last resort
        score += rng_range(r, 0.0f, 1.6f);
        if (score > best_score) { best_score = score; best = d; }
    }

    if (best < 0) {                                       // boxed in; turn around
        ped.next_x = ped.node_x - DIR_DX[back < 0 ? 0 : back];
        ped.next_z = ped.node_z - DIR_DZ[back < 0 ? 0 : back];
        return;
    }
    ped.next_x = ped.node_x + DIR_DX[best];
    ped.next_z = ped.node_z + DIR_DZ[best];
}

// Where to run. The ordinary chooser prefers whichever neighbour is nearest
// the destination; this one prefers whichever puts the most ground between it
// and whatever just happened, which is the only difference between wandering
// and fleeing.
static void city__ped_flee_choose(city_ped& ped, const city_world& w, rng& r) {
    ped.node_x = ped.next_x;
    ped.node_z = ped.next_z;
    ped.cross_dir = DIR_NONE;

    int best = -1;
    float best_score = -1e30f;
    for (int d = 0; d < 4; d++) {
        int nx = ped.node_x + DIR_DX[d], nz = ped.node_z + DIR_DZ[d];
        if (!city_is_walkable(w, nx, nz)) continue;
        vec3 q = cell_centre(nx, nz);
        float dx = q.x - ped.threat.x, dz = q.z - ped.threat.z;
        // a little noise, so a crowd fleeing one punch does not become a
        // column of people running single file down the same pavement
        float score = dx * dx + dz * dz + rng_range(r, 0.0f, 60.0f);
        if (score > best_score) { best_score = score; best = d; }
    }
    if (best < 0) {                         // cornered: fall back to wandering
        city__ped_choose(ped, w, r);
        return;
    }
    ped.next_x = ped.node_x + DIR_DX[best];
    ped.next_z = ped.node_z + DIR_DZ[best];
}

static int city_peds_spawn(city_peds& p, const city_world& w, const city_catalog& cat,
                           phys_world& phys, int cx, int cz, float hour) {
    if (!cat.character_count || !city_is_walkable(w, cx, cz)) return -1;

    int slot = -1;
    for (size_t i = 0; i < MAX_PEDS; i++)
        if (!p.people[i].active) { slot = (int)i; break; }
    if (slot < 0) return -1;

    city_ped& ped = p.people[slot];
    memset(&ped, 0, sizeof(ped));
    ped.node_x = ped.next_x = cx;
    ped.node_z = ped.next_z = cz;
    ped.position = city_stand_point(w, cx, cz);
    // Always to the right of travel, never either side of the centreline -
    // see city__ped_target. The floor keeps somebody from walking the exact
    // centre of a two-way pavement and being pushed about by both flows.
    float spread = (SIDEWALK_WIDTH - KERB_STRIP) * 0.5f - 0.35f;
    if (spread < 0.15f) spread = 0.15f;
    ped.lateral = rng_range(p.random, spread * 0.25f, spread);
    ped.character = (uint8_t)(rng_u32(p.random) % cat.character_count);
    ped.material = (uint8_t)(rng_u32(p.random)
                             % (cat.characters[ped.character].material_count | 1u));
    ped.pose = (uint8_t)(rng_u32(p.random) % PED_WALK_POSES);
    ped.bank_pose = (uint8_t)(rng_u32(p.random) % (PED_IDLE_POSES * 2));
    // Adults are not all the same height, and a crowd of identical silhouettes
    // is the first thing that gives a generated one away. Kept narrow: this is
    // build variation, not a cast of giants and children.
    ped.height = rng_range(p.random, 0.92f, 1.08f);
    // Traits are rolled towards the middle rather than flat across 0..1: a
    // street where every third person is a maximally aggressive coward is not
    // a livelier street, it is a noisier one. Averaging two rolls gives a
    // crowd that is mostly ordinary with a real tail at both ends.
    ped.friendliness = (rng_range(p.random, 0.0f, 1.0f) + rng_range(p.random, 0.0f, 1.0f)) * 0.5f;
    ped.bravery      = (rng_range(p.random, 0.0f, 1.0f) + rng_range(p.random, 0.0f, 1.0f)) * 0.5f;
    ped.curiosity    = (rng_range(p.random, 0.0f, 1.0f) + rng_range(p.random, 0.0f, 1.0f)) * 0.5f;
    ped.aggression   = (rng_range(p.random, 0.0f, 1.0f) + rng_range(p.random, 0.0f, 1.0f)) * 0.5f;
    ped.lawfulness   = (rng_range(p.random, 0.0f, 1.0f) + rng_range(p.random, 0.0f, 1.0f)) * 0.5f;
    ped.purpose = city__ped_purpose(hour, p.random);
    // and the taller ones cover ground slightly faster
    ped.speed = PED_WALK_SPEED * ped.height * city__ped_pace(ped)
              * rng_range(p.random, 0.88f, 1.14f);
    ped.cross_dir = DIR_NONE;
    ped.partner = -1;
    ped.active = true;
    ped.health = PED_MAX_HEALTH;
    ped.reactor = -1;
    city__ped_goal(ped, w, p.random);
    city__ped_choose(ped, w, p.random);

    phys_body b = {};
    b.shape = PHYS_CYLINDER;
    b.position = ped.position;
    b.radius = 0.34f;
    b.height = PLAYER_HEIGHT;
    b.inv_mass = 1.0f / 75.0f;
    b.restitution = 0.0f;
    b.drag = 6.0f;
    // Enough grip that being clipped by a wing mirror drags somebody along
    // rather than firing them off sideways at the speed they were walking.
    b.friction = 0.45f;
    b.group = PHYS_LAYER_PED;
    b.collides = PHYS_LAYER_ALL;
    b.gravity = true;
    ped.body = phys_add_body(phys, b);
    if (ped.body == (idx)-1) { ped.active = false; return -1; }

    p.count++;
    return slot;
}

// Claims an animation slot for a stagger or a death.
//
// The pool is small and a corpse holds its slot for as long as it lies there,
// which is the whole difficulty: once twenty-four people have died near the
// player, the twenty-fifth has nothing left to fall over with and stands
// bolt upright instead. That is exactly the bug this recycling exists to
// stop - if nothing is free, the slot is taken back off the pedestrian who
// has been dead longest, and that one topples over instead (see
// city_ped::topple), which is a far better answer than a corpse standing to
// attention in the middle of the road.
static int city__reactor_take(city_peds& p, int owner) {
    for (int i = 0; i < MAX_PED_REACTORS; i++)
        if (!p.reactor_used[i]) {
            p.reactor_used[i] = true;
            p.reactor_owner[i] = owner;
            return i;
        }

    int steal = -1;
    float oldest = 0.0f;
    for (int i = 0; i < MAX_PED_REACTORS; i++) {
        int who = p.reactor_owner[i];
        if (who < 0 || (size_t)who >= MAX_PEDS) continue;
        const city_ped& other = p.people[who];
        if (!other.active || other.state != PED_STATE_DEAD) continue;
        if (other.dead_time > oldest) { oldest = other.dead_time; steal = i; }
    }
    if (steal < 0) return -1;             // everything is mid-stagger; wait for one

    city_ped& victim = p.people[p.reactor_owner[steal]];
    victim.reactor = -1;
    victim.topple = 1.0f;                 // already down; stay down without a slot
    p.reactor_owner[steal] = owner;
    return steal;
}

static void city__reactor_free(city_peds& p, int& slot) {
    if (slot >= 0 && slot < MAX_PED_REACTORS) {
        p.reactor_used[slot] = false;
        p.reactor_owner[slot] = -1;
    }
    slot = -1;
}

// A conversation ends the moment either side stops being able to hold one.
static void city__chat_end(city_peds& p, int index) {
    if (index < 0 || (size_t)index >= MAX_PEDS) return;
    city_ped& ped = p.people[index];
    int other = ped.partner;
    ped.partner = -1;
    ped.chat = 0.0f;
    if (ped.state == PED_STATE_CHAT) {
        ped.state = PED_STATE_IDLE;
        ped.loiter = 0.6f;
    }
    if (other >= 0 && (size_t)other < MAX_PEDS && p.people[other].partner == index) {
        p.people[other].partner = -1;
        p.people[other].chat = 0.0f;
        if (p.people[other].state == PED_STATE_CHAT) {
            p.people[other].state = PED_STATE_IDLE;
            p.people[other].loiter = 0.6f;
        }
    }
}

// The one entry point anything outside this file hits a pedestrian through -
// a punch landing, a car's bumper, a bullet. `impulse` is added straight to
// the body's velocity (so a fast car launches someone further than a fist
// does, for free, just from the numbers each already hands in), and `damage`
// comes off health; at zero the stagger becomes the last one.
//
// Already-dead pedestrians never take another hit - there is nothing left
// to launch harder or kill twice - and one still inside its invulnerability
// window from the last hit ignores this one too, so a car resting on a body,
// or a fist's several overlapping frames, cannot fire the reaction repeatedly.
static void city_ped_hit(city_peds& p, const city_catalog& cat, phys_world& phys,
                         int index, vec3 impulse, float damage, vec3 from) {
    if (index < 0 || (size_t)index >= MAX_PEDS) return;
    city_ped& ped = p.people[index];
    if (!ped.active || ped.state == PED_STATE_DEAD || ped.invuln > 0.0f) return;
    if (ped.character >= cat.character_count) return;

    city__chat_end(p, index);

    phys_body* b = phys_get_body(phys, ped.body);
    if (b) b->velocity = v3add(b->velocity, impulse);

    ped.health -= damage;
    ped.invuln = PED_HIT_INVULN;
    ped.threat = from;

    bool dead = ped.health <= 0.0f;
    const city_character& ch = cat.characters[ped.character];
    // A death with no death clip falls back to the hit stagger, because a
    // body that at least crumples is far closer to right than one that keeps
    // its walk pose - and if there is no stagger either, city_ped::topple
    // below tips it over with no animation at all. Something always falls.
    idx clip = ch.clips[dead ? CLIP_DEATH : CLIP_HIT];
    if (clip == (idx)-1 && dead) clip = ch.clips[CLIP_HIT];

    if (dead) {
        ped.state = PED_STATE_DEAD;
        ped.dead_time = 0.0f;
        ped.react_time = 0.0f;
        ped.reactor = city__reactor_take(p, index);
        // No clip and no slot: go down the only way left, by tipping over.
        if (clip == (idx)-1 || ped.reactor < 0) {
            city__reactor_free(p, ped.reactor);
            ped.topple = ped.topple > 0.0f ? ped.topple : 0.001f;
        }
        return;
    }

    // Alive, and now frightened. Which way this person jumps was decided the
    // first time they were touched and is theirs from then on - but it is
    // their own nerve that decides it, not a flat roll every pedestrian in the
    // city shares. A timid person almost always runs and an aggressive one
    // almost always swings back, which is what makes hitting two different
    // people in the same street play out two different ways.
    if (ped.alarm <= 0.0f) {
        float nerve = ped.bravery * 0.65f + ped.aggression * 0.35f;
        ped.brave = rng_chance(p.random, PED_FIGHT_CHANCE * 0.35f + nerve * 0.7f);
    }
    ped.alarm = ped.brave ? PED_FIGHT_TIME : PED_FLEE_TIME;
    ped.swing = PED_SWING_PERIOD * 0.5f;

    if (clip != (idx)-1) {
        if (ped.reactor < 0) ped.reactor = city__reactor_take(p, index);
        ped.react_time = 0.0f;
        ped.state = PED_STATE_HIT;      // the stagger plays first; the AI takes over after
    } else {
        ped.state = ped.brave ? PED_STATE_FIGHT : PED_STATE_FLEE;
    }
}

static void city_peds_alarm(city_peds& p, vec3 at, float radius) {
    float r2 = radius * radius;
    for (size_t i = 0; i < MAX_PEDS; i++) {
        city_ped& ped = p.people[i];
        if (!ped.active) continue;
        if (ped.state == PED_STATE_DEAD || ped.state == PED_STATE_HIT) continue;
        if (ped.state == PED_STATE_FIGHT) continue;      // already in it
        float dx = ped.position.x - at.x, dz = ped.position.z - at.z;
        if (dx * dx + dz * dz > r2) continue;
        // A bystander never piles in. Squaring up is for the person who was
        // actually hit - a whole street deciding to join a fight is a riot,
        // and a riot every time the player throws one punch is not a city.
        //
        // What a bystander does choose is whether to run or to stand and
        // watch. Somebody with the nerve for it and an interest in what is
        // going on stops and looks; everybody else gets out of the way. That
        // is what turns one punch into a scene - a ring of people watching and
        // the rest scattering past them - rather than into a uniform stampede.
        city__chat_end(p, (int)i);
        ped.threat = at;
        ped.brave = false;

        float nosy = ped.curiosity * 0.6f + ped.bravery * 0.4f;
        float dist = sqrtf(dx * dx + dz * dz);
        // Never stop to watch from the middle of the road. Somebody halfway
        // across a junction has one job, and standing in live traffic to
        // rubberneck would get them run over by the system that is supposed
        // to be yielding to them.
        bool in_road = ped.state == PED_STATE_CROSS || ped.cross_dir != DIR_NONE;
        bool watches = !in_road && nosy > PED_ONLOOKER_NERVE && dist > radius * 0.4f
                       && rng_chance(p.random, 0.65f);
        if (watches) {
            // Turn to face it and hold still for a while. Standing at the
            // back of the crowd, not walking into the middle of it.
            ped.yaw = yaw_from_forward(v3norm(v3(at.x - ped.position.x, 0.0f,
                                                 at.z - ped.position.z)));
            ped.state = PED_STATE_IDLE;
            ped.loiter = rng_range(p.random, PED_WATCH_MIN, PED_WATCH_MAX);
            ped.alarm = 0.0f;
        } else {
            ped.alarm = PED_FLEE_TIME * rng_range(p.random, 0.6f, 1.0f);
            ped.state = PED_STATE_FLEE;
        }
    }
}

// The other half of being frightened: not one loud event, but a person who
// can see something about to happen to them.
//
// The cone is what makes it read as being aimed at rather than as an area
// effect - somebody behind the player carries on walking past, and steps into
// it only when they come round in front. And the reaction splits: a timid
// person runs, a steadier one stops dead and stares, which is the difference
// between a street clearing and a street holding its breath.
static void city_peds_menace(city_peds& p, vec3 at, vec3 facing, float radius) {
    float r2 = radius * radius;
    for (size_t i = 0; i < MAX_PEDS; i++) {
        city_ped& ped = p.people[i];
        if (!ped.active) continue;
        if (ped.state == PED_STATE_DEAD || ped.state == PED_STATE_HIT) continue;
        if (ped.state == PED_STATE_FLEE || ped.state == PED_STATE_FIGHT) continue;
        // Somebody halfway across a junction finishes crossing. Freezing in
        // live traffic because they saw a gun would kill them with the system
        // that is supposed to be yielding to them, which is not the reaction
        // anybody is looking for here.
        if (ped.state == PED_STATE_CROSS || ped.cross_dir != DIR_NONE) continue;

        vec3 to = v3sub(ped.position, at);
        to.y = 0.0f;
        float d2 = v3dot(to, to);
        if (d2 > r2 || d2 < 1e-4f) continue;
        float d = sqrtf(d2);
        // Roughly the front third of a circle: what a levelled barrel covers,
        // not what a person can see.
        if (v3dot(v3scale(to, 1.0f / d), facing) < 0.55f) continue;

        // Nearer is worse, and nerve is what is being spent against it.
        float pressure = 1.0f - d / radius;
        float nerve = ped.bravery * 0.6f + ped.aggression * 0.4f;
        if (nerve > pressure + 0.35f) continue;      // unimpressed; carries on

        city__chat_end(p, (int)i);
        ped.threat = at;
        ped.brave = false;
        ped.yaw = yaw_from_forward(v3norm(v3(at.x - ped.position.x, 0.0f,
                                             at.z - ped.position.z)));
        if (nerve < pressure * 0.6f) {
            ped.alarm = PED_FLEE_TIME * rng_range(p.random, 0.5f, 0.9f);
            ped.state = PED_STATE_FLEE;
        } else {
            // Frozen, facing it. Held only briefly, and re-applied for as long
            // as the gun stays up, so lowering it lets the street move again.
            ped.state = PED_STATE_IDLE;
            if (ped.loiter < 1.2f) ped.loiter = 1.2f;
            ped.alarm = 0.0f;
        }
    }
}

// ---- the threaded half ----

// Everybody close and roughly in front, turned into one sideways nudge.
//
// The rule is the one people actually use on a pavement: you do not steer
// away from somebody beside you or behind you, only from somebody you are
// about to walk into, and you go round them rather than stopping. Which side
// is chosen by which side they are already on, so two people walking at each
// other resolve in opposite directions instead of mirroring each other into
// a deadlock.
static vec3 city__ped_avoid(const phys_world& phys, const city_ped& ped, vec3 heading) {
    idx near_by[PED_AVOID_MAX];
    size_t n = phys_query_neighbours(phys, ped.position, PED_AVOID_RANGE,
                                     near_by, PED_AVOID_MAX);
    vec3 push = v3(0.0f, 0.0f, 0.0f);
    for (size_t k = 0; k < n; k++) {
        const phys_body& b = phys.bodies[near_by[k]];
        if (near_by[k] == ped.body) continue;
        if (!(b.group & (PHYS_LAYER_PED | PHYS_LAYER_PLAYER | PHYS_LAYER_VEHICLE))) continue;

        vec3 to = v3sub(b.position, ped.position);
        to.y = 0.0f;
        float d = v3len(to);
        if (d < 1e-3f || d > PED_AVOID_RANGE) continue;
        vec3 dir = v3scale(to, 1.0f / d);
        float ahead = v3dot(dir, heading);
        if (ahead < 0.35f) continue;                 // beside or behind; not in the way

        // A car is worth a much wider berth than a person is.
        float weight = (b.group & PHYS_LAYER_VEHICLE) ? 3.0f : 1.0f;
        vec3 side = dir_right(heading);
        float which = v3dot(dir, side) >= 0.0f ? -1.0f : 1.0f;   // go round the far side
        float strength = weight * ahead * (1.0f - d / PED_AVOID_RANGE);
        push = v3add(push, v3scale(side, which * strength));
    }
    return v3scale(push, PED_AVOID_FORCE);
}

// Is anything driving at the stretch of road about to be walked across?
//
// Asked of the physics broadphase rather than of the traffic list, so it is
// answered the same way for an AI car, the player's car and anything else on
// four wheels that turns up later.
static bool city__road_busy(const phys_world& phys, vec3 kerb, vec3 across) {
    idx near_by[24];
    // Watched a good way up the road, because what matters is not what is
    // beside the crossing now but what will be in it shortly.
    size_t n = phys_query_neighbours(phys, v3add(kerb, v3scale(across, 3.0f)),
                                     PED_CAR_WATCH, near_by, 24);
    for (size_t k = 0; k < n; k++) {
        const phys_body& b = phys.bodies[near_by[k]];
        if (!(b.group & PHYS_LAYER_VEHICLE)) continue;
        float speed = sqrtf(b.velocity.x * b.velocity.x + b.velocity.z * b.velocity.z);
        if (speed < PED_CAR_MOVING) continue;             // stopped at the light, or parked

        // Only a car that is coming *this* way. One driving away up the same
        // street is no reason to stand on a kerb.
        vec3 to = v3sub(kerb, b.position);
        to.y = 0.0f;
        float d = v3len(to);
        if (d > PED_CAR_WATCH || d < 1e-3f) continue;
        if (v3dot(v3scale(to, 1.0f / d), v3norm(v3(b.velocity.x, 0.0f, b.velocity.z))) < 0.35f)
            continue;
        return true;
    }
    return false;
}

// One person's steering, for one frame. Everything it writes belongs to that
// one person: their own struct and their own physics body. That is the whole
// reason this can be handed to a worker thread, and the reason nothing in it
// spawns, despawns, claims an animation slot or touches anybody else.
static void city__ped_steer(city_peds& p, city_ped& ped, const city_world& w,
                            const phys_world& phys, phys_body& body,
                            vec3 focus, float time, float dt, rng& r) {
    // ---- frightened ----
    //
    // Both of these run on the same clock: when it expires the person goes
    // back to whatever they were doing, which is what stops a city that has
    // seen one punch from sprinting for the rest of its life.
    if (ped.state == PED_STATE_FLEE || ped.state == PED_STATE_FIGHT) {
        ped.alarm -= dt;
        if (ped.alarm <= 0.0f) {
            ped.state = PED_STATE_WALK;
            city__ped_goal(ped, w, r);
            city__ped_choose(ped, w, r);
            return;
        }
    }

    if (ped.state == PED_STATE_FIGHT) {
        // Close on whoever is being fought and swing at them. `focus` is the
        // point the whole crowd is simulated around, which is the player -
        // the one thing in the world worth fighting.
        vec3 to = v3sub(focus, ped.position);
        to.y = 0.0f;
        float distance = v3len(to);
        vec3 dir = distance > 1e-3f ? v3scale(to, 1.0f / distance) : forward_from_yaw(ped.yaw);
        ped.yaw = yaw_from_forward(dir);

        if (distance > PED_FIGHT_RANGE) {
            body.velocity.x = dir.x * PED_FLEE_SPEED * 0.75f;
            body.velocity.z = dir.z * PED_FLEE_SPEED * 0.75f;
        } else {
            body.velocity.x = 0.0f;
            body.velocity.z = 0.0f;
            ped.swing -= dt;
            if (ped.swing <= 0.0f) {
                ped.swing = PED_SWING_PERIOD;
                ped.struck = true;     // city_game turns this into a shove
            }
        }
        // Lost them: give up and get away instead of chasing across the map.
        if (distance > PED_CHASE_RANGE) {
            ped.threat = focus;
            ped.state = PED_STATE_FLEE;
            city__ped_flee_choose(ped, w, r);
        }
        return;
    }

    if (ped.state == PED_STATE_FLEE) {
        vec3 target = city__ped_target(w, ped);
        vec3 to = v3sub(target, ped.position);
        to.y = 0.0f;
        float distance = v3len(to);
        if (distance < 0.9f) {
            city__ped_flee_choose(ped, w, r);
            target = city__ped_target(w, ped);
            to = v3sub(target, ped.position);
            to.y = 0.0f;
            distance = v3len(to);
        }
        vec3 dir = distance > 1e-3f ? v3scale(to, 1.0f / distance) : forward_from_yaw(ped.yaw);
        body.velocity.x = dir.x * PED_FLEE_SPEED;
        body.velocity.z = dir.z * PED_FLEE_SPEED;
        ped.yaw = yaw_from_forward(dir);

        if (body.touched) ped.stuck += dt; else ped.stuck = 0.0f;
        if (ped.stuck > 1.0f) { city__ped_flee_choose(ped, w, r); ped.stuck = 0.0f; }
        return;
    }

    // In conversation with the player: city_dialogue has already turned this
    // ped to face them and owns its state entirely until the chat ends - no
    // steering, no route, just standing still and holding a talk pose.
    if (ped.state == PED_STATE_TALK) {
        body.velocity.x = 0.0f;
        body.velocity.z = 0.0f;
        return;
    }

    // Two people who stopped near each other. Facing is set here rather than
    // by the pairing pass so it keeps tracking if either of them is shoved.
    if (ped.state == PED_STATE_CHAT) {
        body.velocity.x = 0.0f;
        body.velocity.z = 0.0f;
        if (ped.partner >= 0 && (size_t)ped.partner < MAX_PEDS) {
            vec3 to = v3sub(p.people[ped.partner].position, ped.position);
            to.y = 0.0f;
            if (v3dot(to, to) > 1e-4f) ped.yaw = damp_angle(ped.yaw, yaw_from_forward(v3norm(to)), 6.0f, dt);
        }
        ped.chat -= dt;
        return;                       // the maintenance pass ends it, since it ends it for two
    }

    // Standing about at the end of a trip. The clock is the whole of it; when
    // it runs out they find somewhere new to be.
    if (ped.state == PED_STATE_IDLE) {
        body.velocity.x = damp(body.velocity.x, 0.0f, 12.0f, dt);
        body.velocity.z = damp(body.velocity.z, 0.0f, 12.0f, dt);
        ped.loiter -= dt;
        if (ped.loiter <= 0.0f) {
            city__ped_goal(ped, w, r);
            city__ped_choose(ped, w, r);
        }
        return;
    }

    // ---- on the kerb ----
    //
    // Three separate reasons to stay put, checked in the order a person
    // checks them: the light, the look, and the car that is coming anyway.
    if (ped.state == PED_STATE_WAIT) {
        body.velocity.x = damp(body.velocity.x, 0.0f, 14.0f, dt);
        body.velocity.z = damp(body.velocity.z, 0.0f, 14.0f, dt);
        ped.patience += dt;

        // Face the way they are about to go, so a kerb full of people is a
        // queue looking across the road rather than a huddle facing anywhere.
        if (ped.cross_dir != DIR_NONE)
            ped.yaw = damp_angle(ped.yaw, yaw_from_forward(dir_to_vec(ped.cross_dir)), 5.0f, dt);

        int traffic_axis = (ped.cross_dir == DIR_PX || ped.cross_dir == DIR_NX) ? 1 : 0;
        bool green_for_traffic = traffic_axis_green(time, traffic_axis);
        // Long enough on the kerb with the road to themselves, and they go
        // anyway. People jaywalk - and how long "long enough" is depends on
        // the person. Somebody law-abiding stands there until the light gives
        // way; somebody who is not steps off the kerb almost at once. Every
        // pedestrian used to share one patience, so a kerb either held or
        // emptied all together.
        bool impatient = ped.patience > PED_PATIENCE * (0.35f + ped.lawfulness * 1.5f);
        if (green_for_traffic && !impatient) { ped.look = PED_LOOK_TIME; return; }

        if (ped.look > 0.0f) { ped.look -= dt; return; }   // looking first

        vec3 across = ped.cross_dir != DIR_NONE ? dir_to_vec(ped.cross_dir)
                                                : forward_from_yaw(ped.yaw);
        if (city__road_busy(phys, ped.position, across)) {
            ped.patience += dt;      // a car running the light is worth losing patience over
            return;
        }
        ped.state = PED_STATE_CROSS;
    }

    vec3 target = city__ped_target(w, ped);
    vec3 to = v3sub(target, ped.position);
    to.y = 0.0f;
    float distance = v3len(to);

    if (distance < 0.9f) {
        city__ped_choose(ped, w, r);
        if (ped.state != PED_STATE_WALK && ped.state != PED_STATE_CROSS) return;
        target = city__ped_target(w, ped);
        to = v3sub(target, ped.position);
        to.y = 0.0f;
        distance = v3len(to);
    }

    vec3 dir = distance > 1e-3f ? v3scale(to, 1.0f / distance) : forward_from_yaw(ped.yaw);
    float speed = ped.speed > 0.01f ? ped.speed : PED_WALK_SPEED;
    bool crossing = ped.state == PED_STATE_CROSS;
    if (crossing) {
        speed = PED_CROSS_SPEED;
        // Halfway across with something bearing down: stop dithering and run.
        if (city__road_busy(phys, ped.position, dir)) speed = PED_FLEE_SPEED * 0.8f;
    }

    // Weaving round whoever is in the way. Applied to the direction rather
    // than to the velocity so it never makes anybody faster - a crowd where
    // giving way is a speed boost drifts apart within seconds.
    vec3 steer = v3add(dir, city__ped_avoid(phys, ped, dir));
    float len = v3len(steer);
    if (len > 1e-3f) dir = v3scale(steer, 1.0f / len);

    body.velocity.x = dir.x * speed;
    body.velocity.z = dir.z * speed;
    ped.yaw = damp_angle(ped.yaw, yaw_from_forward(dir), 9.0f, dt);

    // Shoved into a wall by a car, or wedged in a doorway: re-pick a route
    // rather than grinding against geometry forever.
    if (body.touched) ped.stuck += dt; else ped.stuck = 0.0f;
    if (ped.stuck > 1.5f) {
        city__ped_choose(ped, w, r);
        ped.stuck = 0.0f;
    }
}

static void city__peds_steer_job(void* user, size_t begin, size_t end, int worker) {
    city_peds& p = *(city_peds*)user;
    rng& r = p.worker_random[worker < PED_MAX_THREADS ? worker : 0];
    for (size_t i = begin; i < end; i++) {
        city_ped& ped = p.people[i];
        if (!ped.steerable) continue;
        phys_body* body = phys_get_body(*p.job_phys_rw, ped.body);
        if (!body) continue;
        city__ped_steer(p, ped, *p.job_world, *p.job_phys, *body,
                        p.job_focus, p.job_time, p.job_dt, r);
    }
}

// ---- the serial half ----

// Timers, despawns, animation slots and everything else that either touches a
// shared pool or touches two people at once.
static void city__peds_maintain(city_peds& p, const city_world& w, const city_catalog& cat,
                                phys_world& phys, vec3 focus, float dt) {
    for (size_t i = 0; i < MAX_PEDS; i++) {
        city_ped& ped = p.people[i];
        ped.steerable = false;
        if (!ped.active) continue;

        if (ped.invuln > 0.0f) ped.invuln -= dt;

        phys_body* body = phys_get_body(phys, ped.body);
        if (!body) {
            city__chat_end(p, (int)i);
            city__reactor_free(p, ped.reactor);
            ped.active = false;
            if (p.count) p.count--;
            continue;
        }
        ped.position = body->position;

        float dx = ped.position.x - focus.x, dz = ped.position.z - focus.z;
        if (dx * dx + dz * dz > (PED_SPAWN_RADIUS * 1.4f) * (PED_SPAWN_RADIUS * 1.4f)) {
            city__chat_end(p, (int)i);
            city__reactor_free(p, ped.reactor);
            phys_remove_body(phys, ped.body);
            ped.active = false;
            if (p.count) p.count--;
            continue;
        }

        // Hit or dead: no steering, no destination - just the reaction clip
        // playing out on the ped's own clock. A stagger hands the walking AI
        // back once its clip ends; a death holds its last frame - the body
        // stays a physics object (drag and gravity still settle it, a car can
        // still shove it) but never asks for a route again.
        if (ped.state == PED_STATE_HIT || ped.state == PED_STATE_DEAD) {
            const city_pose_bank& bank = p.banks[ped.character];
            const city_character& ch = cat.characters[ped.character];
            bool dead = ped.state == PED_STATE_DEAD;
            idx clip = ch.clips[dead ? CLIP_DEATH : CLIP_HIT];
            if (clip == (idx)-1 && dead) clip = ch.clips[CLIP_HIT];
            float duration = bank.ready && clip != (idx)-1
                ? animator_duration(bank.source, clip) : 0.5f;

            if (dead) {
                ped.dead_time += dt;
                // Whatever happened to its animation slot, a body goes down:
                // toppling runs to one over about half a second and the draw
                // pass lays the model on its side by that much.
                if (ped.reactor < 0 && ped.topple > 0.0f)
                    ped.topple = ped.topple + dt * 2.0f > 1.0f ? 1.0f : ped.topple + dt * 2.0f;
                // and is cleared away, which is what gives its slot back
                if (ped.dead_time > PED_CORPSE_LIFETIME) {
                    city__reactor_free(p, ped.reactor);
                    phys_remove_body(phys, ped.body);
                    ped.active = false;
                    if (p.count) p.count--;
                    continue;
                }
            } else if (ped.react_time >= duration) {
                // The stagger is over. Whether they run or square up was
                // settled when the blow landed - see city_ped_hit.
                city__reactor_free(p, ped.reactor);
                ped.state = ped.alarm > 0.0f
                    ? (ped.brave ? PED_STATE_FIGHT : PED_STATE_FLEE)
                    : PED_STATE_WALK;
                if (ped.state == PED_STATE_FLEE) city__ped_flee_choose(ped, w, p.random);
                continue;
            }

            // The clock a stagger ends on has to run whether or not this
            // pedestrian ever got an animation slot to play it with -
            // advancing it only when one was free left everybody who missed
            // out frozen mid-flinch for good, which is precisely what a crowd
            // looks like after more people are hit at once than the pool can
            // animate. The sampling itself is city_peds_animate's job now.
            ped.react_time += dt;
            continue;
        }

        // A conversation is two people's state, so it can only be ended from
        // out here - which is also why the steering pass only ever runs its
        // clock down and never acts on it reaching zero.
        if (ped.state == PED_STATE_CHAT && ped.chat <= 0.0f) {
            city__chat_end(p, (int)i);
            continue;
        }

        ped.steerable = true;
    }
}

// Fear is catching.
//
// A street does not empty because everyone in it saw the same thing; it empties
// because a few people saw it and everybody else saw *them*. So panic spreads
// person to person here rather than being handed out in one radius by whatever
// caused it: somebody already running is a reason to run, the chance of taking
// it is what nerve the onlooker has, and a scare that started with one person
// twenty metres away arrives second hand a moment later.
//
// It is also what stops a panic from being permanent - the infection rate is
// per second and the alarm it passes on is shorter than the one it caught, so
// each remove of the chain calms down sooner than the last and the street
// settles instead of running forever.
#define PED_PANIC_RANGE   7.0f
#define PED_PANIC_RATE    0.9f    // per second, at zero nerve, from one neighbour

static void city__spread_panic(city_peds& p, float dt) {
    for (size_t i = 0; i < MAX_PEDS; i++) {
        const city_ped& runner = p.people[i];
        if (!runner.active || runner.state != PED_STATE_FLEE) continue;
        // Somebody at the tail end of their own scare is not frightening
        // anybody; without this the panic sustains itself indefinitely.
        if (runner.alarm < PED_FLEE_TIME * 0.35f) continue;

        for (size_t j = 0; j < MAX_PEDS; j++) {
            if (j == i) continue;
            city_ped& other = p.people[j];
            if (!other.active) continue;
            if (other.state != PED_STATE_WALK && other.state != PED_STATE_IDLE
                && other.state != PED_STATE_CHAT) continue;
            float dx = other.position.x - runner.position.x;
            float dz = other.position.z - runner.position.z;
            if (dx * dx + dz * dz > PED_PANIC_RANGE * PED_PANIC_RANGE) continue;

            float nerve = other.bravery * 0.7f + other.lawfulness * 0.3f;
            if (!rng_chance(p.random, PED_PANIC_RATE * (1.0f - nerve) * dt)) continue;

            city__chat_end(p, (int)j);
            other.threat = runner.threat;
            other.brave = false;
            other.alarm = runner.alarm * rng_range(p.random, 0.5f, 0.85f);
            other.state = PED_STATE_FLEE;
        }
    }
}

// Two people standing near each other fall into conversation.
//
// Pairing is the one thing a pedestrian does that writes to somebody else, so
// it lives out here on the calling thread. It is also a quadratic scan over
// the idlers only, which is a handful of people rather than the whole crowd.
static void city__pair_chats(city_peds& p) {
    for (size_t i = 0; i < MAX_PEDS; i++) {
        city_ped& a = p.people[i];
        if (!a.active || a.state != PED_STATE_IDLE || a.partner >= 0) continue;
        // Not the instant they stop - somebody who stops walking and starts
        // talking in the same frame reads as scripted.
        if (a.loiter > PED_IDLE_MAX - 1.2f) continue;

        for (size_t j = i + 1; j < MAX_PEDS; j++) {
            city_ped& b = p.people[j];
            if (!b.active || b.state != PED_STATE_IDLE || b.partner >= 0) continue;
            float dx = a.position.x - b.position.x, dz = a.position.z - b.position.z;
            if (dx * dx + dz * dz > PED_CHAT_RANGE * PED_CHAT_RANGE) continue;
            // Takes two to start one, so it is the pair's warmth that decides,
            // not either person's - two reserved people standing near each
            // other go on ignoring each other, which is what they would do.
            if (!rng_chance(p.random, 0.12f + a.friendliness * b.friendliness * 0.65f)) continue;

            // and warm people stay talking longer
            float sociable = (a.friendliness + b.friendliness) * 0.5f;
            float length = rng_range(p.random, PED_CHAT_MIN, PED_CHAT_MAX)
                         * (0.7f + sociable * 0.6f);
            a.state = b.state = PED_STATE_CHAT;
            a.chat = b.chat = length;
            a.partner = (int)j;
            b.partner = (int)i;
            break;
        }
    }
}

static void city_peds_update(city_peds& p, const city_world& w, const city_catalog& cat,
                             phys_world& phys, vec3 focus, float time, float hour, float dt,
                             pix_jobs* jobs) {
    // The city forgets. Without this a single punch would sour every
    // conversation for the rest of the session, which is a grudge, not a
    // reputation - roughly three minutes back to neutral from either end.
    if (p.reputation > 0.0f) {
        p.reputation -= dt * PED_REPUTATION_DECAY;
        if (p.reputation < 0.0f) p.reputation = 0.0f;
    } else if (p.reputation < 0.0f) {
        p.reputation += dt * PED_REPUTATION_DECAY;
        if (p.reputation > 0.0f) p.reputation = 0.0f;
    }

    city__peds_maintain(p, w, cat, phys, focus, dt);

    p.job_world = &w;
    p.job_phys = &phys;
    p.job_phys_rw = &phys;
    p.job_time = time;
    p.job_dt = dt;
    p.job_focus = focus;
    if (jobs) pix_parallel_for(*jobs, MAX_PEDS, city__peds_steer_job, &p, 12);
    else      city__peds_steer_job(&p, 0, MAX_PEDS, 0);

    city__pair_chats(p);
    city__spread_panic(p, dt);

    // Keep the crowd topped up around the player, at whatever size the hour
    // of the day calls for. Spawning takes a physics body out of a shared
    // pool, so like everything else that does, it stays out here.
    size_t want = (size_t)(MAX_PEDS * city_crowd_factor(hour));
    if (want > MAX_PEDS) want = MAX_PEDS;
    if (w.walk_cell_count) {
        for (int attempt = 0; attempt < 5 && p.count < want; attempt++) {
            uint32_t pick = rng_u32(p.random) % (uint32_t)w.walk_cell_count;
            uint32_t cell = w.walk_cells[pick];
            int cx = (int)(cell % CITY_CELLS), cz = (int)(cell / CITY_CELLS);
            vec3 q = cell_centre(cx, cz);
            float dx = q.x - focus.x, dz = q.z - focus.z;
            float d = sqrtf(dx * dx + dz * dz);
            if (d < 30.0f || d > PED_SPAWN_RADIUS) continue;
            city_peds_spawn(p, w, cat, phys, cx, cz, hour);
        }
    }
}

static void city_peds_draw(const city_peds& p, const city_catalog& cat, pix_renderer& renderer,
                           const frustum& view, vec3 eye) {
    for (size_t i = 0; i < MAX_PEDS; i++) {
        const city_ped& ped = p.people[i];
        if (!ped.active || ped.character >= p.bank_count) continue;

        const city_pose_bank& bank = p.banks[ped.character];
        const city_character& ch = cat.characters[ped.character];
        if (!bank.ready || !ch.ok) continue;

        float dx = ped.position.x - eye.x, dz = ped.position.z - eye.z;
        if (dx * dx + dz * dz > ANIMATED_DISTANCE * ANIMATED_DISTANCE) continue;
        if (!frustum_test_sphere(view, v3(ped.position.x, ped.position.y + 0.9f, ped.position.z), 1.4f))
            continue;

        // Standing at a kerb waiting for the lights is an idle, not a walk -
        // and which idle is picked per person, so a queue is a group of people
        // rather than one person rendered six times. Hit and dead are each
        // one pedestrian's own reactor pose, sampled on its own clock by
        // city_peds_animate - never the shared bank, which only ever holds
        // looping locomotion.
        const animation* pose;
        if ((ped.state == PED_STATE_HIT || ped.state == PED_STATE_DEAD) && ped.reactor >= 0) {
            pose = &p.reactor_pose[ped.reactor];
        } else if (ped.state == PED_STATE_WAIT || ped.state == PED_STATE_IDLE) {
            int slot = ped.bank_pose % PED_IDLE_POSES;
            pose = (ped.bank_pose < PED_IDLE_POSES) ? &bank.idle[slot] : &bank.stand[slot];
        } else if (ped.state == PED_STATE_TALK || ped.state == PED_STATE_CHAT) {
            // The rig's talk clip specifically, not whichever standing pose
            // this ped happened to be holding at a kerb - see CLIP_TALK.
            pose = &bank.stand[ped.bank_pose % PED_IDLE_POSES];
        } else if (ped.state == PED_STATE_FLEE) {
            pose = &bank.run[ped.pose % PED_RUN_POSES];
        } else if (ped.state == PED_STATE_FIGHT) {
            // Swinging for the first part of every cycle, guard up for the
            // rest, so a fighter reads as throwing punches rather than as
            // one continuous windmill.
            pose = (ped.swing > PED_SWING_PERIOD * 0.55f)
                 ? &bank.punch[ped.pose % PED_PUNCH_POSES]
                 : &bank.stand[ped.bank_pose % PED_IDLE_POSES];
        } else if (ped.state == PED_STATE_DEAD) {
            // No death clip for this rig and no reactor to show one anyway -
            // hold an idle, which the topple below lays on its side.
            pose = &bank.idle[ped.bank_pose % PED_IDLE_POSES];
        } else {
            pose = &bank.walk[ped.pose % PED_WALK_POSES];
        }

        pix_render_instance inst = {};
        inst.mesh = ch.mesh;
        inst.material = ch.materials[ped.material % ch.material_count];
        float scale = ch.scale * ped.height;
        if (ped.topple > 0.0f) {
            // Going down without an animation to do it with. The body is
            // tipped a quarter turn about its own side axis and dropped by
            // roughly the height of a lying person, so it ends up on the
            // ground rather than pivoting in the air about its feet.
            float fall = ped.topple * 1.5707963f;
            vec3 down = v3(ped.position.x, ped.position.y + 0.28f * ped.topple, ped.position.z);
            inst.transform = mat4_mul(
                mat4_mul(mat4_translate(down.x, down.y, down.z),
                         mat4_mul(mat4_rotate_y(ped.yaw + ch.yaw_offset), mat4_rotate_x(fall))),
                mat4_scale(scale, scale, scale));
        } else {
            inst.transform = mat4_trs_y(ped.position, ped.yaw + ch.yaw_offset, scale);
        }
        push_animated_instance(renderer, inst, *pose);
    }
}
