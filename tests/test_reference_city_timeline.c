#include "qs_game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) {fprintf(stderr,"global timeline FAIL %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static QsInput idle(void){QsInput x={0};return x;}
static QsGame city(void){
    QsGame g;int i;qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.shots,0,sizeof(g.shots));
    memset(g.pickups,0,sizeof(g.pickups));
    g.hero.x=40;g.hero.y=188;g.hero.vx=g.hero.vy=0;
    for(i=0;i<QS_COLUMNS;i++)g.terrain[13][i]=g.terrain[14][i]='#';
    return g;
}
static void foe(QsGame *g,int i,float x,float y){
    QsEnemy *e=&g->enemies[i];memset(e,0,sizeof(*e));
    e->active=1;e->x=x;e->y=y;e->min_x=e->max_x=x;e->dir=1;
}
static void shot(QsGame *g,int i,float x,float vx,float y,int red){
    QsShot *s=&g->shots[i];memset(s,0,sizeof(*s));
    s->active=1;s->frames=65;s->x=x;s->vx=vx;s->y=y;s->red=red;
}
static void three_shots_must_apply_events_in_temporal_order(void){
    QsGame g=city();
    foe(&g,0,135,184);foe(&g,1,180,184);
    shot(&g,0,196,-40,194,0); /* far enemy first, t=.075 */
    shot(&g,1,209,-100,194,0);/* passes far, reaches near t=.61 */
    shot(&g,2,110,70,194,1);  /* hits near earlier t=.27 */
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==179);
    CHECK(g.enemies[1].stunned_frames==179);
    CHECK(!g.shots[0].active && !g.shots[2].active);
    CHECK(g.shots[1].active && !g.shots[1].attached);
    CHECK(g.shots[1].x==109);
}
static void permutations_of_shot_slots_have_equivalent_outcomes(void){
    int a,b;
    for(a=0;a<3;a++)for(b=0;b<3;b++){
        QsGame g;
        int slots[3]={0,1,2},k,temp;
        if(a==b)continue;
        temp=slots[a];slots[a]=slots[b];slots[b]=temp;
        g=city();
        foe(&g,0,135,184);foe(&g,1,180,184);
        shot(&g,slots[0],196,-40,194,0);
        shot(&g,slots[1],209,-100,194,0);
        shot(&g,slots[2],110,70,194,1);
        qs_step(&g,idle());
        CHECK(g.enemies[0].stunned_frames==179);
        CHECK(g.enemies[1].stunned_frames==179);
        CHECK(!g.shots[slots[0]].active);
        CHECK(g.shots[slots[1]].active && g.shots[slots[1]].x==109);
        CHECK(!g.shots[slots[2]].active);
        for(k=0;k<3;k++)CHECK(g.shots[k].frames>0);
    }
}
static void wall_blocks_returning_shot_but_not_near_enemy_hit(void){
    QsGame g=city();
    foe(&g,0,135,184);foe(&g,1,180,184);
    g.terrain[12][10]='#'; /* real wall x160..175 */
    shot(&g,0,196,-40,194,0);
    shot(&g,1,209,-100,194,1);
    shot(&g,2,110,70,194,0);
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==179);
    CHECK(g.enemies[1].stunned_frames==179);
    CHECK(g.shots[1].active && g.shots[1].attached);
    CHECK(g.shots[1].x==176);
    CHECK(!g.shots[0].active && !g.shots[2].active);
}
static void equal_time_opposite_projectiles_both_consume(void){
    QsGame g=city();
    foe(&g,0,135,184);
    shot(&g,0,120,10,194,0);
    shot(&g,1,157,-10,194,1);
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==179);
    CHECK(!g.shots[0].active && !g.shots[1].active);
}
static void slightly_later_returning_shot_passes(void){
    QsGame g=city();
    foe(&g,0,135,184);
    shot(&g,0,120,10,194,0); /* t=.9 */
    shot(&g,1,158,-10,194,1);/* t=1 */
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==179);
    CHECK(!g.shots[0].active && g.shots[1].active);
}
static void coplanar_enemies_are_hit_together(void){
    QsGame g=city();
    foe(&g,0,135,184);foe(&g,1,135,192);
    shot(&g,0,129,1,194,0);
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==179);
    CHECK(g.enemies[1].stunned_frames==179);
    CHECK(!g.shots[0].active);
}
static void two_different_shots_blocked_by_real_walls(void){
    QsGame g=city();
    foe(&g,0,135,184);
    g.terrain[12][7]=g.terrain[12][9]='#';
    shot(&g,0,100,46,194,0);
    shot(&g,1,167,-46,194,1);
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==0);
    CHECK(!g.shots[0].active);
    CHECK(g.shots[1].active && g.shots[1].attached);
    CHECK(g.shots[1].x==160);
}
static void expired_shot_does_not_reserve_enemy(void){
    QsGame g=city();
    foe(&g,0,135,184);
    shot(&g,0,120,10,194,0);g.shots[0].frames=1;
    shot(&g,1,157,-10,194,1);
    qs_step(&g,idle());
    CHECK(!g.shots[0].active && !g.shots[1].active);
    CHECK(g.enemies[0].stunned_frames==179);
}
static void previously_stunned_enemy_still_absorbs(void){
    QsGame g=city();
    foe(&g,0,135,184);g.enemies[0].stunned_frames=22;
    shot(&g,0,120,10,194,0);
    qs_step(&g,idle());
    CHECK(!g.shots[0].active);
    CHECK(g.enemies[0].stunned_frames==179);
}
static void legacy_prototype_is_unchanged(void){
    QsGame g=city();
    g.physics_profile=QS_PHYSICS_PROTOTYPE;
    foe(&g,0,135,184);
    shot(&g,0,128,7,194,0);
    shot(&g,1,149,-7,194,1);
    qs_step(&g,idle());
    CHECK(!g.shots[0].active && !g.shots[1].active);
    CHECK(g.enemies[0].stunned_frames==179);
}
static void replay_950_global_ticks(void){
    QsGame a=city(),b;
    int t,i;
    foe(&a,0,135,184);foe(&a,1,180,184);b=a;
    for(t=0;t<950;t++){
        QsInput in=idle();
        if(t%97==0){
            shot(&a,0,196,-40,194,0);
            shot(&a,1,209,-100,194,0);
            shot(&a,2,110,70,194,1);
            shot(&b,0,196,-40,194,0);
            shot(&b,1,209,-100,194,0);
            shot(&b,2,110,70,194,1);
        }
        if(t%147==0){
            a.enemies[0].stunned_frames=b.enemies[0].stunned_frames=0;
            a.enemies[1].stunned_frames=b.enemies[1].stunned_frames=0;
        }
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.mode==b.mode && a.frames==b.frames);
        CHECK(a.hero.hearts==b.hero.hearts);
        CHECK(a.camera_x==b.camera_x && a.render_camera_x==b.render_camera_x);
        for(i=0;i<2;i++){
            CHECK(a.enemies[i].stunned_frames==b.enemies[i].stunned_frames);
            CHECK(a.enemies[i].x==b.enemies[i].x);
        }
        for(i=0;i<3;i++){
            CHECK(a.shots[i].active==b.shots[i].active);
            CHECK(a.shots[i].x==b.shots[i].x);
        }
    }
    CHECK(a.frames==950 && b.frames==950);
}
int main(void){
    three_shots_must_apply_events_in_temporal_order();
    permutations_of_shot_slots_have_equivalent_outcomes();
    wall_blocks_returning_shot_but_not_near_enemy_hit();
    equal_time_opposite_projectiles_both_consume();
    slightly_later_returning_shot_passes();
    coplanar_enemies_are_hit_together();
    two_different_shots_blocked_by_real_walls();
    expired_shot_does_not_reserve_enemy();
    previously_stunned_enemy_still_absorbs();
    legacy_prototype_is_unchanged();
    replay_950_global_ticks();
    puts("PASS: 11 global timeline scenarios, 950-frame deterministic replay");
    return 0;
}
