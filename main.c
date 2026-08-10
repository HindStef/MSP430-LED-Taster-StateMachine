/**
 * @file main.c
 * @brief LED-Taste-StateMachine
 * State Machine zur Auswertung eines Tasters zur Steuerung einer LED.
 *
 * @author Stefan Hinderegger
 *
 * @todo doxygen Dokumentation.
 *
 * @section TODO todo
 * 
 * @mainpage LED Taster Statemachine 
 *
 * @anchor mango Sensor Mainpage
 *
 * Testprogramm für das MSP-EXP430F5529LP Eval Board von Texas Instruments. <br>
 * Leiterplatte : MSP-EXP430F5529LP Eval Board
 *
 * \version   0.01
 * 
 *
 * @author Stefan Hinderegger
 *
 * \addtogroup compiling_group Compiler Vorgaben
 * - nnn
 *
 * @section sec_call Compilierung
 *  -# mit TI Code Composer Studio
 *
 * \author Stefan Hinderegger
 *
 *
 * \addtogroup COPYRIGHT_GROUP  Copyright
 * --COPYRIGHT--,BSD
 *  \copyright (c) 2017, Texas Instruments Incorporated , All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * --/COPYRIGHT--
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>                               // to include printf()
#include <string.h>
// Basic MSP430 and driverLib #includes
#include "msp430.h"
#include "driverlib.h"
#include "BCUart.h"           // Include the backchannel UART "library"

/* ---------------------------------------------------------------------- */
/** Konfiguration / Zeitkonstanten                                        */
/* ---------------------------------------------------------------------- */

#define DEBOUNCE_TIME_MS         20U     /* Entprellzeit                  */
#define LONG_PRESS_THRESHOLD_MS  2000U   /* Schwelle fuer langen Tastendruck */
#define BLINK_PERIOD_MS          330U    //500U    /* LED-Blinkperiode (an/aus je 500ms) */

/* ---------------------------------------------------------------------- */
/* Definitionen */
#define FOREVER         1
#define LED_ROT         0x01  /* rote LED an PortA.0 */
#define LED_ROT_MASK    2  /* Taster an PortA.0 */
#define LED_GREEN_P4    0x80

/* ---------------------------------------------------------------------- */
/* Strukturen */
typedef enum {
    BTN_STATE_IDLE = 0,      /* Taste nicht gedrueckt                    */
    BTN_STATE_DEBOUNCE,      /* Flankenwechsel erkannt, entprellen        */
    BTN_STATE_PRESSED,       /* Taste sicher gedrueckt, Zeit wird gezaehlt */
    BTN_STATE_LONG_DETECTED  /* Lange Presszeit bereits gemeldet          */
} ButtonState_t;

typedef enum {
    BTN_EVT_NONE = 0,
    BTN_EVT_SHORT_PRESS,   /* kurzer Tastendruck abgeschlossen (bei Release) */
    BTN_EVT_LONG_PRESS,    /* Presszeit hat Schwelle ueberschritten          */
    BTN_EVT_RELEASE        /* Taste losgelassen (informativ)                */
} ButtonEvent_t;

typedef struct {
    ButtonState_t state;
    uint16_t      press_time_ms;   /* Dauer seit gesichertem "gedrueckt"   */
    uint16_t      debounce_time_ms;
    bool          raw_level_last;
} Button_t;

typedef enum {
    LED_STATE_OFF = 0,
    LED_STATE_ON,
    LED_STATE_BLINK
} LedState_t;

typedef struct {
    LedState_t state;
    uint16_t   blink_timer_ms;
    bool       blink_phase_on;   /* aktueller Zustand waehrend BLINK */
} Led_t;

/* ---------------------------------------------------------------------- */
/* globale Variable */
static Button_t g_button;
static Led_t    g_led;

/* ---------------------------------------------------------------------- */
/* Prototypen */
void setup (void) ;
void StopWatchdog (void) ;
void init_clock(void) ;
bool read_KeyS1( void) ;
void delay(uint32_t x) ;
void str_to_bcUART ( uint8_t * sStr) ;
static bool Button_ReadRaw(void) ;
static void LED_SetHardware(bool on) ;
static void Button_Init(Button_t *btn) ;
static ButtonEvent_t Button_Process_1ms(Button_t *btn) ;
static void LED_Init(Led_t *led) ;
static void LED_HandleEvent(Led_t *led, ButtonEvent_t event) ;
static void LED_Process_1ms(Led_t *led) ;
void App_1ms_Tick(void) ;

/* ---------------------------------------------------------------------- */
/**
 * @brief Watchdog ausschalten
 */
void StopWatchdog (void) {
	WDTCTL = WDTPW + WDTHOLD;		// Halt the dog
}

/**
 * @brief Systemtak einstellen
 */
void init_clock(void) {
        // Config clocks. MCLK=SMCLK=FLL=8MHz; ACLK=REFO=32kHz
    uint32_t mclkFreq = 8000000 ;

    // Assign the REFO as the FLL reference clock
	UCS_initClockSignal(
	   UCS_FLLREF,
	   UCS_REFOCLK_SELECT,
	   UCS_CLOCK_DIVIDER_1);

	// Assign the REFO as the source for ACLK
	UCS_initClockSignal(
	   UCS_ACLK,
	   UCS_REFOCLK_SELECT,
	   UCS_CLOCK_DIVIDER_1);

    UCS_initFLLSettle(
        mclkFreq/1000,
        mclkFreq/32768);
        //use REFO for FLL and ACLK
        UCSCTL3 = (UCSCTL3 & ~(SELREF_7)) | (SELREF__REFOCLK);
        UCSCTL4 = (UCSCTL4 & ~(SELA_7)) | (SELA__REFOCLK);
}

/**
 * @brief Ports und Timer einrichten
 */
void setup (void) {
    StopWatchdog () ;
    // MSP430 USB requires a Vcore setting of at least 2.  2 is high enough
	// for 8MHz MCLK, below.
    PMM_setVCore(PMM_CORE_LEVEL_2);

    init_clock();
    bcUartInit();          // Init the back-channel UART: 115200 bps

    // initPorts();           // Config all the GPIOS for low-power (output low)
        // an Port 1 bit 0 ist die rote LED angeschlossen, LED über Vorwiderstand an VCC, 0 bedeutet LED ist an und leuchtet.
        // an Port 4 bit 7 ist die grüne LED angeschlossen, 
        // an Port 2 bit 1 ist der Taster S1 angeschlossen
    P1DIR |= 0x01;          // P1.0 output, P1.1 input
	P1OUT = 0x03;           // rote LED aus, LED
	P4DIR = 0x80;           // P4.7 output grüne LED / LED_GREEN_P4
   	P4OUT = 0x00;
        //Set P2.1 to input direction
    GPIO_setAsInputPin ( GPIO_PORT_P2, GPIO_PIN1 ) ;
    GPIO_setAsInputPinWithPullUpResistor ( GPIO_PORT_P2, GPIO_PIN1 ) ;
    GPIO_setAsOutputPin ( GPIO_PORT_P1, GPIO_PIN1 ) ;
    GPIO_setAsOutputPin ( GPIO_PORT_P4, GPIO_PIN7 ) ;

    GPIO_setOutputLowOnPin(GPIO_PORT_P1, GPIO_PIN1);
    GPIO_setOutputLowOnPin(GPIO_PORT_P4, GPIO_PIN7);
    
    // init Variable of FSM
    Button_Init(&g_button);
    LED_Init(&g_led);
} 

/**
 * @brief delay.c
 * @param x Verzögerungszähler als long int
 * @return void
 */
void delay(uint32_t x) {
    volatile uint32_t i=0 ;
    
    for (i=0;i<x;i++) {
    }
    return;
}

/**
 * @brief gibt den Zustand des Tasters S1 zurück
 * S1 an P2.1
 * S1 0x02  Taster an PortB.1 
 *
 */
bool read_KeyS1( void) {
    bool bRet = false ;
    
    //if (GPIO_INPUT_PIN_HIGH == GPIO_getInputPinValue( GPIO_PORT_P2, GPIO_PIN1 )) {
    
         //Test S1 P2.1
    if (GPIO_INPUT_PIN_HIGH == GPIO_getInputPinValue (GPIO_PORT_P2, GPIO_PIN1) ) {
        bRet = false ;
    } else {
        bRet = true ;
    }
   
    return (bRet);
}

/**
 * @brief Liefert den aktuellen (entprellten oder rohen) Tasterpegel.
 * @return true  = Taste gedrueckt
 *         false = Taste losgelassen
 *
 * TODO: An reale GPIO-Abfrage anpassen, z.B.:
 *       return (P0_PIN_BUTTON == 0); // Taster aktiv-low
 */
static bool Button_ReadRaw(void) {
    return read_KeyS1 () ;
}

/**
 * @brief Schaltet die LED hardwarenah ein/aus.
 * TODO: An reale GPIO-Ansteuerung anpassen, z.B.:
 *       if (on) { LED_PORT |= LED_MASK; } else { LED_PORT &= ~LED_MASK; }
 */
static void LED_SetHardware(bool on) {
    if (on) {
        // LED einschalten.
        P1OUT |= LED_ROT;  // P1.0 = 1
    } else {
        // LED ausschalten.
        P1OUT &= ~LED_ROT;  // P1.0 = 0
    }
}

#if 0
/**
 * @brief LED Satus bestimmen.
 */
bool readLED(void) {
    bool bRet = false;
    
    if ((P1OUT | LED_ROT) == LED_ROT) {
        bRet = true ;
    }
    return (bRet);
}
#endif 

/* ---------------------------------------------------------------------- */
/* Button FSM                                                              */
/* ---------------------------------------------------------------------- */

static void Button_Init(Button_t *btn) {
    btn->state            = BTN_STATE_IDLE;
    btn->press_time_ms    = 0U;
    btn->debounce_time_ms = 0U;
    btn->raw_level_last   = false;
}

/**
 * @brief Zyklisch (alle 1 ms) aufzurufende Button-FSM.
 * @param btn Zeiger auf Button-Instanz.
 * @return Erkanntes Ereignis (oder BTN_EVT_NONE, wenn keins aufgetreten ist).
 */
static ButtonEvent_t Button_Process_1ms(Button_t *btn) {
    ButtonEvent_t event = BTN_EVT_NONE;
    bool raw_level = Button_ReadRaw();

    if (raw_level == true) {
        P4OUT |= LED_GREEN_P4;      // Grüne LED an
    } else {
        P4OUT &= ~LED_GREEN_P4;     // Grüne LED aus
    }

    switch (btn->state)
    {
        case BTN_STATE_IDLE:
            if (raw_level == true) {
                /* Moeglicher Tastendruck erkannt -> entprellen */
                btn->state = BTN_STATE_DEBOUNCE;
                btn->debounce_time_ms = 0U;
            }
            break;

        case BTN_STATE_DEBOUNCE:
            btn->debounce_time_ms++;
            if (raw_level == false) {
                /* War nur ein Stoerimpuls, zurueck zu IDLE */
                btn->state = BTN_STATE_IDLE;
            }
            else if (btn->debounce_time_ms >= DEBOUNCE_TIME_MS) {
                /* Tastendruck bestaetigt */
                btn->state = BTN_STATE_PRESSED;
                btn->press_time_ms = 0U;
                str_to_bcUART ("BTN_STATE_PRESSED.\r\n") ;
            }
            break;

        case BTN_STATE_PRESSED:
            if (raw_level == false) {
                /* Taste innerhalb der kurzen Zeit losgelassen */
                event = BTN_EVT_SHORT_PRESS;
                btn->state = BTN_STATE_IDLE;
                str_to_bcUART ("BTN_EVT_SHORT_PRESS.\r\n") ;
            } else {
                btn->press_time_ms++;
                if (btn->press_time_ms >= LONG_PRESS_THRESHOLD_MS) {
                    /* Schwelle erreicht -> langer Tastendruck melden */
                    event = BTN_EVT_LONG_PRESS;
                    btn->state = BTN_STATE_LONG_DETECTED;
                    str_to_bcUART ("BTN_EVT_LONG_PRESS.\r\n") ;
                }
            }
            break;

        case BTN_STATE_LONG_DETECTED:
            /* LONG_PRESS wurde bereits gemeldet, warten auf Loslassen */
            if (raw_level == false) {
                event = BTN_EVT_RELEASE;
                btn->state = BTN_STATE_IDLE;
                str_to_bcUART ("BTN_EVT_RELEASE.\r\n") ;
            }
            break;

        default:
            btn->state = BTN_STATE_IDLE;
            break;
    }

    btn->raw_level_last = raw_level;

    return event;
}

/* ---------------------------------------------------------------------- */
/* LED FSM                                                                 */
/* ---------------------------------------------------------------------- */

static void LED_Init(Led_t *led) {
    led->state           = LED_STATE_OFF;
    led->blink_timer_ms   = 0U;
    led->blink_phase_on   = false;
    LED_SetHardware(false);
}

/**
 * @brief Verarbeitet ein Button-Ereignis und aktualisiert den LED-Zustand.
 *        Zustandsuebergaenge gemaess Aufgabenstellung:
 *          - SHORT_PRESS: OFF <-> ON (Toggle)
 *          - LONG_PRESS : -> BLINK (unabhaengig vom vorherigen Zustand)
 */
static void LED_HandleEvent(Led_t *led, ButtonEvent_t event) {
    switch (event)
    {
        case BTN_EVT_SHORT_PRESS:
            if (led->state == LED_STATE_ON) {
                led->state = LED_STATE_OFF;
                LED_SetHardware(false);
                str_to_bcUART ("rote LED aus.\r\n") ;
            } else if (led->state == LED_STATE_BLINK) {
                /* aus BLINK heraus: LED ausschalten */
                led->state = LED_STATE_OFF;
                LED_SetHardware(false);
                str_to_bcUART ("stop blinken und rote LED aus.\r\n") ;
            } else {
                /* aus OFF heraus: LED einschalten */
                led->state = LED_STATE_ON;
                LED_SetHardware(true);
                str_to_bcUART ("rote LED an.\r\n") ;
            }
            break;

        case BTN_EVT_LONG_PRESS:
            led->state = LED_STATE_BLINK;
            led->blink_timer_ms = 0U;
            led->blink_phase_on = true;
            LED_SetHardware(true);
            str_to_bcUART ("start blinken mit der roten LED.\r\n") ;
            break;

        case BTN_EVT_RELEASE:
        case BTN_EVT_NONE:
        default:
            /* Kein Zustandswechsel bei diesen Ereignissen */
            break;
    }
}

/**
 * @brief Zyklisch (alle 1 ms) aufzurufende LED-FSM fuer das Blinkverhalten.
 *        Ereignisbehandlung erfolgt separat ueber LED_HandleEvent().
 */
static void LED_Process_1ms(Led_t *led) {
    if (led->state == LED_STATE_BLINK) {
        led->blink_timer_ms++;
        if (led->blink_timer_ms >= BLINK_PERIOD_MS) {
            led->blink_timer_ms = 0U;
            led->blink_phase_on = !led->blink_phase_on;
            LED_SetHardware(led->blink_phase_on);
        }
    }
    /* OFF und ON benoetigen keine periodische Aktion */
}

/**
 * @brief sendet eine string an den MSP430 Backchannel UART.
 */
void str_to_bcUART ( uint8_t * sStr) {
 
    bcUartSend(sStr, strlen((char *) sStr));
    // uint8_t * buf, uint8_t len)
}

/**
 * @brief Zentraler 1ms-Tick, z.B. aus Timer-ISR oder Hauptschleife aufgerufen.
 *        Verbindet Button-FSM (Ereigniserzeugung) mit LED-FSM (Ereignis-
 *        verarbeitung + eigenes Zeitverhalten fuer das Blinken).
 */
void App_1ms_Tick(void) {
    ButtonEvent_t event = Button_Process_1ms(&g_button);

    if (event != BTN_EVT_NONE) {
        LED_HandleEvent(&g_led, event);
    }

    LED_Process_1ms(&g_led);
}

//******************************************************************************
/**
 * @brief LED-Taste-StateMachine
 * State Machine zur Auswertung eines Tasters zur Steuerung einer LED.
 * 
 *
 ******************************************************************************/
#define STR_START "
void main (void) {
    volatile bool bTick = 0 ;
    uint16_t u16_MS_cnt = 0 ;
    uint16_t u16SekCnt = 0 ;
//    uint8_t sStr[80];

    setup ();
    
    // strcpy ((char*) &sStr[0], "LED Tasten State Machine.\r\n");
    // bcUartSend(sStr, strlen(sStr));
    
    str_to_bcUART ("LED Tasten State Machine.\r\n") ;

    while (FOREVER) {
            /* Timer setzt Tick alle 10ms */
        if (bTick) {
            u16_MS_cnt ++ ;
            
            App_1ms_Tick() ;

            if (u16_MS_cnt == 1000) {
                /* Sekundenraster */
                u16_MS_cnt = 0 ;
                u16SekCnt ++;
            }
        }

        /* besser bTick im Timer Interrupt setzen! */
#define DLY_10MS    40000   // 500 bei 1MHz
#define DLY_1MS     344     // 43 bei 1MHz
        delay (DLY_1MS); /* wait 1ms */
        bTick = true; 

#if 0
        // debug Test 1ms Intervall
        if ((P4IN & LED_GREEN_P4) == LED_GREEN_P4) {
            P4OUT &= ~LED_GREEN_P4;
        } else {
            P4OUT |= LED_GREEN_P4;
        }
#endif
    
    }
}


/*   ***   EOF   ***   */
