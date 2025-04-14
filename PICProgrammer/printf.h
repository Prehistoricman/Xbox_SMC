#include <Arduino.h>

extern "C" {

#include "xprintf.h"

void serialprint(char c) {
	Serial.print(c);
}

void printf_init(void) {
	xdev_out(serialprint);
}

#define printf xprintf

}