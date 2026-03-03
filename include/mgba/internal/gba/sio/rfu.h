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
};

void GBASIORFUDriverCreate(struct GBASIORFUDriver*);
bool GBASIORFUDriverConnect(struct GBASIORFUDriver*);
void GBASIORFUDriverDisconnect(struct GBASIORFUDriver*);
bool GBASIORFUDriverIsConnected(const struct GBASIORFUDriver*);

CXX_GUARD_END

#endif
