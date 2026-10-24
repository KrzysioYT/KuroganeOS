#include <kurogane/display.h>
#include <assert.h>
#include <stdint.h>
#include <limits.h>

static ku_display_info area(uint32_t w,uint32_t h) {
    ku_display_info info = {0};
    info.structure_size=sizeof(info);
    info.version=KU_DISPLAY_INFO_VERSION;
    info.screen_width=w;
    info.screen_height=h;
    info.work_x=34; info.work_y=50;
    info.work_width=(int32_t)w-46;
    info.work_height=(int32_t)h-132;
    return info;
}
static ku_ui_window_options win(int32_t x,int32_t y,int32_t w,int32_t h) {
    ku_ui_window_options o={sizeof(o),x,y,w,h};
    return o;
}
int main(void) {
    ku_display_info d=area(1024,768);
    ku_ui_window_options w=win(400,110,510,560);
    assert(ku_display_fit_window(&d,&w));
    assert(w.x==400&&w.y==110&&w.width==510&&w.height==560);

    d=area(800,600); w=win(400,110,510,560);
    assert(ku_display_fit_window(&d,&w));
    assert(w.x==278&&w.y==50&&w.width==510&&w.height==468);

    d=area(1920,1080); w=win(400,110,510,560);
    assert(ku_display_fit_window(&d,&w));
    assert(w.x==400&&w.y==110&&w.width==510&&w.height==560);

    d=area(640,480); w=win(INT32_MAX,INT32_MAX,INT32_MAX,INT32_MAX);
    assert(ku_display_fit_window(&d,&w));
    assert(w.x==34&&w.y==50&&w.width==594&&w.height==348);

    d=area(800,600); w=win(INT32_MIN,INT32_MIN,1,1);
    assert(ku_display_fit_window(&d,&w));
    assert(w.x==34&&w.y==50&&w.width==260&&w.height==160);

    d=area(800,600); w=win(400,110,510,560);
    d.work_x=-1;
    assert(!ku_display_fit_window(&d,&w));
    assert(w.x==400&&w.y==110);
    d=area(800,600); d.work_width=INT32_MAX;
    assert(!ku_display_fit_window(&d,&w));
    d=area(800,600); d.structure_size=0;
    assert(!ku_display_fit_window(&d,&w));
    d=area(800,600); w=win(0,0,-1,300);
    assert(!ku_display_fit_window(&d,&w));
    d=area(320,200); w=win(0,0,400,400);
    assert(!ku_display_fit_window(&d,&w));
    assert(!ku_display_fit_window((const ku_display_info*)0,&w));
    assert(!ku_display_fit_window(&d,(ku_ui_window_options*)0));
    return 0;
}
