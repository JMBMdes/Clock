#pragma once
#include <stdbool.h>

/* Initialize the analog clock face (static scaffold). */
void clock_face_init(void);

/* Update hand positions from wall-clock time. */
void clock_face_set_time(int hours, int minutes, int seconds);

/* Update the date label (e.g. "THU 22 MAY"). */
void clock_face_set_date(const char *date_str);

/* Toggle NTP sync indicator dot (green = synced, grey = not synced). */
void clock_face_set_synced(bool synced);

/* Show a centred status overlay (e.g. "Connecting...").
 * Pass NULL to hide the overlay. */
void clock_face_show_status(const char *msg);
