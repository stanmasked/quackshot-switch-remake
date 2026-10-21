#include "qs_game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { \
    fprintf(stderr,"crate priority FAIL line %d: %s\n",__LINE__,#x); \
    exit(EXIT_FAILURE); } } while(0)

static QsInput idle(void){QsInput in={0};return in;}
static QsGame scene(void){
    QsGame g;qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    for(int r=0;r<QS_ROWS;r++)
        for(int c=0;c<QS_COLUMNS;c++)
            g.terrain[r][c]=r>=13?'#':'.';
    g.hero.x=40;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    return g;
}
static void shot(QsGame *g,int index,float x,float speed,int red){
    QsShot *s=&g->shots[index];
    memset(s,0,sizeof(*s));
    s->active=1;s->red=red;s->x=x;s->y=197;
    s->vx=speed;s->frames=65;
}
static void foe(QsGame *g,int index,float x){
    QsEnemy *e=&g->enemies[index];
    memset(e,0,sizeof(*e));e->active=1;
    e->x=e->min_x=e->max_x=x;
    e->y=192;e->dir=1;
}
static void test_plain_projectile_breaks_box(void){
    QsGame g=scene();
    shot(&g,0,QS_CITY_CRATE_X-7,1.0f,0);
    qs_step(&g,idle());
    CHECK(!g.city_crate_active);
    CHECK(g.city_crate_fx_frames==QS_CITY_CRATE_FX_TICKS);
    CHECK(!g.shots[0].active);
}
static void test_red_projectile_breaks_without_wall_anchor(void){
    QsGame g=scene();
    shot(&g,0,QS_CITY_CRATE_X-7,1.0f,1);
    qs_step(&g,idle());
    CHECK(!g.city_crate_active);
    CHECK(!g.shots[0].active && !g.shots[0].attached);
}
static void test_leftward_projectile_breaks_box(void){
    QsGame g=scene();
    shot(&g,0,QS_CITY_CRATE_X+QS_CITY_CRATE_SIZE, -1.0f,0);
    qs_step(&g,idle());
    CHECK(!g.city_crate_active);
    CHECK(!g.shots[0].active);
}
static void test_wall_front_wins_over_rightward_box_overlap(void){
    QsGame g=scene();
    g.terrain[12][60]='#'; /* 960..976, nearer than crate at 968 */
    shot(&g,0,QS_CITY_CRATE_X-7,1.0f,0);
    qs_step(&g,idle());
    CHECK(g.city_crate_active);
    CHECK(!g.city_crate_fx_frames);
    CHECK(!g.shots[0].active);
}
static void test_wall_front_keeps_red_anchor(void){
    QsGame g=scene();
    g.terrain[12][60]='#';
    shot(&g,0,QS_CITY_CRATE_X-7,1.0f,1);
    qs_step(&g,idle());
    CHECK(g.city_crate_active && !g.city_crate_fx_frames);
    CHECK(g.shots[0].active && g.shots[0].attached);
    CHECK(g.shots[0].x==952.0f);
}
static void test_leftward_wall_front_keeps_box(void){
    QsGame g=scene();
    g.terrain[12][61]='#'; /* 976..992, nearer than back face 984 */
    shot(&g,0,QS_CITY_CRATE_X+QS_CITY_CRATE_SIZE,-1.0f,0);
    qs_step(&g,idle());
    CHECK(g.city_crate_active && !g.city_crate_fx_frames);
    CHECK(!g.shots[0].active);
}
static void test_leftward_red_wall_anchor(void){
    QsGame g=scene();
    g.terrain[12][61]='#';
    shot(&g,0,QS_CITY_CRATE_X+QS_CITY_CRATE_SIZE,-1.0f,1);
    qs_step(&g,idle());
    CHECK(g.city_crate_active && !g.city_crate_fx_frames);
    CHECK(g.shots[0].active && g.shots[0].attached);
    CHECK(g.shots[0].x==992.0f);
}
static void test_wall_behind_box_does_not_protect_box(void){
    QsGame g=scene();
    g.terrain[12][61]='#'; /* right-hand wall, behind crate from left */
    /* Inject an in-flight shot overlapping BOTH the crate and the
     * wall in the same substep; wall at 976 is farther than crate 968. */
    shot(&g,0,QS_CITY_CRATE_X+3,1.0f,0);
    qs_step(&g,idle());
    CHECK(!g.city_crate_active);
    CHECK(g.city_crate_fx_frames==QS_CITY_CRATE_FX_TICKS);
}
static void test_enemy_face_in_front_of_box_wins(void){
    QsGame g=scene();
    foe(&g,0,QS_CITY_CRATE_X-5); /* leftmost face at x=963 */
    shot(&g,0,QS_CITY_CRATE_X-7,1.0f,0);
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==179);
    CHECK(g.city_crate_active);
    CHECK(!g.city_crate_fx_frames);
    CHECK(!g.shots[0].active);
}
static void test_red_enemy_in_front_of_box_wins(void){
    QsGame g=scene();
    foe(&g,0,QS_CITY_CRATE_X-5);
    shot(&g,0,QS_CITY_CRATE_X-7,1.0f,1);
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==179);
    CHECK(g.city_crate_active && !g.city_crate_fx_frames);
    CHECK(!g.shots[0].active && !g.shots[0].attached);
}
static void test_box_face_in_front_of_enemy_wins(void){
    QsGame g=scene();
    foe(&g,0,QS_CITY_CRATE_X+0.5f);
    shot(&g,0,QS_CITY_CRATE_X-7,1.0f,0);
    qs_step(&g,idle());
    CHECK(!g.city_crate_active);
    CHECK(g.enemies[0].stunned_frames==0);
    CHECK(!g.shots[0].active);
}
static void test_enemy_tied_to_box_uses_box_first(void){
    QsGame g=scene();
    foe(&g,0,QS_CITY_CRATE_X);
    shot(&g,0,QS_CITY_CRATE_X-7,1.0f,0);
    qs_step(&g,idle());
    CHECK(!g.city_crate_active);
    CHECK(g.enemies[0].stunned_frames==0);
}
static void test_opposite_side_enemy_front_wins(void){
    QsGame g=scene();
    foe(&g,0,QS_CITY_CRATE_X+QS_CITY_CRATE_SIZE-10);
    shot(&g,0,QS_CITY_CRATE_X+QS_CITY_CRATE_SIZE,-1.0f,0);
    qs_step(&g,idle());
    CHECK(g.city_crate_active);
    CHECK(g.enemies[0].stunned_frames==179);
}
static void test_two_projectiles_other_passes_after_destruction(void){
    QsGame g=scene();
    shot(&g,0,QS_CITY_CRATE_X-7,1.0f,0);
    shot(&g,1,QS_CITY_CRATE_X-7,1.0f,0);
    qs_step(&g,idle());
    CHECK(!g.city_crate_active);
    CHECK(g.city_crate_fx_frames==QS_CITY_CRATE_FX_TICKS);
    CHECK(!g.shots[0].active);
    CHECK(g.shots[1].active);
}
static void test_enemy_shielded_by_wall_does_not_get_hit(void){
    QsGame g=scene();
    g.terrain[12][60]='#';
    foe(&g,0,QS_CITY_CRATE_X-5);
    shot(&g,0,QS_CITY_CRATE_X-7,1.0f,0);
    qs_step(&g,idle());
    CHECK(g.city_crate_active);
    CHECK(g.enemies[0].stunned_frames==0);
    CHECK(!g.shots[0].active);
}
static void test_legacy_profile_has_no_box_geometry(void){
    QsGame g;qs_init(&g);qs_begin_stage(&g,0);
    g.city_crate_active=1; /* forged flag must not affect legacy */
    CHECK(!qs_solid(&g,60,12));
}
static void replay_540_frames_with_occluding_walls(void){
    QsGame a=scene(),b;
    a.terrain[12][60]='#';
    b=a;
    for(int t=0;t<540;t++){
        int p=t%90;
        QsInput in=idle();
        if(p==2 && a.mode==QS_STAGE){
            shot(&a,0,QS_CITY_CRATE_X-7,1,0);
            shot(&b,0,QS_CITY_CRATE_X-7,1,0);
        }
        if(p==13 && a.mode==QS_STAGE){
            a.terrain[12][60]=b.terrain[12][60]='.';
            shot(&a,0,QS_CITY_CRATE_X-7,1,0);
            shot(&b,0,QS_CITY_CRATE_X-7,1,0);
        }
        if(p==24 && a.mode==QS_STAGE)in.back=in.dash=1;
        if(p==49 && a.mode==QS_STAGE)in.back=1;
        if(p==53 && a.mode==QS_MAP)in.select=in.dash=1;
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.frames==b.frames && a.mode==b.mode);
        CHECK(a.city_crate_active==b.city_crate_active);
        CHECK(a.city_crate_fx_frames==b.city_crate_fx_frames);
        CHECK(a.shots[0].active==b.shots[0].active);
        CHECK(a.shots[0].attached==b.shots[0].attached);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
    }
    CHECK(a.frames==540 && b.frames==540);
}
int main(void){
    test_plain_projectile_breaks_box();
    test_red_projectile_breaks_without_wall_anchor();
    test_leftward_projectile_breaks_box();
    test_wall_front_wins_over_rightward_box_overlap();
    test_wall_front_keeps_red_anchor();
    test_leftward_wall_front_keeps_box();
    test_leftward_red_wall_anchor();
    test_wall_behind_box_does_not_protect_box();
    test_enemy_face_in_front_of_box_wins();
    test_red_enemy_in_front_of_box_wins();
    test_box_face_in_front_of_enemy_wins();
    test_enemy_tied_to_box_uses_box_first();
    test_opposite_side_enemy_front_wins();
    test_two_projectiles_other_passes_after_destruction();
    test_enemy_shielded_by_wall_does_not_get_hit();
    test_legacy_profile_has_no_box_geometry();
    replay_540_frames_with_occluding_walls();
    puts("PASS: 17 nearest-face crate priority cases and 540 deterministic ticks");
    return EXIT_SUCCESS;
}
