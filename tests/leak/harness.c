/* Execute the real app_leak.c with observed SDK boundaries, not an OR model.
 * Each scenario runs in a fresh process; production static state is untouched.
 */
#include <stdlib.h>
#include "watermeter.h"

#define BASE_STATUS 0x02eeu
#define MAX_EVENTS 128

static const u8 cie_address[8] = {0x81, 0x22, 0x63, 0x44, 0xa5, 0x16, 0x77, 0x08};
static int gpio_level[2] = {1, 1};
static u8 joined = TRUE;
static u8 enrolled = ZONE_STATE_ENROLLED;
static u16 zone_status = BASE_STATUS;
static zcl_onOffSwitchCfgAttr_t settings;
static int drop_notifications;
static unsigned calls;
static const char *scenario;

typedef struct {
    u16 status;
    unsigned call;
    int delivered;
} notification_t;

typedef struct {
    u8 endpoint;
    u8 command;
    unsigned call;
} command_t;

static notification_t notifications[MAX_EVENTS];
static command_t commands[MAX_EVENTS];
static unsigned notification_count;
static unsigned command_count;

static void fail(const char *message, unsigned line) {
    unsigned i;
    fprintf(stderr, "FAIL %s at call %u (line %u): %s; IAS=0x%04x\n",
            scenario, calls, line, message, zone_status);
    for (i = 0; i < notification_count; ++i) {
        fprintf(stderr, "  IAS call=%u status=0x%04x delivered=%d\n",
                notifications[i].call, notifications[i].status,
                notifications[i].delivered);
    }
    for (i = 0; i < command_count; ++i) {
        fprintf(stderr, "  CMD call=%u sourceEP=%u command=%u\n",
                commands[i].call, commands[i].endpoint, commands[i].command);
    }
    exit(1);
}

#define CHECK(condition, message) do { if (!(condition)) fail((message), __LINE__); } while (0)

int drv_gpio_read(unsigned pin) {
    CHECK(pin < 2, "unexpected GPIO");
    return gpio_level[pin];
}

void sleep_ms(unsigned milliseconds) {
    CHECK(milliseconds == 1, "debounce sleep changed");
}

u8 zb_isDeviceJoinedNwk(void) {
    return joined;
}

zcl_onOffSwitchCfgAttr_t *zcl_onOffSwitchCfgAttrGet(void) {
    return &settings;
}

u8 zcl_getAttrVal(u8 endpoint, u16 cluster, u16 attribute, u16 *length, u8 *value) {
    CHECK(endpoint == 4 && cluster == ZCL_CLUSTER_SS_IAS_ZONE, "IAS attribute endpoint/cluster");
    switch (attribute) {
        case ZCL_ATTRID_ZONE_STATE:
            *length = sizeof(enrolled);
            memcpy(value, &enrolled, *length);
            break;
        case ZCL_ATTRID_ZONE_STATUS:
            *length = sizeof(zone_status);
            memcpy(value, &zone_status, *length);
            break;
        case ZCL_ATTRID_ZONE_ID:
            *length = 1;
            *value = 0x5a;
            break;
        case ZCL_ATTRID_IAS_CIE_ADDR:
            *length = sizeof(cie_address);
            memcpy(value, cie_address, *length);
            break;
        default:
            fail("unexpected attribute read", __LINE__);
    }
    return 0;
}

u8 zcl_setAttrVal(u8 endpoint, u16 cluster, u16 attribute, u8 *value) {
    CHECK(joined, "IAS attribute written outside joined gate");
    CHECK(endpoint == 4 && cluster == ZCL_CLUSTER_SS_IAS_ZONE &&
          attribute == ZCL_ATTRID_ZONE_STATUS, "unexpected IAS attribute write");
    memcpy(&zone_status, value, sizeof(zone_status));
    CHECK((zone_status & ~ZONE_STATUS_BIT_ALARM1) == BASE_STATUS, "other IAS bits changed");
    return 0;
}

u8 zcl_iasZone_statusChangeNotificationCmd(u8 endpoint, epInfo_t *destination,
                                        u8 disableDefaultResponse,
                                        zoneStatusChangeNoti_t *notification) {
    CHECK(joined, "IAS sent outside joined gate");
    CHECK(notification_count < MAX_EVENTS, "too many notifications");
    CHECK(endpoint == 4 && destination->dstEp == 4 &&
          destination->profileId == HA_PROFILE_ID, "IAS endpoint/profile changed");
    CHECK(disableDefaultResponse == TRUE, "IAS default response policy changed");
    CHECK(notification->zoneStatus == zone_status && notification->zoneId == 0x5a &&
          notification->extStatus == 0 && notification->delay == 0, "IAS payload changed");
    if (enrolled) {
        CHECK(destination->dstAddrMode == APS_LONG_DSTADDR_WITHEP &&
              memcmp(destination->dstAddr.extAddr, cie_address, sizeof(cie_address)) == 0,
              "enrolled CIE destination changed");
    } else {
        CHECK(destination->dstAddrMode == APS_SHORT_DSTADDR_WITHEP &&
              destination->dstAddr.shortAddr == 0, "unenrolled destination changed");
    }
    notifications[notification_count].status = notification->zoneStatus;
    notifications[notification_count].call = calls;
    notifications[notification_count].delivered = !drop_notifications;
    ++notification_count;
    return drop_notifications ? 1 : 0;
}

static u8 record_command(u8 endpoint, epInfo_t *destination, u8 disableDefaultResponse,
                         u8 command) {
    CHECK(joined, "command sent outside joined gate");
    CHECK(command_count < MAX_EVENTS, "too many commands");
    CHECK((endpoint == 4 || endpoint == 5) && destination->profileId == HA_PROFILE_ID &&
          destination->dstAddrMode == APS_DSTADDR_EP_NOTPRESETNT,
          "OnOff source/destination changed");
    CHECK(disableDefaultResponse == FALSE, "OnOff default response policy changed");
    commands[command_count].endpoint = endpoint;
    commands[command_count].command = command;
    commands[command_count].call = calls;
    ++command_count;
    return 0;
}

u8 zcl_onOff_offCmd(u8 endpoint, epInfo_t *destination, u8 disableDefaultResponse) {
    return record_command(endpoint, destination, disableDefaultResponse, ZCL_CMD_ONOFF_OFF);
}
u8 zcl_onOff_onCmd(u8 endpoint, epInfo_t *destination, u8 disableDefaultResponse) {
    return record_command(endpoint, destination, disableDefaultResponse, ZCL_CMD_ONOFF_ON);
}
u8 zcl_onOff_toggleCmd(u8 endpoint, epInfo_t *destination, u8 disableDefaultResponse) {
    return record_command(endpoint, destination, disableDefaultResponse, ZCL_CMD_ONOFF_TOGGLE);
}

/* mask bit0=A wet, bit1=B wet; invert only at the hardware boundary. */
static void tick(unsigned mask) {
    gpio_level[0] = !(mask & 1);
    gpio_level[1] = !(mask & 2);
    ++calls;
    waterleak_handler();
}

static void hold(unsigned mask, unsigned count) {
    while (count--) tick(mask);
}

static void check_alarm(int alarm, unsigned first_notification) {
    unsigned i;
    CHECK(!!(zone_status & ZONE_STATUS_BIT_ALARM1) == alarm, "incorrect aggregate IAS attribute");
    for (i = first_notification; i < notification_count; ++i) {
        CHECK(!!(notifications[i].status & ZONE_STATUS_BIT_ALARM1) == alarm,
              "incorrect intermediate outgoing IAS alarm");
    }
}

static void tick_checked(unsigned mask, int alarm) {
    unsigned mark = notification_count;
    tick(mask);
    check_alarm(alarm, mark);
}

static void stable_transition(unsigned initial, unsigned target) {
    unsigned i;
    unsigned before_boundary;
    hold(initial, 31);
    check_alarm(initial != 0, 0);
    for (i = 0; i < 30; ++i) tick_checked(target, initial != 0);
    before_boundary = notification_count;
    tick_checked(target, target != 0);
    CHECK(notification_count == before_boundary + (initial != target ? 1u : 0u),
          "simultaneous boundaries must produce one aggregate IAS notification");
    CHECK(!waterleak_idle(), "debounce did not reach a stable boundary");
}

static void overlap(unsigned first) {
    unsigned i;
    hold(first, 31);
    for (i = 0; i < 31; ++i) tick_checked(3, 1);
    for (i = 0; i < 31; ++i) tick_checked(first, 1);
    CHECK(command_count == (UART_PRINTF_MODE && DEBUG_WATERLEAK ? 6u : 4u),
          "overlap command count changed");
    for (i = 0; i < 30; ++i) tick_checked(0, 1);
    tick_checked(0, 0);
}

static void partial_other(unsigned wet, unsigned pulse) {
    unsigned i;
    hold(wet, 31);
    for (i = 0; i < pulse; ++i) tick_checked(3, 1);
    for (i = 0; i < pulse; ++i) tick_checked(wet, 1);
    CHECK(command_count == (UART_PRINTF_MODE && DEBUG_WATERLEAK ? 4u : 2u),
          "partial dry boundary command guard changed");
}

static void wet_bounce(unsigned wet, unsigned dry_samples) {
    unsigned i;
    hold(wet, 31);
    for (i = 0; i < dry_samples; ++i) tick_checked(0, 1);
    CHECK(waterleak_idle(), "intermediate debounce should inhibit sleep");
    for (i = 0; i < dry_samples; ++i) tick_checked(wet, 1);
    CHECK(command_count == 4, "return to wet threshold must repeat the original wet command pair");
}

static void latched_during_other_event(unsigned wet) {
    unsigned other = wet ^ 3;
    unsigned i;
    hold(wet, 31);
    tick_checked(0, 1);          /* confirmed wet, counter has already left 32 */
    tick_checked(other, 1);      /* one-sample pulse on the other input */
    tick_checked(0, 1);          /* other dry boundary must still report wet */
    for (i = 0; i < 27; ++i) tick_checked(0, 1);
    tick_checked(0, 0);          /* last confirmed wet reaches dry threshold */
}

static void expect_pair(unsigned *index, unsigned action4, unsigned action5, int dry) {
    unsigned actions[2] = {action4, action5};
    unsigned i;
    for (i = 0; i < 2; ++i) {
        unsigned action = actions[i];
        if (action > 2) continue;
        CHECK(*index < command_count, "missing OnOff command");
        CHECK(commands[*index].endpoint == 4 + i, "OnOff endpoint order changed");
        CHECK(commands[*index].command == (action == 2 ? 2 : (dry ? 1 - action : action)),
              "OnOff action/direction changed");
        ++*index;
    }
}

static void command_trace(unsigned action4, unsigned action5) {
    unsigned index = 0;
    settings.ep4_attrs.switchActions = action4;
    settings.ep5_attrs.switchActions = action5;
    hold(1, 31);                 /* A wet -> EP4, EP5 */
    hold(3, 31);                 /* B wet -> EP4, EP5 */
    tick(2);                    /* A 32 -> 31: no command */
    tick(3);                    /* A 31 -> 32: repeat original wet commands */
    hold(0, 31);                /* dry commands remain debug-only */
    expect_pair(&index, action4, action5, 0);
    expect_pair(&index, action4, action5, 0);
    expect_pair(&index, action4, action5, 0);
    if (UART_PRINTF_MODE && DEBUG_WATERLEAK) {
        expect_pair(&index, action4, action5, 1);
        expect_pair(&index, action4, action5, 1);
    }
    CHECK(index == command_count, "extra OnOff commands");
}

static void partial_dry_trace(unsigned action4, unsigned action5) {
    unsigned index = 0;
    settings.ep4_attrs.switchActions = action4;
    settings.ep5_attrs.switchActions = action5;
    hold(1, 7);
    hold(0, 7);                 /* no confirmed wet, but original dry boundary event */
    if (UART_PRINTF_MODE && DEBUG_WATERLEAK) expect_pair(&index, action4, action5, 1);
    CHECK(index == command_count, "partial pulse command guard changed");
    CHECK(notification_count == 1, "joined dry boundary notification was suppressed");
    check_alarm(0, 0);
}

static void offline_then_second_wet(unsigned first) {
    joined = FALSE;
    hold(first, 31);
    CHECK(notification_count == 0 && command_count == 0, "offline event escaped gate");
    check_alarm(0, 0);
    joined = TRUE;
    hold(first, 64);
    CHECK(notification_count == 0 && command_count == 0, "unexpected automatic network resync");
    check_alarm(0, 0);          /* known limitation deliberately remains */
    hold(3, 31);
    CHECK(notification_count == 1 && command_count == 2, "second online wet event suppressed");
    check_alarm(1, 0);
}

static void offline_clear(unsigned first) {
    hold(first, 31);
    joined = FALSE;
    hold(0, 31);
    CHECK(notification_count == 1 && command_count == 2, "offline dry escaped gate");
    joined = TRUE;
    hold(0, 64);
    CHECK(notification_count == 1 && command_count == 2, "unexpected dry network resync");
    check_alarm(1, 0);          /* existing stale-IAS limitation is not fixed here */
    hold(first ^ 3, 31);
    check_alarm(1, 1);
    hold(0, 31);
    check_alarm(0, 2);
}

static void threshold_gate(unsigned first, int wet_event, int joined_at_boundary) {
    if (!wet_event) hold(first, 31);
    joined = !joined_at_boundary;
    hold(wet_event ? first : 0, 30);
    {
        unsigned before_ias = notification_count;
        unsigned before_commands = command_count;
        joined = joined_at_boundary;
        tick(wet_event ? first : 0);
        CHECK(notification_count == before_ias + (joined_at_boundary ? 1u : 0u),
              "IAS boundary joined gate changed");
        CHECK(command_count == before_commands +
              ((joined_at_boundary && (wet_event || (UART_PRINTF_MODE && DEBUG_WATERLEAK))) ? 2u : 0u),
              "OnOff boundary joined gate changed");
    }
}

static void lost_delivery(unsigned wet) {
    unsigned count;
    drop_notifications = 1;
    hold(wet, 31);
    CHECK(notification_count == 1 && !notifications[0].delivered, "delivery failure not exercised");
    check_alarm(1, 0);
    count = notification_count;
    hold(wet, 200);
    CHECK(notification_count == count && command_count == 2, "unexpected IAS/actuator retry");
}

static unsigned number(const char *value) {
    char *end;
    unsigned long result = strtoul(value, &end, 0);
    CHECK(*value && !*end && result <= 255, "invalid scenario argument");
    return (unsigned)result;
}

int main(int argc, char **argv) {
    CHECK(argc >= 2, "scenario required");
    scenario = argv[1];
    if (!strcmp(scenario, "transition") && argc == 4) {
        stable_transition(number(argv[2]), number(argv[3]));
    } else if (!strcmp(scenario, "overlap") && argc == 3) {
        overlap(number(argv[2]));
    } else if (!strcmp(scenario, "partial-other") && argc == 4) {
        partial_other(number(argv[2]), number(argv[3]));
    } else if (!strcmp(scenario, "wet-bounce") && argc == 4) {
        wet_bounce(number(argv[2]), number(argv[3]));
    } else if (!strcmp(scenario, "latched-other") && argc == 3) {
        latched_during_other_event(number(argv[2]));
    } else if (!strcmp(scenario, "commands") && argc == 4) {
        command_trace(number(argv[2]), number(argv[3]));
    } else if (!strcmp(scenario, "partial-dry") && argc == 4) {
        partial_dry_trace(number(argv[2]), number(argv[3]));
    } else if (!strcmp(scenario, "offline-wet") && argc == 3) {
        offline_then_second_wet(number(argv[2]));
    } else if (!strcmp(scenario, "offline-clear") && argc == 3) {
        offline_clear(number(argv[2]));
    } else if (!strcmp(scenario, "threshold-gate") && argc == 5) {
        threshold_gate(number(argv[2]), number(argv[3]), number(argv[4]));
    } else if (!strcmp(scenario, "lost-delivery") && argc == 3) {
        lost_delivery(number(argv[2]));
    } else if (!strcmp(scenario, "unenrolled") && argc == 2) {
        enrolled = 0;
        stable_transition(0, 1);
    } else {
        fail("unknown scenario/arguments", __LINE__);
    }
    printf("PASS %s (%u handler calls, %u IAS, %u OnOff)\n",
           scenario, calls, notification_count, command_count);
    return 0;
}
