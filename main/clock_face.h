#pragma once

/* Initialize the analog clock face (Phase 1: static display). */
void clock_face_init(void);

/* Update hand positions — implemented in Phase 2. */
void clock_face_set_time(int hours, int minutes, int seconds);
