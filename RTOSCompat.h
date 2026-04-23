// RTOSCompat.h
#ifndef RTOSCOMPAT_H
#define RTOSCOMPAT_H

#ifdef ESP8266

#include <Arduino.h>
#include <ets_sys.h>

#define portTICK_PERIOD_MS    1   // 1 ms per tick

typedef void* TaskHandle_t;
typedef void* SemaphoreHandle_t;

inline void vTaskDelay(uint32_t ticks) {
    delay(ticks / portTICK_PERIOD_MS);
}

inline SemaphoreHandle_t xSemaphoreCreateMutex() { return (SemaphoreHandle_t)1; }

inline bool xSemaphoreTake(SemaphoreHandle_t, uint32_t) { return true; }
inline void xSemaphoreGive(SemaphoreHandle_t) {}

#define pdTRUE            true
#define portMAX_DELAY     0

// ESP8266 không có RTOS task → không tạo task, mining sẽ chạy trong loop
inline BaseType_t xTaskCreate(
    void (*)(void*), const char*, uint32_t, void*,
    uint32_t, TaskHandle_t* pxCreatedTask
) {
    if (pxCreatedTask) *pxCreatedTask = NULL;
    return pdPASS;
}

#endif // ESP8266
#endif
