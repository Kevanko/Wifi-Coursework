#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "webserver.h"
#include "thermostat.h"

static const char *TAG = "MAIN";

#define WIFI_SSID "ESP32_THERMO_COURSE"
#define WIFI_PASS "12345678"
#define MAX_STA_CONN 4

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "Client connected to AP");
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        ESP_LOGI(TAG, "Client disconnected from AP");
    }
}

void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = (uint8_t)strlen(WIFI_SSID),
            .channel = 1,
            .password = WIFI_PASS,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    if (strlen(WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi AP started. SSID:%s PASS:%s", WIFI_SSID, WIFI_PASS);
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    thermostat_init();    // создаёт mutex и задачу датчика
    wifi_init_softap();
    start_webserver();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
