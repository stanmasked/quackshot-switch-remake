#include "qs_game.h"
#include <math.h>
#include <string.h>

static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

/*
 * Original first-city controlled ROM run (private Genesis Plus GX):
 * 16.16 stored RAM velocity changes by exactly 0x2800/65536 per step
 * to +/-0x1A000/65536, and jump velocity starts at -0x2EE00/65536.
 * For max 15 jump-held frames, vertical velocity stays constant;
 * after release or 15 steps, it increments 0x3200/65536 each frame.
 * These parameters are measured at ONE original city spawn, not
 * copied original instructions or proven universal terrain physics.
 */
#define QS_CITY_ACCEL (10240.0f / 65536.0f)
#define QS_CITY_MAX_WALK (106496.0f / 65536.0f)
#define QS_CITY_GRAVITY (12800.0f / 65536.0f)
#define QS_CITY_JUMP_IMPULSE (-192000.0f / 65536.0f)
#define QS_CITY_MAX_JUMP_HOLD 15

static int edge(int now, int previous) { return now && !previous; }
static float absolute(float n) { return n < 0 ? -n : n; }

const char *qs_stage_name(int stage) {
    static const char *names[QS_STAGE_COUNT] = {"CITY TEST", "RUINS TEST", "ROOFTOP TEST"};
    return (stage >= 0 && stage < QS_STAGE_COUNT) ? names[stage] : "UNKNOWN";
}

int qs_solid(const QsGame *g, int col, int row) {
    if (col < 0 || col >= QS_COLUMNS || row < 0) return 1;
    if (row >= QS_ROWS) return 0; /* Pits have no invisible floor. */
    return g->terrain[row][col] == '#';
}

static void tiles(QsGame *g, int stage) {
    int row, col;
    for (row = 0; row < QS_ROWS; ++row)
        for (col = 0; col < QS_COLUMNS; ++col)
            g->terrain[row][col] = '.';
    for (col = 0; col < QS_COLUMNS; ++col) {
        g->terrain[13][col] = '#';
        g->terrain[14][col] = '#';
    }
    /* Abstract testing layouts, NOT copied from any original game. */
    for (col = 9; col < 17; ++col) g->terrain[10][col] = '#';
    for (col = 27; col < 34; ++col) g->terrain[9][col] = '#';
    for (col = 45; col < 51; ++col) g->terrain[10][col] = '#';
    for (col = 60; col < 70; ++col) g->terrain[9][col] = '#';
    if (stage == 0) {
        for (col = 20; col <= 22; ++col) { g->terrain[13][col] = '.'; g->terrain[14][col] = '.'; }
        for (row = 10; row <= 12; ++row) g->terrain[row][35] = '#';
    } else if (stage == 1) {
        for (col = 37; col <= 39; ++col) { g->terrain[13][col] = '.'; g->terrain[14][col] = '.'; }
        for (row = 8; row <= 12; ++row) g->terrain[row][53] = '#';
    } else {
        for (col = 52; col <= 54; ++col) { g->terrain[13][col] = '.'; g->terrain[14][col] = '.'; }
        for (col = 17; col <= 24; ++col) g->terrain[8][col] = '#';
    }
}

static void add_enemy(QsGame *g, int slot, float x) {
    QsEnemy *e = &g->enemies[slot];
    e->x = x; e->y = 192; e->min_x = x - 40; e->max_x = x + 40;
    e->dir = 1; e->stunned_frames = 0; e->active = 1;
}

void qs_begin_stage(QsGame *g, int stage) {
    if (stage < 0 || stage >= QS_STAGE_COUNT) return;
    g->mode = QS_STAGE; g->stage = stage;
    /* A city-only model: no extrapolation to unmeasured original levels. */
    if (stage != 0) g->physics_profile = QS_PHYSICS_PROTOTYPE;
    tiles(g, stage);
    memset(g->enemies, 0, sizeof(g->enemies));
    memset(g->shots, 0, sizeof(g->shots));
    g->pending_reference_shot = 0;
    g->pending_reference_red = 0;
    g->reference_jump_steps = 0;
    g->city_checkpoint_active = 0;
    g->city_bonus_mask = 0;
    memset(g->pickups, 0, sizeof(g->pickups));
    g->hero.x = 40; g->hero.y = 188;
    g->hero.vx = 0; g->hero.vy = 0;
    g->hero.facing = 1; g->hero.grounded = 0;
    g->hero.hearts = 3; g->hero.hurt_frames = 0;
    g->camera_x = 0;
    g->render_camera_x = 0;
    add_enemy(g, 0, 410); add_enemy(g, 1, 752); add_enemy(g, 2, 1100);
    if (stage == 0 && !g->red_unlocked) {
        g->pickups[0].x = 179; g->pickups[0].y = 140;
        g->pickups[0].kind = QS_PICKUP_RED;
        g->pickups[0].active = 1;
    }
    g->pickups[1].x = 680; g->pickups[1].y = 177;
    g->pickups[1].kind = QS_PICKUP_HEART;
    g->pickups[1].active = 1;
    memset(&g->previous, 0, sizeof(g->previous));
}

void qs_init(QsGame *g) {
    memset(g, 0, sizeof(*g));
    g->mode = QS_MAP;
    g->physics_profile = QS_PHYSICS_PROTOTYPE;
    g->stage = 0;
    g->selected_stage = 0;
    g->weapon = QS_YELLOW;
}

static int overlaps(float ax, float ay, float aw, float ah,
                    float bx, float by, float bw, float bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static int hits_tiles(const QsGame *g, float x, float y, float w, float h) {
    int x0 = (int)floorf(x / QS_TILE);
    int x1 = (int)floorf((x + w - 0.01f) / QS_TILE);
    int y0 = (int)floorf(y / QS_TILE);
    int y1 = (int)floorf((y + h - 0.01f) / QS_TILE);
    int row, col;
    for (row = y0; row <= y1; row++)
        for (col = x0; col <= x1; col++)
            if (qs_solid(g, col, row)) return 1;
    return 0;
}

/*
 * Clear transient actors on a CITY TEST retry or map exit.
 * Persistent unlocks, completion flags and authored stage geometry
 * intentionally survive. Other prototype profiles retain their
 * original respawn and map-exit behavior.
 */
static void clear_city_transients(QsGame *g) {
    if (g->physics_profile != QS_PHYSICS_REFERENCE_CITY || g->stage != 0)
        return;
    memset(g->shots, 0, sizeof(g->shots));
    g->pending_reference_shot = 0;
    g->pending_reference_red = 0;
    g->reference_jump_steps = 0;
}

/* Both manual and automatic CITY TEST exits must leave a consistent
 * MAP state. Camera values otherwise stay on the previous stage even
 * though the rendered map no longer uses them. Progress and unlocks
 * belong to the game session and are intentionally left untouched. */
static void city_leave_to_map(QsGame *g) {
    if (g->physics_profile == QS_PHYSICS_REFERENCE_CITY && g->stage == 0) {
        clear_city_transients(g);
        g->camera_x = 0;
        g->render_camera_x = 0;
        g->city_checkpoint_active = 0; /* next attempt starts fresh */
        g->city_bonus_mask = 0; /* replay-collect on the next attempt */
    }
    g->mode = QS_MAP;
}

static void respawn(QsGame *g) {
    /* A deliberately authored mid-city landmark becomes the new restart
     * only after a clean nonlethal contact in this attempt. The original
     * prototype and an unactivated CITY TEST still start at x=40. */
    int checkpoint = g->physics_profile == QS_PHYSICS_REFERENCE_CITY &&
                     g->stage == 0 && g->city_checkpoint_active == 1;
    g->hero.x = checkpoint ? QS_CITY_CHECKPOINT_X : 40;
    g->hero.y = checkpoint ? QS_CITY_CHECKPOINT_Y : 188;
    g->hero.vx = 0; g->hero.vy = 0;
    g->hero.hearts = 3; g->hero.hurt_frames = 90;
    if (g->physics_profile == QS_PHYSICS_REFERENCE_CITY && g->stage == 0) {
        clear_city_transients(g);
        g->hero.facing = 1;
        g->hero.grounded = 0;
        /* A new spawn must not present a viewport from the old pit. */
        g->camera_x = checkpoint ?
            clampf(g->hero.x - 141.0f, 0.0f, QS_COLUMNS * QS_TILE - QS_VIEW_W) : 0.0f;
        g->render_camera_x = g->camera_x;
    }
}

static void hurt(QsGame *g) {
    if (g->hero.hurt_frames) return;
    if (--g->hero.hearts <= 0) respawn(g);
    else g->hero.hurt_frames = 70;
}

/*
 * An independent swept-AABB approximation for the optional CITY TEST.
 * Prototype profile remains byte-for-byte on the earlier collision path.
 *
 * We advance by <=1 px and resolve the first solid tile or enemy hit
 * along the shot's path. Red anchors are aligned with the *actual wall
 * face*, not with the tile containing the shot's left edge; otherwise
 * a width-7 projectile can embed the anchor 16 px too far from the wall.
 *
 * These are deliberate safety/gameplay rules for original-free geometry,
 * NOT decoded QuackShot collision routines or measured anchor distances.
 */
/*
 * Resolve simultaneous projectile hits by the leading collision face
 * rather than by the order of the wall/enemy checks. The substep is
 * at most one pixel wide, but fractional target boundaries may still
 * make a wall and an enemy appear during that very same substep.
 *
 * Returns the nearest solid tile face along this horizontal trajectory.
 * No Mega Drive collision code or original tiles are involved.
 */
static int city_first_wall_face(const QsGame *g, const QsShot *s,
                                float step, float *face, int *column) {
    int x0 = (int)floorf(s->x / QS_TILE);
    int x1 = (int)floorf((s->x + 7.0f - 0.01f) / QS_TILE);
    int y0 = (int)floorf(s->y / QS_TILE);
    int y1 = (int)floorf((s->y + 4.0f - 0.01f) / QS_TILE);
    int x, y, found = 0;
    for (y = y0; y <= y1; ++y) {
        for (x = x0; x <= x1; ++x) {
            float candidate;
            if (!qs_solid(g, x, y)) continue;
            candidate = step >= 0 ? (float)(x * QS_TILE)
                                  : (float)((x + 1) * QS_TILE);
            if (!found || (step >= 0 ? candidate < *face :
                                       candidate > *face)) {
                *face = candidate;
                *column = x;
                found = 1;
            }
        }
    }
    return found;
}

/*
 * Rank simultaneously travelling shots by estimated time to their
 * nearest intersectable enemy face. The previous slot-index order
 * could waste a second plunger on a foe already hit in the SAME tick
 * while leaving another foe behind it untouched. This ranking is a
 * synthetic CITY TEST rule; it is not an original game algorithm.
 *
 * No candidate -> large key; ties use immutable geometric shot fields,
 * with array index as a last resort for physically identical shots.
 */
/*
 * CITY TEST: use a common fractional-frame timeline for ALL
 * projectile substeps. Sorting whole projectiles by their first
 * anticipated enemy (old approach) is insufficient when a projectile
 * passes through an already-hit actor and then meets another actor
 * LATER than a different shot. Every collision must observe earlier
 * contacts, regardless of array-slot assignment.
 *
 * The old one-pixel-per-substep sweep, wall/face arbitration and
 * lifetime semantics are retained. Tied substeps use the original
 * array index only to pick evaluation order; hit_at_time admits all
 * impacts at the same fractional instant, including opposing shots.
 */
static void update_shots_reference_city(QsGame *g) {
    int i, j;
    int slices[QS_MAX_SHOTS] = {0};
    int progressed[QS_MAX_SHOTS] = {0};
    float delta[QS_MAX_SHOTS] = {0};
    int hit_step[QS_MAX_ENEMIES] = {0};
    int hit_slices[QS_MAX_ENEMIES] = {0};
    /*
     * Exact rational timestamps replace floats. Both numerator and
     * denominator are <= 1280 (bounded by the city speed guard),
     * so cross-products are <= 1,638,400 and fit a signed 32-bit int.
     * This makes chronological ties independent of compiler floating
     * point division/rounding. A zero numerator is the "not hit" mark.
     */
    for (i=0; i<QS_MAX_SHOTS; ++i) {
        QsShot *s=&g->shots[i];
        if (!s->active) continue;
        /*
         * Malformed injected speeds/coordinates must never reach
         * ceilf -> int or floorf -> int. Reject these shots before
         * the first timeline step, including damaged attached shots.
         * Any finite velocity spanning more than the 80-tile world
         * in ONE 60-Hz tick is likewise invalid synthetic input.
         * A maximum legal velocity of 1280 px/tick yields at most
         * 8 * 1280 = 10240 substeps for this entire frame.
         *
         * frames<=1 is tested BEFORE decrement to avoid overflow
         * when corrupt metadata supplies INT_MIN.
         * This strict guard is limited to opt-in CITY TEST.
         */
        if (!isfinite(s->x) || !isfinite(s->y) ||
            !isfinite(s->vx) ||
            s->x < 0.0f || s->x + 7.0f > QS_COLUMNS * QS_TILE ||
            s->y < -4.0f || s->y > QS_ROWS * QS_TILE ||
            absolute(s->vx) > QS_COLUMNS * QS_TILE ||
            s->frames <= 1) {
            s->active=0;
            s->attached=0;
            continue;
        }
        --s->frames;
        if (s->attached) continue;
        /* abs(vx) is now finite and <= world width, so casting
         * ceilf() to int has a proven bounded range [0,1280]. */
        slices[i]=(int)ceilf(absolute(s->vx));
        if (slices[i] < 1) slices[i]=1;
        delta[i]=s->vx/slices[i];
    }
    for (;;) {
        int selected=-1;
        QsShot *s;
        float step;
        int arrival_step, arrival_slices;
        int target=-1, face_col=0, wall_hit;
        float enemy_face=0, wall_face=0;
        /*
         * Advance exactly one substep of the chronologically next
         * projectile. Completed, attached and expired projectiles
         * never reserve the timeline.
         */
        for (i=0; i<QS_MAX_SHOTS; ++i) {
            if (!g->shots[i].active || g->shots[i].attached ||
                !slices[i] || progressed[i] >= slices[i])
                continue;
            /* Rational comparison (k+1)/N without float division. */
            if (selected < 0 ||
                (progressed[i]+1)*slices[selected] <
                (progressed[selected]+1)*slices[i])
                selected=i;
        }
        if (selected < 0) break;
        s=&g->shots[selected];
        step=delta[selected];
        ++progressed[selected];
        arrival_step=progressed[selected];
        arrival_slices=slices[selected];
        s->x += step;
        if (s->x < 0 || s->x + 7.0f > QS_COLUMNS * QS_TILE) {
            s->active=0;
            continue;
        }
        wall_hit=city_first_wall_face(g,s,step,&wall_face,&face_col);
        for (j=0; j<QS_MAX_ENEMIES; ++j) {
            const QsEnemy *e=&g->enemies[j];
            float candidate;
            if (!e->active ||
                (hit_step[j] > 0 &&
                 arrival_step*hit_slices[j] >
                 hit_step[j]*arrival_slices) ||
                !overlaps(s->x,s->y,7,4,e->x,e->y,14,16))
                continue;
            candidate=step >= 0 ? e->x : e->x+14;
            if (target < 0 || (step >= 0 ? candidate < enemy_face :
                                           candidate > enemy_face)) {
                target=j;
                enemy_face=candidate;
            }
        }
        /* Original geometry semantics: the near face wins, and walls
         * win exact face ties. A red shot anchors on a real wall only. */
        if (wall_hit && (target < 0 ||
            (step >= 0 ? wall_face <= enemy_face :
                         wall_face >= enemy_face))) {
            if (s->red) {
                s->x=step >= 0 ? wall_face-8.0f : wall_face;
                s->attached=1;
                s->frames=230;
            } else {
                s->active=0;
            }
            continue;
        }
        if (target >= 0) {
            for (j=0; j<QS_MAX_ENEMIES; ++j) {
                QsEnemy *e=&g->enemies[j];
                float face;
                if (!e->active ||
                    (hit_step[j] > 0 &&
                     arrival_step*hit_slices[j] >
                     hit_step[j]*arrival_slices) ||
                    !overlaps(s->x,s->y,7,4,e->x,e->y,14,16))
                    continue;
                face=step >= 0 ? e->x : e->x+14;
                if (absolute(face-enemy_face) <= 0.0001f) {
                    e->stunned_frames=180;
                    if (hit_step[j] == 0 ||
                        arrival_step*hit_slices[j] <
                        hit_step[j]*arrival_slices) {
                        hit_step[j]=arrival_step;
                        hit_slices[j]=arrival_slices;
                    }
                }
            }
            s->active=0;
        }
    }
}

static void update_shots(QsGame *g) {
    int i, j;
    if (g->physics_profile == QS_PHYSICS_REFERENCE_CITY && g->stage == 0) {
        update_shots_reference_city(g);
        return;
    }
    for (i = 0; i < QS_MAX_SHOTS; i++) {
        QsShot *s = &g->shots[i];
        if (!s->active) continue;
        if (--s->frames <= 0) { s->active = 0; continue; }
        if (s->attached) continue;
        s->x += s->vx;
        if (s->x < 0 || s->x >= QS_COLUMNS * QS_TILE) { s->active = 0; continue; }
        if (hits_tiles(g, s->x, s->y, 7, 4)) {
            if (s->red) {
                s->attached = 1; s->frames = 230;
                if (s->vx > 0) s->x = floorf(s->x / QS_TILE) * QS_TILE - 8;
                else s->x = (floorf(s->x / QS_TILE) + 1) * QS_TILE;
            } else s->active = 0;
            continue;
        }
        for (j = 0; j < QS_MAX_ENEMIES; ++j) {
            QsEnemy *e = &g->enemies[j];
            if (e->active && overlaps(s->x, s->y, 7, 4, e->x, e->y, 14, 16)) {
                e->stunned_frames = 180; s->active = 0; break;
            }
        }
    }
}

static int update_enemies(QsGame *g) {
    int i;
    for (i = 0; i < QS_MAX_ENEMIES; i++) {
        QsEnemy *e = &g->enemies[i];
        if (!e->active) continue;
        /* The city validator normalized every active foe's stun timer
         * before this loop. The legacy decrement-and-skip semantics remain:
         * stun=1 protects on this tick, stun=0 allows contact next tick. */
        if (e->stunned_frames) { --e->stunned_frames; continue; }
        if (g->physics_profile == QS_PHYSICS_REFERENCE_CITY && g->stage == 0) {
            /*
             * The city actor is 14 px wide. Constrain the whole actor to
             * the world and to its chosen patrol interval, then reflect
             * at the EXACT endpoint. Legacy patrol overshoot is retained
             * outside this optional research profile.
             */
            const float world_last_x = QS_COLUMNS * QS_TILE - 14.0f;
            float lo = clampf(e->min_x, 0.0f, world_last_x);
            float hi = clampf(e->max_x, 0.0f, world_last_x);
            float candidate;
            if (lo > hi) {
                float tmp = lo;
                lo = hi;
                hi = tmp;
            }
            candidate = e->x + e->dir * 0.55f;
            if (candidate >= hi) {
                e->x = hi;
                e->dir = -1;
            } else if (candidate <= lo) {
                e->x = lo;
                e->dir = 1;
            } else {
                e->x = candidate;
            }
        } else {
            e->x += e->dir * 0.55f;
            if (e->x > e->max_x || e->x < e->min_x) e->dir = -e->dir;
        }
        if (overlaps(g->hero.x, g->hero.y, 12, 20, e->x, e->y, 14, 16)) {
            int fatal_city = g->physics_profile == QS_PHYSICS_REFERENCE_CITY &&
                             g->stage == 0 && g->hero.hurt_frames == 0 &&
                             g->hero.hearts <= 1;
            hurt(g);
            if (fatal_city) return 1;
        }
    }
    return 0;
}

static void update_pickups(QsGame *g) {
    int i;
    for (i = 0; i < QS_MAX_PICKUPS; ++i) {
        QsPickup *p = &g->pickups[i];
        if (p->active && overlaps(g->hero.x, g->hero.y, 12, 20, p->x, p->y, 10, 10)) {
            p->active = 0;
            if (p->kind == QS_PICKUP_RED) { g->red_unlocked = 1; g->weapon = QS_RED; }
            if (p->kind == QS_PICKUP_HEART && g->hero.hearts < 3) ++g->hero.hearts;
        }
    }
}

/*
 * Stage 58: a geometric, hand-authored checkpoint for the optional city
 * research scene. It is activated only after surviving the current tick,
 * so fatal collisions and numeric/pit retries never grant old-life
 * progress. No original QuackShot checkpoint data is used.
 */
static void update_city_checkpoint(QsGame *g) {
    if (g->physics_profile != QS_PHYSICS_REFERENCE_CITY ||
        g->stage != 0 || g->city_checkpoint_active)
        return;
    if (overlaps(g->hero.x,g->hero.y,12,20,
                 QS_CITY_CHECKPOINT_X,QS_CITY_CHECKPOINT_Y,12,20))
        g->city_checkpoint_active = 1;
}

/*
 * Stage 60: two player-visible, ORIGINAL-FREE bonus beacons provide
 * optional score progress after the first CITY TEST checkpoint.
 * Beacons may only be claimed during a valid survived frame; a lethal
 * contact, pit or numeric retry cannot collect one at the new spawn.
 * Their bitmask persists across lives in the same attempt, not across
 * a map exit. Each bit awards 500 points; no arithmetic accumulation
 * is needed, eliminating overflow and duplicate rewards.
 */
static void update_city_bonus(QsGame *g) {
    if (g->physics_profile != QS_PHYSICS_REFERENCE_CITY || g->stage != 0)
        return;
    if (!(g->city_bonus_mask & 1) &&
        overlaps(g->hero.x,g->hero.y,12,20,
                 QS_CITY_BONUS_A_X,QS_CITY_BONUS_Y,10,10))
        g->city_bonus_mask |= 1;
    if (!(g->city_bonus_mask & 2) &&
        overlaps(g->hero.x,g->hero.y,12,20,
                 QS_CITY_BONUS_B_X,QS_CITY_BONUS_Y,10,10))
        g->city_bonus_mask |= 2;
}

static void shoot_as(QsGame *g, int red) {
    int i;
    for (i = 0; i < QS_MAX_SHOTS; ++i) {
        QsShot *s = &g->shots[i];
        if (s->active) continue;
        s->active = 1; s->attached = 0;
        s->red = red;
        s->x = g->hero.x + (g->hero.facing > 0 ? 12 : -8);
        s->y = g->hero.y + 9;
        s->vx = 5.5f * g->hero.facing;
        s->frames = 65;
        break;
    }
}

/*
 * City-only descending one-way red anchors.
 *
 * When several supports are crossed in the same fractional movement,
 * choose the uppermost surface (minimum world Y), NOT the first shot
 * slot. Slots are recycled, so array order has no geometric meaning.
 * This is independently implemented test geometry, not measured
 * original Mega Drive collision code.
 */
static void shoot(QsGame *g) {
    shoot_as(g, g->red_unlocked && g->weapon == QS_RED);
}

static int city_anchor_surface(const QsGame *g, float hero_x,
                               float old_bottom, float new_bottom,
                               float *surface_y) {
    int i, found = 0;
    float nearest = 0;
    for (i = 0; i < QS_MAX_SHOTS; ++i) {
        const QsShot *shot = &g->shots[i];
        if (!shot->active || !shot->attached || !shot->red) continue;
        if (old_bottom > shot->y + 0.01f ||
            new_bottom < shot->y ||
            hero_x >= shot->x + 16 ||
            hero_x + 12 <= shot->x) continue;
        if (!found || shot->y < nearest) {
            nearest = shot->y;
            found = 1;
        }
    }
    if (found) *surface_y = nearest;
    return found;
}

/*
 * In the legacy update order, movement precedes shot timer decrement.
 * City-only expired attached supports must be retired BEFORE collision,
 * so a platform with 1 remaining frame cannot hold the hero one extra
 * simulation step. Preserve the ordinary prototype's update ordering.
 */
static void retire_city_expiring_anchors(QsGame *g) {
    int i;
    if (g->physics_profile != QS_PHYSICS_REFERENCE_CITY || g->stage != 0)
        return;
    for (i = 0; i < QS_MAX_SHOTS; ++i) {
        QsShot *s = &g->shots[i];
        if (s->active && s->attached && s->frames <= 1)
            s->active = 0;
    }
}

/* Returns 1 if a CITY TEST pit forces a new life this tick. */
static int move_hero(QsGame *g, QsInput in, int jump_pressed) {
    QsPlayer *h = &g->hero;
    float dx = 0, dy, segment, trial;
    int i, slices;
    if (g->physics_profile == QS_PHYSICS_REFERENCE_CITY) {
        int dir = (in.right && !in.left) ? 1 : ((in.left && !in.right) ? -1 : 0);
        float requested = dir * QS_CITY_MAX_WALK;
        if (dir) h->facing = dir;
        /* Measured symmetric fixed-point ramp and release braking.
         * Dash is not calibrated and must NOT silently change this profile. */
        if (h->vx < requested)
            h->vx = fminf(h->vx + QS_CITY_ACCEL, requested);
        else if (h->vx > requested)
            h->vx = fmaxf(h->vx - QS_CITY_ACCEL, requested);
        dx = h->vx;
    } else if (in.left != in.right) {
        h->facing = in.left ? -1 : 1;
        dx = h->facing * (in.dash ? 3.1f : 1.9f);
    }
    if (jump_pressed && h->grounded) {
        h->vy = g->physics_profile == QS_PHYSICS_REFERENCE_CITY
                    ? QS_CITY_JUMP_IMPULSE : -7.2f;
        if (g->physics_profile == QS_PHYSICS_REFERENCE_CITY)
            g->reference_jump_steps = 0;
        h->grounded = 0;
    }
    slices = (int)ceilf(absolute(dx));
    segment = slices ? dx / slices : 0;
    for (i = 0; i < slices; i++) {
        trial = h->x + segment;
        if (hits_tiles(g, trial, h->y, 12, 20)) {
            /* Observed original city wall: held direction stops X velocity.
             * Retain legacy prototype collision behavior verbatim. */
            if (g->physics_profile == QS_PHYSICS_REFERENCE_CITY)
                h->vx = 0;
            break;
        }
        h->x = trial;
    }
    if (g->physics_profile == QS_PHYSICS_REFERENCE_CITY) {
        if (!(in.jump && h->vy < 0 &&
              g->reference_jump_steps < QS_CITY_MAX_JUMP_HOLD))
            h->vy = clampf(h->vy + QS_CITY_GRAVITY, -8.0f, 7.5f);
        if (g->reference_jump_steps < QS_CITY_MAX_JUMP_HOLD + 1)
            ++g->reference_jump_steps;
    } else {
        h->vy = clampf(h->vy + 0.32f, -8.0f, 7.5f);
    }
    dy = h->vy;
    slices = (int)ceilf(absolute(dy));
    segment = slices ? dy / slices : 0;
    h->grounded = 0;
    for (i = 0; i < slices; i++) {
        float old_bottom = h->y + 20;
        int k, on_anchor = 0;
        trial = h->y + segment;
        if (dy > 0) {
            if (g->physics_profile == QS_PHYSICS_REFERENCE_CITY && g->stage == 0) {
                float nearest;
                if (city_anchor_surface(g, h->x, old_bottom, trial + 20, &nearest)) {
                    h->y = nearest - 20;
                    on_anchor = 1;
                }
            } else {
                /* Keep ordinary prototype's original slot-order behavior. */
                for (k = 0; k < QS_MAX_SHOTS; ++k) {
                    const QsShot *shot = &g->shots[k];
                    if (!shot->active || !shot->attached) continue;
                    if (old_bottom <= shot->y + 0.01f && trial + 20 >= shot->y &&
                        h->x < shot->x + 16 && h->x + 12 > shot->x) {
                        h->y = shot->y - 20; on_anchor = 1; break;
                    }
                }
            }
        }
        /* City-only: the first *higher* world-space collision wins.
         * An anchor just below the top of a solid tile cannot place
         * the hero inside the tile. Keep legacy profile untouched. */
        if (on_anchor && g->physics_profile == QS_PHYSICS_REFERENCE_CITY &&
            g->stage == 0 && hits_tiles(g, h->x, trial, 12, 20)) {
            float tile_top = floorf((trial + 20) / QS_TILE) * QS_TILE;
            if (tile_top <= h->y + 20) on_anchor = 0;
        }
        if (on_anchor || hits_tiles(g, h->x, trial, 12, 20)) {
            if (dy > 0) {
                h->grounded = 1;
                /* Snap to the actual tile top instead of stopping at the
                 * previous fractional substep. Only the measured city model
                 * opts into this; the legacy prototype remains bit-identical. */
                if (!on_anchor && g->physics_profile == QS_PHYSICS_REFERENCE_CITY)
                    h->y = floorf((trial + 20) / QS_TILE) * QS_TILE - 20;
            }
            h->vy = 0; break;
        }
        h->y = trial;
    }
    if (h->grounded && g->physics_profile == QS_PHYSICS_REFERENCE_CITY)
        g->reference_jump_steps = 0;
    if (h->y > QS_VIEW_H + 22) {
        int city_retry = g->physics_profile == QS_PHYSICS_REFERENCE_CITY &&
                         g->stage == 0;
        respawn(g);
        return city_retry;
    }
    return 0;
}

/*
 * Corrupt enemy data is rejected before it can enter projectile
 * geometry, contact arbitration or SDL's float-to-int drawing.
 * The 14x16 body may legitimately start partly outside the world
 * while patrolling in, but completely out-of-world coordinates and
 * non-finite inputs are not meaningful CITY TEST actors.
 *
 * This safety boundary is opt-in and deliberately does not alter
 * the legacy prototype's actor behavior.
 */
static void sanitize_city_enemies(QsGame *g) {
    int i;
    const float world_w = (float)(QS_COLUMNS * QS_TILE);
    const float world_h = (float)(QS_ROWS * QS_TILE);
    if (g->physics_profile != QS_PHYSICS_REFERENCE_CITY || g->stage != 0)
        return;
    for (i=0; i<QS_MAX_ENEMIES; ++i) {
        QsEnemy *e=&g->enemies[i];
        if (!e->active) continue;
        if (!isfinite(e->x) || !isfinite(e->y) ||
            !isfinite(e->min_x) || !isfinite(e->max_x) ||
            e->x < -14.0f || e->x > world_w ||
            e->y < -16.0f || e->y > world_h) {
            /* Canonicalize discarded actor too: avoid keeping NaN
             * in replay/snapshot data even after it is inactive. */
            memset(e,0,sizeof(*e));
            continue;
        }
        /* Normalize ALL active actor timers before any enemy contact
         * can end the loop in a fatal respawn. Later slots must not
         * retain INT_MAX/INT_MIN on an early return or map exit. */
        if (e->stunned_frames < 0)
            e->stunned_frames = 0;
        else if (e->stunned_frames > 180)
            e->stunned_frames = 180;
    }
}

/*
 * Stage 47: validate the opt-in CITY TEST player and both camera phases
 * BEFORE exit checks, shot creation, movement, collisions or SDL drawing.
 * Corrupt finite/non-finite coordinates would otherwise reach float-to-int
 * conversions in tile stepping, parallax and the desktop renderer.
 * Real first-city motion stays well within these deliberately generous
 * ceilings. The ordinary prototype is not modified.
 */
static int sanitize_city_hero_camera(QsGame *g) {
    QsPlayer *h = &g->hero;
    const float world_w = (float)(QS_COLUMNS * QS_TILE);
    const float camera_max = world_w - QS_VIEW_W;
    if (g->physics_profile != QS_PHYSICS_REFERENCE_CITY || g->stage != 0)
        return 0;

    /* A completely displaced actor is not a valid exit or a safe body
     * for contact/terrain casts. Respawn also resets both camera phases,
     * the fixed-point ramp and pending city shots atomically. */
    if (!isfinite(h->x) || !isfinite(h->y) ||
        h->x < -12.0f || h->x > world_w ||
        h->y < -20.0f || h->y > QS_VIEW_H + 22.0f) {
        respawn(g);
        return 1;
    }

    /* Stage 52: damaged health is not a fourth heart or an already
     * dead actor that can collect rewards or cross the exit. A fresh
     * life also prevents INT_MIN decrement inside hurt(). Treat it
     * like a numeric retry and delay spawn pickups until next tick. */
    if (h->hearts < 1 || h->hearts > 3) {
        respawn(g);
        return 1;
    }

    /* Facing is multiplied into projectile velocity; a damaged integer
     * must never create an unbounded shot. Preserve valid +/-1 and the
     * direction sign when a malformed nonzero facing can be recovered. */
    if (h->facing != -1 && h->facing != 1)
        h->facing = h->facing < 0 ? -1 : 1;
    if (h->grounded != 0 && h->grounded != 1)
        h->grounded = h->grounded > 0 ? 1 : 0;

    /* Original-free CITY TEST jump hold is at most 16 counted ticks
     * (15 held plus the release sentinel), not an arbitrary int. */
    if (g->reference_jump_steps < 0)
        g->reference_jump_steps = 0;
    else if (g->reference_jump_steps > QS_CITY_MAX_JUMP_HOLD + 1)
        g->reference_jump_steps = QS_CITY_MAX_JUMP_HOLD + 1;

    /* Never call ceilf -> int or enter substep loops with corrupt speeds.
     * Preserve valid near-wall, jumping and pit motion exactly. */
    if (!isfinite(h->vx) || absolute(h->vx) > 8.0f)
        h->vx = 0.0f;
    if (!isfinite(h->vy) || absolute(h->vy) > 16.0f) {
        h->vy = 0.0f;
        h->grounded = 0;
        g->reference_jump_steps = 0;
    }

    /* Stage 51: only 70 post-hit or 90 post-respawn protection ticks
     * are authored. Negative counters used to decrement forever and
     * INT_MAX could grant effectively permanent invulnerability.
     * Keep valid counters and their final safe tick unchanged. */
    if (h->hurt_frames < 0)
        h->hurt_frames = 0;
    else if (h->hurt_frames > 90)
        h->hurt_frames = 90;

    /* A damaged camera must not reach parallax's integer conversion.
     * Recover both present/simulation phases together; keeping only one
     * would create an unrelated full-screen scroll jump. */
    if (!isfinite(g->camera_x) || !isfinite(g->render_camera_x) ||
        g->camera_x < 0.0f || g->camera_x > camera_max ||
        g->render_camera_x < 0.0f ||
        g->render_camera_x > camera_max) {
        float recovered = clampf(h->x - 141.0f, 0.0f, camera_max);
        g->camera_x = recovered;
        g->render_camera_x = recovered;
    }
    return 0;
}

/*
 * Stage 49: invalid CITY TEST pickups are retired BEFORE contact/exit
 * arbitration and SDL float-to-int conversion. Legitimate objects can
 * be partially visible at the edge; wholly outside the authored world,
 * non-finite or unknown-kind objects cannot be collected or rendered.
 * Only the opt-in research scene uses this validator.
 */
static void sanitize_city_pickups(QsGame *g) {
    int i;
    const float world_w = (float)(QS_COLUMNS * QS_TILE);
    const float world_h = (float)(QS_ROWS * QS_TILE);
    if (g->physics_profile != QS_PHYSICS_REFERENCE_CITY || g->stage != 0)
        return;
    for (i = 0; i < QS_MAX_PICKUPS; ++i) {
        QsPickup *p = &g->pickups[i];
        if (!p->active) continue;
        if (!isfinite(p->x) || !isfinite(p->y) ||
            p->x < -10.0f || p->x > world_w ||
            p->y < -10.0f || p->y > world_h ||
            (p->kind != QS_PICKUP_RED && p->kind != QS_PICKUP_HEART)) {
            /* Prevent discarded NaNs from persisting in snapshots too. */
            memset(p, 0, sizeof(*p));
        }
    }
}

/*
 * Stage 53: shot validation must occur BEFORE move_hero(), not just
 * inside update_shots_reference_city(). A damaged attached red anchor
 * would otherwise reach city_anchor_surface() during vertical stepping
 * and might assign NaN to the player's position before rendering.
 *
 * Preserve active valid shots and original reference-city lifetimes;
 * do not canonicalize inactive frame counters because older replay
 * tests retain raw expiration metadata after deactivation.
 */
static void sanitize_city_shots(QsGame *g) {
    int i;
    const float world_w=(float)(QS_COLUMNS * QS_TILE);
    const float world_h=(float)(QS_ROWS * QS_TILE);
    if (g->physics_profile != QS_PHYSICS_REFERENCE_CITY || g->stage != 0)
        return;
    for (i=0;i<QS_MAX_SHOTS;i++) {
        QsShot *s=&g->shots[i];
        if (!s->active) continue;
        /*
         * Stage 57: a discarded projectile must not carry NaN/Inf into
         * a deterministic snapshot. Keep expired-but-otherwise-valid
         * coordinates and the original frames counter untouched;
         * only malformed geometry/flags need canonicalization.
         */
        {
            int malformed_geometry =
                !isfinite(s->x) || !isfinite(s->y) || !isfinite(s->vx) ||
                s->x < 0.0f || s->x + 7.0f > world_w ||
                s->y < -4.0f || s->y > world_h ||
                absolute(s->vx) > world_w;
            int malformed_flags =
                (s->attached != 0 && s->attached != 1) ||
                (s->red != 0 && s->red != 1);
            if (malformed_geometry || malformed_flags || s->frames <= 1) {
                s->active=0;
                s->attached=0;
                if (malformed_geometry || malformed_flags) {
                    s->x=0.0f;
                    s->y=0.0f;
                    s->vx=0.0f;
                    s->red=0;
                }
            }
        }
    }
}

/*
 * Stage 54: only the optional CITY TEST uses one-tick, color-latched
 * fire events. Reject damaged queue flags and weapon metadata before
 * dispatch: an arbitrary int must never behave as a pending shot or
 * unlock a red projectile. A legitimate snapshot (shot=1, red=1)
 * remains RED even if the weapon changes during the next tick.
 */
static void sanitize_city_weapon_queue(QsGame *g) {
    if (g->physics_profile != QS_PHYSICS_REFERENCE_CITY || g->stage != 0)
        return;

    if (g->red_unlocked != 0 && g->red_unlocked != 1)
        g->red_unlocked = g->red_unlocked > 0 ? 1 : 0;
    if (g->weapon != QS_YELLOW && g->weapon != QS_RED)
        g->weapon = QS_YELLOW;
    if (!g->red_unlocked && g->weapon == QS_RED)
        g->weapon = QS_YELLOW;

    if (g->pending_reference_shot != 1) {
        g->pending_reference_shot = 0;
        g->pending_reference_red = 0;
    } else {
        if (g->pending_reference_red != 0 && g->pending_reference_red != 1)
            g->pending_reference_red = 0;
        if (!g->red_unlocked)
            g->pending_reference_red = 0;
    }
}

/*
 * Stage 55: a completed CITY TEST keeps its research profile while
 * returning to the map. Imported/corrupted menu state must not enter
 * modulo navigation or claim completion with arbitrary nonzero ints.
 * Only an exactly committed 1 is a completion witness. The ordinary
 * prototype's map behavior is deliberately unchanged.
 */
static void sanitize_city_map_progress(QsGame *g) {
    int i;
    if (g->mode != QS_MAP ||
        g->physics_profile != QS_PHYSICS_REFERENCE_CITY ||
        g->stage != 0)
        return;
    if (g->selected_stage < 0 || g->selected_stage >= QS_STAGE_COUNT)
        g->selected_stage = 0;
    for (i=0;i<QS_STAGE_COUNT;i++) {
        if (g->cleared[i] != 1)
            g->cleared[i] = 0;
    }
}

/*
 * Stage 56: only CITY TEST may carry the reference physics profile.
 * An invalid mode or stage index must not reach an array subscript
 * (cleared[g->stage]) or masquerade as a second research stage.
 * Convert damaged scene metadata to a clean map handoff and consume
 * the current input edge. No completion is granted by recovery.
 *
 * This guard is opt-in. Legacy prototype state flow is unchanged.
 */
static int recover_city_invalid_scene(QsGame *g) {
    if (g->physics_profile != QS_PHYSICS_REFERENCE_CITY)
        return 0;
    if ((g->mode == QS_MAP || g->mode == QS_STAGE) && g->stage == 0)
        return 0;

    g->stage = 0; /* Make the established city teardown applicable. */
    city_leave_to_map(g);
    g->selected_stage = 0;
    sanitize_city_map_progress(g);
    return 1;
}

void qs_step(QsGame *g, QsInput input) {
    if (recover_city_invalid_scene(g)) {
        /* One safe recovery tick, not a hidden menu action from the
         * corrupted scene's jump/confirm/back chord. */
        ++g->frames;
        g->previous = input;
        return;
    }
    if (g->mode == QS_MAP) {
        sanitize_city_map_progress(g);
        int held_back = input.left || input.up;
        int held_forward = input.right || input.down;
        int nav_back = edge(input.left, g->previous.left) ||
                       edge(input.up, g->previous.up);
        int nav_forward = edge(input.right, g->previous.right) ||
                          edge(input.down, g->previous.down);
        /*
         * One map action per simulation tick. If a direction and
         * confirmation start together, navigate only. Previously the
         * newly selected stage immediately launched by accident.
         * The rule also covers STAGGERED opposing holds: an old left
         * hold plus a newly pressed right must not move the menu.
         * Confirmation requires no direction held at all, even when
         * the direction's new-edge event already happened earlier.
         * A suppressed confirmation is consumed; release/repress it.
         */
        if (held_back && !held_forward && nav_back)
            g->selected_stage = (g->selected_stage + QS_STAGE_COUNT - 1) % QS_STAGE_COUNT;
        if (held_forward && !held_back && nav_forward)
            g->selected_stage = (g->selected_stage + 1) % QS_STAGE_COUNT;
        if (!held_back && !held_forward &&
            (edge(input.select, g->previous.select) ||
             edge(input.jump, g->previous.jump))) {
            /*
             * R+A together on the city-map selector opts in to the research
             * envelope. Normal A or B leaves the v0.1 prototype unchanged.
             */
            g->physics_profile =
                g->selected_stage == 0 && input.select && input.dash
                    ? QS_PHYSICS_REFERENCE_CITY : QS_PHYSICS_PROTOTYPE;
            qs_begin_stage(g, g->selected_stage);
        }
    } else {
        /* Validation precedes even the early Back/finish handoffs:
         * no invalid actor may survive into the next rendered state.
         * Remember a numeric respawn for this frame's reward arbitration. */
        int numeric_retry = sanitize_city_hero_camera(g);
        sanitize_city_enemies(g);
        sanitize_city_pickups(g);
        sanitize_city_shots(g);
        sanitize_city_weapon_queue(g);
        if (g->physics_profile == QS_PHYSICS_REFERENCE_CITY && g->stage == 0) {
            if (g->city_checkpoint_active != 1)
                g->city_checkpoint_active = 0;
            if (g->city_bonus_mask < 0 || g->city_bonus_mask > 3)
                g->city_bonus_mask = 0;
        }
        /*
         * A CITY TEST exit already crossed at tick start is a completed
         * stage, even if the same input also presses Back. Treat the
         * achievement as committed before processing voluntary exits.
         * Do not grant a completion when Back occurs before the exit:
         * physics are intentionally skipped by that explicit exit.
         * The original v0.1 prototype's Back priority stays intact.
         */
        if (g->physics_profile == QS_PHYSICS_REFERENCE_CITY &&
            g->stage == 0 &&
            g->hero.x >= (QS_COLUMNS - 4) * QS_TILE) {
            g->cleared[g->stage] = 1;
            city_leave_to_map(g);
        } else if (edge(input.back, g->previous.back)) {
            city_leave_to_map(g);
        } else {
            if (edge(input.change, g->previous.change) && g->red_unlocked)
                g->weapon = g->weapon == QS_YELLOW ? QS_RED : QS_YELLOW;
            {
                int fire_due = g->pending_reference_shot;
                int fire_due_red = g->pending_reference_red;
                g->pending_reference_shot = 0;
                g->pending_reference_red = 0;
                if (edge(input.fire, g->previous.fire)) {
                    if (g->physics_profile == QS_PHYSICS_REFERENCE_CITY) {
                        /* The city test queues appearance for one step.
                         * Capture the selected ammo NOW: a weapon swap
                         * next tick cannot retroactively recolor it. */
                        g->pending_reference_shot = 1;
                        g->pending_reference_red =
                            g->red_unlocked && g->weapon == QS_RED;
                    } else {
                        shoot(g);
                    }
                }
                if (fire_due) shoot_as(g, fire_due_red);
            }
            retire_city_expiring_anchors(g);
            int pit_retry = move_hero(g, input,
                                      edge(input.jump, g->previous.jump));
            if (pit_retry) {
                /* Retain 90 -> 89 retry immunity, but do not advance
                 * old-life enemies or collect new-spawn pickups yet. */
                if (g->hero.hurt_frames) --g->hero.hurt_frames;
            } else {
            /*
             * CITY TEST: reaching the exit during movement is a
             * committed finish. Process reachable pickups once (so a
             * red unlock or heart at the goal is not lost), but do not
             * let subsequent enemy contact kill/respawn the hero and
             * erase the just-achieved goal coordinate. Retire all
             * projectiles through the usual city map handoff.
             *
             * This exit arbitration is a self-authored test rule;
             * the legacy prototype retains its existing update order.
             */
            if (g->physics_profile == QS_PHYSICS_REFERENCE_CITY &&
                g->stage == 0 &&
                g->hero.x >= (QS_COLUMNS - 4) * QS_TILE) {
                update_pickups(g);
                g->cleared[g->stage] = 1;
                city_leave_to_map(g);
            } else {
                int lethal_retry;
                update_shots(g);
                lethal_retry = update_enemies(g);
                /* The death location and new spawn are different
                 * simulation phases. Do not pick up a spawn item
                 * on the fatal contact's old-life tick. */
                /* A corrupted hero position can force a fresh spawn
                 * before movement. Like lethal/pit retries, the spawn
                 * must not collect items during the OLD attempt's tick.
                 * Nonfatal contact still allows the usual same-tick item. */
                if (!lethal_retry && !numeric_retry) {
                    update_pickups(g);
                    update_city_checkpoint(g);
                    update_city_bonus(g);
                }
            if (g->hero.hurt_frames) --g->hero.hurt_frames;
            /* Snapshot BEFORE simulation camera update. In the measured city
             * the VDP HScroll buffer can trail the gameplay RAM position.
             * One frame is an explicit working hypothesis, not recovered code. */
            g->render_camera_x = g->camera_x;
            if (g->physics_profile == QS_PHYSICS_REFERENCE_CITY) {
                /*
                 * Private first-city Genesis RAM: the 0x8524 scroll-state
                 * candidate keeps worldX-scroll in [140,142] while tracking
                 * rightward, but [122,124] while tracking leftward. Reversing
                 * direction causes a real deadzone: world X retreats first.
                 *
                 * Use the two measured screen-space bands as an *approximate*
                 * tracking envelope on our synthetic 400px viewport. This is
                 * NOT original VDP HScroll code or the original camera logic.
                 */
                float screen_x = g->hero.x - g->camera_x;
                if (screen_x > 141.0f)
                    g->camera_x = g->hero.x - 141.0f;
                else if (screen_x < 123.0f)
                    g->camera_x = g->hero.x - 123.0f;
                g->camera_x = clampf(g->camera_x, 0,
                                     QS_COLUMNS * QS_TILE - QS_VIEW_W);
            } else {
                /* Bit-preserve prototype v0.1 camera behavior. */
                g->camera_x = clampf(g->hero.x - 150, 0,
                                     QS_COLUMNS * QS_TILE - QS_VIEW_W);
                /* Normal test profiles do NOT receive a presentation delay. */
                g->render_camera_x = g->camera_x;
            }
            if (g->hero.x >= (QS_COLUMNS - 4) * QS_TILE) {
                g->cleared[g->stage] = 1;
                city_leave_to_map(g);
            }
            } /* non-finish path (CITY TEST and standard prototype) */
            } /* no pit retry at movement time */
        }
    }
    g->frames++;
    g->previous = input;
}
