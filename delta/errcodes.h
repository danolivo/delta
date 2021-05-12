#pragma once

typedef enum errcode_t
{
	/* General */
	CONN_UNKNOWN_PROBLEM,
	SUCCESS,

	/*
	 * Errors, related to this code
	 */
	DB_UNKNOWN_PROBLEM,
	DB_NOT_OPENED,
	DB_ACCESS_FAILED,
	DB_CELL_NOT_FOUND,
	DB_INCORRECT_STATE,
	INCORRECT_EAN_FORMAT,

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
