/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/sio/rfu.h>

#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/io.h>

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
	rfu->mode = -1;
	rfu->connected = false;
	rfu->active = false;
}

bool GBASIORFUDriverConnect(struct GBASIORFUDriver* rfu) {
	rfu->connected = true;
	_updateActive(rfu);
	return true;
}

void GBASIORFUDriverDisconnect(struct GBASIORFUDriver* rfu) {
	rfu->connected = false;
	_updateActive(rfu);
}

bool GBASIORFUDriverIsConnected(const struct GBASIORFUDriver* rfu) {
	return rfu->connected;
}

static bool GBASIORFUDriverInit(struct GBASIODriver* driver) {
	GBASIORFUDriverReset(driver);
	return true;
}

static void GBASIORFUDriverReset(struct GBASIODriver* driver) {
	struct GBASIORFUDriver* rfu = (struct GBASIORFUDriver*) driver;
	rfu->mode = -1;
	rfu->active = false;
	rfu->writeCount = 0;
	rfu->startCount = 0;
	rfu->finishCount = 0;
}

static void GBASIORFUDriverSetMode(struct GBASIODriver* driver, enum GBASIOMode mode) {
	struct GBASIORFUDriver* rfu = (struct GBASIORFUDriver*) driver;
	rfu->mode = mode;
	_updateActive(rfu);
}

static bool GBASIORFUDriverHandlesMode(struct GBASIODriver* driver, enum GBASIOMode mode) {
	UNUSED(driver);
	return mode == GBA_SIO_NORMAL_32;
}

static int GBASIORFUDriverConnectedDevices(struct GBASIODriver* driver) {
	struct GBASIORFUDriver* rfu = (struct GBASIORFUDriver*) driver;
	return rfu->connected ? 1 : 0;
}

static int GBASIORFUDriverDeviceId(struct GBASIODriver* driver) {
	UNUSED(driver);
	return 0;
}

static uint16_t GBASIORFUDriverWriteSIOCNT(struct GBASIODriver* driver, uint16_t value) {
	struct GBASIORFUDriver* rfu = (struct GBASIORFUDriver*) driver;
	++rfu->writeCount;
	if (rfu->active) {
		return GBASIONormalFillSi(value);
	}
	return value;
}

static bool GBASIORFUDriverStart(struct GBASIODriver* driver) {
	struct GBASIORFUDriver* rfu = (struct GBASIORFUDriver*) driver;
	++rfu->startCount;
	return true;
}

static uint32_t GBASIORFUDriverFinishNormal32(struct GBASIODriver* driver) {
	struct GBASIORFUDriver* rfu = (struct GBASIORFUDriver*) driver;
	struct GBASIO* sio = driver->p;
	++rfu->finishCount;
	if (!rfu->active) {
		return 0;
	}
	return sio->p->memory.io[GBA_REG(SIODATA32_LO)] | (sio->p->memory.io[GBA_REG(SIODATA32_HI)] << 16);
}

static void _updateActive(struct GBASIORFUDriver* rfu) {
	rfu->active = rfu->connected && rfu->mode == GBA_SIO_NORMAL_32;
}
