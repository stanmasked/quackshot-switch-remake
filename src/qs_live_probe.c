#include "qs_live_probe.h"
#include <string.h>
/* Format is small and bounded: exactly seven milestones at most.
 * Event naming explicitly distinguishes observed SDL-input movement
 * from the independently scripted qs_route_probe_write self-test. */
static unsigned bits(QsInput in) {
    return (in.left?1u:0u)|(in.right?2u:0u)|(in.jump?4u:0u)|
           (in.fire?8u:0u)|(in.dash?16u:0u)|(in.select?32u:0u)|
           (in.back?64u:0u);
}
static int shot_count(const QsGame *g) {
    int count=0;
    for(int i=0;i<QS_MAX_SHOTS;i++)if(g->shots[i].active)count++;
    return count;
}
static int record(QsLiveProbe *p,const QsGame *g,QsInput in,
                  const char *event,int shots,int detail) {
    if(p->error || p->records>=7)return 0;
    if(fprintf(p->file,"%u,%d,%s,%d,%d,1,%u,%d,%d,%d,%d,sdl_controller_observed\n",
               p->records,g->frames,event,(int)g->mode,g->stage,
               bits(in),(int)(g->hero.x*1000.0f),
               (int)(g->hero.y*1000.0f),shots,detail)<0 ||
       fflush(p->file)!=0) {
        p->error=1;
        return 0;
    }
    p->records++;
    p->phase++;
    return 1;
}
int qs_live_probe_attach(QsLiveProbe *p,FILE *stream){
    if(!p)return 0;
    memset(p,0,sizeof(*p));
    if(!stream)return 0;
    p->file=stream;
    if(fputs("seq,frame,event,mode,stage,controller,input_bits,x_milli,y_milli,shots,detail,source\n",stream)==EOF ||
       fflush(stream)!=0){
        p->error=1;
        fclose(stream);
        p->file=NULL;
        return 0;
    }
    return 1;
}
int qs_live_probe_open(QsLiveProbe *p,const char *path){
    if(!p)return 0;
    memset(p,0,sizeof(*p));
    if(!path || !*path)return 0;
    FILE *f=fopen(path,"w");
    if(!f)return 0;
    return qs_live_probe_attach(p,f);
}
int qs_live_probe_tick(QsLiveProbe *p,const QsGame *g,QsInput in,
                       QsMode old_mode,float old_x,float old_y,
                       int controller_present) {
    if(!p || !p->file || p->error || !g || !controller_present ||
       p->phase>=7)return 0;
    if(p->phase==0) {
        if(old_mode==QS_MAP && g->mode==QS_STAGE && g->stage==0 &&
           g->physics_profile==QS_PHYSICS_REFERENCE_CITY &&
           in.select && in.dash && g->hero.x==40 && g->hero.y==188)
            return record(p,g,in,"ENTER",0,1);
        return 0;
    }
    if(p->phase==6) {
        if(old_mode==QS_STAGE && g->mode==QS_MAP && in.back &&
           !g->cleared[0])
            return record(p,g,in,"RETURN_MAP",0,0);
        return 0;
    }
    if(g->mode!=QS_STAGE || g->stage!=0 ||
       g->physics_profile!=QS_PHYSICS_REFERENCE_CITY)return 0;
    int active_shots=shot_count(g);
    if(p->phase==1 && in.jump && g->hero.y<old_y-0.001f)
        return record(p,g,in,"JUMP_RISE",active_shots,0);
    if(p->phase==2 && in.right && !in.left &&
       g->hero.x>old_x+0.001f) {
        p->right_highwater=g->hero.x;
        return record(p,g,in,"MOVE_RIGHT",active_shots,0);
    }
    if(p->phase==3) {
        if(g->hero.x>p->right_highwater)
            p->right_highwater=g->hero.x;
        if(in.left && !in.right && g->hero.x<old_x-0.001f &&
           p->right_highwater-g->hero.x>=8.0f)
            return record(p,g,in,"REVERSE_LEFT",active_shots,
                          (int)(p->right_highwater*1000.0f));
    }
    if(p->phase==4 && in.fire && !g->previous.fire)
        return record(p,g,in,"FIRE_INPUT",active_shots,1);
    if(p->phase==5 && active_shots>0)
        return record(p,g,in,"SHOT_ACTIVE",active_shots,1);
    return 0;
}
void qs_live_probe_close(QsLiveProbe *p){
    if(!p)return;
    if(p->file)fclose(p->file);
    p->file=NULL;
}
