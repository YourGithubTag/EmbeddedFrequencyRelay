// COMPSYS 723 Assignment 1
// Cecil Symes, Nikhil Kumar
// csym531, nkmu576

/* Standard includes. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Scheduler includes. */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

/* HAL API includes */
#include "system.h"
#include "sys/alt_irq.h"
#include "io.h"
#include "altera_avalon_pio_regs.h"

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

// Definition of Task Stacks
#define   TASK_STACKSIZE       2048

// Definition of Task Priorities
#define PRINT_STATUS_TASK_PRIORITY 14
#define GETSEM_TASK1_PRIORITY      13
#define GETSEM_TASK2_PRIORITY      12
#define RECEIVE_TASK1_PRIORITY    11
#define RECEIVE_TASK2_PRIORITY    10
#define SEND_TASK_PRIORITY        9

// Definition of Message Queue
#define   MSG_QUEUE_SIZE  30

QueueHandle_t msgqueue;

// used to delete a task
TaskHandle_t xHandle;

// Definition of Semaphore
SemaphoreHandle_t shared_resource_sem;

// globals variables
QueueHandle_t newLoadQ ;

// ISR for handling Frequency Relay Interrupt
void freq_relay(){
	unsigned int temp = IORD(FREQUENCY_ANALYSER_BASE, 0);
	printf("%f Hz\n", 16000/(double)temp);
	return;
}

int main(int argc, char* argv[], char* envp[])
{
	// Register interrupt for frequency analyser component
	alt_irq_register(FREQUENCY_ANALYSER_IRQ, 0, freq_relay);

	// Start Scheduler
	vTaskStartScheduler();

	// Program will only reach here if insufficient heap to start scheduler
	for(;;)
    return 0;
}



static void WallSwitchPoll(void *pvParameters) {
	unsigned int CurrSwitchValue = 0;
	unsigned int PrevSwitchValue = 0;

  while (1){
    // read the value of the switch and store to uiSwitchValue
    CurrSwitchValue = IORD_ALTERA_AVALON_PIO_DATA(SLIDE_SWITCH_BASE) & 0x7F;

    if (CurrSwitchValue != PrevSwitchValue ) {
        
        if (xQueueSend(newLoadQ, &CurrSwitchValue, 10) == pdTRUE) {
        	printf("%d", CurrSwitchValue );
        } else {
            printf("failed send");
        }
    }

  vTaskDelay(100);

  }

}

int main(int argc, char* argv[], char* envp[]){
	WallSwitchPoll();
}


