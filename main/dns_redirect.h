#pragma once

/* Start a UDP task on port 53 that answers all DNS queries with 192.168.4.1,
 * triggering the OS captive-portal popup on iOS, Android, and Windows. */
void dns_redirect_start(void);
