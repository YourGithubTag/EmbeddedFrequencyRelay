// COMPSYS 723 Assignment 1
// Cecil Symes, Nikhil Kumar
// csym531, nkmu576

/*---------- INCLUDES ----------*/
/* Standard includes. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

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
#include "unistd.h"


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

QueueHandle_t ControlQ;

// used to delete a task
TaskHandle_t xHandle;

// Definition of Semaphore
SemaphoreHandle_t shared_resource_sem;

// globals variables
QueueHandle_t SwitchQ;

QueueHandle_t LEDQ;

// Queue for communication between StabilityControlCheck & VGADisplayTask
QueueHandle_t vgaDisplayQ;

// Queue for communication between FreqAnalyserISR & StabilityControlCheck
QueueHandle_t newFreqQ;

// Mutex for protecting loadManageState flag
SemaphoreHandle_t InStabilityFlag_sem;

SemaphoreHandle_t TimerSync_sem;

// Flag that represents if system is in shedding mode
unsigned int InStabilityFlag = 0;

unsigned int Timer500Full = 0;

unsigned int loadManageState = 0;

unsigned int MaintanenceModeFlag = 0;


typedef struct LEDstatus {
	unsigned int Red;
	unsigned int Green;
	
} LEDStruct;

// Global double stores current rate of change
double rateOfChange = 0;

// Mutex to protect rate of change global variable
SemaphoreHandle_t roc_mutex;

/*---------- INTERRUPT SERVICE ROUTINES ----------*/
// ISR for handling Frequency Relay Interrupt
void freq_relay(){
	// Read ADC count
	unsigned int adcCount = IORD(FREQUENCY_ANALYSER_BASE, 0);
	//unsigned int adcCount = 0;

	// Send count if queue not full
	if (xQueueIsQueueFullFromISR(newFreqQ) == pdFALSE)
	{
		xQueueSendFromISR(newFreqQ, (void *)&adcCount, NULL);

//		// DEBUG
//		printf("%f Hz\n", 16000/(double)adcCount);
//		usleep(50);
	}
	return;
}

/*---------- FUNCTION DEFINITIONS ----------*/


static void load_manage(void *pvParameters) {

	int previousStabilitystate;
	// Flag for shedding
	int loadShedStatus;
	//flag for monitor
	int monitorMode;

	LEDStruct Led2Send;

	unsigned int switchNum;

	while(1) {

		if (xQueueReceive(SwitchQ, &switchNum, portMAX_DELAY) == pdTRUE) {
			if (switchNum >= 64) {
				Led2Send.Red = switchNum;
				Led2Send.Green = switchNum;
			} else {
				Led2Send.Red = switchNum;
				Led2Send.Green = 0;
			}

			if (xQueueSend(LEDQ,&Led2Send,10) != pdTRUE) {
				printf("FUCK couldnt send to Leds \n ");
			} 
		}
		
/*
		if (MaintanenceModeFlag == 0) {

			if (xSemaphoreTake(InStabilityFlag_sem,portMAX_DELAY) == pdTRUE){

				if (InStabilityFlag && !loadShedStatus) {
					monitorMode = true;
					loadShedStatus = true;
					LoadShed();
					xTimerStart(Timer500, 0);
				}

				else if (monitorMode && (previousStabilitystate != InStabilityFlag) ) {
					xTimerReset(Timer500,0);
					previousStabilitystate = InStabilityFlag;
				} 

				else if (monitorMode && (Timer500Full == 1)) {

					Timer500Full = 0;
					xTimerReset(Timer500,0);

					if (InStabilityFlag == 1) {
						LoadShed();
					} else {
						LoadConnect();
					} 
				}


			}
		}
		*/
	}
}

static void LEDcontrol(void *pvParameters) {
	LEDStruct Temp;

	while (1) {
		if (xQueueReceive(LEDQ, &Temp, portMAX_DELAY) == pdTRUE) {

			IOWR_ALTERA_AVALON_PIO_DATA(RED_LEDS_BASE, Temp.Red);
			IOWR_ALTERA_AVALON_PIO_DATA(GREEN_LEDS_BASE, Temp.Green);

		} else {
			printf("Dead \n");
		}

	}
}

static void WallSwitchPoll(void *pvParameters) {

	unsigned int CurrSwitchValue = 0;
	unsigned int PrevSwitchValue = 0;
	unsigned int temp = 0;
	printf("WALL SWITCH \n");

	CurrSwitchValue = IORD_ALTERA_AVALON_PIO_DATA(SLIDE_SWITCH_BASE) & 0x7F;
		if (xQueueSend(SwitchQ, &CurrSwitchValue, 10) !=pdTRUE) {
			printf("failed INIT switch send \n");
		}

  while (1){
   		 
		if (!MaintanenceModeFlag) {

			CurrSwitchValue = IORD_ALTERA_AVALON_PIO_DATA(SLIDE_SWITCH_BASE) & 0x7F;
			
			if (loadManageState && (CurrSwitchValue < PrevSwitchValue)) {
				if (xQueueSend(SwitchQ, &CurrSwitchValue, 10) <= pdTRUE) {
						printf("failed to send");
					}
			} else {
				if (CurrSwitchValue != PrevSwitchValue ) {
					if (xQueueSend(SwitchQ, &CurrSwitchValue, 10) != pdTRUE) {
						printf("failed to send");
					} 
				}
			}

			PrevSwitchValue = CurrSwitchValue;
		}

  vTaskDelay(10);
  }

}

/*

// Checks the newFreqQ for new frequencies, calculates rate of change
// Updates the loadManageState flag if the system needs to enter shedding mode
// Sends frequency information to the VGA Display Task
void StabilityControlCheck(void *pvParameters)
{
// TEST CODE
//	unsigned int temp = 0;
//
//	while(1)
//	{
//		if (xQueueReceive(newFreqQ, &temp, portMAX_DELAY) == pdTRUE)
//		{
//			printf("Received ADC count of %d\n", temp);
//			usleep(50);
//
//			if (xQueueSend(vgaFreqQ, (void *)&temp, NULL) == pdTRUE)
//			{
//				printf("Sent ADC count of %d to VGA Task\n", temp);
//				usleep(50);
//			}
//		}
//	}

	// Stores old frequency and ADC count for rate of change calculation
	double oldFreq = 0;
	unsigned int oldCount = 0;

	// Stores latest frequency and ADC count information from freq_relay ISR
	double newFreq = 0;
	unsigned int newCount = 0;

	// Buffers the queue data so old count can be saved
	unsigned int temp = 0;

	// Lowest frequency possible before system enters shedding mode
	double lowerBound = 0;

	// Local variable for rate of change
	double rocLocal = 0;

	// Max absolute rate of change before system enters shedding mode
	double rocThreshold = 0;

	while(1)
	{
		// THRESHOLD VALUE CHECK 
		// Keyboard logic
		// Chekc keyboard queue
		// Take in inputs
		// Change the lower bound and rate of change threshold

		//SYSTEM STABILITY CHECK 
		// Wait for new frequency in queue
		if (xQueueReceive(newFreqQ, &temp, portMAX_DELAY) == pdTRUE)
		{
			// Save current count, save old count
			oldCount = newCount;
			newCount = temp;

			// Convert counts to frequencies
			oldFreq = 16000/(double)oldCount;
			newFreq = 16000/(double)newCount;

			// Write rate of change to global variable
			xSemaphoreTake(roc_mutex, portMAX_DELAY);
			rateOfChange = fabs((newFreq - oldFreq) * 16000 / ((newCount + (double)oldCount)/2));
			rocLocal = rateOfChange;
			xSemaphoreGive(roc_mutex);

			// Send current frequency to VGA Display Task if queue isn't full
			if (uxQueueSpacesAvailable(vgaFreqQ) == 0)
			{
				printf("VGA FREQ QUEUE FULL.\n");
				usleep(50);
			}
			else
			{
				xQueueSend(vgaFreqQ, &newCount, 0);
			}

			// Check stable system for instability
			// Take stability flag mutex
			if () // System stable
			{
				// TODO: Check if current freq is under lower threshold OR rate of change too high
				if ((newFreq < lowerBound) || (rocLocal > rocThreshold))
				{
					// Take mutex
					// Set the unstable flag
					// Give mutex
				}
			}
			// Check unstable system for stability
			else
			{
				// TODO: Check if current freq is under lower threshold OR rate of change too high
				if ((newFreq >= lowerBound) && (rocLocal <= rocThreshold))
				{
					// Take mutex
					// Remove the unstable flag
					// Give mutex
				}
			}

//			// DEBUG PRINT
//			printf("====================\n");
//			printf("Old freq = %f.\n", oldFreq);
//			printf("Old count = %d.\n", oldCount);
//			printf("New freq = %f.\n", newFreq);
//			printf("New count = %d.\n", newCount);
//			printf("Absolute Rate of change = %f.\n", rateOfChange);
//			usleep(100);

		}
	}

}
*/
 /*
// Receives frequency information, displays to VGA
void VGADisplayTask(void *pvParameters)
{
//	TEST CODE
//	unsigned int temp = 0;
//
//	while(1)
//	{
//		if (xQueueReceive(vgaFreqQ, &temp, portMAX_DELAY) == pdTRUE)
//		{
//			printf("Received ADC count of %d from StabControlCheck\n", temp);
//			usleep(50);
//		}
//	}

	// Stores latest ADC Count, frequency from StabilityControlChecker, and rate of change from global variable
	unsigned int newCount = 0;
	double newFreq = 0;
	double roc_local = 0;

	// TODO: Do we need an array to store old values for freq/count?

	while (1)
	{
		// Receive new ADC Count
		if (xQueueReceive(vgaFreqQ, &newCount, portMAX_DELAY) == pdPASS)
		{
			newFreq = 16000/(double)newCount;
			printf("VGA received new frequency of %f.\n", newFreq);
			usleep(50);

			// Store global rate of change locally
			xSemaphoreTake(roc_mutex, portMAX_DELAY);
			roc_local = rateOfChange;
			xSemaphoreGive(roc_mutex);
			printf("Current rate of change is %f\n", roc_local);

			// Send data to monitor
			// TODO: All displaying info and shit here
		}
	}
}
*/

// Creates all tasks used
int CreateTasks() {
	xTaskCreate(WallSwitchPoll, "SwitchPoll", TASK_STACKSIZE, NULL, 1, NULL);
	//xTaskCreate(StabilityControlCheck, "StabCheck", TASK_STACKSIZE, NULL, 2, NULL);
	//xTaskCreate(VGADisplayTask, "VGADisplay", TASK_STACKSIZE, NULL, 3, NULL);
	xTaskCreate(load_manage,"LDM",TASK_STACKSIZE,NULL,4,NULL);
	xTaskCreate(LEDcontrol,"LCC",TASK_STACKSIZE,NULL,3,NULL);
	return 0;
}

// Initialises all data structures used

int OSDataInit() {
	SwitchQ = xQueueCreate( 100, sizeof(unsigned int) );
	//newFreqQ = xQueueCreate(10, sizeof( void* ));
	LEDQ = xQueueCreate(100, sizeof(LEDStruct));
	//vgaDisplayQ = xQueueCreate(MSG_QUEUE_SIZE, sizeof( void* ));
	// Initialise Queues
	//newLoadQ = xQueueCreate( 100, sizeof(unsigned int) );
	//newFreqQ = xQueueCreate(MSG_QUEUE_SIZE, sizeof( void* ));
	//vgaFreqQ = xQueueCreate(MSG_QUEUE_SIZE, sizeof( void* ));

	// Initialise mutexes
	roc_mutex = xSemaphoreCreateMutex();
	return 0;
}

static void flaginit() {


}

// Initialise all ISRs
int initISR()
{
	alt_irq_register(FREQUENCY_ANALYSER_IRQ, 0, freq_relay);
	return 0;
}

int main(int argc, char* argv[], char* envp[])
{
	printf("Hello from Nios II!\n");

	// Initialise data structures
	OSDataInit();

	// Create all tasks
	CreateTasks();

	// Register all ISRs
	initISR();

	// Start Scheduler
	vTaskStartScheduler();

	// Program will only reach here if insufficient heap to start scheduler
	for(;;) {
		;
	}

    return 0;
}
