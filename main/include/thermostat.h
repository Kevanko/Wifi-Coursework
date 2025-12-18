#ifndef THERMOSTAT_H
#define THERMOSTAT_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

typedef struct {
    float thresholds[5];
} led_settings_t;

extern led_settings_t g_settings;
extern SemaphoreHandle_t settings_mutex;
extern float current_temperature;

void thermostat_init(void);
void thermostat_task(void *pvParameters);

#endif // THERMOSTAT_H
