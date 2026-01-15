#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _objects_t {
    lv_obj_t *spalsh_screen;
    lv_obj_t *main;
    lv_obj_t *second;
    lv_obj_t *seventh;
    lv_obj_t *splash_bg;
    lv_obj_t *pgs_splash;
    lv_obj_t *lbl_counter;
    lv_obj_t *spin_loading;
    lv_obj_t *cb_1;
    lv_obj_t *btn_onoff_1;
    lv_obj_t *prg_1;
    lv_obj_t *lbl_on_off;
    lv_obj_t *lbl_vbat;
    lv_obj_t *lbl_vbat_1;
    lv_obj_t *ctnr_statusbar;
    lv_obj_t *lbl_uptime;
    lv_obj_t *lbl_wifi;
} objects_t;

extern objects_t objects;

enum ScreensEnum {
    SCREEN_ID_SPALSH_SCREEN = 1,
    SCREEN_ID_MAIN = 2,
    SCREEN_ID_SECOND = 3,
    SCREEN_ID_SEVENTH = 4,
};

void create_screen_spalsh_screen();
void tick_screen_spalsh_screen();

void create_screen_main();
void tick_screen_main();

void create_screen_second();
void tick_screen_second();

void create_screen_seventh();
void tick_screen_seventh();

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();


#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/