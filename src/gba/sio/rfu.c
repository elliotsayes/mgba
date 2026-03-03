/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/sio/rfu.h>

#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/io.h>

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

static void _logState(const struct GBASIORFUDriver* rfu, const char* tag) {
	mLOG(GBA_SIO, DEBUG, "[RFU] %s connected=%i active=%i mode=%s(%i) writes=%u starts=%u finishes=%u",
		tag,
		rfu->connected ? 1 : 0,
		rfu->active ? 1 : 0,
		_modeName(rfu->mode),
		rfu->mode,
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
	_logState(rfu, "create");
}

bool GBASIORFUDriverConnect(struct GBASIORFUDriver* rfu) {
	mLOG(GBA_SIO, INFO, "[RFU] connect requested");
	rfu->connected = true;
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
	rfu->mode = -1;
	rfu->active = false;
	rfu->writeCount = 0;
	rfu->startCount = 0;
	rfu->finishCount = 0;
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
		uint16_t updated = GBASIONormalFillSi(value);
		mLOG(GBA_SIO, DEBUG, "[RFU] writeSIOCNT forcing SI high: %04X -> %04X", value, updated);
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
	uint32_t result = lo | (hi << 16);
	mLOG(GBA_SIO, DEBUG, "[RFU] finishNormal32 lo=%04X hi=%04X -> %08X (count=%u)",
		lo, hi, (unsigned) result, (unsigned) rfu->finishCount);
	_logState(rfu, "finishNormal32(active)");
	return result;
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
