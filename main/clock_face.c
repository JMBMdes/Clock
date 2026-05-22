#include "clock_face.h"

#include <math.h>
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"

static const char *TAG = "clock_face";

/* Display geometry */
#define CX  120     /* center x (screen coords) */
#define CY  120     /* center y (screen coords) */
#define R   107     /* usable clock radius in pixels */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* -----------------------------------------------------------------------
 * Persistent point arrays — LVGL holds pointers; must not be on the stack
 * ----------------------------------------------------------------------- */
static lv_point_t s_tick_pts[12][2];
static lv_point_t s_hour_pts[2];
static lv_point_t s_min_pts[2];
static lv_point_t s_sec_pts[2];

static lv_obj_t *s_hour_line;
static lv_obj_t *s_min_line;
static lv_obj_t *s_sec_line;

/* Convert clock angle (0 = 12 o'clock, clockwise) + radius → screen x,y */
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
    ESP_LOGI(TAG, "Building clock face (Phase 1 — static 10:10:32)");

    lvgl_port_lock(0);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0D0D1A), 0);

    /* Outer decorative ring — transparent fill, thin border */
    lv_obj_t *ring = lv_obj_create(scr);
    lv_obj_set_size(ring, 238, 238);
    lv_obj_set_pos(ring, 1, 1);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring, 2, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(0x4A4A8A), 0);
    lv_obj_set_style_pad_all(ring, 0, 0);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);

    /* Hour tick marks — 12 total, major at 0/3/6/9 o'clock positions */
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

    /* ---------------------------------------------------------------
     * Static time: 10:10:32 (classic watch advertisement position —
     * all three hands clearly visible and symmetrical)
     *
     * Hour   → 10 h * 30° + 10 min * 0.5°/min = 305°
     * Minute → 10 min * 6°/min                 = 60°
     * Second → 32 s  * 6°/s                    = 192°
     * --------------------------------------------------------------- */
    int hx, hy, mx, my, sx, sy, stx, sty;

    angle_to_xy(305.0f, 62.0f, &hx,  &hy);      /* hour tip */
    angle_to_xy( 60.0f, 85.0f, &mx,  &my);      /* minute tip */
    angle_to_xy(192.0f, 97.0f, &sx,  &sy);      /* second tip */
    angle_to_xy(192.0f - 180.0f, 22.0f, &stx, &sty); /* second counterweight */

    s_hour_pts[0] = (lv_point_t){CX, CY};
    s_hour_pts[1] = (lv_point_t){hx, hy};

    s_min_pts[0]  = (lv_point_t){CX, CY};
    s_min_pts[1]  = (lv_point_t){mx, my};

    s_sec_pts[0]  = (lv_point_t){stx, sty};   /* tail end */
    s_sec_pts[1]  = (lv_point_t){sx,  sy};    /* tip end */

    s_hour_line = make_hand(scr, s_hour_pts, lv_color_hex(0xEEEEFF), 6);
    s_min_line  = make_hand(scr, s_min_pts,  lv_color_hex(0xCCCCEE), 4);
    s_sec_line  = make_hand(scr, s_sec_pts,  lv_color_hex(0xFF4444), 2);

    /* Center cap — red dot over all hands */
    lv_obj_t *cap = lv_obj_create(scr);
    lv_obj_set_size(cap, 10, 10);
    lv_obj_set_pos(cap, CX - 5, CY - 5);
    lv_obj_set_style_radius(cap, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(cap, lv_color_hex(0xFF4444), 0);
    lv_obj_set_style_border_width(cap, 0, 0);

    /* Date label at lower-center of face */
    lv_obj_t *date_lbl = lv_label_create(scr);
    lv_label_set_text(date_lbl, "THU 22 MAY");
    lv_obj_set_style_text_color(date_lbl, lv_color_hex(0x8888AA), 0);
    lv_obj_align(date_lbl, LV_ALIGN_CENTER, 0, 42);

    lvgl_port_unlock();

    ESP_LOGI(TAG, "Clock face ready");
}

void clock_face_set_time(int hours, int minutes, int seconds)
{
    /* Phase 2 — will update hand angles and redraw */
    (void)hours;
    (void)minutes;
    (void)seconds;
}
