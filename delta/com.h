#pragma once

#include "common.h"

#define MAX_BUFFER_LENGTH	(1024)

/*
 * Default address for the controller.
 * Only one controller can be connected for one serial port.
 */
#define DEVICE_ADDR	(1)

extern char modbus_errmsg[ERRMSG_MAX_LEN];

extern bool ModbusSerialOpen(int port);
extern bool ModbusSerialClose(void);

extern int ModbusSerialRequest(const char* msg);
extern int ModbusSerialResponse(char* recvbuf, int buflen, int timeout);
