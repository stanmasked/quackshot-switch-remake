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
    g->reference_jump_steps = 0;
    memset(g->pickups, 0, sizeof(g->pickups));
    g->hero.x = 40; g->hero.y = 188;
    g->hero.vx = 0; g->hero.vy = 0;
    g->hero.facing = 1; g->hero.grounded = 0;
    g->hero.hearts = 3; g->hero.hurt_frames = 0;
    g->camera_x = 0;
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

static void respawn(QsGame *g) {
    g->hero.x = 40; g->hero.y = 188;
    g->hero.vx = 0; g->hero.vy = 0;
    g->hero.hearts = 3; g->hero.hurt_frames = 90;
}

static void hurt(QsGame *g) {
    if (g->hero.hurt_frames) return;
    if (--g->hero.hearts <= 0) respawn(g);
    else g->hero.hurt_frames = 70;
}

static void update_shots(QsGame *g) {
    int i, j;
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
        e->x += e->dir * 0.55f;
        if (e->x > e->max_x || e->x < e->min_x) e->dir = -e->dir;
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

static void shoot(QsGame *g) {
    int i;
    for (i = 0; i < QS_MAX_SHOTS; ++i) {
        QsShot *s = &g->shots[i];
        if (s->active) continue;
        s->active = 1; s->attached = 0;
        s->red = g->red_unlocked && g->weapon == QS_RED;
        s->x = g->hero.x + (g->hero.facing > 0 ? 12 : -8);
        s->y = g->hero.y + 9;
        s->vx = 5.5f * g->hero.facing;
        s->frames = 65;
        break;
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
            for (k = 0; k < QS_MAX_SHOTS; ++k) {
                const QsShot *shot = &g->shots[k];
                if (!shot->active || !shot->attached) continue;
                if (old_bottom <= shot->y + 0.01f && trial + 20 >= shot->y &&
                    h->x < shot->x + 16 && h->x + 12 > shot->x) {
                    h->y = shot->y - 20; on_anchor = 1; break;
                }
            }
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
            g->mode = QS_MAP;
        } else {
            if (edge(input.change, g->previous.change) && g->red_unlocked)
                g->weapon = g->weapon == QS_YELLOW ? QS_RED : QS_YELLOW;
            {
                int fire_due = g->pending_reference_shot;
                g->pending_reference_shot = 0;
                if (edge(input.fire, g->previous.fire)) {
                    if (g->physics_profile == QS_PHYSICS_REFERENCE_CITY)
                        g->pending_reference_shot = 1;
                    else
                        shoot(g);
                }
                if (fire_due) shoot(g);
            }
            move_hero(g, input, edge(input.jump, g->previous.jump));
            update_shots(g);
            update_enemies(g);
            update_pickups(g);
            if (g->hero.hurt_frames) --g->hero.hurt_frames;
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
            }
            if (g->hero.x >= (QS_COLUMNS - 4) * QS_TILE) {
                g->cleared[g->stage] = 1;
                g->mode = QS_MAP;
            }
        }
    }
    g->frames++;
    g->previous = input;
}
