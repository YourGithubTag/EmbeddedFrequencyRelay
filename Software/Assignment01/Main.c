/* Standard includes. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Scheduler includes. */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

/* The parameters passed to the reg test tasks.  This is just done to check
 the parameter passing mechanism is working correctly. */
#define mainREG_TEST_1_PARAMETER    ( ( void * ) 0x12345678 )
#define mainREG_TEST_2_PARAMETER    ( ( void * ) 0x87654321 )
#define mainREG_TEST_PRIORITY       ( tskIDLE_PRIORITY + 1)
static void prvFirstRegTestTask(void *pvParameters);
static void prvSecondRegTestTask(void *pvParameters);

/*
 * Create the demo tasks then start the scheduler.
 */
int main(void)
{
	/* The RegTest tasks as described at the top of this file. */

	/* Finally start the scheduler. */
	vTaskStartScheduler();

	/* Will only reach here if there is insufficient heap available to start
	 the scheduler. */
	for (;;);

}

static void InitInterupt(void *pvParameters ) {



}


static void InitDataStructs(void *pvParameters ) {



}

static void InitTasks(void *pvParameters ) {



}


static void StabilityTask(void *pvParameters)
{
	while (1)
	{

	}

}

static void WallSwitchTask(void *pvParameters)
{
	while (1)
	{

	}
}

static void RedLEDControl(void *pvParameters)
{
	while (1)
	{

	}
}

static void GreenLEDControl(void *pvParameters)
{
	while (1)
	{

	}
}


static void VGADisplayTask(void *pvParameters)
{
	while (1)
	{

	}
}
