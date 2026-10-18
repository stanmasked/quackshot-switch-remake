#include "qs_game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"multishot FAIL %d: %s\n",__LINE__,#x);exit(EXIT_FAILURE);}}while(0)
static QsInput idle(void){QsInput q={0};return q;}
static QsGame scene(QsPhysicsProfile profile){
    QsGame g;int col;qs_init(&g);
    g.physics_profile=profile;qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.shots,0,sizeof(g.shots));
    memset(g.pickups,0,sizeof(g.pickups));
    g.hero.x=40;g.hero.y=188;g.hero.vx=g.hero.vy=0;
    for(col=0;col<QS_COLUMNS;col++)
        g.terrain[13][col]=g.terrain[14][col]='#';
    return g;
}
static void foe(QsGame *g,int slot,float x,float y){
    QsEnemy *e=&g->enemies[slot];
    memset(e,0,sizeof(*e));
    e->active=1;e->x=x;e->y=y;e->dir=1;
    e->min_x=e->max_x=x;
}
static void shot(QsGame *g,int slot,float x,float vx,int red,float y){
    QsShot *s=&g->shots[slot];memset(s,0,sizeof(*s));
    s->active=1;s->frames=65;s->x=x;s->y=y;
    s->vx=vx;s->red=red;
}
static void front_and_rear_both_stunned_regardless_of_shot_slots(void){
    int reversed;
    for(reversed=0;reversed<=1;reversed++){
        QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
        foe(&g,0,135,184);foe(&g,1,165,192);
        shot(&g,reversed?1:0,110,70,0,194);
        shot(&g,reversed?0:1,130,65,1,194);
        qs_step(&g,idle());
        CHECK(g.enemies[0].stunned_frames==179);
        CHECK(g.enemies[1].stunned_frames==179);
        CHECK(!g.shots[0].active && !g.shots[1].active);
    }
}
static void both_shots_against_one_enemy_do_not_both_get_consumed(void){
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    foe(&g,0,135,184);
    shot(&g,0,110,70,0,194);
    shot(&g,1,130,65,1,194);
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==179);
    CHECK(g.shots[0].active && !g.shots[0].attached);
    CHECK(!g.shots[1].active);
    CHECK(g.shots[0].x>135);
}
static void trailing_shot_out_of_range_is_not_spent(void){
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    foe(&g,0,135,184);foe(&g,1,165,192);
    shot(&g,0,90,12,0,194);
    shot(&g,1,130,65,1,194);
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==179);
    CHECK(g.enemies[1].stunned_frames==0);
    CHECK(g.shots[0].active && g.shots[0].x==102);
    CHECK(!g.shots[1].active);
}
static void already_stunned_enemy_from_previous_tick_absorbs_shot(void){
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    foe(&g,0,135,184);g.enemies[0].stunned_frames=30;
    foe(&g,1,165,192);
    shot(&g,0,110,70,0,194);
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==179);
    CHECK(g.enemies[1].stunned_frames==0);
    CHECK(!g.shots[0].active);
}
static void wall_blocks_followup_shot_before_far_enemy(void){
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    g.terrain[12][9]='#'; /* x=144-159 */
    foe(&g,0,135,184);foe(&g,1,165,192);
    shot(&g,0,110,70,1,194);
    shot(&g,1,130,65,0,194);
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==179);
    CHECK(g.enemies[1].stunned_frames==0);
    CHECK(g.shots[0].active && g.shots[0].attached);
    CHECK(g.shots[0].x==136);
    CHECK(!g.shots[1].active);
}
static void opposite_direction_hits_near_then_far(void){
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    foe(&g,0,165,184);foe(&g,1,135,192);
    shot(&g,0,205,-80,0,194);
    shot(&g,1,182,-65,1,194);
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==179);
    CHECK(g.enemies[1].stunned_frames==179);
    CHECK(!g.shots[0].active && !g.shots[1].active);
}
static void first_shot_that_expires_cannot_reserve_enemy(void){
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    foe(&g,0,135,184);
    shot(&g,0,130,65,0,194);g.shots[0].frames=1;
    shot(&g,1,110,70,1,194);
    qs_step(&g,idle());
    CHECK(!g.shots[0].active && !g.shots[1].active);
    CHECK(g.enemies[0].stunned_frames==179);
}
static void two_distinct_vertical_lanes_still_work(void){
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    foe(&g,0,135,170);foe(&g,1,165,192);
    shot(&g,0,110,70,0,194);
    shot(&g,1,130,65,1,177);
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==179);
    CHECK(g.enemies[1].stunned_frames==179);
    CHECK(!g.shots[0].active && !g.shots[1].active);
}
static void legacy_two_shots_continue_to_hit_first_foe(void){
    QsGame g=scene(QS_PHYSICS_PROTOTYPE);
    foe(&g,0,135,184);foe(&g,1,165,192);
    shot(&g,0,110,70,0,194);
    shot(&g,1,130,65,1,194);
    qs_step(&g,idle());
    /* Legacy prototype simply steps vx before checking collisions;
     * positions after each move are beyond both foes, so no stun. */
    CHECK(g.enemies[0].stunned_frames==0);
    CHECK(g.enemies[1].stunned_frames==0);
    CHECK(g.shots[0].active && g.shots[1].active);
}
static void long_replay_still_deterministic(void){
    QsGame a=scene(QS_PHYSICS_REFERENCE_CITY),b;
    int t,i;foe(&a,0,135,184);foe(&a,1,165,192);
    b=a;
    for(t=0;t<820;t++){
        QsInput in=idle();
        if(t%65==0){
            shot(&a,t%2,110,70,t%2,194);
            shot(&a,(t%2)^1,130,65,(t+1)%2,194);
            shot(&b,t%2,110,70,t%2,194);
            shot(&b,(t%2)^1,130,65,(t+1)%2,194);
        }
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.mode==b.mode && a.frames==b.frames);
        CHECK(a.hero.hearts==b.hero.hearts);
        CHECK(a.camera_x==b.camera_x && a.render_camera_x==b.render_camera_x);
        for(i=0;i<2;i++) {
            CHECK(a.enemies[i].x==b.enemies[i].x);
            CHECK(a.enemies[i].stunned_frames==b.enemies[i].stunned_frames);
            CHECK(a.shots[i].active==b.shots[i].active);
            CHECK(a.shots[i].x==b.shots[i].x);
        }
    }
    CHECK(a.frames==820 && b.frames==820);
}
int main(void){
    front_and_rear_both_stunned_regardless_of_shot_slots();
    both_shots_against_one_enemy_do_not_both_get_consumed();
    trailing_shot_out_of_range_is_not_spent();
    already_stunned_enemy_from_previous_tick_absorbs_shot();
    wall_blocks_followup_shot_before_far_enemy();
    opposite_direction_hits_near_then_far();
    first_shot_that_expires_cannot_reserve_enemy();
    two_distinct_vertical_lanes_still_work();
    legacy_two_shots_continue_to_hit_first_foe();
    long_replay_still_deterministic();
    puts("PASS: 10 multi-projectile arrival-priority tests and 820-tick replay");
    return EXIT_SUCCESS;
}
