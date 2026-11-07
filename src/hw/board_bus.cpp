#include "hw/board_bus.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static SemaphoreHandle_t bus_mux = nullptr;

static void ensure_mux()
{
    if (bus_mux) return;
    bus_mux = xSemaphoreCreateRecursiveMutex();
}

void board_bus_lock()
{
    ensure_mux();
    if (bus_mux) xSemaphoreTakeRecursive(bus_mux, portMAX_DELAY);
}

void board_bus_unlock()
{
    if (bus_mux) xSemaphoreGiveRecursive(bus_mux);
}
