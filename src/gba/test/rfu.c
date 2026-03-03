/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "util/test/suite.h"

#include <stdlib.h>

#include <mgba/core/core.h>
#include <mgba/gba/core.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/io.h>
#include <mgba/internal/gba/sio/rfu.h>
#include <mgba-util/audio-buffer.h>

static const uint32_t sLoginPackets[] = {
	0xFFFF494E,
	0xFFFF494E,
	0xB6B1494E,
	0xB6B1544E,
	0xABB1544E,
	0xABB14E45,
	0xB1BA4E45,
	0xB1BA4F44,
	0xB0BB4F44,
	0xB0BB8001,
};

static uint32_t _rfuTransfer32(struct GBA* gba, struct GBASIORFUDriver* rfu, uint32_t txValue) {
	gba->memory.io[GBA_REG(SIODATA32_LO)] = txValue & 0xFFFF;
	gba->memory.io[GBA_REG(SIODATA32_HI)] = txValue >> 16;
	GBASIOWriteSIOCNT(&gba->sio, 0x1083);
	uint32_t rxValue = rfu->d.finishNormal32(&rfu->d);
	GBASIONormal32FinishTransfer(&gba->sio, rxValue, 0);
	return rxValue;
}

M_TEST_DEFINE(driverScaffoldTransfer) {
	struct mCore* core = GBACoreCreate();
	assert_non_null(core);
	assert_true(core->init(core));
	mCoreInitConfig(core, NULL);
	core->reset(core);

	struct GBA* gba = core->board;
	struct GBASIORFUDriver rfu;
	GBASIORFUDriverCreate(&rfu);
	GBASIOSetDriver(&gba->sio, &rfu.d);
	assert_false(GBASIORFUDriverIsConnected(&rfu));
	assert_true(rfu.d.handlesMode(&rfu.d, GBA_SIO_NORMAL_32));
	assert_int_equal(rfu.d.connectedDevices(&rfu.d), 0);

	GBASIOWriteRCNT(&gba->sio, 0);
	GBASIOWriteSIOCNT(&gba->sio, 0x1000);
	assert_int_equal(rfu.mode, GBA_SIO_NORMAL_32);
	assert_false(rfu.active);
	assert_int_equal(rfu.writeCount, 1);
	assert_true(GBASIORFUDriverConnect(&rfu));
	assert_true(GBASIORFUDriverIsConnected(&rfu));
	assert_true(rfu.active);
	assert_true(rfu.d.handlesMode(&rfu.d, GBA_SIO_NORMAL_32));
	assert_int_equal(rfu.d.connectedDevices(&rfu.d), 1);

	for (size_t i = 0; i < sizeof(sLoginPackets) / sizeof(sLoginPackets[0]); ++i) {
		uint32_t data = _rfuTransfer32(gba, &rfu, sLoginPackets[i]);
		if (i == 0) {
			assert_int_equal(data, 0);
		}
	}
	assert_int_equal(rfu.comState, GBASIO_RFU_COM_WAIT_CMD);

	uint32_t data = _rfuTransfer32(gba, &rfu, 0x99660010);
	assert_int_equal(data, 0x80000000);
	data = _rfuTransfer32(gba, &rfu, 0x80000000);
	assert_int_equal(data, 0x99660090);

	data = _rfuTransfer32(gba, &rfu, 0x99660013);
	assert_int_equal(data, 0x80000000);
	data = _rfuTransfer32(gba, &rfu, 0x80000000);
	assert_int_equal(data, 0x99660193);
	data = _rfuTransfer32(gba, &rfu, 0x80000000);
	assert_int_equal(data >> 24, 0);
	assert_int_equal(data & 0xFFFF, 1);
	assert_true(rfu.startCount >= 10);
	assert_true(rfu.finishCount >= 10);

	GBASIORFUDriverDisconnect(&rfu);
	assert_false(GBASIORFUDriverIsConnected(&rfu));
	assert_false(rfu.active);
	assert_true(rfu.d.handlesMode(&rfu.d, GBA_SIO_NORMAL_32));
	assert_int_equal(rfu.d.connectedDevices(&rfu.d), 0);

	data = _rfuTransfer32(gba, &rfu, 0xFFFFFFFF);
	assert_int_equal(data, 0);

	mCoreConfigDeinit(&core->config);
	core->deinit(core);
}

#ifdef ENABLE_VFS
M_TEST_DEFINE(rawWirelessDemoSmoke) {
	const char* romPath = getenv("MGBA_TEST_RAW_WIRELESS_ROM");
	assert_non_null(romPath);
	assert_true(romPath[0] != '\0');

	struct mCore* core = mCoreFind(romPath);
	assert_non_null(core);
	assert_int_equal(core->platform(core), mPLATFORM_GBA);
	assert_true(core->init(core));
	mCoreInitConfig(core, "test");
	mCoreConfigSetDefaultValue(&core->config, "idleOptimization", "remove");
	mCoreLoadConfig(core);
	assert_true(mCoreLoadFile(core, romPath));

	struct GBA* gba = core->board;
	gba->hardCrash = false;

	struct GBASIORFUDriver rfu;
	GBASIORFUDriverCreate(&rfu);
	assert_true(GBASIORFUDriverConnect(&rfu));
	GBASIOSetDriver(&gba->sio, &rfu.d);
	core->reset(core);

	const int frames = 120;
	for (int i = 0; i < frames; ++i) {
		core->runFrame(core);
		mAudioBufferClear(core->getAudioBuffer(core));
	}
	assert_true(core->frameCounter(core) >= frames);

	GBASIOWriteRCNT(&gba->sio, 0);
	GBASIOWriteSIOCNT(&gba->sio, 0x1000);
	uint32_t data = 0;
	for (size_t i = 0; i < sizeof(sLoginPackets) / sizeof(sLoginPackets[0]); ++i) {
		data = _rfuTransfer32(gba, &rfu, sLoginPackets[i]);
	}
	assert_int_equal(rfu.comState, GBASIO_RFU_COM_WAIT_CMD);
	data = _rfuTransfer32(gba, &rfu, 0x99660010);
	assert_int_equal(data, 0x80000000);
	data = _rfuTransfer32(gba, &rfu, 0x80000000);
	assert_int_equal(data, 0x99660090);
	assert_true(rfu.startCount >= 1);
	assert_true(rfu.finishCount >= 1);

	GBASIORFUDriverDisconnect(&rfu);
	data = rfu.d.finishNormal32(&rfu.d);
	assert_int_equal(data, 0);

	core->unloadROM(core);
	mCoreConfigDeinit(&core->config);
	core->deinit(core);
}
#endif

M_TEST_SUITE_DEFINE(GBARFU,
	cmocka_unit_test(driverScaffoldTransfer)
#ifdef ENABLE_VFS
	,
	cmocka_unit_test(rawWirelessDemoSmoke)
#endif
)
