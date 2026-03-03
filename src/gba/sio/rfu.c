/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/sio/rfu.h>

#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/io.h>

enum {
	RFU_COMMAND_HEADER = 0x9966,
	RFU_DATA_REQUEST = 0x80000000,
	RFU_ACK_OFFSET = 0x80,
	RFU_ERROR_ACK = 0xEE,

	RFU_COMMAND_HELLO = 0x10,
	RFU_COMMAND_SIGNAL_LEVEL = 0x11,
	RFU_COMMAND_VERSION_STATUS = 0x12,
	RFU_COMMAND_SYSTEM_STATUS = 0x13,
	RFU_COMMAND_SLOT_STATUS = 0x14,
	RFU_COMMAND_CONFIG_STATUS = 0x15,
	RFU_COMMAND_BROADCAST = 0x16,
	RFU_COMMAND_SETUP = 0x17,
	RFU_COMMAND_START_HOST = 0x19,
	RFU_COMMAND_POLL_CONNECTIONS = 0x1A,
	RFU_COMMAND_END_HOST = 0x1B,
	RFU_COMMAND_BROADCAST_READ_START = 0x1C,
	RFU_COMMAND_BROADCAST_READ_POLL = 0x1D,
	RFU_COMMAND_BROADCAST_READ_END = 0x1E,
	RFU_COMMAND_CONNECT = 0x1F,
	RFU_COMMAND_IS_CONNECTION_COMPLETE = 0x20,
	RFU_COMMAND_FINISH_CONNECTION = 0x21,
	RFU_COMMAND_SEND_DATA = 0x24,
	RFU_COMMAND_SEND_DATA_AND_WAIT = 0x25,
	RFU_COMMAND_RECEIVE_DATA = 0x26,
	RFU_COMMAND_WAIT = 0x27,
	RFU_COMMAND_DISCONNECT_CLIENT = 0x30,
	RFU_COMMAND_RETRANSMIT_AND_WAIT = 0x37,
	RFU_COMMAND_BYE = 0x3D,

	RFU_SYSTEM_VERSION = 0x00830117,
	RFU_WAIT_STILL_CONNECTING = 0x01000000,
};

static const char* _modeName(enum GBASIOMode mode) {
	switch (mode) {
	case GBA_SIO_NORMAL_8:
		return "NORMAL8";
	case GBA_SIO_NORMAL_32:
		return "NORMAL32";
	case GBA_SIO_MULTI:
		return "MULTI";
	case GBA_SIO_UART:
		return "UART";
	case GBA_SIO_GPIO:
		return "GPIO";
	case GBA_SIO_JOYBUS:
		return "JOYBUS";
	default:
		return "UNKNOWN";
	}
}

static const char* _adapterStateName(uint8_t adapterState) {
	switch (adapterState) {
	case GBASIO_RFU_ADAPTER_AUTHENTICATED:
		return "AUTHENTICATED";
	case GBASIO_RFU_ADAPTER_SERVING:
		return "SERVING";
	case GBASIO_RFU_ADAPTER_SEARCHING:
		return "SEARCHING";
	case GBASIO_RFU_ADAPTER_CONNECTING:
		return "CONNECTING";
	case GBASIO_RFU_ADAPTER_CONNECTED:
		return "CONNECTED";
	default:
		return "UNKNOWN";
	}
}

static const char* _comStateName(uint8_t comState) {
	switch (comState) {
	case GBASIO_RFU_COM_RESET:
		return "RESET";
	case GBASIO_RFU_COM_HANDSHAKE:
		return "HANDSHAKE";
	case GBASIO_RFU_COM_WAIT_CMD:
		return "WAIT_CMD";
	case GBASIO_RFU_COM_WAIT_PARAMS:
		return "WAIT_PARAMS";
	case GBASIO_RFU_COM_RESP_ACK:
		return "RESP_ACK";
	case GBASIO_RFU_COM_RESP_DATA:
		return "RESP_DATA";
	default:
		return "UNKNOWN";
	}
}

static void _logState(const struct GBASIORFUDriver* rfu, const char* tag) {
	mLOG(GBA_SIO, DEBUG,
		"[RFU] %s connected=%i active=%i mode=%s(%i) com=%s adapter=%s deviceId=%04X slot=%u writes=%u starts=%u finishes=%u",
		tag,
		rfu->connected ? 1 : 0,
		rfu->active ? 1 : 0,
		_modeName(rfu->mode),
		rfu->mode,
		_comStateName(rfu->comState),
		_adapterStateName(rfu->adapterState),
		rfu->deviceId,
		(unsigned) rfu->playerSlot,
		(unsigned) rfu->writeCount,
		(unsigned) rfu->startCount,
		(unsigned) rfu->finishCount);
}

static bool GBASIORFUDriverInit(struct GBASIODriver* driver);
static void GBASIORFUDriverReset(struct GBASIODriver* driver);
static void GBASIORFUDriverSetMode(struct GBASIODriver* driver, enum GBASIOMode mode);
static bool GBASIORFUDriverHandlesMode(struct GBASIODriver* driver, enum GBASIOMode mode);
static int GBASIORFUDriverConnectedDevices(struct GBASIODriver* driver);
static int GBASIORFUDriverDeviceId(struct GBASIODriver* driver);
static uint16_t GBASIORFUDriverWriteSIOCNT(struct GBASIODriver* driver, uint16_t value);
static bool GBASIORFUDriverStart(struct GBASIODriver* driver);
static uint32_t GBASIORFUDriverFinishNormal32(struct GBASIODriver* driver);

static void _updateActive(struct GBASIORFUDriver* rfu);
static void _resetProtocolState(struct GBASIORFUDriver* rfu);
static void _setSuccessResponse(struct GBASIORFUDriver* rfu, uint8_t words);
static void _setErrorResponse(struct GBASIORFUDriver* rfu, uint8_t code);
static uint32_t _buildSystemStatus(const struct GBASIORFUDriver* rfu);
static void _processCommand(struct GBASIORFUDriver* rfu);
static uint32_t _transferWord(struct GBASIORFUDriver* rfu, uint32_t txValue);

void GBASIORFUDriverCreate(struct GBASIORFUDriver* rfu) {
	memset(rfu, 0, sizeof(*rfu));
	rfu->d.init = GBASIORFUDriverInit;
	rfu->d.reset = GBASIORFUDriverReset;
	rfu->d.setMode = GBASIORFUDriverSetMode;
	rfu->d.handlesMode = GBASIORFUDriverHandlesMode;
	rfu->d.connectedDevices = GBASIORFUDriverConnectedDevices;
	rfu->d.deviceId = GBASIORFUDriverDeviceId;
	rfu->d.writeSIOCNT = GBASIORFUDriverWriteSIOCNT;
	rfu->d.start = GBASIORFUDriverStart;
	rfu->d.finishNormal32 = GBASIORFUDriverFinishNormal32;
	rfu->mode = (enum GBASIOMode) -1;
	rfu->connected = false;
	rfu->active = false;
	rfu->adapterState = GBASIO_RFU_ADAPTER_AUTHENTICATED;
	rfu->serverClosed = false;
	rfu->deviceId = 0x0001;
	rfu->playerSlot = 0;
	_resetProtocolState(rfu);
	_logState(rfu, "create");
}

bool GBASIORFUDriverConnect(struct GBASIORFUDriver* rfu) {
	mLOG(GBA_SIO, INFO, "[RFU] connect requested");
	rfu->connected = true;
	if (!rfu->deviceId) {
		rfu->deviceId = 0x0001;
	}
	_updateActive(rfu);
	_logState(rfu, "connect");
	return true;
}

void GBASIORFUDriverDisconnect(struct GBASIORFUDriver* rfu) {
	mLOG(GBA_SIO, INFO, "[RFU] disconnect requested");
	rfu->connected = false;
	_updateActive(rfu);
	_logState(rfu, "disconnect");
}

bool GBASIORFUDriverIsConnected(const struct GBASIORFUDriver* rfu) {
	mLOG(GBA_SIO, DEBUG, "[RFU] isConnected -> %i", rfu->connected ? 1 : 0);
	return rfu->connected;
}

static bool GBASIORFUDriverInit(struct GBASIODriver* driver) {
	mLOG(GBA_SIO, INFO, "[RFU] init");
	GBASIORFUDriverReset(driver);
	return true;
}

static void GBASIORFUDriverReset(struct GBASIODriver* driver) {
	struct GBASIORFUDriver* rfu = (struct GBASIORFUDriver*) driver;
	mLOG(GBA_SIO, INFO, "[RFU] reset");
	rfu->mode = (enum GBASIOMode) -1;
	rfu->active = false;
	rfu->writeCount = 0;
	rfu->startCount = 0;
	rfu->finishCount = 0;
	rfu->adapterState = GBASIO_RFU_ADAPTER_AUTHENTICATED;
	rfu->serverClosed = false;
	rfu->playerSlot = 0;
	rfu->pendingServerId = 0;
	_resetProtocolState(rfu);
	_logState(rfu, "reset");
}

static void GBASIORFUDriverSetMode(struct GBASIODriver* driver, enum GBASIOMode mode) {
	struct GBASIORFUDriver* rfu = (struct GBASIORFUDriver*) driver;
	mLOG(GBA_SIO, INFO, "[RFU] setMode %s(%i) -> %s(%i)",
		_modeName(rfu->mode), rfu->mode, _modeName(mode), mode);
	rfu->mode = mode;
	_updateActive(rfu);
	_logState(rfu, "setMode");
}

static bool GBASIORFUDriverHandlesMode(struct GBASIODriver* driver, enum GBASIOMode mode) {
	UNUSED(driver);
	mLOG(GBA_SIO, DEBUG, "[RFU] handlesMode? mode=%s(%i) -> %i",
		_modeName(mode), mode, mode == GBA_SIO_NORMAL_32 ? 1 : 0);
	return mode == GBA_SIO_NORMAL_32;
}

static int GBASIORFUDriverConnectedDevices(struct GBASIODriver* driver) {
	struct GBASIORFUDriver* rfu = (struct GBASIORFUDriver*) driver;
	mLOG(GBA_SIO, DEBUG, "[RFU] connectedDevices -> %i", rfu->connected ? 1 : 0);
	return rfu->connected ? 1 : 0;
}

static int GBASIORFUDriverDeviceId(struct GBASIODriver* driver) {
	UNUSED(driver);
	mLOG(GBA_SIO, DEBUG, "[RFU] deviceId -> 0");
	return 0;
}

static uint16_t GBASIORFUDriverWriteSIOCNT(struct GBASIODriver* driver, uint16_t value) {
	struct GBASIORFUDriver* rfu = (struct GBASIORFUDriver*) driver;
	++rfu->writeCount;
	mLOG(GBA_SIO, DEBUG, "[RFU] writeSIOCNT value=%04X active=%i writeCount=%u",
		value, rfu->active ? 1 : 0, (unsigned) rfu->writeCount);
	if (rfu->active) {
		/* Emulate the adapter-side ACK line behavior expected by LinkRawWireless:
		 * when the GBA drives SO low, SI is observed high; when SO is high, SI drops low. */
		bool soHigh = (value & 0x0008) != 0;
		uint16_t updated = soHigh ? (value & ~0x0004) : (value | 0x0004);
		mLOG(GBA_SIO, DEBUG, "[RFU] writeSIOCNT driving SI from SO: %04X -> %04X", value, updated);
		_logState(rfu, "writeSIOCNT(active)");
		return updated;
	}
	_logState(rfu, "writeSIOCNT(inactive)");
	return value;
}

static bool GBASIORFUDriverStart(struct GBASIODriver* driver) {
	struct GBASIORFUDriver* rfu = (struct GBASIORFUDriver*) driver;
	++rfu->startCount;
	mLOG(GBA_SIO, DEBUG, "[RFU] start count=%u active=%i mode=%s(%i)",
		(unsigned) rfu->startCount, rfu->active ? 1 : 0, _modeName(rfu->mode), rfu->mode);
	_logState(rfu, "start");
	return true;
}

static uint32_t GBASIORFUDriverFinishNormal32(struct GBASIODriver* driver) {
	struct GBASIORFUDriver* rfu = (struct GBASIORFUDriver*) driver;
	struct GBASIO* sio = driver->p;
	++rfu->finishCount;
	if (!rfu->active) {
		mLOG(GBA_SIO, DEBUG, "[RFU] finishNormal32 inactive -> 0 (count=%u)", (unsigned) rfu->finishCount);
		_logState(rfu, "finishNormal32(inactive)");
		return 0;
	}
	uint16_t lo = sio->p->memory.io[GBA_REG(SIODATA32_LO)];
	uint16_t hi = sio->p->memory.io[GBA_REG(SIODATA32_HI)];
	uint32_t txValue = lo | ((uint32_t) hi << 16);
	uint32_t rxValue = _transferWord(rfu, txValue);
	mLOG(GBA_SIO, DEBUG, "[RFU] finishNormal32 tx=%08X -> rx=%08X (count=%u)",
		(unsigned) txValue, (unsigned) rxValue, (unsigned) rfu->finishCount);
	_logState(rfu, "finishNormal32(active)");
	return rxValue;
}

static void _updateActive(struct GBASIORFUDriver* rfu) {
	bool oldActive = rfu->active;
	bool newActive = rfu->connected && rfu->mode == GBA_SIO_NORMAL_32;
	if (oldActive != newActive) {
		mLOG(GBA_SIO, INFO, "[RFU] active state change %i -> %i (connected=%i mode=%s(%i))",
			oldActive ? 1 : 0,
			newActive ? 1 : 0,
			rfu->connected ? 1 : 0,
			_modeName(rfu->mode),
			rfu->mode);
	} else {
		mLOG(GBA_SIO, DEBUG, "[RFU] active unchanged at %i (connected=%i mode=%s(%i))",
			newActive ? 1 : 0,
			rfu->connected ? 1 : 0,
			_modeName(rfu->mode),
			rfu->mode);
	}
	rfu->active = newActive;
	_logState(rfu, "_updateActive");
}

static void _resetProtocolState(struct GBASIORFUDriver* rfu) {
	rfu->comState = GBASIO_RFU_COM_RESET;
	rfu->prevTx = 0;
	rfu->cmdId = 0;
	rfu->cmdLen = 0;
	rfu->cmdIndex = 0;
	rfu->rspLen = 0;
	rfu->rspIndex = 0;
	rfu->ackCode = 0;
	memset(rfu->cmdBuf, 0, sizeof(rfu->cmdBuf));
	memset(rfu->rspBuf, 0, sizeof(rfu->rspBuf));
}

static void _setSuccessResponse(struct GBASIORFUDriver* rfu, uint8_t words) {
	rfu->ackCode = (uint8_t) (rfu->cmdId + RFU_ACK_OFFSET);
	rfu->rspLen = words;
	rfu->rspIndex = 0;
}

static void _setErrorResponse(struct GBASIORFUDriver* rfu, uint8_t code) {
	rfu->ackCode = RFU_ERROR_ACK;
	rfu->rspLen = 1;
	rfu->rspIndex = 0;
	rfu->rspBuf[0] = code;
}

static uint32_t _buildSystemStatus(const struct GBASIORFUDriver* rfu) {
	uint8_t statusCode = 0;
	uint8_t slotMask = 0;

	switch (rfu->adapterState) {
	case GBASIO_RFU_ADAPTER_SERVING:
		statusCode = rfu->serverClosed ? 1 : 2;
		slotMask = 1 << (rfu->playerSlot & 0x3);
		break;
	case GBASIO_RFU_ADAPTER_SEARCHING:
		statusCode = 3;
		break;
	case GBASIO_RFU_ADAPTER_CONNECTING:
		statusCode = 4;
		slotMask = 1 << (rfu->playerSlot & 0x3);
		break;
	case GBASIO_RFU_ADAPTER_CONNECTED:
		statusCode = 5;
		slotMask = 1 << (rfu->playerSlot & 0x3);
		break;
	case GBASIO_RFU_ADAPTER_AUTHENTICATED:
	default:
		statusCode = 0;
		slotMask = 0;
		break;
	}

	return ((uint32_t) statusCode << 24) | ((uint32_t) slotMask << 16) | rfu->deviceId;
}

static void _processCommand(struct GBASIORFUDriver* rfu) {
	_setSuccessResponse(rfu, 0);

	switch (rfu->cmdId) {
	case RFU_COMMAND_HELLO:
	case RFU_COMMAND_SETUP:
	case RFU_COMMAND_CONFIG_STATUS:
	case RFU_COMMAND_SEND_DATA:
	case RFU_COMMAND_DISCONNECT_CLIENT:
		return;
	case RFU_COMMAND_SIGNAL_LEVEL:
		rfu->rspBuf[0] = 0;
		_setSuccessResponse(rfu, 1);
		return;
	case RFU_COMMAND_VERSION_STATUS:
		rfu->rspBuf[0] = RFU_SYSTEM_VERSION;
		_setSuccessResponse(rfu, 1);
		return;
	case RFU_COMMAND_SYSTEM_STATUS:
		rfu->rspBuf[0] = _buildSystemStatus(rfu);
		_setSuccessResponse(rfu, 1);
		return;
	case RFU_COMMAND_SLOT_STATUS:
		rfu->rspBuf[0] = 0;
		_setSuccessResponse(rfu, 1);
		return;
	case RFU_COMMAND_BROADCAST:
		if (rfu->cmdLen < 6) {
			_setErrorResponse(rfu, 1);
			return;
		}
		memcpy(rfu->broadcastData, rfu->cmdBuf, sizeof(rfu->broadcastData));
		return;
	case RFU_COMMAND_START_HOST:
		rfu->adapterState = GBASIO_RFU_ADAPTER_SERVING;
		rfu->serverClosed = false;
		rfu->playerSlot = 0;
		return;
	case RFU_COMMAND_POLL_CONNECTIONS:
		_setSuccessResponse(rfu, 0);
		return;
	case RFU_COMMAND_END_HOST:
		if (rfu->adapterState == GBASIO_RFU_ADAPTER_SERVING) {
			rfu->serverClosed = true;
		}
		return;
	case RFU_COMMAND_BROADCAST_READ_START:
		rfu->adapterState = GBASIO_RFU_ADAPTER_SEARCHING;
		return;
	case RFU_COMMAND_BROADCAST_READ_POLL:
		_setSuccessResponse(rfu, 0);
		return;
	case RFU_COMMAND_BROADCAST_READ_END:
		rfu->adapterState = GBASIO_RFU_ADAPTER_AUTHENTICATED;
		return;
	case RFU_COMMAND_CONNECT:
		if (rfu->cmdLen < 1) {
			_setErrorResponse(rfu, 1);
			return;
		}
		rfu->pendingServerId = (uint16_t) (rfu->cmdBuf[0] & 0xFFFF);
		rfu->adapterState = GBASIO_RFU_ADAPTER_CONNECTING;
		rfu->playerSlot = 0;
		rfu->rspBuf[0] = ((uint32_t) rfu->playerSlot << 16) | rfu->deviceId;
		_setSuccessResponse(rfu, 1);
		return;
	case RFU_COMMAND_IS_CONNECTION_COMPLETE:
		if (rfu->adapterState != GBASIO_RFU_ADAPTER_CONNECTING &&
		    rfu->adapterState != GBASIO_RFU_ADAPTER_CONNECTED) {
			rfu->rspBuf[0] = RFU_WAIT_STILL_CONNECTING;
		} else {
			rfu->rspBuf[0] = ((uint32_t) rfu->playerSlot << 16) | rfu->deviceId;
		}
		_setSuccessResponse(rfu, 1);
		return;
	case RFU_COMMAND_FINISH_CONNECTION:
		if (rfu->adapterState == GBASIO_RFU_ADAPTER_CONNECTING) {
			rfu->adapterState = GBASIO_RFU_ADAPTER_CONNECTED;
		}
		rfu->rspBuf[0] = ((uint32_t) rfu->playerSlot << 16) | rfu->deviceId;
		_setSuccessResponse(rfu, 1);
		return;
	case RFU_COMMAND_RECEIVE_DATA:
		rfu->rspBuf[0] = 0;
		_setSuccessResponse(rfu, 1);
		return;
	case RFU_COMMAND_BYE:
		rfu->adapterState = GBASIO_RFU_ADAPTER_AUTHENTICATED;
		rfu->serverClosed = false;
		return;
	case RFU_COMMAND_SEND_DATA_AND_WAIT:
	case RFU_COMMAND_WAIT:
	case RFU_COMMAND_RETRANSMIT_AND_WAIT:
		_setErrorResponse(rfu, 1);
		return;
	default:
		_setErrorResponse(rfu, 2);
		return;
	}
}

static uint32_t _transferWord(struct GBASIORFUDriver* rfu, uint32_t txValue) {
	uint32_t rxValue = RFU_DATA_REQUEST;

	switch (rfu->comState) {
	case GBASIO_RFU_COM_RESET:
		rxValue = 0;
		if ((txValue & 0xFFFF) == 0x494E) {
			rfu->comState = GBASIO_RFU_COM_HANDSHAKE;
		}
		break;
	case GBASIO_RFU_COM_HANDSHAKE:
		rxValue = ((txValue & 0xFFFF) << 16) | ((~rfu->prevTx) & 0xFFFF);
		if (txValue == 0xB0BB8001) {
			rfu->comState = GBASIO_RFU_COM_WAIT_CMD;
		}
		break;
	case GBASIO_RFU_COM_WAIT_CMD:
		if ((txValue >> 16) == RFU_COMMAND_HEADER) {
			rfu->cmdLen = (uint8_t) ((txValue >> 8) & 0xFF);
			rfu->cmdId = (uint8_t) (txValue & 0xFF);
			rfu->cmdIndex = 0;
			if (rfu->cmdLen == 0) {
				_processCommand(rfu);
				rfu->comState = GBASIO_RFU_COM_RESP_ACK;
			} else {
				rfu->comState = GBASIO_RFU_COM_WAIT_PARAMS;
			}
		}
		break;
	case GBASIO_RFU_COM_WAIT_PARAMS:
		if (rfu->cmdIndex < sizeof(rfu->cmdBuf) / sizeof(rfu->cmdBuf[0])) {
			rfu->cmdBuf[rfu->cmdIndex] = txValue;
		}
		++rfu->cmdIndex;
		if (rfu->cmdIndex == rfu->cmdLen) {
			_processCommand(rfu);
			rfu->comState = GBASIO_RFU_COM_RESP_ACK;
		}
		break;
	case GBASIO_RFU_COM_RESP_ACK:
		rxValue = ((uint32_t) RFU_COMMAND_HEADER << 16) | ((uint32_t) rfu->rspLen << 8) | rfu->ackCode;
		if (rfu->rspLen) {
			rfu->comState = GBASIO_RFU_COM_RESP_DATA;
		} else {
			rfu->comState = GBASIO_RFU_COM_WAIT_CMD;
		}
		break;
	case GBASIO_RFU_COM_RESP_DATA:
		if (rfu->rspIndex < rfu->rspLen) {
			rxValue = rfu->rspBuf[rfu->rspIndex];
		}
		++rfu->rspIndex;
		if (rfu->rspIndex >= rfu->rspLen) {
			rfu->comState = GBASIO_RFU_COM_WAIT_CMD;
		}
		break;
	default:
		rfu->comState = GBASIO_RFU_COM_RESET;
		rxValue = 0;
		break;
	}

	rfu->prevTx = txValue;
	return rxValue;
}
