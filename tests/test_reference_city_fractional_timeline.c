#include "qs_game.h"
#include <inttypes.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { \
  fprintf(stderr,"rational timeline FAIL %d: %s\n",__LINE__,#x); \
  exit(EXIT_FAILURE); } } while(0)

/* Asset-free independent collision timing fixtures. */
static QsInput idle(void){ QsInput in={0};return in; }
static QsGame scene(void) {
    QsGame g;int r,c;
    qs_init(&g);g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.shots,0,sizeof(g.shots));
    memset(g.pickups,0,sizeof(g.pickups));
    /* This fixture isolates projectile/foe rational ordering on
     * empty terrain. The independently tested stage-72 crate is
     * deliberately absent, just like unrelated authored walls. */
    g.city_crate_active=0;
    g.hero.x=40;g.hero.y=188;g.hero.vx=g.hero.vy=0;
    /* No unrelated artificial wall or pits, only safe ground. */
    for(r=0;r<QS_ROWS;r++)
        for(c=0;c<QS_COLUMNS;c++)
            g.terrain[r][c]=r>=13?'#':'.';
    return g;
}
static void foe(QsGame *g,int i,float x) {
    QsEnemy *e=&g->enemies[i];memset(e,0,sizeof(*e));
    e->active=1;e->x=x;e->y=184;e->dir=1;
    e->min_x=e->max_x=x;
}
static void shot(QsGame *g,int i,float x,float vx) {
    QsShot *s=&g->shots[i];memset(s,0,sizeof(*s));
    s->active=1;s->frames=65;s->x=x;s->y=194;s->vx=vx;
}
static void near_tie_arrives_at_different_exact_fractions(void) {
    QsGame g=scene();
    foe(&g,0,135);
    shot(&g,0,127,999);  /* intersection at 2/999 */
    shot(&g,1,127,1000); /* intersection at 2/1000, FIRST */
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==179);
    CHECK(g.shots[0].active && !g.shots[0].attached);
    CHECK(!g.shots[1].active);
    CHECK(g.shots[0].x==1126);
    CHECK(g.shots[0].frames==64);
}
static void near_tie_reverse_slots(void) {
    QsGame g=scene();
    foe(&g,0,135);
    shot(&g,0,127,1000); /* first */
    shot(&g,1,127,999);  /* after 2/999 */
    qs_step(&g,idle());
    CHECK(!g.shots[0].active);
    CHECK(g.shots[1].active && g.shots[1].x==1126);
}
static void near_tie_leftward_arrival(void) {
    QsGame g=scene();
    foe(&g,0,1100);
    shot(&g,0,1115,-999);
    shot(&g,1,1115,-1000);
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==179);
    CHECK(g.shots[0].active && g.shots[0].x==116);
    CHECK(!g.shots[1].active);
}
static void exactly_equal_rationals_are_both_hits(void) {
    QsGame g=scene();
    foe(&g,0,135);
    shot(&g,0,128,3); /* 1/3 */
    shot(&g,1,127,6); /* 2/6 = 1/3 */
    qs_step(&g,idle());
    CHECK(!g.shots[0].active && !g.shots[1].active);
    CHECK(g.enemies[0].stunned_frames==179);
}
static void near_tie_different_enemy_slots(void) {
    QsGame g=scene();
    foe(&g,4,135);
    shot(&g,5,127,999);
    shot(&g,6,127,1000);
    qs_step(&g,idle());
    CHECK(g.enemies[4].stunned_frames==179);
    CHECK(g.shots[5].active && !g.shots[6].active);
}
static void guard_maximum_legal_speed_in_rational_timeline(void) {
    QsGame g=scene();
    foe(&g,0,135);
    shot(&g,0,0,1280);
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==179);
    CHECK(!g.shots[0].active);
}
static void expired_shot_does_not_reserve_rational_time(void) {
    QsGame g=scene();
    foe(&g,0,135);
    shot(&g,0,127,1000);g.shots[0].frames=1;
    shot(&g,1,127,999);
    qs_step(&g,idle());
    CHECK(!g.shots[0].active && !g.shots[1].active);
    CHECK(g.enemies[0].stunned_frames==179);
}
static void wall_still_blocks_with_rational_order(void) {
    QsGame g=scene();
    g.terrain[12][8]='#';
    foe(&g,0,135);
    shot(&g,0,127,1000);
    shot(&g,1,127,999);
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==0);
    CHECK(!g.shots[0].active && !g.shots[1].active);
}
static void seeded_replay_has_stable_discrete_digest(void) {
    QsGame g=scene();
    uint64_t digest=UINT64_C(14695981039346656037);
    int t,s,j,b;
    foe(&g,0,135);
    foe(&g,1,1100);
    for(t=0;t<64;t++) {
        if(t%4==0) {
            shot(&g,0,127,999);
            shot(&g,1,127,1000);
            shot(&g,2,1115,-999);
            shot(&g,3,1115,-1000);
        }
        if(t%11==0) {
            g.enemies[0].stunned_frames=0;
            g.enemies[1].stunned_frames=0;
        }
        qs_step(&g,idle());
        for(s=0;s<4;s++) {
            /* Hash only discrete/precisely integral outcomes,
             * not host struct padding or native endianness. */
            int values[4]={
                g.shots[s].active,g.shots[s].frames,
                (int)g.shots[s].x,g.enemies[s%2].stunned_frames
            };
            for(j=0;j<4;j++) {
                uint32_t v=(uint32_t)values[j];
                for(b=0;b<4;b++) {
                    digest ^= (uint8_t)(v>>(8*b));
                    digest *= UINT64_C(1099511628211);
                }
            }
        }
    }
    CHECK(g.frames==64);
    printf("TIMELINE_DIGEST=%016" PRIx64 "\n",digest);
}
int main(void) {
    near_tie_arrives_at_different_exact_fractions();
    near_tie_reverse_slots();
    near_tie_leftward_arrival();
    exactly_equal_rationals_are_both_hits();
    near_tie_different_enemy_slots();
    guard_maximum_legal_speed_in_rational_timeline();
    expired_shot_does_not_reserve_rational_time();
    wall_still_blocks_with_rational_order();
    seeded_replay_has_stable_discrete_digest();
    return 0;
}
