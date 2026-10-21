#include "qs_game.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr,"map chord FAIL line %d: %s\n",__LINE__,#x); \
    exit(EXIT_FAILURE); } } while(0)

/* Tests synthetic menu inputs only. No QuackShot assets or ROM. */
static QsInput none(void) {QsInput a={0};return a;}
static void step(QsGame *g,QsInput in) {qs_step(g,in);}
static void staggered_left_then_right_is_suppressed(void) {
    QsGame g;QsInput in=none();qs_init(&g);
    in.left=1;step(&g,in);
    CHECK(g.selected_stage==2);
    in.right=1;step(&g,in);
    CHECK(g.mode==QS_MAP && g.selected_stage==2);
    in.left=0;step(&g,in);
    CHECK(g.selected_stage==2); /* held right alone: not a new edge */
    in.right=0;step(&g,in);
    in.right=1;step(&g,in);
    CHECK(g.selected_stage==0);
}
static void staggered_right_then_left_is_suppressed(void) {
    QsGame g;QsInput in=none();qs_init(&g);
    in.right=1;step(&g,in);
    CHECK(g.selected_stage==1);
    in.left=1;step(&g,in);
    CHECK(g.selected_stage==1);
    in.right=0;step(&g,in);
    CHECK(g.selected_stage==1);
    in.left=0;step(&g,in);
    in.left=1;step(&g,in);
    CHECK(g.selected_stage==0);
}
static void held_right_then_select_must_not_enter(void) {
    QsGame g;QsInput in=none();qs_init(&g);
    in.right=1;step(&g,in);
    in.select=1;in.dash=1;step(&g,in);
    CHECK(g.mode==QS_MAP && g.selected_stage==1);
    in.right=0;step(&g,in); /* select still held */
    CHECK(g.mode==QS_MAP);
    in.select=0;step(&g,in);
    in.select=1;step(&g,in);
    CHECK(g.mode==QS_STAGE && g.stage==1);
    CHECK(g.physics_profile==QS_PHYSICS_PROTOTYPE);
}
static void held_left_then_jump_must_not_enter(void) {
    QsGame g;QsInput in=none();qs_init(&g);
    in.left=1;step(&g,in);
    in.jump=1;step(&g,in);
    CHECK(g.mode==QS_MAP && g.selected_stage==2);
    in.left=0;step(&g,in);
    CHECK(g.mode==QS_MAP);
    in.jump=0;step(&g,in);
    in.jump=1;step(&g,in);
    CHECK(g.mode==QS_STAGE && g.stage==2);
}
static void held_both_directions_block_later_confirm(void) {
    QsGame g;QsInput in=none();qs_init(&g);
    in.left=1;in.right=1;step(&g,in);
    CHECK(g.selected_stage==0);
    in.select=1;step(&g,in);
    CHECK(g.mode==QS_MAP && g.selected_stage==0);
    in.left=0;step(&g,in);
    CHECK(g.mode==QS_MAP && g.selected_stage==0);
}
static void diagonal_opposite_chords_are_suppressed(void) {
    QsGame g;QsInput in=none();qs_init(&g);
    in.up=1;step(&g,in);
    CHECK(g.selected_stage==2);
    in.down=1;step(&g,in);
    CHECK(g.selected_stage==2);
    in.up=0;step(&g,in);
    CHECK(g.selected_stage==2);
}
static void simultaneous_both_directions_never_confirms(void) {
    QsGame g;QsInput in=none();qs_init(&g);
    in.up=1;in.right=1;in.select=1;in.dash=1;
    step(&g,in);step(&g,in);
    CHECK(g.mode==QS_MAP && g.selected_stage==0);
    in.up=0;step(&g,in);
    CHECK(g.mode==QS_MAP);
}
static void valid_separate_nav_then_city_opt_in(void) {
    QsGame g;QsInput in=none();qs_init(&g);
    in.right=1;step(&g,in);
    in=none();step(&g,in);
    in.left=1;step(&g,in);
    in=none();step(&g,in);
    in.select=1;in.dash=1;step(&g,in);
    CHECK(g.mode==QS_STAGE && g.stage==0);
    CHECK(g.physics_profile==QS_PHYSICS_REFERENCE_CITY);
}
static void map_reentry_after_cleared_stages_keeps_progress(void) {
    QsGame g;QsInput in=none();qs_init(&g);
    g.cleared[0]=1;g.cleared[1]=1;g.cleared[2]=1;
    g.red_unlocked=1;g.weapon=QS_RED;
    in.right=1;step(&g,in);
    in=none();step(&g,in);
    in.select=1;step(&g,in);
    CHECK(g.stage==1 && g.mode==QS_STAGE);
    in=none();in.back=1;step(&g,in);
    CHECK(g.mode==QS_MAP && g.selected_stage==1);
    in=none();in.left=1;step(&g,in);
    CHECK(g.selected_stage==0);
    in=none();step(&g,in);
    in.select=1;in.dash=1;step(&g,in);
    CHECK(g.mode==QS_STAGE && g.stage==0);
    CHECK(g.physics_profile==QS_PHYSICS_REFERENCE_CITY);
    CHECK(g.weapon==QS_RED && g.red_unlocked);
    CHECK(!g.pickups[0].active);
    CHECK(g.cleared[0] && g.cleared[1] && g.cleared[2]);
}
static void repeat_nav_same_hold_is_one_step(void) {
    QsGame g;QsInput in=none();int i;qs_init(&g);
    in.right=1;
    for(i=0;i<40;i++)step(&g,in);
    CHECK(g.mode==QS_MAP && g.selected_stage==1);
    in.right=0;step(&g,in);
    in.right=1;step(&g,in);
    CHECK(g.selected_stage==2);
}
static unsigned next_rand(unsigned *seed) {
    *seed ^= *seed<<13; *seed ^= *seed>>17; *seed ^= *seed<<5;
    return *seed;
}
static void long_chord_stress_deterministic(void) {
    QsGame a,b;unsigned seed=0x12345678u;
    int i,j,transitions=0;
    qs_init(&a);b=a;
    a.red_unlocked=b.red_unlocked=1;
    for(i=0;i<1200;i++) {
        QsInput in=none();
        unsigned r=next_rand(&seed);
        in.left=(r&1)!=0;in.right=(r&2)!=0;
        in.up=(r&4)!=0;in.down=(r&8)!=0;
        in.select=(r&16)!=0;in.jump=(r&32)!=0;
        in.back=(r&64)!=0;in.dash=(r&128)!=0;
        if(a.mode==QS_STAGE && (i%79)==10)
            a.hero.x=(QS_COLUMNS-4)*QS_TILE+1;
        if(b.mode==QS_STAGE && (i%79)==10)
            b.hero.x=(QS_COLUMNS-4)*QS_TILE+1;
        QsMode prev=a.mode;
        int prev_choice=a.selected_stage;
        if(prev==QS_MAP &&
           ((in.left||in.up)&&(in.right||in.down))) {
            /* No chord is allowed to launch or navigate. */
            qs_step(&a,in);qs_step(&b,in);
            CHECK(a.mode==QS_MAP && a.selected_stage==prev_choice);
        } else {
            qs_step(&a,in);qs_step(&b,in);
        }
        if(prev!=a.mode)++transitions;
        CHECK(a.mode==b.mode && a.stage==b.stage);
        CHECK(a.selected_stage==b.selected_stage);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
        CHECK(a.camera_x==b.camera_x && a.render_camera_x==b.render_camera_x);
        CHECK(a.physics_profile==b.physics_profile);
        CHECK(a.red_unlocked==b.red_unlocked && a.weapon==b.weapon);
        CHECK(a.selected_stage>=0 && a.selected_stage<QS_STAGE_COUNT);
        for(j=0;j<QS_STAGE_COUNT;j++)CHECK(a.cleared[j]==b.cleared[j]);
    }
    CHECK(transitions>2);
}
int main(void) {
    staggered_left_then_right_is_suppressed();
    staggered_right_then_left_is_suppressed();
    held_right_then_select_must_not_enter();
    held_left_then_jump_must_not_enter();
    held_both_directions_block_later_confirm();
    diagonal_opposite_chords_are_suppressed();
    simultaneous_both_directions_never_confirms();
    valid_separate_nav_then_city_opt_in();
    map_reentry_after_cleared_stages_keeps_progress();
    repeat_nav_same_hold_is_one_step();
    long_chord_stress_deterministic();
    puts("PASS: 11 map button chord regressions and 1200-tick deterministic stress");
    return EXIT_SUCCESS;
}
