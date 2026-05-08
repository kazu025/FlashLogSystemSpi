#ifndef TASK_H
#define TASK_H
/* ----------------------------------------------------------- */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "FlashLogStorage.h"

struct AppContext {
    FlashLogStorage* storage;
    UartDma* uart;
};
void command_task(void *pvParameters);
bool isLogPaused();
void pauseLogGeneration();
void resumeLogGeneration();

/* ------------------------------------------------------------ */
#endif // TASK_H
