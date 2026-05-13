#ifndef TASK_H
#define TASK_H
/* ----------------------------------------------------------- */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "EventId.h"
#include "FlashLogStorage.h"
#include "EventLogger.h"
#include "LogTypes.h"
void adc_task(void* param);
/* ------------------------------------------------------------ */
#endif // TASK_H
