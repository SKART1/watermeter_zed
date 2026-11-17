#ifndef LEAK_TEST_ZCL_INCLUDE_H
#define LEAK_TEST_ZCL_INCLUDE_H

#include "tl_common.h"

/* Wire constants mirror aps_api.h, zcl_ias_zone.h and zcl_onoff.h. */
#define HA_PROFILE_ID 0x0104
#define APS_DSTADDR_EP_NOTPRESETNT 0
#define APS_SHORT_DSTADDR_WITHEP 2
#define APS_LONG_DSTADDR_WITHEP 3
#define ZCL_CLUSTER_SS_IAS_ZONE 0x0500
#define ZCL_ATTRID_ZONE_STATE 0x0000
#define ZCL_ATTRID_ZONE_STATUS 0x0002
#define ZCL_ATTRID_IAS_CIE_ADDR 0x0010
#define ZCL_ATTRID_ZONE_ID 0x0011
#define ZONE_STATE_ENROLLED 1
#define ZONE_STATUS_BIT_ALARM1 0x0001
#define ZCL_CMD_ONOFF_OFF 0
#define ZCL_CMD_ONOFF_ON 1
#define ZCL_CMD_ONOFF_TOGGLE 2

typedef struct {
    u16 profileId;
    u8 dstAddrMode;
    u8 dstEp;
    union {
        u16 shortAddr;
        u8 extAddr[8];
    } dstAddr;
} epInfo_t;

typedef struct {
    u16 zoneStatus;
    u8 extStatus;
    u8 zoneId;
    u16 delay;
} zoneStatusChangeNoti_t;

u8 zb_isDeviceJoinedNwk(void);
u8 zcl_getAttrVal(u8 endpoint, u16 cluster, u16 attribute, u16 *length, u8 *value);
u8 zcl_setAttrVal(u8 endpoint, u16 cluster, u16 attribute, u8 *value);
u8 zcl_iasZone_statusChangeNotificationCmd(u8 endpoint, epInfo_t *destination,
                                        u8 disableDefaultResponse,
                                        zoneStatusChangeNoti_t *notification);
u8 zcl_onOff_offCmd(u8 endpoint, epInfo_t *destination, u8 disableDefaultResponse);
u8 zcl_onOff_onCmd(u8 endpoint, epInfo_t *destination, u8 disableDefaultResponse);
u8 zcl_onOff_toggleCmd(u8 endpoint, epInfo_t *destination, u8 disableDefaultResponse);

#endif
