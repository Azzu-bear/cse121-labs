/*
 * Lab 6.2 — Weather Station: post results
 * Author: Azam Mohamed
 *
 * Reads the ESP32-C3 internal temperature sensor and POSTs it as JSON
 * to a server on the same network at port 1234.
 *
 * The server can be the matching python script in lab7_server/.
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
#include "driver/temperature_sensor.h"

/* ---------- EDIT THESE ---------- */
#define WIFI_SSID    "iPhoXIV"
#define WIFI_PASS    "YOUR_PASSWORD"
#define SERVER_IP    "172.20.10.3"     /* IP of laptop/pi running the server */
#define SERVER_PORT  1234
/* -------------------------------- */

static const char *TAG = "lab6_2";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
#define MAX_RETRY 10

static temperature_sensor_handle_t temp_handle = NULL;

/* ---------------- WiFi (same as 7.1) ---------------- */
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

/* ---------------- temperature sensor ---------------- */
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

/* ---------------- POST to server ---------------- */
static void post_temp(float celsius) {
    char url[64];
    snprintf(url, sizeof(url), "http://%s:%d/temp", SERVER_IP, SERVER_PORT);

    char body[96];
    int len = snprintf(body, sizeof(body),
                       "{\"sensor_temp_c\": %.2f}", celsius);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, len);

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "POST %s body=%s status=%d",
                 url, body, esp_http_client_get_status_code(client));
    } else {
        ESP_LOGE(TAG, "POST failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
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
        float t = read_temp_c();
        printf("Sensor temp: %.2f C\n", t);
        post_temp(t);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
