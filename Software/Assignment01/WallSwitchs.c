#include "main.h"
#include "WallSwitchs.h"

#include <stdio.h>
#include <unistd.h>
#include "system.h"
#include "altera_avalon_pio_regs.h"

unsigned int CurrSwitchValue = 0;
unsigned int PrevSwitchValue = 0;

static void WallSwitchPoll() {

while(1)
  {
    // read the value of the switch and store to uiSwitchValue
    CurrSwitchValue = IORD_ALTERA_AVALON_PIO_DATA(SLIDE_SWITCH_BASE);

    if (CurrSwitchValue != PrevSwitchValue ) {
        
        if (xQueueSend(newLoadQ, &CurrSwitchValue, 10) == pdTRUE) {
        	;
            
        } else {
            printf("failed send");
        }

    }


  }




}


