#include "qs_game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"multi enemy FAIL %d: %s\n",__LINE__,#x);exit(EXIT_FAILURE);}} while(0)
static QsInput idle(void){QsInput in={0};return in;}
static QsGame city(void){
    QsGame g;qs_init(&g);g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    g.hero.x=100;g.hero.y=188;g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    return g;
}
static void foe(QsGame *g,int slot,float x){
    QsEnemy *e=&g->enemies[slot];memset(e,0,sizeof(*e));
    e->active=1;e->x=x;e->y=188;e->min_x=e->max_x=x;e->dir=1;
}
static void simultaneous_contacts_cost_only_one_heart(void){
    QsGame g=city();int i;
    for(i=0;i<4;i++)foe(&g,i,100);
    qs_step(&g,idle());
    CHECK(g.hero.hearts==2 && g.hero.hurt_frames==69);
    qs_step(&g,idle());
    CHECK(g.hero.hearts==2 && g.hero.hurt_frames==68);
}
static void multiple_fatal_contacts_have_one_respawn(void){
    QsGame g=city();
    g.hero.hearts=1;
    foe(&g,0,100);foe(&g,1,100);foe(&g,2,40);
    qs_step(&g,idle());
    CHECK(g.hero.x==40 && g.hero.hearts==3 && g.hero.hurt_frames==89);
    CHECK(g.enemies[1].x==100 && g.enemies[2].x==40);
}
static void one_remaining_immunity_tick_blocks_every_enemy(void){
    QsGame g=city();int i;
    g.hero.hurt_frames=1;
    for(i=0;i<4;i++)foe(&g,i,100);
    qs_step(&g,idle());
    CHECK(g.hero.hearts==3 && g.hero.hurt_frames==0);
    qs_step(&g,idle());
    CHECK(g.hero.hearts==2 && g.hero.hurt_frames==69);
}
static void one_frame_stun_recovery_never_attacks_early(void){
    QsGame g=city();foe(&g,0,100);
    g.enemies[0].stunned_frames=1;
    qs_step(&g,idle());
    CHECK(g.hero.hearts==3 && g.enemies[0].stunned_frames==0);
    qs_step(&g,idle());
    CHECK(g.hero.hearts==2 && g.hero.hurt_frames==69);
}
static void simultaneous_wakeup_and_active_enemy_contact(void){
    QsGame g=city();
    foe(&g,0,100);foe(&g,1,100);
    g.enemies[0].stunned_frames=1;
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==0);
    CHECK(g.hero.hearts==2 && g.hero.hurt_frames==69);
    qs_step(&g,idle());
    CHECK(g.hero.hearts==2 && g.hero.hurt_frames==68);
}
static void second_enemy_hits_after_exact_spawn_immunity(void){
    QsGame g=city();int k;
    g.hero.hearts=1;
    foe(&g,0,100);foe(&g,1,100);foe(&g,2,40);
    qs_step(&g,idle());
    CHECK(g.hero.x==40 && g.hero.hurt_frames==89);
    for(k=0;k<89;k++) {
        qs_step(&g,idle());
        CHECK(g.hero.hearts==3);
    }
    CHECK(g.hero.hurt_frames==0);
    qs_step(&g,idle());
    CHECK(g.hero.hearts==2 && g.hero.hurt_frames==69);
}
static void negative_stun_timer_is_normalized_in_city(void){
    QsGame g=city();foe(&g,0,100);
    g.enemies[0].stunned_frames=-1;
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==0);
    CHECK(g.hero.hearts==2 && g.hero.hurt_frames==69);
}
static void legacy_negative_stun_counter_is_untouched(void){
    QsGame g=city();
    g.physics_profile=QS_PHYSICS_PROTOTYPE;
    foe(&g,0,100);g.enemies[0].stunned_frames=-1;
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==-2);
    CHECK(g.hero.hearts==3);
}
static void negative_stun_unrelated_enemy_cannot_bypass_immunity(void){
    QsGame g=city();
    foe(&g,0,100);foe(&g,1,100);
    g.enemies[0].stunned_frames=-1;
    g.hero.hurt_frames=1;
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==0);
    CHECK(g.hero.hearts==3 && g.hero.hurt_frames==0);
    qs_step(&g,idle());
    CHECK(g.hero.hearts==2 && g.hero.hurt_frames==69);
}
static void repeated_multi_enemy_retries_are_reproducible(void){
    QsGame a=city(),b;int tick,j,cycles=0;
    a.hero.hearts=1;
    foe(&a,0,100);foe(&a,1,100);foe(&a,2,40);foe(&a,3,110);
    b=a;
    for(tick=0;tick<1200;tick++){
        QsInput in=idle();
        if(tick>0 && tick%185==0){
            if(a.mode==QS_STAGE){a.hero.x=100;a.hero.y=188;a.hero.hearts=1;a.hero.hurt_frames=0;}
            if(b.mode==QS_STAGE){b.hero.x=100;b.hero.y=188;b.hero.hearts=1;b.hero.hurt_frames=0;}
            cycles++;
        }
        if(tick%181==11)in.fire=1;
        if(tick%73>=20 && tick%73<30)in.right=1;
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.mode==b.mode && a.frames==b.frames);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
        CHECK(a.hero.hearts==b.hero.hearts &&
              a.hero.hurt_frames==b.hero.hurt_frames);
        CHECK(a.camera_x==b.camera_x && a.render_camera_x==b.render_camera_x);
        CHECK(a.pending_reference_shot==b.pending_reference_shot);
        for(j=0;j<QS_MAX_ENEMIES;j++){
            CHECK(a.enemies[j].x==b.enemies[j].x);
            CHECK(a.enemies[j].stunned_frames==b.enemies[j].stunned_frames);
        }
    }
    CHECK(cycles>=6 && a.frames==1200);
}
int main(void){
    simultaneous_contacts_cost_only_one_heart();
    multiple_fatal_contacts_have_one_respawn();
    one_remaining_immunity_tick_blocks_every_enemy();
    one_frame_stun_recovery_never_attacks_early();
    simultaneous_wakeup_and_active_enemy_contact();
    second_enemy_hits_after_exact_spawn_immunity();
    negative_stun_timer_is_normalized_in_city();
    legacy_negative_stun_counter_is_untouched();
    negative_stun_unrelated_enemy_cannot_bypass_immunity();
    repeated_multi_enemy_retries_are_reproducible();
    puts("PASS: 10 multi enemy immunity cases and 1200-tick replay");
    return EXIT_SUCCESS;
}
