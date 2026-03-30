#ifndef AQM_TASKS_H
#define AQM_TASKS_H

#include <freertos/FreeRTOS.h>


extern TaskHandle_t factory_reset_task_handle;

void aqm_tasks_start(void);

#endif // AQM_TASKS_H