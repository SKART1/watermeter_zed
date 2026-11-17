#ifndef LEAK_TEST_WATERMETER_H
#define LEAK_TEST_WATERMETER_H

#include "zcl_include.h"

#ifndef UART_PRINTF_MODE
#define UART_PRINTF_MODE 0
#endif
#ifndef DEBUG_WATERLEAK
#define DEBUG_WATERLEAK 0
#endif
#define DEBUG_ONOFF 0

/* Abstract GPIO IDs: the harness controls physical input levels independently. */
#define WLEAK1_GPIO 0
#define WLEAK2_GPIO 1
#define WATERMETER_ENDPOINT4 4
#define WATERMETER_ENDPOINT5 5
#define ZCL_SWITCH_ACTION_ON_OFF 0
#define ZCL_SWITCH_ACTION_OFF_ON 1
#define ZCL_SWITCH_ACTION_TOGGLE 2

typedef struct {
    u8 switchType;
    u8 switchActions;
} onOffSwitchCfgAttr_t;

typedef struct {
    onOffSwitchCfgAttr_t ep4_attrs;
    onOffSwitchCfgAttr_t ep5_attrs;
} zcl_onOffSwitchCfgAttr_t;

zcl_onOffSwitchCfgAttr_t *zcl_onOffSwitchCfgAttrGet(void);
void waterleak_handler(void);
uint8_t waterleak_idle(void);
void fillIASAddress(epInfo_t *destination);

#endif
