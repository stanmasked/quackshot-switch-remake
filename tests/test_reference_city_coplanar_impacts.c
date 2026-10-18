#include "qs_game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"coplanar impacts FAIL line %d: %s\n",__LINE__,#x);exit(EXIT_FAILURE);}}while(0)

/* No copyrighted QuackShot sprites, maps, ROM instructions or music. */
static QsInput idle(void){QsInput in={0};return in;}
static QsGame scenario(QsPhysicsProfile profile){
    QsGame g;int i;
    qs_init(&g);g.physics_profile=profile;qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=40;g.hero.y=188;g.hero.vx=g.hero.vy=0;
    for(i=0;i<QS_COLUMNS;i++)
        g.terrain[13][i]=g.terrain[14][i]='#';
    return g;
}
static void foe(QsGame *g,int id,float x,float y){
    QsEnemy *e=&g->enemies[id];
    memset(e,0,sizeof(*e));
    e->active=1;e->x=x;e->y=y;e->dir=1;
    e->min_x=e->max_x=x;
}
static void shot(QsGame *g,int id,float x,float vx,int red,float y){
    QsShot *s=&g->shots[id];
    memset(s,0,sizeof(*s));
    s->active=1;s->frames=65;s->x=x;s->y=y;
    s->vx=vx;s->red=red;
}
static void rightward_same_face_stuns_both_ammo_colors(void){
    int color;
    for(color=0;color<2;color++){
        QsGame g=scenario(QS_PHYSICS_REFERENCE_CITY);
        foe(&g,0,135,184);foe(&g,1,135,192);
        shot(&g,0,129,1,color,194);
        qs_step(&g,idle());
        CHECK(g.enemies[0].stunned_frames==179);
        CHECK(g.enemies[1].stunned_frames==179);
        CHECK(!g.shots[0].active && !g.shots[0].attached);
    }
}
static void coface_order_is_slot_independent(void){
    QsGame a=scenario(QS_PHYSICS_REFERENCE_CITY),b;
    foe(&a,0,135,184);foe(&a,1,135,192);
    b=a;
    b.enemies[0]=a.enemies[1];b.enemies[1]=a.enemies[0];
    shot(&a,0,129,1,0,194);shot(&b,0,129,1,0,194);
    qs_step(&a,idle());qs_step(&b,idle());
    CHECK(a.enemies[0].stunned_frames==179);
    CHECK(a.enemies[1].stunned_frames==179);
    CHECK(b.enemies[0].stunned_frames==179);
    CHECK(b.enemies[1].stunned_frames==179);
}
static void leftward_same_face_stuns_both(void){
    QsGame g=scenario(QS_PHYSICS_REFERENCE_CITY);
    foe(&g,0,135,184);foe(&g,1,135,192);
    shot(&g,0,149,-1,1,194);
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==179);
    CHECK(g.enemies[1].stunned_frames==179);
    CHECK(!g.shots[0].active);
}
static void strictly_farther_face_is_not_hit(void){
    QsGame g=scenario(QS_PHYSICS_REFERENCE_CITY);
    foe(&g,0,136,184);foe(&g,1,135,192);
    shot(&g,0,129,1,0,194);
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==0);
    CHECK(g.enemies[1].stunned_frames==179);
}
static void vertically_disjoint_enemy_is_excluded(void){
    QsGame g=scenario(QS_PHYSICS_REFERENCE_CITY);
    foe(&g,0,135,184);foe(&g,1,135,214);
    shot(&g,0,129,1,1,194);
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==179);
    CHECK(g.enemies[1].stunned_frames==0);
}
static void solid_wall_on_same_face_wins(void){
    QsGame g=scenario(QS_PHYSICS_REFERENCE_CITY);
    g.terrain[12][9]='#'; /* wall face x=144, y=192..207 */
    foe(&g,0,144,184);foe(&g,1,144,192);
    shot(&g,0,136,2,1,194);
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==0 &&
          g.enemies[1].stunned_frames==0);
    CHECK(g.shots[0].active && g.shots[0].attached);
    CHECK(g.shots[0].x==136);
}
static void two_shots_hit_two_different_faces_in_one_tick(void){
    QsGame g=scenario(QS_PHYSICS_REFERENCE_CITY);
    foe(&g,0,135,184);foe(&g,1,135,192);
    foe(&g,2,180,184);foe(&g,3,180,192);
    shot(&g,0,129,1,0,194);
    shot(&g,1,174,1,1,194);
    qs_step(&g,idle());
    for(int i=0;i<4;i++)CHECK(g.enemies[i].stunned_frames==179);
    CHECK(!g.shots[0].active && !g.shots[1].active);
}
static void one_frame_stun_with_new_projectile_refreshes_timer(void){
    QsGame g=scenario(QS_PHYSICS_REFERENCE_CITY);
    foe(&g,0,135,184);foe(&g,1,135,192);
    g.enemies[0].stunned_frames=1;
    g.enemies[1].stunned_frames=1;
    shot(&g,0,129,1,1,194);
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==179);
    CHECK(g.enemies[1].stunned_frames==179);
}
static void legacy_same_face_stuns_only_first_slot(void){
    QsGame g=scenario(QS_PHYSICS_PROTOTYPE);
    foe(&g,0,135,184);foe(&g,1,135,192);
    shot(&g,0,129,1,0,194);
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==179);
    CHECK(g.enemies[1].stunned_frames==0);
}
static void coplanar_stun_cycles_are_reproducible(void){
    QsGame a=scenario(QS_PHYSICS_REFERENCE_CITY),b;
    int tick,i;
    foe(&a,0,135,184);foe(&a,1,135,192);
    foe(&a,2,180,184);foe(&a,3,180,192);
    b=a;
    for(tick=0;tick<650;tick++){
        QsInput in=idle();
        if(tick%83==0){
            shot(&a,0,129,1,tick%2,194);
            shot(&b,0,129,1,tick%2,194);
        }
        if(tick%97==0){
            shot(&a,1,174,1,1,194);
            shot(&b,1,174,1,1,194);
        }
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.hero.hearts==b.hero.hearts);
        CHECK(a.hero.hurt_frames==b.hero.hurt_frames);
        CHECK(a.camera_x==b.camera_x &&
              a.render_camera_x==b.render_camera_x);
        for(i=0;i<4;i++){
            CHECK(a.enemies[i].stunned_frames==b.enemies[i].stunned_frames);
            CHECK(a.enemies[i].x==b.enemies[i].x);
        }
        for(i=0;i<2;i++){
            CHECK(a.shots[i].active==b.shots[i].active);
            CHECK(a.shots[i].attached==b.shots[i].attached);
        }
    }
    CHECK(a.frames==650 && b.frames==650);
}
int main(void){
    rightward_same_face_stuns_both_ammo_colors();
    coface_order_is_slot_independent();
    leftward_same_face_stuns_both();
    strictly_farther_face_is_not_hit();
    vertically_disjoint_enemy_is_excluded();
    solid_wall_on_same_face_wins();
    two_shots_hit_two_different_faces_in_one_tick();
    one_frame_stun_with_new_projectile_refreshes_timer();
    legacy_same_face_stuns_only_first_slot();
    coplanar_stun_cycles_are_reproducible();
    puts("PASS: 10 coplanar projectile cases and 650-frame replay");
    return EXIT_SUCCESS;
}
