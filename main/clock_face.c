#include "clock_face.h"

#include <math.h>
#include <string.h>
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"

static const char *TAG = "clock_face";

#define CX  120
#define CY  120
#define R   107

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static lv_point_t s_tick_pts[12][2];
static lv_point_t s_hour_pts[2];
static lv_point_t s_min_pts[2];
static lv_point_t s_sec_pts[2];

static lv_obj_t *s_hour_line;
static lv_obj_t *s_min_line;
static lv_obj_t *s_sec_line;
static lv_obj_t *s_date_lbl;
static lv_obj_t *s_sync_dot;
static lv_obj_t *s_status_lbl;

static void angle_to_xy(float deg, float r, int *x, int *y)
{
    float rad = (deg - 90.0f) * (float)M_PI / 180.0f;
    *x = CX + (int)(r * cosf(rad) + 0.5f);
    *y = CY + (int)(r * sinf(rad) + 0.5f);
}

static lv_obj_t *make_hand(lv_obj_t *parent, lv_point_t *pts,
                            lv_color_t color, int width)
{
    lv_obj_t *line = lv_line_create(parent);
    lv_line_set_points(line, pts, 2);
    lv_obj_set_style_line_color(line, color, 0);
    lv_obj_set_style_line_width(line, width, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    return line;
}

void clock_face_init(void)
{
    ESP_LOGI(TAG, "Building clock face");

    lvgl_port_lock(0);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0D0D1A), 0);

    /* Outer decorative ring */
    lv_obj_t *ring = lv_obj_create(scr);
    lv_obj_set_size(ring, 238, 238);
    lv_obj_set_pos(ring, 1, 1);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring, 2, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(0x4A4A8A), 0);
    lv_obj_set_style_pad_all(ring, 0, 0);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);

    /* Hour tick marks */
    for (int i = 0; i < 12; i++) {
        float angle   = i * 30.0f;
        bool  major   = (i % 3 == 0);
        int   x1, y1, x2, y2;
        float inner_r = (float)(R - (major ? 16 : 8));

        angle_to_xy(angle, inner_r,  &x1, &y1);
        angle_to_xy(angle, (float)R, &x2, &y2);
        s_tick_pts[i][0] = (lv_point_t){x1, y1};
        s_tick_pts[i][1] = (lv_point_t){x2, y2};

        lv_obj_t *tick = lv_line_create(scr);
        lv_line_set_points(tick, s_tick_pts[i], 2);
        lv_obj_set_style_line_color(tick,
            major ? lv_color_hex(0xDDDDEE) : lv_color_hex(0x555566), 0);
        lv_obj_set_style_line_width(tick, major ? 3 : 1, 0);
    }

    /* Initial hand positions at 12:00:00 */
    int hx, hy, mx, my, sx, sy, stx, sty;
    angle_to_xy(0.0f,   62.0f, &hx,  &hy);
    angle_to_xy(0.0f,   85.0f, &mx,  &my);
    angle_to_xy(0.0f,   97.0f, &sx,  &sy);
    angle_to_xy(180.0f, 22.0f, &stx, &sty);

    s_hour_pts[0] = (lv_point_t){CX, CY};
    s_hour_pts[1] = (lv_point_t){hx, hy};
    s_min_pts[0]  = (lv_point_t){CX, CY};
    s_min_pts[1]  = (lv_point_t){mx, my};
    s_sec_pts[0]  = (lv_point_t){stx, sty};
    s_sec_pts[1]  = (lv_point_t){sx,  sy};

    s_hour_line = make_hand(scr, s_hour_pts, lv_color_hex(0xEEEEFF), 6);
    s_min_line  = make_hand(scr, s_min_pts,  lv_color_hex(0xCCCCEE), 4);
    s_sec_line  = make_hand(scr, s_sec_pts,  lv_color_hex(0xFF4444), 2);

    /* Center cap */
    lv_obj_t *cap = lv_obj_create(scr);
    lv_obj_set_size(cap, 10, 10);
    lv_obj_set_pos(cap, CX - 5, CY - 5);
    lv_obj_set_style_radius(cap, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(cap, lv_color_hex(0xFF4444), 0);
    lv_obj_set_style_border_width(cap, 0, 0);

    /* Date label */
    s_date_lbl = lv_label_create(scr);
    lv_label_set_text(s_date_lbl, "--");
    lv_obj_set_style_text_color(s_date_lbl, lv_color_hex(0x8888AA), 0);
    lv_obj_align(s_date_lbl, LV_ALIGN_CENTER, 0, 42);

    /* NTP sync indicator — small dot at 2 o'clock position (inside ring) */
    s_sync_dot = lv_obj_create(scr);
    lv_obj_set_size(s_sync_dot, 8, 8);
    lv_obj_set_pos(s_sync_dot, 191, 72);   /* ~2 o'clock, r≈78 */
    lv_obj_set_style_radius(s_sync_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_sync_dot, lv_color_hex(0x555566), 0);
    lv_obj_set_style_border_width(s_sync_dot, 0, 0);

    /* Status overlay label — hidden by default */
    s_status_lbl = lv_label_create(scr);
    lv_label_set_text(s_status_lbl, "");
    lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(s_status_lbl, lv_color_hex(0x0D0D1A), 0);
    lv_obj_set_style_bg_opa(s_status_lbl, LV_OPA_80, 0);
    lv_obj_set_style_pad_all(s_status_lbl, 6, 0);
    lv_obj_align(s_status_lbl, LV_ALIGN_CENTER, 0, -42);
    lv_obj_add_flag(s_status_lbl, LV_OBJ_FLAG_HIDDEN);

    lvgl_port_unlock();

    ESP_LOGI(TAG, "Clock face ready");
}

void clock_face_set_time(int hours, int minutes, int seconds)
{
    float sec_deg  = seconds * 6.0f;
    float min_deg  = minutes * 6.0f + seconds * 0.1f;
    float hour_deg = (hours % 12) * 30.0f + minutes * 0.5f;

    int hx, hy, mx, my, sx, sy, stx, sty;
    angle_to_xy(hour_deg,        62.0f, &hx,  &hy);
    angle_to_xy(min_deg,         85.0f, &mx,  &my);
    angle_to_xy(sec_deg,         97.0f, &sx,  &sy);
    angle_to_xy(sec_deg + 180.0f, 22.0f, &stx, &sty);

    lvgl_port_lock(0);
    s_hour_pts[1] = (lv_point_t){hx, hy};
    s_min_pts[1]  = (lv_point_t){mx, my};
    s_sec_pts[0]  = (lv_point_t){stx, sty};
    s_sec_pts[1]  = (lv_point_t){sx,  sy};
    lv_line_set_points(s_hour_line, s_hour_pts, 2);
    lv_line_set_points(s_min_line,  s_min_pts,  2);
    lv_line_set_points(s_sec_line,  s_sec_pts,  2);
    lvgl_port_unlock();
}

void clock_face_set_date(const char *date_str)
{
    if (!s_date_lbl) return;
    lvgl_port_lock(0);
    lv_label_set_text(s_date_lbl, date_str);
    lvgl_port_unlock();
}

void clock_face_set_synced(bool synced)
{
    if (!s_sync_dot) return;
    lvgl_port_lock(0);
    lv_obj_set_style_bg_color(s_sync_dot,
        synced ? lv_color_hex(0x22CC44) : lv_color_hex(0x555566), 0);
    lvgl_port_unlock();
}

void clock_face_show_status(const char *msg)
{
    if (!s_status_lbl) return;
    lvgl_port_lock(0);
    if (msg && msg[0]) {
        lv_label_set_text(s_status_lbl, msg);
        lv_obj_clear_flag(s_status_lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_status_lbl, LV_OBJ_FLAG_HIDDEN);
    }
    lvgl_port_unlock();
}
