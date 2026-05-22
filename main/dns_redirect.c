#include "dns_redirect.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include <string.h>

static const char *TAG = "dns_redirect";

#define DNS_PORT     53
#define DNS_BUF_SIZE 512

/* All DNS queries are answered with 192.168.4.1 */
static const uint8_t PORTAL_IP[4] = {192, 168, 4, 1};

static void dns_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket() failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind() failed on port 53");
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "DNS redirect listening on port 53");

    uint8_t buf[DNS_BUF_SIZE];
    uint8_t resp[DNS_BUF_SIZE];
    struct sockaddr_in client;
    socklen_t client_len = sizeof(client);

    while (1) {
        int len = recvfrom(sock, buf, sizeof(buf) - 1, 0,
                           (struct sockaddr *)&client, &client_len);
        if (len < 12) continue;

        /* Copy header + question verbatim, then patch the header */
        memcpy(resp, buf, len);
        resp[2] = 0x81;  /* QR=1, AA=1, RD=1 */
        resp[3] = 0x80;  /* RA=1, RCODE=0 */
        resp[6] = 0; resp[7] = 1;  /* ANCOUNT = 1 */
        resp[8] = resp[9] = resp[10] = resp[11] = 0;

        /* Find end of QNAME in the question section */
        int pos = 12;
        while (pos < len && buf[pos] != 0) {
            pos += buf[pos] + 1;
            if (pos >= len) break;
        }
        pos += 5;  /* null label + QTYPE(2) + QCLASS(2) */

        if (pos > len || pos + 16 > (int)sizeof(resp)) continue;

        int rlen = pos;
        /* Answer: name ptr to offset 12, type A, class IN, TTL 10, 4-byte RDATA */
        resp[rlen++] = 0xC0; resp[rlen++] = 0x0C;
        resp[rlen++] = 0x00; resp[rlen++] = 0x01;  /* type A  */
        resp[rlen++] = 0x00; resp[rlen++] = 0x01;  /* class IN */
        resp[rlen++] = 0x00; resp[rlen++] = 0x00;
        resp[rlen++] = 0x00; resp[rlen++] = 0x0A;  /* TTL 10 s */
        resp[rlen++] = 0x00; resp[rlen++] = 0x04;  /* RDLENGTH */
        memcpy(&resp[rlen], PORTAL_IP, 4);
        rlen += 4;

        sendto(sock, resp, rlen, 0,
               (struct sockaddr *)&client, client_len);
    }
}

void dns_redirect_start(void)
{
    xTaskCreate(dns_task, "dns_redirect", 4096, NULL, 5, NULL);
}
