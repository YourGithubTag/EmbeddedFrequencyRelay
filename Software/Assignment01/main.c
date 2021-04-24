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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"

/* HAL API includes */
#include "system.h"
#include "sys/alt_irq.h"
#include "io.h"
#include "altera_avalon_pio_regs.h"
#include "unistd.h"
#include "altera_up_avalon_ps2.h"
#include "altera_up_ps2_keyboard.h"


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

SempahoreHandle_t TimerTaskSync;

// globals variables
QueueHandle_t SwitchQ;

QueueHandle_t LEDQ;

// Queue for communication between StabilityControlCheck & VGADisplayTask
QueueHandle_t vgaFreqQ;

// Queue for communication between FreqAnalyserISR & StabilityControlCheck
QueueHandle_t newFreqQ;

// Mutex for protecting InStabilityFlag
SemaphoreHandle_t InStabilityFlag_mutex;

SemaphoreHandle_t TimerSync_sem;

// Flag that represents if system is in shedding mode
unsigned int InStabilityFlag = 0;

unsigned int Timer500Full = 0;

unsigned int loadManageState;

// Mutex for protecting maintenance mode flag
SemaphoreHandle_t maintenanceModeFlag_mutex;

// System flag for if we are in maintenance mode
unsigned int maintenanceModeFlag;


typedef struct LEDstatus {
	unsigned int Red;
	unsigned int Green;
	
} LEDStruct;



// Global double stores current rate of change
double rateOfChange = 0;

// Mutex to protect rate of change global variable
SemaphoreHandle_t roc_mutex;

// Queue for communication between keyboard ISR and keyboard reader
QueueHandle_t keyboardQ;

// Lower frequency bound
double lowerFreqBound = 0;

// Mutex for protecting lower frequency bound
SemaphoreHandle_t lowerFreqBound_mutex;

// Absolute rate of change bound
double rocBound = 0;

// Mutex for protecting rate of change bound
SemaphoreHandle_t rocBound_mutex;

TimerHandle_t MonitoringTimer;

SemaphoreHandle_t MonitorTimer_sem;

SemaphoreHandle_t SystemState_mutex;

LEDStruct SystemState;

/*---------- INTERRUPT SERVICE ROUTINES ----------*/
// ISR for handling Frequency Relay Interrupt
void freq_relay(){
	// Read ADC count
	unsigned int adcCount = IORD(FREQUENCY_ANALYSER_BASE, 0);

	// Send count if queue not full
	if (xQueueIsQueueFullFromISR(newFreqQ) == pdFALSE)
	{
		xQueueSendFromISR(newFreqQ, (void *)&adcCount, NULL);
	}
	return;
}

void vMonitoringTimerCallback(xTimerHandle_t timer) {
	xSemaphoreGive(MonitorTimer_sem,NULL);
}

// ISR for handling PS/2 keyboard presses
void ps2_isr (void* context, alt_u32 id){

	char ascii;
	int status = 0;
	unsigned char key = 0;
	KB_CODE_TYPE decode_mode;

	status = decode_scancode (context, &decode_mode , &key , &ascii) ;

	// Display key on seven seg display
	IOWR(SEVEN_SEG_BASE, 0,key);

	// Create local copy of maintenanceModeFlag
	xSemaphoreTakeFromISR(maintenanceModeFlag_mutex, NULL);
	unsigned int maintModeFlag_local = 1; //maintenanceModeFlag TODO: Change back
	xSemaphoreGiveFromISR(maintenanceModeFlag_mutex, NULL);

	// If key is pressed & maintenance mode
	if ((status == 0 ) && (maintModeFlag_local))
	{
		// Print out the result
		switch ( decode_mode )
		{
			// If the key pressed is ASCII attempt to send through queue
			case KB_ASCII_MAKE_CODE :
//				printf ( "ASCII: %x\n", key ) ;
//				usleep(10);
				if(xQueueIsQueueFullFromISR(keyboardQ) == pdFALSE)
				{
					xQueueSendFromISR(keyboardQ, &ascii, NULL);
//					printf ("SENT ASCII.\n") ;
//					usleep(10);
				}
				break ;

			// If key pressed is make code then send
			case KB_BINARY_MAKE_CODE :
//				printf ( "MAKE CODE : %x\n", key ) ;
//				usleep(10);
				if(xQueueIsQueueFullFromISR(keyboardQ) == pdFALSE)
				{
					xQueueSendFromISR(keyboardQ, &key, NULL);
//					printf ("SENT MAKE CODE.\n") ;
//					usleep(10);
				}
				break ;


			// Otherwise do not send
			default:
//				printf ( "NON-ASCII: %x\n", key ) ;
//				usleep(10);
				break ;
		}
	}
}

/*---------- TASK DEFINITIONS ----------*/

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
		
/*	//Take
 * 	//Write to local
 * 	//Return
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

static void shedLogic(void *pvParameters) {

	unsigned int PrevInstabilityFlag;
	// Flag for shedding
	int loadShedStatus;
	//flag for monitor
	int monitorMode;

	LEDStruct Led2Send;

if (xSemaphoreTake(maintenanceModeFlag_mutex) == pdTRUE) {
	if (!maintenanceModeFlag) {

	if (xSemaphoreTake(MonitorTimer_sem)) {
		if (xSemaphoreTake(InStabilityFlag_mutex) == pdTRUE) {
				if (InStabilityFlag) {
					 LoadShed();
				}
				else {
					 LoadConnect();
				}
			}
		}
	}
}
}

static void MonitorTimer(void *pvParameters) {
	unsigned int PrevInstabilityFlag;

	if ( xSemaphoreTake(MonitorMode_sem) == pdTRUE && monitorMode) {
		if ( xSemaphoreTake(InStabilityFlag_mutex) == pdTRUE) {

		 if (PrevInstabilityFlag != InStabilityFlag ) {
				if (xTimerReset(MonitoringTimer) == pdTRUE) {
					PrevInstabilityFlag = InStabilityFlag;
				} else {
					printf("Timer cannot be reset");
				}

			}
		}
	}
}

static void LEDcontrol(void *pvParameters) {
	LEDStruct Temp;

	while (1) {
		if (xQueueReceive(LEDQ, &Temp, portMAX_DELAY) == pdTRUE) {

			IOWR_ALTERA_AVALON_PIO_DATA(RED_LEDS_BASE, Temp.Red);
			IOWR_ALTERA_AVALON_PIO_DATA(GREEN_LEDS_BASE, Temp.Green);

			SystemState.Red = Temp.Red;
			SystemState.Green = Temp.Green;

		} else {
			printf("Dead \n");
		}
	}
}

static void LoadConnect() {
	LEDStruct temp;

	temp.Green = SystemState.Green;
	temp.Red = SystemState.Red;

    if (!temp.Green) {
        return 0;
	}

    unsigned int ret = 1;

	// Start from bit 7 instead

    while (temp.Green >>= 1) {
        ret <<= 1;
	}

	temp.Green -= ret; 
	temp.Red += ret;

	if (xQueueSend(LEDQ,&temp,10) == pdTRUE) {
		;
	}
	
}

static void LoadShed() {
	LEDStruct temp;

	temp.Green = SystemState.Green;
	temp.Red = SystemState.Red;

	 if (!temp.Red) {
        return 0;
	}
	int num = 1;

	while (temp.Red & num != 1){
		num <<= 1;
	}

	temp.Red -= ret;
	temp.Green += ret;

	if (xQueueSend(LEDQ,&temp,10) == pdTRUE) {
		;
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
   		 
		if (!maintenanceModeFlag) {

			CurrSwitchValue = IORD_ALTERA_AVALON_PIO_DATA(SLIDE_SWITCH_BASE) & 0x7F;
			
			if (loadManageState && (CurrSwitchValue < PrevSwitchValue)) {
				if (xQueueSend(SwitchQ, &CurrSwitchValue, 10) != pdTRUE) {
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

  //vTaskDelay(10);
  }
}

// Checks the newFreqQ for new frequencies, calculates rate of change
// Updates the loadManageState flag if the system needs to enter shedding mode
// Sends frequency information to the VGA Display Task
void StabilityControlCheck(void *pvParameters)
{
	// Stores old frequency and ADC count for rate of change calculation
	double oldFreq = 0;
	unsigned int oldCount = 0;

	// Stores latest frequency and ADC count information from freq_relay ISR
	double newFreq = 0;
	unsigned int newCount = 0;

	// Buffers the queue data so old count can be saved
	unsigned int temp = 0;

	// Local variable for rate of change
	double rocLocal = 0;

	// Lowest frequency possible before system enters shedding mode
	double lowerBound = 48.5; // TODO: Change these by keyboard user input

	// Max absolute rate of change before system enters shedding mode
	double rocThreshold = 8; // TODO: Change these by keyboard user input

	while(1)
	{
		/* THRESHOLD VALUE CHECK */
		// Keyboard logic
		// Chekc keyboard queue
		// Take in inputs
		// Change the lower bound and rate of change threshold

		/* SYSTEM STABILITY CHECK */
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
			if (uxQueueSpacesAvailable(vgaFreqQ) != 0)
			{
				xQueueSend(vgaFreqQ, &newCount, 0);
			}

			// Check stable system for instability
			xSemaphoreTake(InStabilityFlag_mutex, portMAX_DELAY);
			if (InStabilityFlag == 0)
			{
				// Check if current freq is under lower threshold OR rate of change too high
				if ((newFreq < lowerBound) || (rocLocal > rocThreshold))
				{
					InStabilityFlag = 1;
//					printf("====================\n");
//					printf("System swapping to UNSTABLE\nFrequency is %f Hz\nRate of change is %f Hz/s\n", newFreq, rocLocal);
//					usleep(10);
				}
			}
			// Check unstable system for stability
			else
			{
				// Check if current freq is under lower threshold OR rate of change too high
				if ((newFreq >= lowerBound) && (rocLocal <= rocThreshold))
				{
					InStabilityFlag = 0;
//					printf("====================\n");
//					printf("System swapping to STABLE\nFrequency is %f Hz\nRate of change is %f Hz/s\n", newFreq, rocLocal);
//					usleep(10);
				}
			}
			xSemaphoreGive(InStabilityFlag_mutex);

		}
	}

}

// Receives frequency information, displays to VGA
void VGADisplayTask(void *pvParameters)
{
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
			// Calculate current frequency of system
			newFreq = 16000/(double)newCount;

			// Store global rate of change locally
			xSemaphoreTake(roc_mutex, portMAX_DELAY);
			roc_local = rateOfChange;
			xSemaphoreGive(roc_mutex);

			// Send data to monitor
			// TODO: All displaying info and shit here
		}
	}
}

// Reads the keyboard input and updates thresholds for rate of change and lower frequency if in maintenance mode
void KeyboardReader(void *pvParamaters)
{
	// newKey holds current key from ps2_isr
	unsigned int newKey = 0;

	// currentInput array stores all keys previously pressed for current parameter (lowerFreqBound, rocBound)
	unsigned int currentInput[4];

	// Flag represents which parameter is currently pending change, 0 = lowerFreqBound, 1 = rocBound)
	unsigned int whichBoundFlag = 0;

	while(1)
	{
		// Receive keys from ISR
		if (xQueueReceive(keyboardQ, &newKey, portMAX_DELAY) == pdTRUE)
		{
			// Bitmask to keep lower 8 bits of newKey
			newKey = newKey & 0xFF;

			// Check if a digit 0-9 inclusive
			if ((newKey <= 57) && (newKey >= 48))
			{
				// TODO: Switch statement for all different digits
			}
			// Check for period .
			else if (newKey == 46)
			{
				// TODO: Add a period to the array
			}
			// Check if SPACEBAR
			else if (newKey == 41)
			{
				// TODO: Change the parameter flag and set whichever bound is currently active
			}


//			printf("==========\n");
//			printf("DECIMAL: %d.\n", newKey);
//			printf("HEX: %x.\n", newKey);
//			usleep(10);
		}
	}

	return;
}

/*---------- FUNCTION DEFINITIONS ----------*/

// Creates all tasks used
int CreateTasks() {
	xTaskCreate(WallSwitchPoll, "SwitchPoll", TASK_STACKSIZE, NULL, 1, NULL);
	xTaskCreate(StabilityControlCheck, "StabCheck", TASK_STACKSIZE, NULL, 2, NULL);
	xTaskCreate(VGADisplayTask, "VGADisplay", TASK_STACKSIZE, NULL, 3, NULL);
	xTaskCreate(load_manage,"LDM",TASK_STACKSIZE,NULL,4,NULL);
	xTaskCreate(LEDcontrol,"LCC",TASK_STACKSIZE,NULL,5,NULL);
	xTaskCreate(KeyboardReader, "KeyboardReader", TASK_STACKSIZE, NULL, 6, NULL);
	return 0;
}

// Creates all timers used
int CreateTimers() {
	MonitoringTimer = xTimerCreate("MT", 500, pdTRUE, NULL , vMonitoringTimerCallback);
	return 0;
}

// Initialises all data structures used
int OSDataInit() {
	// Initialise qeueues
	SwitchQ = xQueueCreate( 100, sizeof(unsigned int) );
	newFreqQ = xQueueCreate(10, sizeof( void* ));
	LEDQ = xQueueCreate(100, sizeof(LEDStruct));
	vgaFreqQ = xQueueCreate(MSG_QUEUE_SIZE, sizeof( void* ));
	keyboardQ = xQueueCreate(MSG_QUEUE_SIZE, sizeof( void* ));

	// Initialise mutexes
	roc_mutex = xSemaphoreCreateMutex();
	InStabilityFlag_mutex = xSemaphoreCreateMutex();
	maintenanceModeFlag_mutex = xSemaphoreCreateMutex();
	lowerFreqBound_mutex = xSemaphoreCreateMutex();
	rocBound_mutex = xSemaphoreCreateMutex();
	TimerTaskSync = xSemaphoreCreateBinary();
	MonitorTimer_sem = xSemaphoreCreateBinary();

	return 0;
}

int main(int argc, char* argv[], char* envp[])
{
	printf("Hello from Nios II!\n");

	// Initialise data structures
	OSDataInit();

	// Create all tasks
	CreateTasks();

	// Initialise PS2 device
	alt_up_ps2_dev * ps2_device = alt_up_ps2_open_dev(PS2_NAME);

	// Check if PS2 device connected
	if(ps2_device == NULL){
		printf("can't find PS/2 device\n");
	}

	// Register PS2 ISR
	alt_up_ps2_clear_fifo(ps2_device);
	alt_irq_register(PS2_IRQ, ps2_device, ps2_isr);
	IOWR_8DIRECT(PS2_BASE,4,1);

	// Register Frequency Analyser ISR
	alt_irq_register(FREQUENCY_ANALYSER_IRQ, 0, freq_relay);

	// Start Scheduler
	vTaskStartScheduler();

	// Program will only reach here if insufficient heap to start scheduler
	for(;;) {
		;
	}

    return 0;
}
