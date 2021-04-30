// COMPSYS 723 Assignment 1
// Cecil Symes, Nikhil Kumar
// csym531, nkmu576

/*---------- INCLUDES ----------*/
/* Standard includes. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

/* Scheduler includes. */
#include "FreeRTOS/FreeRTOS.h"
#include "FreeRTOS/task.h"
#include "FreeRTOS/queue.h"
#include "FreeRTOS/semphr.h"
#include "FreeRTOS/timers.h"

/* HAL API includes */
#include "system.h"
#include "sys/alt_irq.h"
#include "sys/alt_timestamp.h"
#include "io.h"
#include "altera_avalon_pio_regs.h"
#include "unistd.h"
#include "altera_up_avalon_ps2.h"
#include "alt_types.h"
#include "altera_up_ps2_keyboard.h"
#include <altera_avalon_timer.h>
#include "altera_up_avalon_video_character_buffer_with_dma.h"
#include "altera_up_avalon_video_pixel_buffer_dma.h"

   

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

//For frequency plot
#define FREQPLT_ORI_X 101		//x axis pixel position at the plot origin
#define FREQPLT_GRID_SIZE_X 5	//pixel separation in the x axis between two data points
#define FREQPLT_ORI_Y 199.0		//y axis pixel position at the plot origin
#define FREQPLT_FREQ_RES 20.0	//number of pixels per Hz (y axis scale)

#define ROCPLT_ORI_X 101
#define ROCPLT_GRID_SIZE_X 5
#define ROCPLT_ORI_Y 259.0
#define ROCPLT_ROC_RES 1		//number of pixels per Hz/s (y axis scale)

#define MIN_FREQ 45.0 //minimum frequency to draw

#define PRVGADraw_Task_P      (tskIDLE_PRIORITY+1)


/*---------- GLOBAL VARIABLES ----------*/
QueueHandle_t msgqueue;

QueueHandle_t ControlQ;

// Used to delete tasks
TaskHandle_t xWallSwitchPoll;
TaskHandle_t xStabilityControlCheck;
TaskHandle_t xPRVGADraw_Task;
TaskHandle_t xload_manage;
TaskHandle_t xLEDcontrol;
TaskHandle_t xKeyboardReader;
TaskHandle_t xLCDUpdater;

// Definition of Semaphore
SemaphoreHandle_t shared_resource_sem;


// globals variables
QueueHandle_t SwitchQ;

QueueHandle_t LEDQ;

QueueHandle_t ReactionTimeQ;

// Queue for communication between StabilityControlCheck & VGADisplayTask
QueueHandle_t vgaFreqQ;

// Queue for communication between FreqAnalyserISR & StabilityControlCheck
QueueHandle_t newFreqQ;

// Mutex for protecting InStabilityFlag
SemaphoreHandle_t InStabilityFlag_mutex;

// Flag that represents if system is in shedding mode
unsigned int InStabilityFlag = 0;

// Mutex for protecting maintenance mode flag
SemaphoreHandle_t maintenanceModeFlag_mutex;
// System flag for if we are in maintenance mode
unsigned int maintenanceModeFlag = 0;

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
double lowerFreqBound = 49;
// Mutex for protecting lower frequency bound
SemaphoreHandle_t lowerFreqBound_mutex;
// Absolute rate of change bound
double rocBound = 20;
// Mutex for protecting rate of change bound
SemaphoreHandle_t rocBound_mutex;
/// whichBoundFlag represents which parameter is currently being edited, 0 = lowerFreqBound, 1 = rocBound)
unsigned int whichBoundFlag = 0;
// Mutex for protecting whichBoundFlag
SemaphoreHandle_t whichBoundFlag_mutex;

// Syncronisation semaphore between KeyboardChecker and LCDUpdater
SemaphoreHandle_t lcdUpdate_sem;

// Queue to send freq from ISR to PRVGADraw
QueueHandle_t Q_freq_data;

// Semaphore from KeyboardReader to PRVGADraw indicating a threshold has changed
SemaphoreHandle_t updatedBound_sem;

// Queue to send current system uptime tick count from freq_relay ISR to load_manage
QueueHandle_t tick2StabilityQ;

// Structure for data within PRVGA task
typedef struct{
	unsigned int x1;
	unsigned int y1;
	unsigned int x2;
	unsigned int y2;
}Line;


//Semaphore for monitoring mode

SemaphoreHandle_t monitorTimerControl_sem;
SemaphoreHandle_t monitorSwitchLogic_sem;

SemaphoreHandle_t SpeedCheckFlag_Mutex;

unsigned int SpeedCheckFlag = 1;

SemaphoreHandle_t LEDTickget_sem;

SemaphoreHandle_t EndTimeSemaphore;

QueueHandle_t timeVgaQ;


/*---------- FUNCTION DECLARATIONS ----------*/
double array2double(unsigned int *newVal);

TimerHandle_t MonitoringTimer;

SemaphoreHandle_t MonitorTimer_sem;

SemaphoreHandle_t SystemState_mutex;

LEDStruct SystemState;

unsigned int monitorMode = 0;

SemaphoreHandle_t MonitorMode_Mutex;

SemaphoreHandle_t SOmetjing;

//int tickCount = 0;

int timerLock = 0;

QueueHandle_t tickStab2LoadQ;

/*---------- INTERRUPT SERVICE ROUTINES ----------*/
// ISR for handling Frequency Relay Interrupt
void freq_relay(){
	// Read ADC count
	unsigned int adcCount = IORD(FREQUENCY_ANALYSER_BASE, 0);
	unsigned int tickCount = 0;

	// Send count to StabilityControlChecke if queue not full
	if (xQueueIsQueueFullFromISR(newFreqQ) == pdFALSE)
	{
		xQueueSendFromISR(newFreqQ, (void *)&adcCount, NULL);
	}

	// Send count to PRVGADraw_Task queue not full
	if (xQueueIsQueueFullFromISR(Q_freq_data) == pdFALSE)
	{
		xQueueSendToBackFromISR( Q_freq_data, (void*)&adcCount, pdFALSE );
	}

	// Send System uptime tick count to load_manage
	if (xQueueIsQueueFullFromISR(tick2StabilityQ) == pdFALSE)
	{
		//tickCount = xTaskGetTickCountFromISR();
		tickCount = alt_timestamp();
		xQueueSendFromISR(tick2StabilityQ, (void*)&tickCount, NULL);
	}

	// if (!SpeedCheckFlag) {
	// 	alt_timestamp_start();
	// 	tickCount = xTaskGetTickCount();
	// }
	return;
}
//
//void vMonitoringTimerCallback(xTimerHandle_t timer) {
//	xSemaphoreGive(MonitorTimer_sem,NULL);
//}

void Button_ISR (void* context, alt_u32 id) {

  	// clears the edge capture register
 	IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE, 0x7);

	xSemaphoreTakeFromISR(maintenanceModeFlag_mutex, NULL);
	if (maintenanceModeFlag == 0)
	{
		maintenanceModeFlag = 1;
	}
	else
	{
		maintenanceModeFlag = 0;
	}
	xSemaphoreGiveFromISR(maintenanceModeFlag_mutex, NULL);
//	printf("button \n");
}

void vMonitoringTimerCallback(TimerHandle_t timer) {
	xSemaphoreGiveFromISR(MonitorTimer_sem,NULL);
	
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
	unsigned int maintModeFlag_local = 0;
	if (xSemaphoreTakeFromISR(maintenanceModeFlag_mutex, portMAX_DELAY) == pdTRUE)
	{
		maintModeFlag_local = maintenanceModeFlag;
		xSemaphoreGiveFromISR(maintenanceModeFlag_mutex, NULL);
	}

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

void oneshotSample(void) {
	

	int CurrSwitchValue = IORD_ALTERA_AVALON_PIO_DATA(SLIDE_SWITCH_BASE);
	CurrSwitchValue = CurrSwitchValue & 0x7F;
//	printf("ONESHOT\n");

	if (xQueueSend(SwitchQ, &CurrSwitchValue, portMAX_DELAY) != pdTRUE) {
		printf("failed INIT switch send \n");
	} 

}

void LoadConnect() {
	LEDStruct state2send;

	xSemaphoreTake(SystemState_mutex,portMAX_DELAY);

	state2send = SystemState;

	xSemaphoreGive(SystemState_mutex);

	//All Loads reconnected
    if (!(state2send.Green)) {
		//Monitor mode semaphore 
		xSemaphoreTake(MonitorMode_Mutex,portMAX_DELAY);
		monitorMode = 0;
		xSemaphoreGive(MonitorMode_Mutex);
		xTimerStop(MonitoringTimer, 500);
		//oneshotSample();

	} else {
		unsigned int ret = 64;

		while (!(state2send.Green & ret)) {
			ret >>= 1;
		}

		state2send.Green &= ~ret; 
		state2send.Red |= ret;

		xSemaphoreTake(SystemState_mutex,portMAX_DELAY);
		SystemState = state2send;
		xSemaphoreGive(SystemState_mutex);

		xQueueSend(LEDQ,&state2send,50);
	}
	//printf("LOAD CONNECTED \n");
}

void LoadShed() {
	int timeStamp;
	LEDStruct state2send;

	xSemaphoreTake(SystemState_mutex, portMAX_DELAY);
	state2send = SystemState;
	xSemaphoreGive(SystemState_mutex);

	if (!(state2send.Red)) {
        return;
	} else {
		int num = 1;

		while (!(state2send.Red & num)){
			num <<= 1;
		}

		state2send.Red &= ~num;
		state2send.Green |= num;

		xSemaphoreTake(SystemState_mutex,portMAX_DELAY);
		SystemState = state2send;
		xSemaphoreGive(SystemState_mutex);

		// if (xSemaphoreTake(SpeedCheckFlag_Mutex,portMAX_DELAY) == pdTRUE) {
		// 	if (SpeedCheckFlag){
		// 		SpeedCheckFlag = 0;
		// 		timerLock = 0;

		// 		int DIff = xTaskGetTickCount() - tickCount;
		// 		timeStamp = (long)(alt_timestamp()) / 100000;

		// 		printf("Time taken to shed: %d \n",timeStamp);
		// 		printf("time diff tick for TICKS:%d", DIff);
		// 		xQueueSend(ReactionTimeQ, &timeStamp, 200);

		// 	}
		// xSemaphoreGive(SpeedCheckFlag_Mutex);
		// }

		xQueueSend(LEDQ,&state2send,50);
	}
	//printf("LOAD SHEDED \n");
}

void LoadManagement(void *pvParameters) {
	unsigned int temp;
	LEDStruct state2send;
	int oneshot = 1;
	unsigned int timeTaken;
	unsigned int tempTickCount = 0;
	unsigned int EndTick;
	
	while (1) {
		//TODO: Debug problems with Switch not updating the state fully
		//TODO: INSTANT GIVEBACK OF MUTEXES?? LOCAL COPY OF FLAGS???
		if (xSemaphoreTake(maintenanceModeFlag_mutex, 100) == pdTRUE) {
			if (!maintenanceModeFlag) {

				xSemaphoreGive(maintenanceModeFlag_mutex);

				if (xSemaphoreTake(MonitorMode_Mutex, 65) == pdTRUE) {
					//printf("Moniter mode : %d \n",monitorMode);

					if (!monitorMode) {
						//Normal Mode

							if (oneshot) {
								oneshot = 0;
								oneshotSample();

								//BACK TO NORMAL OPERATION
								//xSemaphoreGive(SpeedCheckFlag_sem);
							}

							//printf("\n NORMAL \n");

							xSemaphoreTake(InStabilityFlag_mutex, portMAX_DELAY);
							//Checking Instability whilst in NOrmal operation
							if (InStabilityFlag) {
								
								LoadShed();

								if (xQueueReceive(tickStab2LoadQ, &tempTickCount, portMAX_DELAY) == pdTRUE)
								{
//									printf("Old tickcount of %d received \n", tempTickCount);
//									usleep(100);
									//EndTick = xTaskGetTickCount();
									EndTick = alt_timestamp();
//									printf("NEW tickcount of %d recived \n", EndTick );

									timeTaken = (EndTick - tempTickCount) / 10000;

									// Send timeTake to PRVGADraw
									xQueueSend(timeVgaQ, &timeTaken, 0);

									//alt_timestamp_start();
								}
								
								xTimerStart(MonitoringTimer, 0);
								monitorMode = 1;


							} 

							xSemaphoreGive(InStabilityFlag_mutex);
							xSemaphoreGive(MonitorMode_Mutex);

							if (xQueueReceive(SwitchQ, &temp, 40) == pdTRUE) {

								xSemaphoreTake(SystemState_mutex,portMAX_DELAY);
								SystemState.Red = temp;
								SystemState.Green = 0;
								state2send = SystemState;
								xSemaphoreGive(SystemState_mutex);

								if (xQueueSend(LEDQ,&state2send,40) == pdTRUE) {
//									printf("Sent!!! \n");
								}
							}	
							
					} else {
						//MONITOR!!!!!

						xSemaphoreGive(MonitorMode_Mutex);
						xSemaphoreGive(monitorTimerControl_sem);
						//printf("GAVE SOME MONITOR SEMS");

						if (!oneshot) {
							oneshot = 1;
						}

						if (xQueueReceive(SwitchQ,&temp, 50) == pdTRUE) {


							if (xSemaphoreTake(SystemState_mutex,50) == pdTRUE ) {
							//If maybe redundant here

									SystemState.Red = SystemState.Red & temp;
									SystemState.Green = SystemState.Green & temp;

									state2send = SystemState;

									xQueueSend(LEDQ,&state2send,40);
							
									xSemaphoreGive(SystemState_mutex);
							}
						}
					}
				}
		
		} else {
			//Maintenance Mode
			xSemaphoreGive(maintenanceModeFlag_mutex);
			
			//printf("Maintenance \n");
				if (oneshot) {
						xTimerStop(MonitoringTimer,500);
						
//						printf("OneShot from Maintenance! \n");
						xSemaphoreTake(SystemState_mutex,portMAX_DELAY);
						oneshot = 0;
						oneshotSample();

						xQueueReceive(SwitchQ, &temp, 40);

						SystemState.Red = temp;
						SystemState.Green = 0;

						xQueueSend(LEDQ,&state2send,40);
						
						state2send = SystemState;
						xSemaphoreGive(SystemState_mutex);

				}


				if (xQueueReceive(SwitchQ, &temp, 40) == pdTRUE) {

					xSemaphoreTake(SystemState_mutex,portMAX_DELAY);
					SystemState.Red = temp;
					SystemState.Green = 0;
					state2send = SystemState;
					xSemaphoreGive(SystemState_mutex);

					if (xQueueSend(LEDQ,&state2send,40) == pdTRUE) {
//						printf("Sent from Maintenance! \n");
					}
				}
		}

		
	}
}

	vTaskDelay(10);
}



void MonitorTimer(void *pvParameters) {
	unsigned int PrevInstabilityFlag = InStabilityFlag;
	while(1) {

		if (xSemaphoreTake(monitorTimerControl_sem, portMAX_DELAY) == pdTRUE) {

			if ( xSemaphoreTake(InStabilityFlag_mutex, 10) == pdTRUE) {
				if (PrevInstabilityFlag != InStabilityFlag ) {

					PrevInstabilityFlag = InStabilityFlag;
					xSemaphoreGive(InStabilityFlag_mutex);
					//printf("Timer Flip\n");

					if (xTimerReset(MonitoringTimer, portMAX_DELAY) != pdTRUE) {
							printf("Timer cannot be reset");
					} 

				} else {
					xSemaphoreGive(InStabilityFlag_mutex);
				}
			}
		}
	}
}

void MonitorLogic(void *pvParameters) {

	while (1){
				
		if (xSemaphoreTake(MonitorTimer_sem,portMAX_DELAY) == pdTRUE) {
			if (xSemaphoreTake(InStabilityFlag_mutex, portMAX_DELAY) == pdTRUE){

				if (InStabilityFlag) {
					LoadShed();
					//printf("Unstable\n");
				} else {
					LoadConnect();
					//printf("Stable\n");
				}

				xSemaphoreGive(InStabilityFlag_mutex);
			}
		}
	}
}


void LEDcontrol(void *pvParameters) {
	LEDStruct Status;

	int check = 0;
	while (1) {
		if (xQueueReceive(LEDQ, &Status, portMAX_DELAY ) == pdTRUE ) {

			IOWR_ALTERA_AVALON_PIO_DATA(RED_LEDS_BASE, Status.Red);
			IOWR_ALTERA_AVALON_PIO_DATA(GREEN_LEDS_BASE, Status.Green);

			//xSemaphoreGive(EndTimeSemaphore);


		} else {
			;
			//printf("Dead Que \n");
		}
	}
}


void WallSwitchPoll(void *pvParameters) {

	unsigned int CurrSwitchValue = 0;
	unsigned int PrevSwitchValue = 0;
	oneshotSample();

  while (1){
	  	//REMOVED MAINTANCE MODE CHECK --MUTEX DEPENDANACE
				CurrSwitchValue = IORD_ALTERA_AVALON_PIO_DATA(SLIDE_SWITCH_BASE);
				CurrSwitchValue = CurrSwitchValue & 0x7F;

					if (CurrSwitchValue != PrevSwitchValue ) {
						if (xQueueSend(SwitchQ, &CurrSwitchValue, portMAX_DELAY) != pdTRUE) {
							printf("failed to send \n");
						} 
					}
				
				PrevSwitchValue = CurrSwitchValue;

 	 vTaskDelay(10);
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

	// Stores tickCount in order to calculate the system's response time
	unsigned int tickCount = 0;

	// Local copy of maintenanceModeFlag
	unsigned int maintenanceModeFlag_local = 0;

	// Local copy of monitorMode flag
	unsigned int monitorMode_local = 0;


	while(1)
	{

		// Wait for new frequency in queue
		if (xQueueReceive(newFreqQ, &temp, portMAX_DELAY) == pdTRUE)
		{
			// Take latest tickCount
			xQueueReceive(tick2StabilityQ, &tickCount, portMAX_DELAY);

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

					// Get mutexes
					xSemaphoreTake(MonitorMode_Mutex, portMAX_DELAY);
					monitorMode_local = monitorMode;
					xSemaphoreGive(MonitorMode_Mutex);

					xSemaphoreTake(maintenanceModeFlag_mutex, portMAX_DELAY);
					maintenanceModeFlag_local = maintenanceModeFlag;
					xSemaphoreGive(maintenanceModeFlag_mutex);

					// Only send the tickCount to loadmanage if in normal mode
					if ((maintenanceModeFlag_local == 0) && (monitorMode_local == 0))
					{
						xQueueReset(tickStab2LoadQ);
						// Send to load_manage
						if (xQueueSend(tickStab2LoadQ, &tickCount, 0) == pdFALSE)
						{
//							printf("Failed to send ticks from STABILITY to LOAD\n");
//							usleep(100);
						}
						else
						{
//							printf("Successfully sent ticks from STABILITY to LOAD\n");
//							usleep(100);
						}
					} else {
						alt_timestamp_start();
					}

					

				}
			}
			// Check unstable system for stability
			else
			{
				// Check if current freq is above low threshold AND rate of change is low
				if ((newFreq >= lowerFreqBound_local) && (rocLocal <= rocBound_local))
				{
					InStabilityFlag = 0;
					
				}
				
			}
			xSemaphoreGive(InStabilityFlag_mutex);
			xQueueReset(tick2StabilityQ);

			if (xQueueReceive(tick2StabilityQ, &tickCount, 0) == pdFALSE)
			{
				// printf("Queue emptied successfully\n");
				// usleep(100);
			}
		}
	}

}

// Receives frequency information, displays to VGA
void PRVGADraw_Task(void *pvParameters ){
	//initialize VGA controllers
	alt_up_pixel_buffer_dma_dev *pixel_buf;
	pixel_buf = alt_up_pixel_buffer_dma_open_dev(VIDEO_PIXEL_BUFFER_DMA_NAME);
	if(pixel_buf == NULL){
		printf("can't find pixel buffer device\n");
	}
	alt_up_pixel_buffer_dma_clear_screen(pixel_buf, 0);

	alt_up_char_buffer_dev *char_buf;
	char_buf = alt_up_char_buffer_open_dev("/dev/video_character_buffer_with_dma");
	if(char_buf == NULL){
		printf("can't find char buffer device\n");
	}
	alt_up_char_buffer_clear(char_buf);

	//Set up plot axes
	alt_up_pixel_buffer_dma_draw_hline(pixel_buf, 100, 590, 200, ((0x3ff << 20) + (0x3ff << 10) + (0x3ff)), 0);
	alt_up_pixel_buffer_dma_draw_hline(pixel_buf, 100, 590, 300, ((0x3ff << 20) + (0x3ff << 10) + (0x3ff)), 0);
	alt_up_pixel_buffer_dma_draw_vline(pixel_buf, 100, 50, 200, ((0x3ff << 20) + (0x3ff << 10) + (0x3ff)), 0);
	alt_up_pixel_buffer_dma_draw_vline(pixel_buf, 100, 220, 300, ((0x3ff << 20) + (0x3ff << 10) + (0x3ff)), 0);

	alt_up_char_buffer_string(char_buf, "Frequency(Hz)", 4, 4);
	alt_up_char_buffer_string(char_buf, "52", 10, 7);
	alt_up_char_buffer_string(char_buf, "50", 10, 12);
	alt_up_char_buffer_string(char_buf, "48", 10, 17);
	alt_up_char_buffer_string(char_buf, "46", 10, 22);

	alt_up_char_buffer_string(char_buf, "df/dt(Hz/s)", 4, 26);
	alt_up_char_buffer_string(char_buf, "30", 10, 28);
	alt_up_char_buffer_string(char_buf, "15", 10, 30);
	alt_up_char_buffer_string(char_buf, "0", 10, 32);
	alt_up_char_buffer_string(char_buf, "-15", 9, 34);
	alt_up_char_buffer_string(char_buf, "-30", 9, 36);

	double freq[100], dfreq[100];
	unsigned int tempCount;
	int i = 99, j = 0;
	Line line_freq, line_roc;

	// Integer represents what system status currently is
	unsigned int systemStatus = 0;
	unsigned int prev_systemStatus = 0;

	// Local copy of instability flag
	unsigned int instableFlag_local = 0;

	// Local copy of maintenance mode flag
	unsigned int maintFlag_local = 0;

	// Local copy of rocBound
	double rocBound_local = 0;

	// Local copy of lowerFreqBound
	double lowerFreqBound_local = 0;

	// Information on system response times
	unsigned int latestTime;
	unsigned int minTime = 0;
	unsigned int maxTime = 0;
	unsigned int avgTime = 0;
	unsigned int lastFive[5] = {0};
	float uptime = 0;
	unsigned int avgCount = 0;

	// Print base miscellaneous text
	char str[100];

	// Get mutexes for global variable thresholds
	xSemaphoreTake(rocBound_mutex, portMAX_DELAY);
	rocBound_local = rocBound;
	xSemaphoreGive(rocBound_mutex);

	sprintf(str, "RoC Threshold: %7.2f Hz/sec", rocBound_local);
	alt_up_char_buffer_string(char_buf, str, 5, 49);

	xSemaphoreTake(lowerFreqBound_mutex, portMAX_DELAY);
	lowerFreqBound_local = lowerFreqBound;
	xSemaphoreGive(lowerFreqBound_mutex);

	sprintf(str, "Low Freq Threshold: %7.2f Hz", lowerFreqBound_local);
	alt_up_char_buffer_string(char_buf, str, 5, 45);

	alt_up_char_buffer_string(char_buf, "System Status: DEFAULT", 5, 41);
	alt_up_char_buffer_string(char_buf, "System Uptime: x", 40, 41);
	alt_up_char_buffer_string(char_buf, "Avg Response Time: x", 40, 43);
	alt_up_char_buffer_string(char_buf, "Min Response Time: x", 40, 45);
	alt_up_char_buffer_string(char_buf, "Max Response Time: x", 40, 47);
	alt_up_char_buffer_string(char_buf, "Last 5 Response Times:", 40, 49);
	alt_up_char_buffer_string(char_buf, "[t0, t1, t2, t3, t4]", 40, 51);

	while(1){

		//receive frequency data from queue
		while(uxQueueMessagesWaiting( Q_freq_data ) != 0){
			xQueueReceive( Q_freq_data, &tempCount, 0 );
			freq[i] = 16000/(double)tempCount;

			// Calculate frequency RoC
			if(i==0){
				dfreq[0] = (freq[0]-freq[99]) * 2.0 * freq[0] * freq[99] / (freq[0]+freq[99]);
			}
			else{
				dfreq[i] = (freq[i]-freq[i-1]) * 2.0 * freq[i]* freq[i-1] / (freq[i]+freq[i-1]);
			}

			if (dfreq[i] > 100.0){
				dfreq[i] = 100.0;
			}


			i =	++i%100; //point to the next data (oldest) to be overwritten

		}

		//clear old graph to draw new graph
		alt_up_pixel_buffer_dma_draw_box(pixel_buf, 101, 0, 639, 199, 0, 0);
		alt_up_pixel_buffer_dma_draw_box(pixel_buf, 101, 201, 639, 299, 0, 0);

		for(j=0;j<99;++j)
		{ //i here points to the oldest data, j loops through all the data to be drawn on VGA
			if (((int)(freq[(i+j)%100]) > MIN_FREQ) && ((int)(freq[(i+j+1)%100]) > MIN_FREQ))
			{
				//Calculate coordinates of the two data points to draw a line in between
				//Frequency plot
				line_freq.x1 = FREQPLT_ORI_X + FREQPLT_GRID_SIZE_X * j;
				line_freq.y1 = (int)(FREQPLT_ORI_Y - FREQPLT_FREQ_RES * (freq[(i+j)%100] - MIN_FREQ));

				line_freq.x2 = FREQPLT_ORI_X + FREQPLT_GRID_SIZE_X * (j + 1);
				line_freq.y2 = (int)(FREQPLT_ORI_Y - FREQPLT_FREQ_RES * (freq[(i+j+1)%100] - MIN_FREQ));

				//Frequency RoC plot
				line_roc.x1 = ROCPLT_ORI_X + ROCPLT_GRID_SIZE_X * j;
				line_roc.y1 = (int)(ROCPLT_ORI_Y - ROCPLT_ROC_RES * dfreq[(i+j)%100]);

				line_roc.x2 = ROCPLT_ORI_X + ROCPLT_GRID_SIZE_X * (j + 1);
				line_roc.y2 = (int)(ROCPLT_ORI_Y - ROCPLT_ROC_RES * dfreq[(i+j+1)%100]);

				//Draw
				alt_up_pixel_buffer_dma_draw_line(pixel_buf, line_freq.x1, line_freq.y1, line_freq.x2, line_freq.y2, 0x3ff << 0, 0);
				alt_up_pixel_buffer_dma_draw_line(pixel_buf, line_roc.x1, line_roc.y1, line_roc.x2, line_roc.y2, 0x3ff << 0, 0);
			}
		}

		// Check if currently in maintenance
		if (xSemaphoreTake(maintenanceModeFlag_mutex, portMAX_DELAY) == pdTRUE)
		{
			maintFlag_local =  maintenanceModeFlag;
			xSemaphoreGive(maintenanceModeFlag_mutex);
		}

		// Check if currently unstable
		if (xSemaphoreTake(InStabilityFlag_mutex, portMAX_DELAY) == pdTRUE)
		{
			instableFlag_local = InStabilityFlag;
			xSemaphoreGive(InStabilityFlag_mutex);
		}

		// Update system status based on priority
		if (maintFlag_local == 1)
		{
			// Maintenance
			prev_systemStatus = systemStatus;
			systemStatus = 2;
//			printf("System is in maintenance\n");
//			usleep(100);
		}
		else if (instableFlag_local == 1)
		{
			// Unstable
			prev_systemStatus = systemStatus;
			systemStatus = 1;
//			printf("System is unstable\n");
//			usleep(100);
		}
		else
		{
			// Stable
			prev_systemStatus = systemStatus;
			systemStatus = 0;
//			printf("System is stable\n");
//			usleep(100);
		}

		// Check if system state has just changed and screen must be updated
		if (prev_systemStatus != systemStatus)
		{
			// Update screen based on systemStatus
			if (systemStatus == 2)
			{
				alt_up_char_buffer_string(char_buf, "MAINTENANCE  ", 20, 41);
//				printf("System just entered maintenance\n");
//				usleep(100);
			}
			else if (systemStatus == 1)
			{
				alt_up_char_buffer_string(char_buf, "UNSTABLE     ", 20, 41);
//				printf("System just became unstable\n");
//				usleep(100);
			}
			else
			{
				alt_up_char_buffer_string(char_buf, "STABLE       ", 20, 41);
//				printf("System just became stable\n");
//				usleep(100);
			}

			// Update prev_systemStatus
			prev_systemStatus = systemStatus;
		}

		// If either rocBound or lowFreqBound have changed then update on screen
		if (xSemaphoreTake(updatedBound_sem, 5)) // TODO: Maybe change to 0 delay???
		{
			// Attempt to get mutexes for both global threshold variables
			xSemaphoreTake(rocBound_mutex, portMAX_DELAY);
			rocBound_local = rocBound;
			xSemaphoreGive(rocBound_mutex);

			xSemaphoreTake(lowerFreqBound_mutex, portMAX_DELAY);
			lowerFreqBound_local = lowerFreqBound;
			xSemaphoreGive(lowerFreqBound_mutex);

			// Update the VGA display values
			sprintf(str, "%7.2f Hz", lowerFreqBound_local);
			alt_up_char_buffer_string(char_buf, str, 25, 45);
			sprintf(str, "%7.2f Hz/sec", rocBound_local);
			alt_up_char_buffer_string(char_buf, str, 20, 49);
		}

		// Display information on system response times
		if (xQueueReceive(timeVgaQ, &latestTime, 5) == pdTRUE)
		{
			// Store latest time
			for (int i = 4; i > 0; i--)
			{
				lastFive[i] = lastFive[i - 1];
			}
			lastFive[0] = latestTime;

			minTime = latestTime;
		}

		// Determine min max, and avg time out of last 5
		avgTime = 0;
		avgCount = 0;
		for (int i = 0; i < 5; i++)
		{
			if ((lastFive[i] < minTime) && (lastFive[i] != 0))
			{
				minTime = lastFive[i];
			}

			if (lastFive[i] > maxTime)
			{
				maxTime = lastFive[i];
			}

			if (lastFive[i] != 0)
			{
				avgCount++;
				avgTime += lastFive[i];
			}

		}
		avgTime /= avgCount;

		// Get total system uptime in seconds
		uptime = (float)xTaskGetTickCount() / 1000;

		// Update values on the monitor

		sprintf(str, "System Uptime: %6.2f s", uptime);
		alt_up_char_buffer_string(char_buf, str, 40, 41);

		sprintf(str, "Avg Response Time: %3d ms", avgTime);
		alt_up_char_buffer_string(char_buf, str, 40, 43);

		sprintf(str, "Min Response Time: %3d ms", minTime);
		alt_up_char_buffer_string(char_buf, str, 40, 45);

		sprintf(str, "Max Response Time: %3d ms", maxTime);
		alt_up_char_buffer_string(char_buf, str, 40, 47);

		alt_up_char_buffer_string(char_buf, "Last 5 Response Times (ms):", 40, 49);
		sprintf(str, "[%3d, %3d, %3d, %3d, %3d]", lastFive[0], lastFive[1], lastFive[2], lastFive[3], lastFive[4]);
		alt_up_char_buffer_string(char_buf, str, 40, 51);
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

//				printf("Double generated: %f\n", updateVal);
//				usleep(10);

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

				// Give sync semaphore so VGA can update
				xSemaphoreGive(updatedBound_sem);

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

	// Initialise local copies of both bounds
	xSemaphoreTake(lowerFreqBound_mutex, portMAX_DELAY);
	lowerFreqBound_local = lowerFreqBound;
	xSemaphoreGive(lowerFreqBound_mutex);

	xSemaphoreTake(rocBound_mutex, portMAX_DELAY);
	rocBound_local = rocBound;
	xSemaphoreGive(rocBound_mutex);

	while (1)
	{
		// Store local copy of maintenanceModeFlag
		xSemaphoreTake(maintenanceModeFlag_mutex, portMAX_DELAY);
		maintenanceModeFlag_local = maintenanceModeFlag;
		xSemaphoreGive(maintenanceModeFlag_mutex);

		// Wait for semaphore indicating that a new value is ready
		if (xSemaphoreTake(lcdUpdate_sem, 5000) == pdTRUE)
		{
			// Store local copy of whichBoundFlag
			xSemaphoreTake(whichBoundFlag_mutex, portMAX_DELAY);
			whichBoundFlag_local = whichBoundFlag;
			xSemaphoreGive(whichBoundFlag_mutex);

			// Update the local copies of both bounds
			xSemaphoreTake(lowerFreqBound_mutex, portMAX_DELAY);
			lowerFreqBound_local = lowerFreqBound;
			xSemaphoreGive(lowerFreqBound_mutex);

			xSemaphoreTake(rocBound_mutex, portMAX_DELAY);
			rocBound_local = rocBound;
			xSemaphoreGive(rocBound_mutex);

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
		// If no update for 5 seconds then just display old values
		else
		{
			// Open the character LCD
			lcd = fopen(CHARACTER_LCD_NAME, "w");

			// If LCD opens successfully
			if(lcd != NULL)
			{
				// Clear the screen
				#define ESC 27
				#define CLEAR_LCD_STRING "[2J"
				fprintf(lcd, "%c%s", ESC, CLEAR_LCD_STRING);

				fprintf(lcd, "LowFreq: %.2f\n", lowerFreqBound_local);
				fprintf(lcd, "RoC: %.2f", rocBound_local);
			}

			fclose(lcd);
		}

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

	xTaskCreate(PRVGADraw_Task, "PRVGADraw_Task", TASK_STACKSIZE, NULL, 1, xPRVGADraw_Task);
	xTaskCreate(LCDUpdater, "LCDUpdater", TASK_STACKSIZE, NULL, 2, xLCDUpdater);
	xTaskCreate(WallSwitchPoll, "SwitchPoll", TASK_STACKSIZE, NULL, 3, xWallSwitchPoll);
	xTaskCreate(KeyboardReader, "KeyboardReader", TASK_STACKSIZE, NULL, 4, xKeyboardReader);
	xTaskCreate(MonitorLogic, "ML",TASK_STACKSIZE, NULL, 5, NULL);
	xTaskCreate(MonitorTimer, "MT",TASK_STACKSIZE, NULL, 6, NULL);
	xTaskCreate(LEDcontrol, "LCC", TASK_STACKSIZE, NULL, 7, NULL);
	xTaskCreate(LoadManagement, "LDM", TASK_STACKSIZE, NULL, 8, NULL);
	xTaskCreate(StabilityControlCheck, "StabCheck", TASK_STACKSIZE, NULL, 9, xStabilityControlCheck);

	//xTaskCreate(load_manage,"LDM",TASK_STACKSIZE,NULL,4,xload_manage);
	//xTaskCreate(LEDcontrol,"LCC",TASK_STACKSIZE,NULL,5,xLEDcontrol);

//	printf("All tasks made Okay! \n");
//	usleep(10);
	return 0;
}


int CreateTimers() {
	MonitoringTimer = xTimerCreate("MT", 500, pdTRUE, NULL , vMonitoringTimerCallback);
	return 0;
}

// Initialises all data structures used
int OSDataInit() {
	// Initialise queues
	SwitchQ = xQueueCreate(40, sizeof(unsigned int) );
	LEDQ = xQueueCreate(50, sizeof(LEDStruct));
	newFreqQ = xQueueCreate(10, sizeof( void* ));
	vgaFreqQ = xQueueCreate(MSG_QUEUE_SIZE, sizeof( void* ));
	keyboardQ = xQueueCreate(MSG_QUEUE_SIZE, sizeof( void* ));
	Q_freq_data = xQueueCreate(MSG_QUEUE_SIZE, sizeof( void* ));
	tick2StabilityQ = xQueueCreate(MSG_QUEUE_SIZE, sizeof( void* ));
	tickStab2LoadQ = xQueueCreate(MSG_QUEUE_SIZE, sizeof( void* ));
	ReactionTimeQ = xQueueCreate(MSG_QUEUE_SIZE, sizeof(int));
	timeVgaQ = xQueueCreate(MSG_QUEUE_SIZE, sizeof( void* ));

	// Initialise Semaphores
	lcdUpdate_sem = xSemaphoreCreateBinary();
	updatedBound_sem = xSemaphoreCreateBinary();
	monitorTimerControl_sem = xSemaphoreCreateBinary();
	monitorSwitchLogic_sem = xSemaphoreCreateBinary();
	MonitorTimer_sem = xSemaphoreCreateBinary();
	LEDTickget_sem = xSemaphoreCreateBinary();
	EndTimeSemaphore = xSemaphoreCreateBinary();

	// Initialise mutexes
	roc_mutex = xSemaphoreCreateMutex();
	InStabilityFlag_mutex = xSemaphoreCreateMutex();
	maintenanceModeFlag_mutex = xSemaphoreCreateMutex();
	lowerFreqBound_mutex = xSemaphoreCreateMutex();
	rocBound_mutex = xSemaphoreCreateMutex();
	whichBoundFlag_mutex = xSemaphoreCreateMutex();
	SystemState_mutex = xSemaphoreCreateMutex();
	MonitorMode_Mutex = xSemaphoreCreateMutex();

	SpeedCheckFlag_Mutex = xSemaphoreCreateMutex();

	return 0;
}

int main(int argc, char* argv[], char* envp[])
{
	printf("Hello from Nios II!\n");

	// Initialise data structures
	OSDataInit();

	// Create all tasks
	CreateTasks();

	CreateTimers();
	
	// Initialise PS2 device
	alt_up_ps2_dev * ps2_device = alt_up_ps2_open_dev(PS2_NAME);

	// Check if PS2 device connected
	if(ps2_device == NULL){
		printf("Can't find PS/2 device.\n");
	}

	// Register PS2 ISR
	alt_up_ps2_clear_fifo(ps2_device);
	alt_irq_register(PS2_IRQ, ps2_device, ps2_isr);
	IOWR_8DIRECT(PS2_BASE,4,1);

	// Register Frequency Analyser ISR
	alt_irq_register(FREQUENCY_ANALYSER_IRQ, 0, freq_relay);

	//BUTTON ISR
	//clear all interrupts in edge registor
	IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE, 0x7);

	// enable interrupt for 1 button
  	IOWR_ALTERA_AVALON_PIO_IRQ_MASK(PUSH_BUTTON_BASE, 0x1);

  	//register the ISR
	//TODO remove buttonvalue
  	alt_irq_register(PUSH_BUTTON_IRQ, 0, Button_ISR);

	alt_timestamp_start();
	
	// Start Scheduler
	vTaskStartScheduler();
	// Program will only reach here if insufficient heap to start scheduler
	printf("Insufficient heap to start scheduler.\n");
	for(;;) {
		;
	}

    return 0;
}
