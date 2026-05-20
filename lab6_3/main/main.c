/*
 * Lab 6.3 — Weather Station: integrate both
 * Author: Azam Mohamed
 *
 * Flow each cycle:
 *   1) GET http://SERVER_IP:1234/location  -> server returns its configured location
 *   2) GET http://wttr.in/<location>?format=%t&m  -> outdoor temperature (Celsius)
 *   3) Read ESP32-C3 internal temp sensor
 *   4) POST {location, outdoor_temp_c, sensor_temp_c} to server /report
 *   5) Print all three values locally over serial
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "driver/temperature_sensor.h"

/* ---------- EDIT THESE ---------- */
#define WIFI_SSID    "iPhoXIV"
#define WIFI_PASS    "YOUR_PASSWORD"
#define SERVER_IP    "172.20.10.3"
#define SERVER_PORT  1234
/* -------------------------------- */

static const char *TAG = "lab6_3";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
#define MAX_RETRY 10

static temperature_sensor_handle_t temp_handle = NULL;

/* shared response buffer for GETs */
static char  g_resp_buf[256];
static int   g_resp_len = 0;

/* ---------------- WiFi ---------------- */
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void) {
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t any_id, got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &event_handler, NULL, &any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &event_handler, NULL, &got_ip));

    wifi_config_t wifi_config = {
        .sta = { .ssid = WIFI_SSID, .password = WIFI_PASS, },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "waiting for wifi...");
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                       pdFALSE, pdFALSE, portMAX_DELAY);
}

/* ---------------- temp sensor ---------------- */
static void temp_sensor_init(void) {
    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    ESP_ERROR_CHECK(temperature_sensor_install(&cfg, &temp_handle));
    ESP_ERROR_CHECK(temperature_sensor_enable(temp_handle));
}

static float read_temp_c(void) {
    float t = 0.0f;
    ESP_ERROR_CHECK(temperature_sensor_get_celsius(temp_handle, &t));
    return t;
}

/* ---------------- HTTP helpers ---------------- */
static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA &&
        !esp_http_client_is_chunked_response(evt->client)) {
        int copy_len = evt->data_len;
        if (g_resp_len + copy_len >= (int) sizeof(g_resp_buf) - 1) {
            copy_len = sizeof(g_resp_buf) - 1 - g_resp_len;
        }
        if (copy_len > 0) {
            memcpy(g_resp_buf + g_resp_len, evt->data, copy_len);
            g_resp_len += copy_len;
            g_resp_buf[g_resp_len] = '\0';
        }
    }
    return ESP_OK;
}

/* GET into g_resp_buf. Returns ESP_OK on success. */
static esp_err_t http_get(const char *url, const char *user_agent) {
    g_resp_len = 0;
    g_resp_buf[0] = '\0';

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
        .user_agent = user_agent,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "GET %s status=%d", url,
                 esp_http_client_get_status_code(client));
    } else {
        ESP_LOGE(TAG, "GET %s failed: %s", url, esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
    return err;
}

/* trim trailing whitespace / newline in-place */
static void rstrip(char *s) {
    int n = (int) strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' ||
                     s[n-1] == ' '  || s[n-1] == '\t')) {
        s[--n] = '\0';
    }
}

/* convert spaces to '+' for url, in-place; caller guarantees buffer is OK */
static void urlize_spaces(char *s) {
    for (; *s; ++s) if (*s == ' ') *s = '+';
}

/* POST a JSON body. */
static void http_post_json(const char *url, const char *body) {
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "POST %s status=%d body=%s",
                 url, esp_http_client_get_status_code(client), body);
    } else {
        ESP_LOGE(TAG, "POST %s failed: %s", url, esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

/* ---------------- main cycle ---------------- */
static void weather_cycle(void) {
    /* 1) ask server where it thinks it is */
    char loc_url[64];
    snprintf(loc_url, sizeof(loc_url), "http://%s:%d/location", SERVER_IP, SERVER_PORT);

    if (http_get(loc_url, NULL) != ESP_OK) return;

    char location[128];
    strncpy(location, g_resp_buf, sizeof(location) - 1);
    location[sizeof(location) - 1] = '\0';
    rstrip(location);

    if (location[0] == '\0') {
        ESP_LOGE(TAG, "empty location from server");
        return;
    }

    /* 2) wttr.in for that location, metric */
    char location_url[160];
    strncpy(location_url, location, sizeof(location_url) - 1);
    location_url[sizeof(location_url) - 1] = '\0';
    urlize_spaces(location_url);

    char wttr_url[256];
    snprintf(wttr_url, sizeof(wttr_url),
             "http://wttr.in/%s?format=%%t&m", location_url);

    if (http_get(wttr_url, "curl/7.81.0") != ESP_OK) return;

    char outdoor[32];
    strncpy(outdoor, g_resp_buf, sizeof(outdoor) - 1);
    outdoor[sizeof(outdoor) - 1] = '\0';
    rstrip(outdoor);

    /* parse the leading numeric portion (handles "+17°C", "-2°C", etc) */
    float outdoor_c = (float) strtol(outdoor, NULL, 10);

    /* 3) read internal sensor */
    float sensor_c = read_temp_c();

    /* 4) log locally */
    printf("\n--- weather report ---\n");
    printf("Server location:    %s\n", location);
    printf("Outdoor (wttr.in):  %s  (parsed %.1f C)\n", outdoor, outdoor_c);
    printf("ESP32 sensor temp:  %.2f C\n", sensor_c);
    printf("----------------------\n");

    /* 5) POST everything back to server */
    char report_url[64];
    snprintf(report_url, sizeof(report_url),
             "http://%s:%d/report", SERVER_IP, SERVER_PORT);

    char body[256];
    snprintf(body, sizeof(body),
             "{\"location\":\"%s\",\"outdoor_temp_c\":%.1f,\"sensor_temp_c\":%.2f}",
             location, outdoor_c, sensor_c);

    http_post_json(report_url, body);
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init_sta();
    temp_sensor_init();

    while (1) {
        weather_cycle();
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
