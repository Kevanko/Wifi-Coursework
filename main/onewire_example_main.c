/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "onewire_bus.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "ds18b20.h"

static const char *TAG = "example";

// ИЗМЕНЕНИЕ: Используем GPIO13 для линии DQ согласно заданию
#define EXAMPLE_ONEWIRE_BUS_GPIO    13

#define EXAMPLE_ONEWIRE_MAX_DS18B20 3 // Максимальное количество датчиков для поиска

#define LED_GREEN GPIO_NUM_9 // Задаем GPIO - встроенный светодиод на плате
#define LED_BLUE GPIO_NUM_10
#define LED_YELLOW GPIO_NUM_11
#define LED_ORANGE GPIO_NUM_12 // Задаем GPIO - встроенный светодиод на плате
#define LED_RED GPIO_NUM_14
#define MAX_STAGES 3
#define MAX_LEDS 5

struct led_t {
    float t;
    int led;
};


struct led_t g_led_arr[5] = {{20, LED_BLUE}, {22, LED_GREEN}, {25, LED_YELLOW}, {28, LED_ORANGE}, {32, LED_RED}};

enum ledIDs{
    blue,
    green,
    yellow,
    orange,
    red,
};

void set_all(int val){
    for(int i = 0; i < 5; i++){
        gpio_set_level(g_led_arr[i].led, val);
    }
}
// **Исправленная функция set_temperature_color**
void set_temperature_color(float temperature){
    // Сначала выключаем все светодиоды (опционально, но рекомендуется)
    // set_all(0); 

    int led_to_turn_on = -1;

    // 1. Проверяем самый высокий порог (red)
    if (temperature >= g_led_arr[red].t) {
        led_to_turn_on = red;
    } 
    // 2. Ищем, в какой диапазон попадает температура (от blue до orange)
    else {
        // Идем от самого низкого порога (blue) до предпоследнего (orange)
        for(int i = 0; i < MAX_LEDS - 1; i++) {
            // Если температура больше или равна текущему порогу И меньше следующего порога
            if (temperature >= g_led_arr[i].t && temperature < g_led_arr[i + 1].t) {
                led_to_turn_on = i;
                break; // Нашли, выходим из цикла
            }
        }
    }
    
    // 3. Обработка случая, когда температура НИЖЕ самого низкого порога (blue)
    // Если led_to_turn_on остается -1, это означает, что temperature < g_led_arr[blue].t
    if (led_to_turn_on == -1) {
        // Ни один светодиод не горит (можно оставить так или включить blue)
         led_to_turn_on = blue; // Если нужно, чтобы blue горел при T < 15.0
    }
    
    // 4. Устанавливаем уровни
    for(int i = 0; i < MAX_LEDS; i++){
        gpio_set_level(g_led_arr[i].led, (i == led_to_turn_on) ? 1 : 0);
    }
}

void police_mode(){
    for(int i = 0; i < 5; i++){
        gpio_set_level(LED_BLUE, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
        gpio_set_level(LED_BLUE, 0);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    for(int i = 0; i < 5; i++){
        gpio_set_level(LED_RED, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
        gpio_set_level(LED_RED, 0);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void debug_leds(){
    static int count = 0;
    count++;
    if(count > 10) count = 1;
    if(count > 1){ 
        police_mode();
        return;
    }
    int stage = 0;
    while(stage != MAX_STAGES){
        for(int i = 0; i < ((stage == 2)? 3 : 5); i++){
            int id = i;
            if(stage == 1)
                id = 4 - i;

            if(stage == 2) set_all(1);
            else gpio_set_level(g_led_arr[id].led, 1);

            vTaskDelay(pdMS_TO_TICKS(100));

            if(stage == 2){ set_all(0); vTaskDelay(pdMS_TO_TICKS(100));}
            else gpio_set_level(g_led_arr[id].led, 0);
        }
        stage++;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main(void)
{
    gpio_set_direction(LED_BLUE, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_GREEN, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_RED, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_ORANGE, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_YELLOW, GPIO_MODE_OUTPUT);
    // Установка новой 1-Wire шины
    onewire_bus_handle_t bus;
    onewire_bus_config_t bus_config = {
        .bus_gpio_num = EXAMPLE_ONEWIRE_BUS_GPIO,
        .flags = {
            .en_pull_up = true, // Включаем внутренний подтягивающий резистор
        }
    };
    onewire_bus_rmt_config_t rmt_config = {
        .max_rx_bytes = 10,
    };
    ESP_ERROR_CHECK(onewire_new_bus_rmt(&bus_config, &rmt_config, &bus));
    ESP_LOGI(TAG, "1-Wire bus installed on GPIO%d (DQ)", EXAMPLE_ONEWIRE_BUS_GPIO);

    int ds18b20_device_num = 0;
    // Массив для хранения хэндлов найденных датчиков
    ds18b20_device_handle_t ds18b20s[EXAMPLE_ONEWIRE_MAX_DS18B20];
    onewire_device_iter_handle_t iter = NULL;
    onewire_device_t next_onewire_device;
    esp_err_t search_result = ESP_OK;

    // Создаем итератор для поиска устройств на шине
    ESP_ERROR_CHECK(onewire_new_device_iter(bus, &iter));
    ESP_LOGI(TAG, "Device iterator created, start searching...");
    
    // Поиск и инициализация DS18B20
    do {
        search_result = onewire_device_iter_get_next(iter, &next_onewire_device);
        if (search_result == ESP_OK) { 
            ds18b20_config_t ds_cfg = {};
            // Пытаемся преобразовать найденное устройство в DS18B20
            if (ds18b20_new_device(&next_onewire_device, &ds_cfg, &ds18b20s[ds18b20_device_num]) == ESP_OK) {
                ESP_LOGI(TAG, "Found a DS18B20[%d], address: %016llX", ds18b20_device_num, next_onewire_device.address);
                ds18b20_device_num++;
                if (ds18b20_device_num >= EXAMPLE_ONEWIRE_MAX_DS18B20) {
                    ESP_LOGI(TAG, "Max DS18B20 number reached, stop searching...");
                    break;
                }
            } else {
                ESP_LOGI(TAG, "Found an unknown device, address: %016llX", next_onewire_device.address);
            }
            police_mode();
        }
    } while (search_result != ESP_ERR_NOT_FOUND);
    ESP_ERROR_CHECK(onewire_del_device_iter(iter));
    ESP_LOGI(TAG, "Searching done, %d DS18B20 device(s) found", ds18b20_device_num);

    // Установка разрешения для всех найденных DS18B20
    for (int i = 0; i < ds18b20_device_num; i++) {
        ESP_ERROR_CHECK(ds18b20_set_resolution(ds18b20s[i], DS18B20_RESOLUTION_12B));
    }

    // Основной цикл считывания температуры
    float temperature;
    while (1) {
        // 1. Запуск преобразования температуры для ВСЕХ датчиков
        // Это делается быстро, так как команда отправляется всем устройствам на шине
        for (int i = 0; i < ds18b20_device_num; i ++) {
            ESP_LOGD(TAG, "Triggering conversion for DS18B20[%d]", i);
            ESP_ERROR_CHECK(ds18b20_trigger_temperature_conversion(ds18b20s[i]));
        }
        
        // 2. Ожидание завершения преобразования (750 мс для 12-битного разрешения)
        // ВАЖНО: это и есть ключевой момент при работе с несколькими датчиками
        ESP_LOGI(TAG, "Waiting 750ms for conversion...");
        vTaskDelay(pdMS_TO_TICKS(750));

        //LIIIIIIIIIIGHT!!!
        //debug_leds();


        // 3. Считывание результатов из всех датчиков
        for (int i = 0; i < ds18b20_device_num; i ++) {
            ESP_ERROR_CHECK(ds18b20_get_temperature(ds18b20s[i], &temperature));
            ESP_LOGI(TAG, "Temperature rad from DS18B20[%d]: %.2fC", i, temperature);
        }
        set_temperature_color(temperature);
    }
}
