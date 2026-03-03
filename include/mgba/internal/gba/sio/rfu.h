/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef GBA_SIO_RFU_H
#define GBA_SIO_RFU_H

#include <mgba-util/common.h>

CXX_GUARD_START

#include <mgba/internal/gba/sio.h>

struct GBASIORFUDriver {
	struct GBASIODriver d;

	enum GBASIOMode mode;
	bool connected;
	bool active;
	uint32_t writeCount;
	uint32_t startCount;
	uint32_t finishCount;

	/* RFU adapter state exposed by command 0x13 */
	enum {
		GBASIO_RFU_ADAPTER_AUTHENTICATED = 0,
		GBASIO_RFU_ADAPTER_SERVING = 1,
		GBASIO_RFU_ADAPTER_SEARCHING = 2,
		GBASIO_RFU_ADAPTER_CONNECTING = 3,
		GBASIO_RFU_ADAPTER_CONNECTED = 4,
	} adapterState;
	bool serverClosed;
	uint16_t deviceId;
	uint8_t playerSlot;

	/* SPI protocol framing state */
	enum {
		GBASIO_RFU_COM_RESET = 0,
		GBASIO_RFU_COM_HANDSHAKE = 1,
		GBASIO_RFU_COM_WAIT_CMD = 2,
		GBASIO_RFU_COM_WAIT_PARAMS = 3,
		GBASIO_RFU_COM_RESP_ACK = 4,
		GBASIO_RFU_COM_RESP_DATA = 5,
	} comState;
	uint32_t prevTx;
	uint8_t cmdId;
	uint8_t cmdLen;
	uint8_t cmdIndex;
	uint8_t rspLen;
	uint8_t rspIndex;
	uint8_t ackCode;
	uint32_t cmdBuf[32];
	uint32_t rspBuf[32];

	/* Scratch protocol/session values used by stub command handlers */
	uint32_t broadcastData[6];
	uint16_t pendingServerId;
};

void GBASIORFUDriverCreate(struct GBASIORFUDriver*);
bool GBASIORFUDriverConnect(struct GBASIORFUDriver*);
void GBASIORFUDriverDisconnect(struct GBASIORFUDriver*);
bool GBASIORFUDriverIsConnected(const struct GBASIORFUDriver*);

CXX_GUARD_END

#endif
