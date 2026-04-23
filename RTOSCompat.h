/*
 * RTOSCompat.h - Compatibility layer for ESP8266 (no FreeRTOS)
 */
#ifndef RTOSCOMPAT_H
#define RTOSCOMPAT_H

#ifdef ESP8266

#include <Arduino.h>
#include <ets_sys.h>

// RTOS types
typedef void* TaskHandle_t;
typedef void* SemaphoreHandle_t;

// RTOS constants
#define portTICK_PERIOD_MS    1
#define pdTRUE                true
#define portMAX_DELAY         0

// RTOS functions (mocked)
inline void vTaskDelay(uint32_t ticks_ms) {
    delay(ticks_ms);
}

inline BaseType_t xTaskCreate(
    void (*taskFunc)(void*),
    const char* name,
    uint32_t stackDepth,
    void* param,
    uint32_t priority,
    TaskHandle_t* taskHandle)
{
    // ESP8266 runs mining in loop(), so we don't create a real task
    if (taskHandle) *taskHandle = NULL;
    return pdPASS;
}

inline SemaphoreHandle_t xSemaphoreCreateMutex() {
    return (SemaphoreHandle_t)1;
}

inline bool xSemaphoreTake(SemaphoreHandle_t, uint32_t) {
    return true;
}

inline void xSemaphoreGive(SemaphoreHandle_t) {}

// Single-core without RTOS -> mining runs inside loop()
// No need for xTaskCreatePinnedToCore
#elif defined(ESP32) && CORE_COUNT == 1

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#endif

#endif
