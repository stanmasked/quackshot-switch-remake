#include "qs_game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Stage173 — synthetic variable-height jump beside a 48px obstacle.
 * Original numeric first-city records identify 1-frame/50-frame jump
 * envelopes, but do NOT establish the original wall geometry.
 * This original-free fixture is a deliberate collision discrimination:
 * short and medium jump stay on the entry side, long hold clears it.
 */
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "Stage173 wall-jump FAIL line %d: %s\n", __LINE__, #expr); \
    exit(EXIT_FAILURE); \
} } while (0)

enum { WALL_COLUMN = 25, WALL_TOP_ROW = 10, WALL_BOTTOM_ROW = 12,
       WALL_X = WALL_COLUMN * QS_TILE, WALL_WIDTH = QS_TILE,
       WALL_TOP = WALL_TOP_ROW * QS_TILE,
       JUMP_TICKS = 92, FRACTION_CASES = 4 };

typedef struct {
    float farthest_x;
    float min_y;
    float final_x;
    int traversed, wall_contacts, grounded_end;
} Result;

static QsGame wall_course(int dir, float fraction) {
    QsGame g;
    int row, col;
    qs_init(&g);
    g.physics_profile = QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g, 0);
    CHECK(g.mode == QS_STAGE && g.physics_profile == QS_PHYSICS_REFERENCE_CITY);
    for (row = 0; row < QS_ROWS; ++row)
        for (col = 0; col < QS_COLUMNS; ++col)
            g.terrain[row][col] = row >= 13 ? '#' : '.';
    for (row = WALL_TOP_ROW; row <= WALL_BOTTOM_ROW; ++row)
        g.terrain[row][WALL_COLUMN] = '#';
    memset(g.enemies, 0, sizeof(g.enemies));
    memset(g.shots, 0, sizeof(g.shots));
    memset(g.pickups, 0, sizeof(g.pickups));
    g.city_crate_active = 0;
    g.hero.x = (dir > 0) ? WALL_X - 12.0f - fraction :
                           WALL_X + WALL_WIDTH + fraction;
    g.hero.y = 188.0f;
    g.hero.vx = g.hero.vy = 0.0f;
    g.hero.grounded = 1;
    g.hero.hearts = 3;
    g.camera_x = g.render_camera_x = 0;
    memset(&g.previous, 0, sizeof(g.previous));
    return g;
}

static void equal_states(const QsGame *first, const QsGame *second) {
    CHECK(first->mode == second->mode);
    CHECK(first->stage == second->stage);
    CHECK(first->hero.x == second->hero.x);
    CHECK(first->hero.y == second->hero.y);
    CHECK(first->hero.vx == second->hero.vx);
    CHECK(first->hero.vy == second->hero.vy);
    CHECK(first->hero.grounded == second->hero.grounded);
    CHECK(first->camera_x == second->camera_x);
    CHECK(first->render_camera_x == second->render_camera_x);
}

static Result trace_jump(int dir, unsigned hold, float fraction) {
    QsGame a = wall_course(dir, fraction);
    QsGame b = wall_course(dir, fraction);
    Result result = {0};
    unsigned t;
    float prev_x = a.hero.x;
    float near_edge = (dir > 0) ? WALL_X - 12.0f : WALL_X + WALL_WIDTH;
    result.farthest_x = a.hero.x;
    result.min_y = a.hero.y;

    for (t = 0; t < JUMP_TICKS; ++t) {
        QsInput in = {0};
        const float prev_camera = a.camera_x;
        in.jump = t < hold;
        if (dir > 0) in.right = 1;
        else in.left = 1;

        qs_step(&a, in);
        qs_step(&b, in);
        equal_states(&a, &b);
        CHECK(a.mode == QS_STAGE);
        CHECK(a.render_camera_x == prev_camera);
        CHECK(a.hero.hearts == 3);
        CHECK(isfinite(a.hero.x) && isfinite(a.hero.y));
        CHECK(a.hero.y < 209.0f);
        /* Strict AABB: a crossing player must be completely above
         * the top of the authored wall while overlapping horizontally. */
        CHECK(!(a.hero.x < WALL_X + WALL_WIDTH - 0.001f &&
                a.hero.x + 12.0f > WALL_X + 0.001f &&
                a.hero.y < WALL_TOP + (WALL_BOTTOM_ROW - WALL_TOP_ROW + 1) * QS_TILE - 0.001f &&
                a.hero.y + 20.0f > WALL_TOP + 0.001f));

        if ((dir > 0 && a.hero.x < prev_x - 0.001f) ||
            (dir < 0 && a.hero.x > prev_x + 0.001f))
            CHECK(0); /* Input never sends opposite direction. */

        if (a.hero.x != prev_x && fabsf(a.hero.x - prev_x) < 0.001f)
            CHECK(0);
        prev_x = a.hero.x;

        if (a.hero.y < result.min_y) result.min_y = a.hero.y;
        if ((dir > 0 && a.hero.x > result.farthest_x) ||
            (dir < 0 && a.hero.x < result.farthest_x))
            result.farthest_x = a.hero.x;
        if (dir > 0 && a.hero.x >= WALL_X + WALL_WIDTH)
            result.traversed = 1;
        if (dir < 0 && a.hero.x + 12.0f <= WALL_X)
            result.traversed = 1;
        if (!result.traversed &&
            fabsf(a.hero.x - near_edge) < 1.6251f && a.hero.vx == 0.0f)
            ++result.wall_contacts;
    }
    result.final_x = a.hero.x;
    result.grounded_end = a.hero.grounded;
    CHECK(result.wall_contacts > 0);
    CHECK(result.grounded_end);
    CHECK(a.hero.y == 188.0f);
    return result;
}

int main(void) {
    const unsigned holds[] = {1, 8, 50};
    int dir, fraction_index, count = 0;
    unsigned k;
    for (dir = -1; dir <= 1; dir += 2) {
        for (fraction_index = 0; fraction_index < FRACTION_CASES;
             ++fraction_index) {
            float fractional_x = fraction_index * 0.25f;
            Result t[3];
            for (k = 0; k < 3; ++k) {
                t[k] = trace_jump(dir, holds[k], fractional_x);
                ++count;
            }
            /* Original measured rise: 1-frame input ~24px, long
             * input ~65px. The intermediate 8-frame case is a
             * regression of our own model, not a new ROM measurement. */
            CHECK(t[0].min_y > 159.0f);
            CHECK(t[1].min_y > 140.0f);
            CHECK(t[2].min_y < 140.0f);
            CHECK(!t[0].traversed);
            CHECK(!t[1].traversed);
            CHECK(t[2].traversed);
            if (dir > 0) {
                CHECK(t[0].final_x <= WALL_X - 12.0f + 0.001f);
                CHECK(t[1].final_x <= WALL_X - 12.0f + 0.001f);
                CHECK(t[2].final_x > WALL_X + WALL_WIDTH + 1.0f);
            } else {
                CHECK(t[0].final_x >= WALL_X + WALL_WIDTH - 0.001f);
                CHECK(t[1].final_x >= WALL_X + WALL_WIDTH - 0.001f);
                CHECK(t[2].final_x < WALL_X - 13.0f);
            }
        }
    }
    CHECK(count == 24);
    puts("STAGE173_WALL_JUMP_PASS: 24 traces (short/medium/long, left/right, four fractional offsets)");
    return EXIT_SUCCESS;
}
