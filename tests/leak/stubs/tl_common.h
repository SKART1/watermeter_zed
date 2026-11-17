#ifndef LEAK_TEST_TL_COMMON_H
#define LEAK_TEST_TL_COMMON_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;

#define TRUE 1
#define FALSE 0
#define TL_SETSTRUCTCONTENT(value, byte) memset(&(value), (byte), sizeof(value))

int drv_gpio_read(unsigned pin);
void sleep_ms(unsigned milliseconds);

#endif
