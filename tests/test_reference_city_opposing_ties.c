#include "qs_game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"opposing tie FAIL %d: %s\n",__LINE__,#x);exit(1);}} while(0)
static QsInput idle(void){QsInput q={0};return q;}
static QsGame scene(QsPhysicsProfile profile){
    QsGame g;int i;qs_init(&g);g.physics_profile=profile;
    qs_begin_stage(&g,0);
    memset(g.shots,0,sizeof(g.shots));
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    g.hero.x=40;g.hero.y=188;g.hero.vx=g.hero.vy=0;
    for(i=0;i<QS_COLUMNS;i++)
        g.terrain[13][i]=g.terrain[14][i]='#';
    return g;
}
static void foe(QsGame *g,int id,float x,float y){
    QsEnemy *e=&g->enemies[id];memset(e,0,sizeof(*e));
    e->active=1;e->x=x;e->y=y;e->dir=1;
    e->min_x=e->max_x=x;
}
static void shot(QsGame *g,int id,float x,float speed,int red,float y){
    QsShot *s=&g->shots[id];memset(s,0,sizeof(*s));
    s->active=1;s->frames=65;s->x=x;s->vx=speed;
    s->red=red;s->y=y;
}
static void identical_arrivals_both_consumed(void){
    int swap;
    for(swap=0;swap<2;swap++){
        QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
        foe(&g,0,135,184);
        shot(&g,swap,120,10,0,194);
        shot(&g,1-swap,157,-10,1,194);
        qs_step(&g,idle());
        CHECK(g.enemies[0].stunned_frames==179);
        CHECK(!g.shots[0].active && !g.shots[1].active);
    }
}
static void normalized_equal_time_at_different_speeds(void){
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    foe(&g,0,135,184);
    shot(&g,0,120,10,0,194);    /* 9/10 = 0.9 */
    shot(&g,1,166,-20,1,194);   /* 18/20 = 0.9 */
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==179);
    CHECK(!g.shots[0].active && !g.shots[1].active);
}
static void right_arrival_earlier_left_passes_after(void){
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    foe(&g,0,135,184);
    shot(&g,0,120,10,0,194); /* impact .9 */
    shot(&g,1,156,-10,1,194);/* impact .8 */
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==179);
    CHECK(g.shots[0].active && !g.shots[0].attached);
    CHECK(!g.shots[1].active);
}
static void left_arrival_earlier_right_passes_after(void){
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    foe(&g,0,135,184);
    shot(&g,0,120,10,1,194); /* impact .9 */
    shot(&g,1,158,-10,0,194);/* impact 1 */
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==179);
    CHECK(!g.shots[0].active && g.shots[1].active);
}
static void same_time_shots_with_different_y_are_both_consumed(void){
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    foe(&g,0,135,184);
    shot(&g,0,120,10,0,184);
    shot(&g,1,157,-10,1,194);
    qs_step(&g,idle());
    CHECK(!g.shots[0].active && !g.shots[1].active);
    CHECK(g.enemies[0].stunned_frames==179);
}
static void wall_on_left_blocks_left_shot_but_right_hits_foe(void){
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    foe(&g,0,145,184);
    g.terrain[12][8]='#'; /* x128..143 wall shields from left */
    shot(&g,0,120,24,1,194);
    shot(&g,1,167,-16,0,194);
    qs_step(&g,idle());
    CHECK(g.shots[0].active && g.shots[0].attached);
    CHECK(!g.shots[1].active);
    CHECK(g.enemies[0].stunned_frames==179);
}
static void matching_wall_faces_block_both_colors(void){
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    /* Distinct solid columns on BOTH sides of the enemy: the left
     * yellow shot dies at col7 while the right red shot attaches
     * to the right face of col9. Neither can reach the central foe. */
    g.terrain[12][7]=g.terrain[12][9]='#';
    foe(&g,0,135,184);
    shot(&g,0,100,46,0,194);
    shot(&g,1,167,-46,1,194);
    qs_step(&g,idle());
    CHECK(!g.shots[0].active);
    CHECK(g.shots[1].active && g.shots[1].attached);
    CHECK(g.shots[1].x==160);
    CHECK(g.enemies[0].stunned_frames==0);
}
static void already_stunned_foe_remains_solid_to_both(void){
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    foe(&g,0,135,184);g.enemies[0].stunned_frames=31;
    shot(&g,0,120,10,1,194);
    shot(&g,1,157,-10,0,194);
    qs_step(&g,idle());
    CHECK(!g.shots[0].active && !g.shots[1].active);
    CHECK(g.enemies[0].stunned_frames==179);
}
static void far_enemy_remains_unstunned_on_same_time_near_hits(void){
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    foe(&g,0,135,184);foe(&g,1,190,184);
    shot(&g,0,120,10,0,194);
    shot(&g,1,157,-10,1,194);
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==179);
    CHECK(g.enemies[1].stunned_frames==0);
}
static void legacy_mode_keeps_first_slot_consumption(void){
    QsGame g=scene(QS_PHYSICS_PROTOTYPE);
    foe(&g,0,135,184);
    shot(&g,0,128,7,0,194);
    shot(&g,1,149,-7,1,194);
    qs_step(&g,idle());
    CHECK(!g.shots[0].active && !g.shots[1].active);
    CHECK(g.enemies[0].stunned_frames==179);
}
static void repeated_700_tick_opposition_is_reproducible(void){
    QsGame a=scene(QS_PHYSICS_REFERENCE_CITY),b;
    int i,k;foe(&a,0,135,184);b=a;
    for(i=0;i<700;i++){
        QsInput in=idle();
        if(i%53==0){
            shot(&a,0,120,10,i%2,194);
            shot(&a,1,157,-10,(i+1)%2,194);
            shot(&b,0,120,10,i%2,194);
            shot(&b,1,157,-10,(i+1)%2,194);
        }
        if(i%71==0) {
            a.enemies[0].stunned_frames=1;
            b.enemies[0].stunned_frames=1;
        }
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.frames==b.frames && a.hero.hearts==b.hero.hearts);
        CHECK(a.camera_x==b.camera_x &&
              a.render_camera_x==b.render_camera_x);
        CHECK(a.enemies[0].stunned_frames==b.enemies[0].stunned_frames);
        CHECK(a.shots[0].active==b.shots[0].active);
        CHECK(a.shots[1].active==b.shots[1].active);
        CHECK(a.shots[0].x==b.shots[0].x);
        CHECK(a.shots[1].x==b.shots[1].x);
        for(k=0;k<QS_STAGE_COUNT;k++)CHECK(a.cleared[k]==b.cleared[k]);
    }
    CHECK(a.frames==700 && b.frames==700);
}
int main(void){
    identical_arrivals_both_consumed();
    normalized_equal_time_at_different_speeds();
    right_arrival_earlier_left_passes_after();
    left_arrival_earlier_right_passes_after();
    same_time_shots_with_different_y_are_both_consumed();
    wall_on_left_blocks_left_shot_but_right_hits_foe();
    matching_wall_faces_block_both_colors();
    already_stunned_foe_remains_solid_to_both();
    far_enemy_remains_unstunned_on_same_time_near_hits();
    legacy_mode_keeps_first_slot_consumption();
    repeated_700_tick_opposition_is_reproducible();
    puts("PASS: 11 opposing-shot tie/wall scenarios and 700-tick replay");
    return 0;
}
