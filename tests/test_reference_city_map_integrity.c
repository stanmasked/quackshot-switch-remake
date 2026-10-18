#include "qs_game.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr,"city map integrity FAIL line %d: %s\n",__LINE__,#x); \
    exit(EXIT_FAILURE); } } while(0)

static QsInput idle(void) { QsInput q={0}; return q; }
static QsGame map_after_city(void) {
    QsGame g;
    QsInput back=idle();
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    back.back=1;
    qs_step(&g,back);
    CHECK(g.mode==QS_MAP && g.stage==0);
    CHECK(g.physics_profile==QS_PHYSICS_REFERENCE_CITY);
    CHECK(g.camera_x==0 && g.render_camera_x==0);
    qs_step(&g,idle());
    return g;
}
static void invalid_selection_resets_to_first_stage(void) {
    const int bad[]={INT_MIN,-100,-1,QS_STAGE_COUNT,INT_MAX};
    for(int i=0;i<5;i++) {
        QsGame g=map_after_city();
        g.selected_stage=bad[i];
        qs_step(&g,idle());
        CHECK(g.mode==QS_MAP && g.selected_stage==0);
        CHECK(g.physics_profile==QS_PHYSICS_REFERENCE_CITY);
    }
}
static void invalid_stage_after_forward_navigation_is_safe(void) {
    const int bad[]={INT_MIN,-1,QS_STAGE_COUNT,INT_MAX};
    for(int i=0;i<4;i++) {
        QsGame g=map_after_city();
        QsInput in=idle();in.right=1;
        g.selected_stage=bad[i];
        qs_step(&g,in);
        CHECK(g.mode==QS_MAP && g.selected_stage==1);
    }
}
static void invalid_stage_after_backward_navigation_wraps(void) {
    QsGame g=map_after_city();
    QsInput in=idle();in.left=1;
    g.selected_stage=INT_MAX;
    qs_step(&g,in);
    CHECK(g.mode==QS_MAP && g.selected_stage==QS_STAGE_COUNT-1);
}
static void corrupt_completion_flags_are_not_awarded(void) {
    QsGame g=map_after_city();
    g.cleared[0]=1;
    g.cleared[1]=INT_MAX;
    g.cleared[2]=INT_MIN;
    qs_step(&g,idle());
    CHECK(g.cleared[0]==1 && g.cleared[1]==0 && g.cleared[2]==0);
}
static void all_nonone_completion_values_retire(void) {
    const int bad[]={INT_MIN,-1,0,2,17,INT_MAX};
    for(int i=0;i<6;i++) {
        QsGame g=map_after_city();
        for(int k=0;k<QS_STAGE_COUNT;k++)g.cleared[k]=bad[i];
        qs_step(&g,idle());
        for(int k=0;k<QS_STAGE_COUNT;k++)CHECK(g.cleared[k]==0);
    }
}
static void actual_finish_survives_bad_neighbor_flags(void) {
    QsGame g=map_after_city();
    QsInput in=idle();in.select=in.dash=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_STAGE && g.physics_profile==QS_PHYSICS_REFERENCE_CITY);
    g.hero.x=(QS_COLUMNS-4)*QS_TILE+0.5f;
    qs_step(&g,idle());
    CHECK(g.mode==QS_MAP && g.cleared[0]==1);
    g.cleared[1]=INT_MAX;g.cleared[2]=-1;
    g.selected_stage=INT_MAX;
    qs_step(&g,idle());
    CHECK(g.mode==QS_MAP && g.cleared[0]==1 &&
          g.cleared[1]==0 && g.cleared[2]==0);
    CHECK(g.selected_stage==0);
}
static void simultaneous_bad_menu_and_confirmation_enters_city(void) {
    QsGame g=map_after_city();
    QsInput in=idle();in.select=in.dash=1;
    g.selected_stage=INT_MIN;g.cleared[0]=INT_MIN;
    qs_step(&g,in);
    CHECK(g.mode==QS_STAGE && g.stage==0);
    CHECK(g.physics_profile==QS_PHYSICS_REFERENCE_CITY);
    CHECK(g.cleared[0]==0);
}
static void simultaneous_navigation_and_confirmation_only_moves(void) {
    QsGame g=map_after_city();
    QsInput in=idle();
    g.selected_stage=INT_MAX;
    in.select=in.dash=in.right=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_MAP && g.selected_stage==1);
    qs_step(&g,in);
    CHECK(g.mode==QS_MAP && g.selected_stage==1);
    qs_step(&g,idle());
    in=idle();in.select=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_STAGE && g.stage==1);
    CHECK(g.physics_profile==QS_PHYSICS_PROTOTYPE);
}
static void unlock_persists_when_menu_metadata_is_repaired(void) {
    QsGame g=map_after_city();
    g.red_unlocked=1;g.weapon=QS_RED;
    g.cleared[1]=INT_MAX;g.selected_stage=-17;
    qs_step(&g,idle());
    CHECK(g.red_unlocked==1 && g.weapon==QS_RED);
    CHECK(g.selected_stage==0 && g.cleared[1]==0);
    QsInput in=idle();in.select=in.dash=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_STAGE && g.physics_profile==QS_PHYSICS_REFERENCE_CITY);
    CHECK(g.red_unlocked==1 && g.weapon==QS_RED);
    CHECK(!g.pickups[0].active);
}
static void ordinary_profile_preserves_original_map_state(void) {
    QsGame g;qs_init(&g);
    g.selected_stage=INT_MAX;
    g.cleared[0]=INT_MAX;g.cleared[1]=-3;
    qs_step(&g,idle());
    CHECK(g.mode==QS_MAP && g.selected_stage==INT_MAX);
    CHECK(g.cleared[0]==INT_MAX && g.cleared[1]==-3);
}
static void deterministic_map_corruption_replay(void) {
    QsGame a=map_after_city(),b;
    b=a;
    for(int t=0;t<600;t++) {
        QsInput in=idle();
        if(t%7==0) {
            a.selected_stage=b.selected_stage=INT_MIN;
            a.cleared[0]=b.cleared[0]=1;
            a.cleared[1]=b.cleared[1]=INT_MAX;
            a.cleared[2]=b.cleared[2]=-1;
        }
        if(t%7==1)in.right=1;
        if(t%7==3)in.left=1;
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.mode==QS_MAP && b.mode==QS_MAP);
        CHECK(a.frames==b.frames);
        CHECK(a.selected_stage==b.selected_stage);
        CHECK(a.selected_stage>=0 && a.selected_stage<QS_STAGE_COUNT);
        for(int k=0;k<QS_STAGE_COUNT;k++) {
            CHECK(a.cleared[k]==b.cleared[k]);
            CHECK(a.cleared[k]==0 || a.cleared[k]==1);
        }
        CHECK(a.camera_x==b.camera_x && a.render_camera_x==b.render_camera_x);
    }
    CHECK(a.frames==602 && b.frames==602);
}
int main(void) {
    invalid_selection_resets_to_first_stage();
    invalid_stage_after_forward_navigation_is_safe();
    invalid_stage_after_backward_navigation_wraps();
    corrupt_completion_flags_are_not_awarded();
    all_nonone_completion_values_retire();
    actual_finish_survives_bad_neighbor_flags();
    simultaneous_bad_menu_and_confirmation_enters_city();
    simultaneous_navigation_and_confirmation_only_moves();
    unlock_persists_when_menu_metadata_is_repaired();
    ordinary_profile_preserves_original_map_state();
    deterministic_map_corruption_replay();
    puts("PASS: 11 city map integrity scenarios and 600 deterministic ticks");
    return EXIT_SUCCESS;
}
