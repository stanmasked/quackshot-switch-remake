#include "qs_live_probe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"live probe unit FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void){
    FILE *f=tmpfile();
    QsLiveProbe p;
    QsGame g={0};
    QsInput in={0};
    char line[256],event[50],source[60];
    CHECK(f && qs_live_probe_attach(&p,f));
    g.mode=QS_STAGE;g.stage=0;g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    g.hero.x=40;g.hero.y=188;g.frames=1;
    in.select=1;in.dash=1;
    CHECK(!qs_live_probe_tick(&p,&g,in,QS_MAP,40,188,0));
    CHECK(qs_live_probe_tick(&p,&g,in,QS_MAP,40,188,1));
    g.frames++;in=(QsInput){0};in.jump=1;g.hero.y=184;
    CHECK(qs_live_probe_tick(&p,&g,in,QS_STAGE,40,188,1));
    g.frames++;in=(QsInput){0};in.right=1;g.hero.x=41;
    CHECK(qs_live_probe_tick(&p,&g,in,QS_STAGE,40,184,1));
    g.frames++;g.hero.x=165;
    CHECK(!qs_live_probe_tick(&p,&g,in,QS_STAGE,164,184,1));
    g.frames++;in=(QsInput){0};in.left=1;g.hero.x=153;
    CHECK(qs_live_probe_tick(&p,&g,in,QS_STAGE,154,184,1));
    g.frames++;in=(QsInput){0};in.fire=1;g.previous.fire=0;
    CHECK(qs_live_probe_tick(&p,&g,in,QS_STAGE,153,184,1));
    g.frames++;in=(QsInput){0};g.shots[0].active=1;
    CHECK(qs_live_probe_tick(&p,&g,in,QS_STAGE,153,184,1));
    g.mode=QS_MAP;g.cleared[0]=0;g.frames++;
    in=(QsInput){0};in.back=1;
    CHECK(qs_live_probe_tick(&p,&g,in,QS_STAGE,153,184,1));
    CHECK(p.records==7 && p.phase==7 && !p.error);
    rewind(f);
    CHECK(fgets(line,sizeof(line),f));
    CHECK(strstr(line,"seq,frame,event,mode,stage,controller")!=NULL);
    const char *names[]={"ENTER","JUMP_RISE","MOVE_RIGHT","REVERSE_LEFT",
                         "FIRE_INPUT","SHOT_ACTIVE","RETURN_MAP"};
    for(int i=0;i<7;i++){
        unsigned seq,bitmask;int frame,mode,stage,controller,x,y,shots,detail;
        CHECK(fgets(line,sizeof(line),f));
        CHECK(sscanf(line,"%u,%d,%49[^,],%d,%d,%d,%u,%d,%d,%d,%d,%59s",
           &seq,&frame,event,&mode,&stage,&controller,&bitmask,&x,&y,
           &shots,&detail,source)==12);
        CHECK(seq==(unsigned)i && strcmp(names[i],event)==0);
        CHECK(controller==1 && strcmp(source,"sdl_controller_observed")==0);
        if(i==3)CHECK(detail-x>=8000);
        if(i==5)CHECK(shots>0);
    }
    CHECK(!fgets(line,sizeof(line),f));
    qs_live_probe_close(&p);
    puts("PASS: seven ordered live controller effects, no synthetic-route ambiguity");
    return 0;
}
