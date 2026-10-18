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
    }
    g->mode = QS_MAP;
}

static void respawn(QsGame *g) {
    g->hero.x = 40; g->hero.y = 188;
    g->hero.vx = 0; g->hero.vy = 0;
    g->hero.hearts = 3; g->hero.hurt_frames = 90;
    if (g->physics_profile == QS_PHYSICS_REFERENCE_CITY && g->stage == 0) {
        clear_city_transients(g);
        g->hero.facing = 1;
        g->hero.grounded = 0;
        /* A new spawn must not present a viewport from the old pit. */
        g->camera_x = 0;
        g->render_camera_x = 0;
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

static void update_shots_reference_city(QsGame *g) {
    int i, j, k;
    for (i = 0; i < QS_MAX_SHOTS; ++i) {
        QsShot *s = &g->shots[i];
        float step;
        int slices;
        if (!s->active) continue;
        if (--s->frames <= 0) { s->active = 0; continue; }
        if (s->attached) continue;
        slices = (int)ceilf(absolute(s->vx));
        if (slices < 1) slices = 1;
        step = s->vx / slices;
        for (k = 0; k < slices; ++k) {
            int target = -1, face_col = 0, wall_hit;
            float enemy_face = 0, wall_face = 0;
            s->x += step;
            /* Discard an entire projectile when any part leaves the
             * synthetic world. qs_solid() correctly treats out-of-range
             * columns as walls for hero physics, but those INVISIBLE
             * sentinels must never create red anchor platforms. */
            if (s->x < 0 || s->x + 7.0f > QS_COLUMNS * QS_TILE) {
                s->active = 0;
                break;
            }
            wall_hit = city_first_wall_face(g, s, step, &wall_face, &face_col);
            for (j = 0; j < QS_MAX_ENEMIES; ++j) {
                const QsEnemy *e = &g->enemies[j];
                float candidate;
                if (!e->active ||
                    !overlaps(s->x, s->y, 7, 4, e->x, e->y, 14, 16))
                    continue;
                candidate = step >= 0 ? e->x : e->x + 14;
                if (target < 0 || (step >= 0 ? candidate < enemy_face :
                                               candidate > enemy_face)) {
                    target = j;
                    enemy_face = candidate;
                }
            }
            /* The physically nearer leading face takes precedence.
             * Solid walls win an exactly co-planar tie, preventing
             * shots from reaching actors embedded at the wall face. */
            if (wall_hit && (target < 0 ||
                (step >= 0 ? wall_face <= enemy_face :
                             wall_face >= enemy_face))) {
                if (s->red) {
                    s->x = step >= 0 ? wall_face - 8.0f : wall_face;
                    s->attached = 1;
                    s->frames = 230;
                } else {
                    s->active = 0;
                }
                break;
            }
            if (target >= 0) {
                g->enemies[target].stunned_frames = 180;
                s->active = 0;
                break;
            }
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

static void update_enemies(QsGame *g) {
    int i;
    for (i = 0; i < QS_MAX_ENEMIES; i++) {
        QsEnemy *e = &g->enemies[i];
        if (!e->active) continue;
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
        if (overlaps(g->hero.x, g->hero.y, 12, 20, e->x, e->y, 14, 16)) hurt(g);
    }
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

static void move_hero(QsGame *g, QsInput in, int jump_pressed) {
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
    if (h->y > QS_VIEW_H + 22) respawn(g);
}

void qs_step(QsGame *g, QsInput input) {
    if (g->mode == QS_MAP) {
        if (edge(input.left, g->previous.left) || edge(input.up, g->previous.up))
            g->selected_stage = (g->selected_stage + QS_STAGE_COUNT - 1) % QS_STAGE_COUNT;
        if (edge(input.right, g->previous.right) || edge(input.down, g->previous.down))
            g->selected_stage = (g->selected_stage + 1) % QS_STAGE_COUNT;
        if (edge(input.select, g->previous.select) || edge(input.jump, g->previous.jump)) {
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
        if (edge(input.back, g->previous.back)) {
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
            move_hero(g, input, edge(input.jump, g->previous.jump));
            update_shots(g);
            update_enemies(g);
            update_pickups(g);
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
        }
    }
    g->frames++;
    g->previous = input;
}
