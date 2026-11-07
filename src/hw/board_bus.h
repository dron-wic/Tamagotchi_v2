#pragma once

// Shared SPI2 bus: the panel and the microSD must not transact at once.
// MSC callbacks run on the TinyUSB task, so this lock wraps both sides.
void board_bus_lock();
void board_bus_unlock();
