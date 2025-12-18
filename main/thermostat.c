#include "thermostat.h"
#include <stdio.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "onewire_bus.h"
#include "ds18b20.h"

static const char *TAG = "THERMOSTAT";

/* HW */
#define ONEWIRE_BUS_GPIO 13
#define LED_BLUE   GPIO_NUM_10
#define LED_GREEN  GPIO_NUM_9
#define LED_YELLOW GPIO_NUM_11
#define LED_ORANGE GPIO_NUM_12
#define LED_RED    GPIO_NUM_14

/* Global definitions (actual storage) */
led_settings_t g_settings = {
    .thresholds = {20.0f, 22.0f, 25.0f, 28.0f, 32.0f}
};
SemaphoreHandle_t settings_mutex = NULL;
float current_temperature = 0.0f;

static const gpio_num_t led_gpios[5] = {
    LED_BLUE, LED_GREEN, LED_YELLOW, LED_ORANGE, LED_RED
};

static void update_leds_internal(float temp)
{
    int active = -1;
    // choose highest threshold that is <= temp
    for (int i = 4; i >= 0; --i) {
        if (temp >= g_settings.thresholds[i]) {
            active = i;
            break;
        }
    }
    for (int i = 0; i < 5; ++i) {
        gpio_set_level(led_gpios[i], (i == active) ? 1 : 0);
    }
}

void update_leds(float temp)
{
    if (settings_mutex) {
        if (xSemaphoreTake(settings_mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
            update_leds_internal(temp);
            xSemaphoreGive(settings_mutex);
        } else {
            ESP_LOGW(TAG, "update_leds: mutex take timed out");
            update_leds_internal(temp); // best-effort
        }
    } else {
        update_leds_internal(temp);
    }
}

void thermostat_init(void)
{
    if (settings_mutex == NULL) {
        settings_mutex = xSemaphoreCreateMutex();
        configASSERT(settings_mutex);
    }

    for (int i = 0; i < 5; ++i) {
        gpio_reset_pin(led_gpios[i]);
        gpio_set_direction(led_gpios[i], GPIO_MODE_OUTPUT);
        gpio_set_level(led_gpios[i], 0);
    }

    // create task
    BaseType_t rc = xTaskCreate(thermostat_task, "thermo_task", 4096, NULL, 5, NULL);
    if (rc != pdPASS) {
        ESP_LOGE(TAG, "Failed to create thermostat_task");
    }
}

void thermostat_task(void *pvParameters)
{
    onewire_bus_handle_t bus = NULL;
    onewire_bus_config_t bus_config = {
        .bus_gpio_num = ONEWIRE_BUS_GPIO,
        .flags = { .en_pull_up = true }
    };
    onewire_bus_rmt_config_t rmt_config = { .max_rx_bytes = 10 };

    esp_err_t err = onewire_new_bus_rmt(&bus_config, &rmt_config, &bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "onewire_new_bus_rmt failed: %s", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    ds18b20_device_handle_t sensor = NULL;
    bool sensor_present = false;

    while (1) {
        if (!sensor_present) {
            ESP_LOGI(TAG, "Searching for DS18B20...");
            onewire_device_iter_handle_t iter = NULL;
            onewire_device_t dev;
            if (onewire_new_device_iter(bus, &iter) == ESP_OK) {
                while (onewire_device_iter_get_next(iter, &dev) == ESP_OK) {
                    ds18b20_config_t cfg = {};
                    if (ds18b20_new_device(&dev, &cfg, &sensor) == ESP_OK) {
                        sensor_present = true;
                        ds18b20_set_resolution(sensor, DS18B20_RESOLUTION_12B);
                        ESP_LOGI(TAG, "DS18B20 found and configured");
                        break;
                    }
                }
            }
            onewire_del_device_iter(iter);
            if (!sensor_present) {
                ESP_LOGW(TAG, "No DS18B20 found, retrying in 2s");
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }
        }

        // Trigger conversion
        if (sensor_present) {
            if (ds18b20_trigger_temperature_conversion(sensor) != ESP_OK) {
                ESP_LOGW(TAG, "trigger conversion failed, will retry sensor discovery");
                ds18b20_del_device(sensor);
                sensor = NULL;
                sensor_present = false;
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }

            vTaskDelay(pdMS_TO_TICKS(750)); // wait conversion (12-bit worst-case ~750ms)

            float temp = 0.0f;
            if (ds18b20_get_temperature(sensor, &temp) == ESP_OK) {
                if (xSemaphoreTake(settings_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                    current_temperature = temp;
                    xSemaphoreGive(settings_mutex);
                } else {
                    current_temperature = temp; // best-effort
                }
                ESP_LOGI(TAG, "Temperature: %.2f C", temp);
                update_leds(temp);
            } else {
                ESP_LOGW(TAG, "Failed to read temperature, dropping sensor handle");
                ds18b20_del_device(sensor);
                sensor = NULL;
                sensor_present = false;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
