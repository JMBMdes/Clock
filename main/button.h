#pragma once

typedef void (*button_cb_t)(void);

/* Configure GPIO0 (BOOT button) and start a polling task.
 * cb is called once after a continuous 5-second press. */
void button_init(button_cb_t cb);
