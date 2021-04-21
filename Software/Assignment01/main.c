// COMPSYS 723 Assignment 1
// Cecil Symes, Nikhil Kumar
// csym531, nkmu576

/* Standard includes. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Scheduler includes. */
#include "FreeRTOS/FreeRTOS.h"
#include "FreeRTOS/task.h"
#include "FreeRTOS/queue.h"
#include "FreeRTOS/semphr.h"

/* HAL API includes */
#include "system.h"
#include "sys/alt_irq.h"
#include "io.h"
#include "altera_avalon_pio_regs.h"

// Standard includes
#include <stddef.h>
#include <stdio.h>
#include <string.h>


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


static void load_Manage(void *pvParameters) {

	int temp = 0;
	int switches[7]; 
	int iter = 0;

	while (1) {
			if (xQueueReceive(newLoadQ, &temp, portMAX_DELAY) == pdTRUE) {

				
				iter = 0;
				for (int i = 1; i <= 64 ; i = i * 2) {
					if ( (temp & i) != 0 ) {
						switches[iter] = 1;
					} else {
						switches[iter] = 0;
					}
					iter++;
				}

				printf("%d, %d, %d, %d, %d, %d, %d \n", switches[6],switches[5],switches[4],switches[3],switches[2],switches[1],switches[0]);
			}

		vTaskDelay(1000);
	}
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
        	PrevSwitchValue = CurrSwitchValue;
        } else {
            printf("failed send \n");
        }
    }

  vTaskDelay(100);
  }

}

int CreateTasks() {
	xTaskCreate(WallSwitchPoll, "SwitchPoll", TASK_STACKSIZE, NULL, 1, NULL);
	xTaskCreate(load_Manage,"Load_Management",TASK_STACKSIZE, NULL, 2,NULL);
	return 0;
}
 
int OSDataInit() {
	newLoadQ = xQueueCreate( 100, sizeof(unsigned int) );
	return 0;
}


// ISR for handling Frequency Relay Interrupt
void freq_relay(){
	unsigned int temp = IORD(FREQUENCY_ANALYSER_BASE, 0);
	//printf("%f Hz\n", 16000/(double)temp);
	return;
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




