#include "qs_game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"retry pickup FAIL %d: %s\n",__LINE__,#x);exit(1);}} while(0)
static QsInput idle(void){QsInput v={0};return v;}
static QsGame scene(QsPhysicsProfile mode){
    QsGame g;qs_init(&g);g.physics_profile=mode;qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    g.hero.x=100;g.hero.y=188;g.hero.vx=g.hero.vy=0;
    g.hero.hearts=1;g.hero.hurt_frames=0;
    return g;
}
static void enemy(QsGame *g,int n,float x){
    QsEnemy *e=&g->enemies[n];memset(e,0,sizeof(*e));
    e->active=1;e->x=x;e->y=188;e->dir=1;e->min_x=e->max_x=x;
}
static void pickup(QsGame *g,int n,float x,int kind){
    QsPickup *p=&g->pickups[n];memset(p,0,sizeof(*p));
    p->active=1;p->x=x;p->y=189;p->kind=kind;
}
static void fatal_distant_enemy_delays_spawn_item(void){
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    enemy(&g,0,100);pickup(&g,0,36,QS_PICKUP_RED);
    qs_step(&g,idle());
    CHECK(g.hero.x==40 && g.hero.hearts==3 && g.hero.hurt_frames==89);
    CHECK(g.pickups[0].active && !g.red_unlocked);
    qs_step(&g,idle());
    CHECK(!g.pickups[0].active && g.red_unlocked && g.weapon==QS_RED);
    CHECK(g.hero.hearts==3 && g.hero.hurt_frames==88);
}
static void pit_retry_delays_spawn_item(void){
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    g.hero.x=21*QS_TILE;g.hero.y=261;g.hero.vy=7.5f;
    pickup(&g,0,36,QS_PICKUP_RED);
    qs_step(&g,idle());
    CHECK(g.hero.x==40 && g.hero.y==188 && g.hero.hurt_frames==89);
    CHECK(g.pickups[0].active && !g.red_unlocked);
    qs_step(&g,idle());
    CHECK(!g.pickups[0].active && g.red_unlocked && g.hero.hurt_frames==88);
}
static void nonfatal_enemy_does_not_block_pickup(void){
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    g.hero.hearts=2;
    enemy(&g,0,100);pickup(&g,0,100,QS_PICKUP_HEART);
    qs_step(&g,idle());
    CHECK(g.hero.hearts==2 && g.hero.hurt_frames==69);
    CHECK(!g.pickups[0].active);
}
static void invulnerability_allows_pickup(void){
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    g.hero.hearts=2;g.hero.hurt_frames=2;
    enemy(&g,0,100);pickup(&g,0,100,QS_PICKUP_HEART);
    qs_step(&g,idle());
    CHECK(g.hero.hearts==3 && g.hero.hurt_frames==1);
    CHECK(!g.pickups[0].active);
}
static void immunity_exactly_expires(void){
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);int i;
    enemy(&g,0,100);enemy(&g,1,40);
    qs_step(&g,idle());
    CHECK(g.hero.x==40 && g.hero.hurt_frames==89);
    for(i=0;i<89;i++){qs_step(&g,idle());CHECK(g.hero.hearts==3);}
    CHECK(g.hero.hurt_frames==0);
    qs_step(&g,idle());
    CHECK(g.hero.hearts==2 && g.hero.hurt_frames==69);
}
static void second_enemy_stays_unchanged_on_fatal_tick(void){
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);float x;
    enemy(&g,0,100);enemy(&g,1,130);
    g.enemies[1].min_x=125;g.enemies[1].max_x=140;
    x=g.enemies[1].x;
    qs_step(&g,idle());
    CHECK(g.enemies[1].x==x);
    qs_step(&g,idle());
    CHECK(g.enemies[1].x>x);
}
static void prototype_preserves_old_pickup_order(void){
    QsGame g=scene(QS_PHYSICS_PROTOTYPE);
    enemy(&g,0,100);pickup(&g,0,36,QS_PICKUP_RED);
    qs_step(&g,idle());
    CHECK(g.hero.x==40 && g.red_unlocked && !g.pickups[0].active);
}
static void finishing_before_damage_is_unchanged(void){
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);QsInput in=idle();
    float goal=(QS_COLUMNS-4)*QS_TILE;
    g.hero.x=goal-0.1f;
    enemy(&g,0,goal+0.5f);
    in.right=1;qs_step(&g,in);
    CHECK(g.mode==QS_MAP && g.cleared[0] && g.hero.hearts==1);
}
static void 900_frame_replay(void){
    QsGame a=scene(QS_PHYSICS_REFERENCE_CITY),b;
    int t,k;
    enemy(&a,0,100);pickup(&a,0,36,QS_PICKUP_RED);
    b=a;
    for(t=0;t<900;t++){
        QsInput in=idle();int phase=t%90;
        if(phase==5)in.back=1;
        if(phase==8){in.select=1;in.dash=1;}
        if(phase>=27 && phase<35)in.right=1;
        if(t>0 && phase==0){
            if(a.mode==QS_STAGE){a.hero.x=100;a.hero.y=188;}
            if(b.mode==QS_STAGE){b.hero.x=100;b.hero.y=188;}
        }
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.mode==b.mode && a.stage==b.stage);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
        CHECK(a.hero.hearts==b.hero.hearts && a.hero.hurt_frames==b.hero.hurt_frames);
        CHECK(a.red_unlocked==b.red_unlocked && a.weapon==b.weapon);
        CHECK(a.camera_x==b.camera_x && a.render_camera_x==b.render_camera_x);
        CHECK(a.pending_reference_shot==b.pending_reference_shot);
        for(k=0;k<QS_STAGE_COUNT;k++) CHECK(a.cleared[k]==b.cleared[k]);
    }
    CHECK(a.frames==900 && b.frames==900);
}
int main(void){
    fatal_distant_enemy_delays_spawn_item();
    pit_retry_delays_spawn_item();
    nonfatal_enemy_does_not_block_pickup();
    invulnerability_allows_pickup();
    immunity_exactly_expires();
    second_enemy_stays_unchanged_on_fatal_tick();
    prototype_preserves_old_pickup_order();
    finishing_before_damage_is_unchanged();
    900_frame_replay();
    puts("PASS: 9 retry-pickup and immunity scenarios; 900 deterministic frames");
    return 0;
}
