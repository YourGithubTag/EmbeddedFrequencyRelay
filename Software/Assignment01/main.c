// COMPSYS 723 Assignment 1
// Cecil Symes, Nikhil Kumar
// csym531, nkmu576

/*---------- INCLUDES ----------*/
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


/*---------- DEFINITIONS ----------*/
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


/*---------- GLOBAL VARIABLES ----------*/
QueueHandle_t msgqueue;

// used to delete a task
TaskHandle_t xHandle;

// Definition of Semaphore
SemaphoreHandle_t shared_resource_sem;

// globals variables
QueueHandle_t newLoadQ ;

// Queue for FreqAnalyserISR and StabilityControlCheck
QueueHandle_t newFreqQ;

// Mutex for protecting sLoadManageState flag
SemaphoreHandle_t loadManageState_sem;

// Flag that represents if system is in shedding mode
unsigned int loadManageState = 0;

/*---------- INTERRUPT SERVICE ROUTINES ----------*/
// ISR for handling Frequency Relay Interrupt
void freq_relay(){
	// Read frequency
	double freq = IORD(FREQUENCY_ANALYSER_BASE, 0);

	// Send frequency, if queue is full then do nothing
	if (xQueueSend(newFreqQ, (void *)&freq, 0) == pdPASS)
	{
		printf("Sent %f Hz.\n", 16000/(double)freq);
	}

	return;
}

static void WallSwitchPoll(void *pvParameters) {
	unsigned int CurrSwitchValue = 0;
	unsigned int PrevSwitchValue = 0;
	unsigned int temp = 100;

  while (1){
	  
    // read the value of the switch
    CurrSwitchValue = IORD_ALTERA_AVALON_PIO_DATA(SLIDE_SWITCH_BASE) & 0x7F;

    if (CurrSwitchValue != PrevSwitchValue ) {
        
        if (xQueueSend(newLoadQ, &CurrSwitchValue, 10) == pdTRUE) {
			xQueueReceive(newLoadQ, &temp, portMAX_DELAY);
        	printf("%d \n", temp);
        } else {
            printf("failed send \n");
        }
    }
  vTaskDelay(100);

  }

}

// Checks the newFreqQ to see if there is a new frequency
void StabilityControlCheck(void *pvParameters)
{
	// QUESTION: How long should this function be waiting for the ISR to put something in the queue? How do we know what the vTaskDelay needs to be?
	// What should we split our main control task into two for? We do not understand

	double oldFreq;
	double *currentFreq;
	while(1)
	{
		// Check if there is a new frequency in the queue
		if (xQueueReceive(newFreqQ, &currentFreq, 0) == pdPASS)
		{
			/* Check if the new frequency is below the lower threshold, or rate of change absolute value is too large
			if (currentFreq < LOWER THRESHOLD VALUE) ||
			{
				// If it is, then system needs to enter unstable mode
				// xSemaphoreTake(loadManageState_sem);
				// loadManageState = 1;
				// xSemaphoreGive(loadManageState_sem);
			}

			// Send frequency values to VGA display
			// xQueueSend(vgaDisplayQ, &currentFreq, 0)

			// printf("Frequency %f received.\n", *currentFreq);
			 */
		}

		vTaskDelay(50);
	}
}

int CreateTasks() {
	xTaskCreate(WallSwitchPoll, "SwitchPoll", TASK_STACKSIZE, NULL, 1, NULL);
	xTaskCreate(StabilityControlCheck, "StabCheck", TASK_STACKSIZE, NULL, 2, NULL);
	return 0;
}

int OSDataInit() {
	newLoadQ = xQueueCreate( 100, sizeof(unsigned int) );
	newFreqQ = xQueueCreate(10, sizeof( void* ));
	return 0;
}

int main(int argc, char* argv[], char* envp[])
{
	printf("hello from Nios II");


	// Register interrupt for frequency analyser component
	alt_irq_register(FREQUENCY_ANALYSER_IRQ, 0, freq_relay);

	OSDataInit();
	CreateTasks();

	// Start Scheduler
	vTaskStartScheduler();

	// Program will only reach here if insufficient heap to start scheduler
	for(;;) {
		;
	}

    return 0;
}




