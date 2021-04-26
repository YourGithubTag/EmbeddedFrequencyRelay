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
double lowerFreqBound = 0; // TODO: Choose a default lowerFreqBound
// Mutex for protecting lower frequency bound
SemaphoreHandle_t lowerFreqBound_mutex;
// Absolute rate of change bound
double rocBound = 0; // TODO: Choose a default rocBound
// Mutex for protecting rate of change bound
SemaphoreHandle_t rocBound_mutex;
/// whichBoundFlag represents which parameter is currently being edited, 0 = lowerFreqBound, 1 = rocBound)
unsigned int whichBoundFlag = 0;
// Mutex for protecting whichBoundFlag
SemaphoreHandle_t whichBoundFlag_mutex;

// Syncronisation semaphore between KeyboardChecker and LCDUpdater
SemaphoreHandle_t lcdUpdate_sem;


/*---------- FUNCTION DECLARATIONS ----------*/
double array2double(unsigned int *newVal);



TimerHandle_t MonitoringTimer;

SemaphoreHandle_t MonitorTimer_sem;

SemaphoreHandle_t SystemState_mutex;

LEDStruct SystemState;

unsigned int SwitchState;

unsigned int monitorMode;
SemaphoreHandle_t MonitorMode_sem;


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

static void LoadManagement(void *pvParameters) {
	unsigned int temp;
	
	while (1) {
		//TODO: See if taking multiple Mutexes at once leads to possible race conditions
		//May have to change to instantly giving the mutex once checked
		xSemaphoreTake(maintenanceModeFlag_mutex, portMAX_DELAY);
		if (!maintenanceModeFlag) {

			xSemaphoreTake(MonitorMode_sem, portMAX_DELAY);
			if (!monitorMode) {
				//Normal Mode
				if (xQueueReceive(SwitchQ,&temp, portMAX_DELAY) == pdTRUE) {
					xSemaphoreTake(SystemState_mutex,portMAX_DELAY);
					SystemState.Red = temp;
					SystemState.Green = 0;
					xSemaphoreGive(SystemState_mutex);
				}

				xSemaphoreTake(InStabilityFlag_sem, portMAX_DELAY);
				//Checking Instability whilst in NOrmal operation
				if (InStabilityFlag) {
					LoadShed();
					xTimerStart(MonitorTimer, 0);
					monitorMode = 1;
				}

				xSemaphoreGive(InStabilityFlag_sem);
				xSemaphoreGive(MonitorMode_sem);

			} else {
				//Moniter Mode

				xSemaphoreTake(InStabilityFlag_sem, portMAX_DELAY);

				if (xSemaphoreTake(MonitorTimer_sem,portMAX_DELAY) == pdTRUE) {
					if (InStabilityFlag) {
						LoadShed();
					} else {
						LoadConnect();
					}
				}

				xSemaphoreGive(InStabilityFlag_sem);
				xSemaphoreGive(MonitorMode_sem);
			}

		} else {
			//Maintenance Mode
			//TODO: Add more Logic
			if (xQueueReceive(SwitchQ,&temp, portMAX_DELAY) == pdTRUE) {
				xQueueSend(LEDQ,&temp,10);
			}
		}
		xSemaphoreGive(maintenanceModeFlag_mutex);
	}
}

static void MonitorSwitchLogic(void *pvParameters) {
	unsigned int temp;
	LEDStruct tempLED;

	while (1) {
		//TODO: Semaphore
		xSemaphoreTake(MonitorMode_sem, portMAX_DELAY);
		if (monitorMode) {
			xSemaphoreGive(MonitorMode_sem);
			if (xQueueReceive(SwitchQ,&temp, portMAX_DELAY) == pdTRUE) {
				
				//If maybe redundant here
				if (temp < SystemState.Red) {
					SystemState.Red = SystemState.Red & temp;
					SystemState.Green = SystemState.Green & temp;
				}
			}
		}
		xSemaphoreGive(MonitorMode_sem);
	}
}

static void MonitorTimer(void *pvParameters) {
	unsigned int PrevInstabilityFlag;
	//TODO: Semaphore
	if (xSemaphoreTake(MonitorMode_sem) == pdTRUE && monitorMode) {
		xSemaphoreGive(MonitorMode_sem);

		if ( xSemaphoreTake(InStabilityFlag_mutex) == pdTRUE) {

		 if (PrevInstabilityFlag != InStabilityFlag ) {
			 xSemaphoreGive(InStabilityFlag_mutex);
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

	//All Loads reconnected
    if (!temp.Green) {
       if (xSemaphoreTake(monitorMode_sem,portMAX_DELAY) == pdTRUE) {
		   monitorMode = 0;
		   xTimerStop(xTimer500, 500);
	   }
	} else {

		unsigned int ret = 1;

		// TODO: Start from bit 7 instead

		while (temp.Green >>= 1) {
			ret <<= 1;
		}

		temp.Green &= ~ret; 
		temp.Red |= ret;

		if (xQueueSend(LEDQ,&temp,10) == pdTRUE) {
			;
		}
	}
}

static void LoadShed() {
	xSemaphoreTake(SystemState_mutex,portMAX_DELAY);
	SystemState.Green;
	SystemState.Red;

	 if (!SystemState.Red) {
        return;
	} else {
		int num = 1;

		while (!(SystemState.Red & num)){
			num <<= 1;
		}

		SystemState.Red &= ~num;
		SystemState.Green |= num;

		if (xQueueSend(LEDQ,&temp,10) == pdTRUE) {
			;
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
   		 
		if (!maintenanceModeFlag) {

			CurrSwitchValue = IORD_ALTERA_AVALON_PIO_DATA(SLIDE_SWITCH_BASE) & 0x7F;
			
			if (monitorMode && (CurrSwitchValue < PrevSwitchValue)) {
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
	double lowerFreqBound_local = 0;

	// Max absolute rate of change before system enters shedding mode
	double rocBound_local = 0;

	while(1)
	{
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

			// Obtain copies of global threshold values
			xSemaphoreTake(lowerFreqBound_mutex, portMAX_DELAY);
			lowerFreqBound_local = lowerFreqBound;
			xSemaphoreGive(lowerFreqBound_mutex);

			xSemaphoreTake(rocBound_mutex, portMAX_DELAY);
			rocBound_local = rocBound;
			xSemaphoreGive(rocBound_mutex);

			// Check stable system for instability
			xSemaphoreTake(InStabilityFlag_mutex, portMAX_DELAY);
			if (InStabilityFlag == 0)
			{
				// Check if current freq is under lower threshold OR rate of change too high
				if ((newFreq < lowerFreqBound_local) || (rocLocal > rocBound_local))
				{
					InStabilityFlag = 1;
				}
			}
			// Check unstable system for stability
			else
			{
				// Check if current freq is under lower threshold OR rate of change too high
				if ((newFreq >= lowerFreqBound_local) && (rocLocal <= rocBound_local))
				{
					InStabilityFlag = 0;
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

	// newValue stores the new value to be updated, digitIndex keeps track of which digit we are on
	unsigned int newVal[4] = {0};
	unsigned int digitIndex = 0;
	double updateVal = 0;

	while(1)
	{
		// Receive keys from ISR
		if (xQueueReceive(keyboardQ, &newKey, portMAX_DELAY) == pdTRUE)
		{
			// Bitmask to keep lower 8 bits of newKey
			newKey = newKey & 0xFF;

			// Check if digit 0
			if (newKey == 48)
			{
				// Add zero to corresponding index
				newVal[digitIndex] = 0;
				digitIndex++;
			}
			// Check if digit 1-9 inclusive
			else if ((newKey > 48) && (newKey <= 57))
			{
				// Switch statement for all different digits
				switch(newKey)
				{
					case 49:
						newVal[digitIndex] = 1;
						break;
					case 50:
						newVal[digitIndex] = 2;
						break;
					case 51:
						newVal[digitIndex] = 3;
						break;
					case 52:
						newVal[digitIndex] = 4;
						break;
					case 53:
						newVal[digitIndex] = 5;
						break;
					case 54:
						newVal[digitIndex] = 6;
						break;
					case 55:
						newVal[digitIndex] = 7;
						break;
					case 56:
						newVal[digitIndex] = 8;
						break;
					case 57:
						newVal[digitIndex] = 9;
						break;
					default:
						newVal[digitIndex] = 0;
						break;
				}
				digitIndex++;
			}
			// Check for period .
			else if (newKey == 46)
			{
				newVal[digitIndex] = '.';
				digitIndex++;
			}
			// Check for SPACE
			else if (newKey == 41)
			{
				newVal[digitIndex] = 41;
			}

			// Check if SPACEBAR or array is full (four slots full)
			if ((newKey == 41) || (digitIndex == 4))
			{
				// Reset the currentInputIndex
				digitIndex = 0;

				// Generate a double from the array
				updateVal = array2double(newVal);

				printf("Double generated: %f\n", updateVal);
				usleep(10);

				xSemaphoreTake(whichBoundFlag_mutex, portMAX_DELAY);

				// Check what parameter is currently being edited
				if (whichBoundFlag == 0)
				{
					xSemaphoreTake(lowerFreqBound_mutex, portMAX_DELAY);

					// Currently editing lower freq bound
					lowerFreqBound = updateVal;
					whichBoundFlag = 1;

					xSemaphoreGive(lowerFreqBound_mutex);
				}
				else
				{
					xSemaphoreTake(rocBound_mutex, portMAX_DELAY);

					// Currently editing RoC bound
					rocBound = updateVal;
					whichBoundFlag = 0;

					xSemaphoreGive(rocBound_mutex);
				}

				xSemaphoreGive(whichBoundFlag_mutex);

				// Give sync semaphore so LCD can update
				xSemaphoreGive(lcdUpdate_sem);

				// Clear the array
				for (int i = 0; i < 4; i++)
				{
					newVal[i] = 0;
				}
			}

		}
	}

	return;
}

// Updates the LCD display with current lowerFreqBound and rocBound values, and indicates what is currently being edited in maintenance mode
void LCDUpdater(void *pvParameters)
{
	// Local copy of whichBoundFlag
	unsigned int whichBoundFlag_local = 0;

	// Local copies of lowerFreqBound and rocBound
	double lowerFreqBound_local = 0;
	double rocBound_local = 0;

	// Local copy of maintenanceModeFlag
	unsigned int maintenanceModeFlag_local = 0;

	// Pointer to the LCD
	FILE *lcd;

	while (1)
	{
		// Wait for semaphore indicating that a new value is ready
		xSemaphoreTake(lcdUpdate_sem, portMAX_DELAY);

		// Store local copy of whichBoundFlag
		xSemaphoreTake(whichBoundFlag_mutex, portMAX_DELAY);
		whichBoundFlag_local = whichBoundFlag;
		xSemaphoreGive(whichBoundFlag_mutex);

		// Store local copies of both bounds
		xSemaphoreTake(lowerFreqBound_mutex, portMAX_DELAY);
		lowerFreqBound_local = lowerFreqBound;
		xSemaphoreGive(lowerFreqBound_mutex);

		xSemaphoreTake(rocBound_mutex, portMAX_DELAY);
		rocBound_local = rocBound;
		xSemaphoreGive(rocBound_mutex);

		// Store local copy of maintenanceModeFlag
		xSemaphoreTake(maintenanceModeFlag_mutex, portMAX_DELAY);
		maintenanceModeFlag_local = maintenanceModeFlag;
		xSemaphoreGive(maintenanceModeFlag_mutex);

		// Open the character LCD
		lcd = fopen(CHARACTER_LCD_NAME, "w");

		// If LCD opens successfully
		if(lcd != NULL)
		{
			// Clear the screen
			#define ESC 27
			#define CLEAR_LCD_STRING "[2J"
			fprintf(lcd, "%c%s", ESC, CLEAR_LCD_STRING);

			// If maintenance mode, only write one
			if (maintenanceModeFlag_local == 1)
			{
				// Editing RoC
				if (whichBoundFlag_local == 1)
				{
					fprintf(lcd, "LowFreq: %.2f\n", lowerFreqBound_local);
					fprintf(lcd, "Enter RoC...\n");
				}
				// Editing lowerFreqBound
				else
				{
					fprintf(lcd, "RoC: %.2f\n", rocBound_local);
					fprintf(lcd, "Enter LowFreq...\n");
				}
			}
			// If normal mode write both to screen
			else
			{
				fprintf(lcd, "LowFreq: %.2f\n", lowerFreqBound_local);
				fprintf(lcd, "RoC: %.2f", rocBound_local);
			}
		}

		fclose(lcd);
	}
}


/*---------- FUNCTION DEFINITIONS ----------*/

// Helper function that returns a double from a given array of size 4 containing unsigned ints
double array2double(unsigned int *newVal)
{
	// Value to return
	double updateVal = 0;

	// Flag represents if we are currently assigning digits to the right of the decimal in updateVal
	unsigned int decimalRightFlag = 0;

	// tenthPwr determines how far right from decimal the current digit needs to go
	unsigned int tenthPwr = 1;

	for (int i = 0; i < 4; i++)
	{
		// If SPACE is detected then return array
		if (newVal[i] == 41)
		{
			return updateVal;
		}
		// If current digit period then set decimalRightFlag, remaining digits in newVal need to be divided by powers of 10
		else if (newVal[i] == 46)
		{
			decimalRightFlag = 1;
		}
		else
		{
			// Currently to right of decimal
			if (decimalRightFlag == 1)
			{
				updateVal += (newVal[i] * pow(0.1, tenthPwr++));
			}
			// Currently to left of decimal
			else
			{
				updateVal *= 10;
				updateVal += newVal[i];
			}
		}
	}

	return updateVal;
}

// Creates all tasks used
int CreateTasks() {
	xTaskCreate(WallSwitchPoll, "SwitchPoll", TASK_STACKSIZE, NULL, 1, NULL);
	xTaskCreate(StabilityControlCheck, "StabCheck", TASK_STACKSIZE, NULL, 2, NULL);
	xTaskCreate(VGADisplayTask, "VGADisplay", TASK_STACKSIZE, NULL, 3, NULL);
	xTaskCreate(load_manage,"LDM",TASK_STACKSIZE,NULL,4,NULL);
	xTaskCreate(LEDcontrol,"LCC",TASK_STACKSIZE,NULL,5,NULL);
	xTaskCreate(KeyboardReader, "KeyboardReader", TASK_STACKSIZE, NULL, 6, NULL);
	xTaskCreate(LCDUpdater, "LCDUpdater", TASK_STACKSIZE, NULL, 7, NULL);
	return 0;
}

// Creates all timers used
int CreateTimers() {
	MonitoringTimer = xTimerCreate("MT", 500, pdTRUE, NULL , vMonitoringTimerCallback);
	return 0;
}

// Initialises all data structures used
int OSDataInit() {
	// Initialise queues
	SwitchQ = xQueueCreate( 100, sizeof(unsigned int) );
	newFreqQ = xQueueCreate(10, sizeof( void* ));
	LEDQ = xQueueCreate(100, sizeof(LEDStruct));
	vgaFreqQ = xQueueCreate(MSG_QUEUE_SIZE, sizeof( void* ));
	keyboardQ = xQueueCreate(MSG_QUEUE_SIZE, sizeof( void* ));

	// Initialise Semaphores
	lcdUpdate_sem = xSemaphoreCreateBinary();

	// Initialise mutexes
	roc_mutex = xSemaphoreCreateMutex();
	InStabilityFlag_mutex = xSemaphoreCreateMutex();
	maintenanceModeFlag_mutex = xSemaphoreCreateMutex();
	lowerFreqBound_mutex = xSemaphoreCreateMutex();
	rocBound_mutex = xSemaphoreCreateMutex();
	whichBoundFlag_mutex = xSemaphoreCreateMutex();
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
