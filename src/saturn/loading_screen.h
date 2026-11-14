#ifndef KOF_LOADING_SCREEN_H
#define KOF_LOADING_SCREEN_H
#include <saturn/loader.h>
#include <saturn/video.h>
#include <saturn/hud.h>

/* The same LibSaturn bar used by sa2-saturn. Keep the font outside the
 * replay atlas, and throttle redraws to avoid adding a VBlank per CD read. */
typedef struct {
    sat_loading_screen_t screen;
    sat_ascii_font_t font;
    sat_hud_t hud;
    const char *phase;
    uint32_t clip, clips, percent, last_percent;
    uint8_t font_ready;
} kof_loading;

static void kof_loading_decorate(void *context) {
    kof_loading *l=context;
    if(!l->font_ready)return;
    (void)sat_hud_text_centered(&l->hud,"THE KING OF FIGHTERS '98",160,76);
    (void)sat_hud_text_centered(&l->hud,l->phase,160,112);
    (void)sat_hud_value(&l->hud,"BLOCK",l->clip,60,136);
    (void)sat_hud_value(&l->hud,"TOTAL",l->clips,180,136);
    (void)sat_hud_value(&l->hud,"PERCENT",l->percent,116,192);
}
static void kof_loading_init(kof_loading *l) {
    sat_loading_screen_default(&l->screen);
    l->screen.background=0x8800u;
    l->screen.fill=0x83ffu;
    l->screen.decorate=kof_loading_decorate;
    l->screen.decorate_context=l;
    l->font_ready=sat_hud_init_default(&l->hud,&l->font)==SAT_OK;
    l->last_percent=UINT32_MAX;
}
static void kof_loading_update(kof_loading *l,uint32_t done,uint32_t total,int force) {
    if(!total)return;
    if(done>total)done=total;
    uint32_t percent=done*100u/total;
    if(!force&&l->last_percent!=UINT32_MAX&&percent<l->last_percent+4u&&done!=total)return;
    l->percent=percent;l->last_percent=percent;
    /* VDP1 erase may be opaque after gameplay; set it as well as VDP2 back. */
    (void)sat_set_clear_color(l->screen.background);
    sat_loading_screen_progress(&l->screen,done,total);
}
static void kof_loading_phase(kof_loading *l,const char *phase,uint32_t clip,uint32_t clips) {
    l->phase=phase;l->clip=clip;l->clips=clips;l->last_percent=UINT32_MAX;
    kof_loading_update(l,0,100,1);
}
#endif
