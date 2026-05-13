#ifndef COMMAND_TASK_H
#define COMMAND_TASK_H
/* ----------------------------------------------------------- */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "FlashLogStorage.h"
#include "EventLogger.h"
struct AppContext {
    FlashLogStorage* storage;
    UartDma* uart;
    EventLogger* logger;
};
void command_task(void *pvParameters);
bool isLogPaused();
void pauseLogGeneration();
void resumeLogGeneration();

/* ------------------------------------------------------------ */
#endif // COMMAND_TASK_H
