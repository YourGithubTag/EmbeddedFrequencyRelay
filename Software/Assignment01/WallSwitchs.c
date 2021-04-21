#include "WallSwitchs.h"

unsigned int CurrSwitchValue;
unsigned int PrevSwitchValue;


static void WallSwitchPoll(void *pvParameters) {
	unsigned int CurrSwitchValue;
	unsigned int PrevSwitchValue;

  {
    // read the value of the switch and store to uiSwitchValue
    CurrSwitchValue = IORD_ALTERA_AVALON_PIO_DATA(SLIDE_SWITCH_BASE) & 0x7F;

    if (CurrSwitchValue != PrevSwitchValue ) {
        
        if (xQueueSend(newLoadQ, &CurrSwitchValue, 10) == pdTRUE) {
        	;
        } else {
            printf("failed send");
        }
    }

  vTaskDelay(100);

  }

}


