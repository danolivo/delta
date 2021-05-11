#pragma once

typedef enum errcode_t
{
	/* General */
	SUCCESS,

	/*
	 * Errors, related to this code
	 */
	DB_NOT_OPENED,
	EAN_CODE_FORMAT,

	/* MODBUS-related errors. */
	MODBUS_UNKNOWN_PROBLEM,
	MODBUS_REQUEST_FAILED,
	MODBUS_RESPONSE_FAILED,

	/*
	* Errors, returned by controller.
	*/
	CNTL_UNKNOWN_PROBLEM,
	CNTL_FORMAT_VIOLENCE,
	CNTL_TIMEOUT_EXPIRED,
	CNTL_FORMAT_INCORRECT_SIGN

	/* ... */
} errcode_t;
