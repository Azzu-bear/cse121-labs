/*
 * Lab 6.1 — Weather Station: get the weather
 * Author: Azam Mohamed
 *
 * Connects to WiFi, then queries wttr.in for the temperature in Celsius
 * at the configured location and prints it to the serial monitor.
 *
 * wttr.in trick used: the "?format=%t" query returns just the temperature
 * (e.g. "+17°C"). With "?format=%l:+%t" we also get the location name.
 * Using metric units (m=...) so it comes back in Celsius.
 */

#include <stdio.h>
#include <string.h>
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

/* ---------- EDIT THESE ---------- */
#define WIFI_SSID       "iPhoXIV"
#define WIFI_PASS    "YOUR_PASSWORD"
#define WEATHER_CITY    "Santa+Cruz"     /* use + for spaces, or leave blank for auto */
/* -------------------------------- */

static const char *TAG = "lab6_1";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
#define MAX_RETRY 10

/* small buffer to hold the wttr.in response */
static char response_buf[256];
static int  response_len = 0;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry connect to AP (%d)", s_retry_num);
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

    esp_event_handler_instance_t any_id;
    esp_event_handler_instance_t got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &event_handler, NULL, &any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &event_handler, NULL, &got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished, waiting for connection...");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to %s", WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "failed to connect to %s", WIFI_SSID);
    }
}

/* HTTP event handler — accumulates response body into response_buf */
static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client)) {
                int copy_len = evt->data_len;
                if (response_len + copy_len >= (int) sizeof(response_buf) - 1) {
                    copy_len = sizeof(response_buf) - 1 - response_len;
                }
                if (copy_len > 0) {
                    memcpy(response_buf + response_len, evt->data, copy_len);
                    response_len += copy_len;
                    response_buf[response_len] = '\0';
                }
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

static void fetch_weather(void) {
    /* Build URL — %t = temperature, m = metric (Celsius)
     * Example: http://wttr.in/Santa+Cruz?format=%t&m
     */
    char url[160];
    snprintf(url, sizeof(url), "http://wttr.in/%s?format=%%l:+%%t&m", WEATHER_CITY);

    ESP_LOGI(TAG, "GET %s", url);

    response_len = 0;
    response_buf[0] = '\0';

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
        .user_agent = "curl/7.81.0",   /* wttr.in returns plain text for curl UA */
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP status = %d, body = %s", status, response_buf);
        printf("Weather: %s\n", response_buf);
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

void app_main(void) {
    /* NVS — required by WiFi */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init_sta();

    /* Loop and re-fetch every 30s */
    while (1) {
        fetch_weather();
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
