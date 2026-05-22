#include "portal.h"
#include "config.h"
#include "clock_face.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "portal";

static httpd_handle_t s_server  = NULL;
static config_t      *s_cfg_ref = NULL;

/* -----------------------------------------------------------------------
 * Embedded configuration page
 * ----------------------------------------------------------------------- */
static const char PORTAL_HTML[] =
    "<!DOCTYPE html><html><head>"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Clock Setup</title>"
    "<style>"
    "body{font-family:sans-serif;max-width:420px;margin:28px auto;padding:0 16px}"
    "h1{margin-bottom:4px;color:#222}p.sub{color:#666;font-size:13px;margin-top:0}"
    "label{display:block;margin-top:14px;font-weight:600;font-size:14px}"
    "input,select{width:100%;padding:8px;margin-top:4px;box-sizing:border-box;"
    "border:1px solid #ccc;border-radius:4px;font-size:14px}"
    "input[type=range]{margin-top:6px}"
    ".hint{font-size:11px;color:#888;margin-top:2px}"
    "button{width:100%;padding:12px;margin-top:22px;background:#0066cc;"
    "color:#fff;border:none;border-radius:4px;font-size:16px;cursor:pointer}"
    "button:hover{background:#0055aa}"
    "</style></head><body>"
    "<h1>&#x23F0; Clock Setup</h1>"
    "<p class=\"sub\">Connect to your WiFi and set your timezone.</p>"
    "<form method=\"POST\" action=\"/config\">"
    "<label>WiFi Network"
    "<input id=\"ssid\" name=\"ssid\" type=\"text\" required maxlength=\"32\" list=\"nets\""
    " placeholder=\"Select or type SSID\">"
    "<datalist id=\"nets\"></datalist></label>"
    "<label>WiFi Password"
    "<input name=\"password\" type=\"password\" maxlength=\"63\""
    " placeholder=\"Leave empty for open networks\">"
    "<span class=\"hint\">WPA2: min 8 characters</span></label>"
    "<label>NTP Server"
    "<input name=\"ntp_server\" type=\"text\" value=\"pool.ntp.org\" maxlength=\"63\"></label>"
    "<label>Timezone (POSIX TZ string)"
    "<input name=\"timezone\" type=\"text\" value=\"UTC0\" maxlength=\"47\">"
    "<span class=\"hint\">e.g. UTC0 &nbsp;|&nbsp; BRT3 &nbsp;|&nbsp;"
    " EST5EDT,M3.2.0,M11.1.0</span></label>"
    "<label>Brightness: <span id=\"bv\">80</span>%"
    "<input name=\"brightness\" type=\"range\" min=\"0\" max=\"100\" value=\"80\""
    " oninput=\"document.getElementById('bv').textContent=this.value\"></label>"
    "<button type=\"submit\">Save &amp; Reboot</button>"
    "</form>"
    "<script>"
    "fetch('/scan').then(r=>r.json()).then(nets=>{"
    "var dl=document.getElementById('nets');"
    "nets.forEach(function(n){"
    "var o=document.createElement('option');"
    "o.value=n.ssid;o.label=n.ssid+' ('+n.rssi+' dBm)';"
    "dl.appendChild(o);});"
    "}).catch(function(){});"
    "</script></body></html>";

/* -----------------------------------------------------------------------
 * URL / form helpers
 * ----------------------------------------------------------------------- */
static void url_decode(char *s)
{
    char *out = s;
    while (*s) {
        if (*s == '+') {
            *out++ = ' '; s++;
        } else if (*s == '%' && s[1] && s[2]) {
            char hex[3] = {s[1], s[2], '\0'};
            *out++ = (char)strtol(hex, NULL, 16);
            s += 3;
        } else {
            *out++ = *s++;
        }
    }
    *out = '\0';
}

static void form_field(const char *body, const char *key, char *out, size_t out_sz)
{
    out[0] = '\0';
    size_t klen = strlen(key);
    const char *p = body;
    while (p && *p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            const char *val = p + klen + 1;
            const char *end = strchr(val, '&');
            size_t len = end ? (size_t)(end - val) : strlen(val);
            if (len >= out_sz) len = out_sz - 1;
            memcpy(out, val, len);
            out[len] = '\0';
            url_decode(out);
            return;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
}

/* -----------------------------------------------------------------------
 * Reboot helper — fires 1.5 s after response is sent
 * ----------------------------------------------------------------------- */
static void reboot_cb(TimerHandle_t t)
{
    (void)t;
    esp_restart();
}

static void schedule_reboot(void)
{
    TimerHandle_t t = xTimerCreate("reboot", pdMS_TO_TICKS(1500),
                                   pdFALSE, NULL, reboot_cb);
    if (t) xTimerStart(t, 0);
}

/* -----------------------------------------------------------------------
 * Redirect to /config
 * ----------------------------------------------------------------------- */
static esp_err_t redirect_to_config(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/config");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* -----------------------------------------------------------------------
 * GET /
 * ----------------------------------------------------------------------- */
static esp_err_t handle_root_get(httpd_req_t *req)
{
    return redirect_to_config(req);
}

/* -----------------------------------------------------------------------
 * GET /config
 * ----------------------------------------------------------------------- */
static esp_err_t handle_config_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, PORTAL_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* -----------------------------------------------------------------------
 * POST /config
 * ----------------------------------------------------------------------- */
static esp_err_t handle_config_post(httpd_req_t *req)
{
    char body[512] = {};
    int  received  = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }
    body[received] = '\0';

    char ssid[CFG_SSID_MAX], pass[CFG_PASS_MAX];
    char ntp[CFG_NTP_MAX], tz[CFG_TZ_MAX], bright_str[8];

    form_field(body, "ssid",       ssid,      sizeof(ssid));
    form_field(body, "password",   pass,      sizeof(pass));
    form_field(body, "ntp_server", ntp,       sizeof(ntp));
    form_field(body, "timezone",   tz,        sizeof(tz));
    form_field(body, "brightness", bright_str, sizeof(bright_str));

    /* Validation — EC-CP-203 */
    if (ssid[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID required");
        return ESP_FAIL;
    }
    if (pass[0] != '\0' && strlen(pass) < 8) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "WPA2 password must be at least 8 characters");
        return ESP_FAIL;
    }
    if (ntp[0] == '\0') strncpy(ntp, "pool.ntp.org", sizeof(ntp) - 1);
    if (tz[0]  == '\0') strncpy(tz,  "UTC0",         sizeof(tz)  - 1);

    int brightness = bright_str[0] ? atoi(bright_str) : 80;
    if (brightness < 0)   brightness = 0;
    if (brightness > 100) brightness = 100;

    config_t cfg = {};
    strncpy(cfg.wifi_ssid,  ssid, CFG_SSID_MAX - 1);
    strncpy(cfg.wifi_pass,  pass, CFG_PASS_MAX - 1);
    strncpy(cfg.ntp_server, ntp,  CFG_NTP_MAX  - 1);
    strncpy(cfg.timezone,   tz,   CFG_TZ_MAX   - 1);
    cfg.brightness = (uint8_t)brightness;
    config_save(&cfg);
    if (s_cfg_ref) *s_cfg_ref = cfg;

    clock_face_show_status("Saved! Rebooting...");

    static const char OK_HTML[] =
        "<!DOCTYPE html><html><body style=\"font-family:sans-serif;text-align:center;"
        "padding-top:60px\"><h2>&#x2714; Saved!</h2>"
        "<p>The clock will reboot and connect to <b>%s</b>.</p></body></html>";
    char resp_buf[256];
    snprintf(resp_buf, sizeof(resp_buf), OK_HTML, ssid);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, resp_buf, HTTPD_RESP_USE_STRLEN);

    schedule_reboot();
    return ESP_OK;
}

/* -----------------------------------------------------------------------
 * GET /scan  — JSON array of {ssid, rssi}
 * ----------------------------------------------------------------------- */
static esp_err_t handle_scan_get(httpd_req_t *req)
{
    esp_err_t err = esp_wifi_scan_start(NULL, true);  /* blocking */
    if (err != ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "[]", 2);
        return ESP_OK;
    }

    uint16_t count = 0;
    esp_wifi_scan_get_ap_num(&count);
    if (count > 20) count = 20;

    wifi_ap_record_t *aps = malloc(count * sizeof(wifi_ap_record_t));
    if (!aps) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "[]", 2);
        return ESP_OK;
    }
    esp_wifi_scan_get_ap_records(&count, aps);

    /* Build JSON — rough bound: 80 chars per entry */
    char *json = malloc(count * 80 + 4);
    if (!json) { free(aps); httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM"); return ESP_FAIL; }

    int pos = 0;
    json[pos++] = '[';
    for (int i = 0; i < count; i++) {
        /* Escape SSID: replace " with \" */
        char esc[66] = {};
        int e = 0;
        for (int j = 0; aps[i].ssid[j] && e < 62; j++) {
            if (aps[i].ssid[j] == '"' || aps[i].ssid[j] == '\\')
                esc[e++] = '\\';
            esc[e++] = aps[i].ssid[j];
        }
        pos += snprintf(json + pos, 80, "%s{\"ssid\":\"%s\",\"rssi\":%d}",
                        i ? "," : "", esc, aps[i].rssi);
    }
    json[pos++] = ']';
    json[pos]   = '\0';

    free(aps);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    return ESP_OK;
}

/* -----------------------------------------------------------------------
 * POST /reset  — factory reset
 * ----------------------------------------------------------------------- */
static esp_err_t handle_reset_post(httpd_req_t *req)
{
    static const char RST_HTML[] =
        "<!DOCTYPE html><html><body style=\"font-family:sans-serif;text-align:center;"
        "padding-top:60px\"><h2>&#x1F504; Resetting...</h2>"
        "<p>All settings erased. Device will reboot into setup mode.</p></body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, RST_HTML, HTTPD_RESP_USE_STRLEN);

    config_erase();
    clock_face_show_status("Resetting...");
    schedule_reboot();
    return ESP_OK;
}

/* -----------------------------------------------------------------------
 * 404 catch-all → redirect (triggers captive portal popup on iOS/Android)
 * ----------------------------------------------------------------------- */
static esp_err_t handle_404(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    return redirect_to_config(req);
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */
void portal_start(config_t *cfg)
{
    s_cfg_ref = cfg;

    httpd_config_t hcfg = HTTPD_DEFAULT_CONFIG();
    hcfg.max_uri_handlers = 8;

    if (httpd_start(&s_server, &hcfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    static const httpd_uri_t routes[] = {
        { .uri = "/",       .method = HTTP_GET,  .handler = handle_root_get   },
        { .uri = "/config", .method = HTTP_GET,  .handler = handle_config_get },
        { .uri = "/config", .method = HTTP_POST, .handler = handle_config_post},
        { .uri = "/scan",   .method = HTTP_GET,  .handler = handle_scan_get   },
        { .uri = "/reset",  .method = HTTP_POST, .handler = handle_reset_post },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++)
        httpd_register_uri_handler(s_server, &routes[i]);

    httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, handle_404);

    ESP_LOGI(TAG, "Captive portal running at http://192.168.4.1/config");
}

void portal_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
