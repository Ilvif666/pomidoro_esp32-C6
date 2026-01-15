#ifndef EEZ_LVGL_UI_EVENTS_H
#define EEZ_LVGL_UI_EVENTS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void action_spinner_pressed(lv_event_t * e);
extern void action_cbx_on_off_changed(lv_event_t * e);
extern void action_page1_clicked(lv_event_t * e);
extern void action_page2_clicked(lv_event_t * e);
extern void action_my_event(lv_event_t * e);
extern void action_cb_value_changed(lv_event_t * e);
extern void action_splashscreen_loaded(lv_event_t * e);


#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_EVENTS_H*/