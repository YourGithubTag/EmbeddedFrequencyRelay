// Standard includes
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// Scheduler includes
#include "FreeRTOS/FreeRTOS.h"
#include "FreeRTOS/task.h"
#include "FreeRTOS/queue.h"
#include "FreeRTOS/semphr.h"

#include <altera_avalon_pio_regs.h>



QueueHandle_t msgqueue;

// used to delete a task
TaskHandle_t xHandle;

// Definition of Semaphore
SemaphoreHandle_t shared_resource_sem;

// globals variables
QueueHandle_t newLoadQ ;

