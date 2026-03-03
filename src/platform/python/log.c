/* Copyright (c) 2013-2016 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "log.h"

static struct mStandardLogger _stdLogger;
static bool _stdLoggerReady = false;

static void _ensureStdLogger(void) {
	if (_stdLoggerReady) {
		return;
	}
	mStandardLoggerInit(&_stdLogger);
	_stdLogger.logToStdout = true;
	_stdLogger.logFile = NULL;
	_stdLoggerReady = true;
}

static void _pyLogShim(struct mLogger* logger, int category, enum mLogLevel level, const char* format, va_list args) {
	struct mLoggerPy* pylogger = (struct mLoggerPy*) logger;
	char message[256] = {0};
	vsnprintf(message, sizeof(message) - 1, format, args);
	_pyLog(pylogger, category, level, message);
}

struct mLogger* mLoggerPythonCreate(void* pyobj) {
	struct mLoggerPy* logger = malloc(sizeof(*logger));
	logger->d.log = _pyLogShim;
	logger->d.filter = NULL;
	logger->pyobj = pyobj;
	return &logger->d;
}

void mLoggerPythonDestroy(struct mLogger* logger) {
	mLoggerPythonFilterClear(logger);
	free(logger);
}

void mLoggerPythonFilterCreate(struct mLogger* logger, int defaultLevels) {
	if (!logger->filter) {
		logger->filter = malloc(sizeof(*logger->filter));
		mLogFilterInit(logger->filter);
	}
	logger->filter->defaultLevels = defaultLevels;
}

void mLoggerPythonFilterSet(struct mLogger* logger, const char* category, int levels) {
	if (!logger->filter) {
		mLoggerPythonFilterCreate(logger, mLOG_ALL);
	}
	mLogFilterSet(logger->filter, category, levels);
}

void mLoggerPythonFilterClear(struct mLogger* logger) {
	if (!logger || !logger->filter) {
		return;
	}
	mLogFilterDeinit(logger->filter);
	free(logger->filter);
	logger->filter = NULL;
}

void mLoggerPythonUseStdLogger(int defaultLevels) {
	_ensureStdLogger();
	_stdLogger.d.filter->defaultLevels = defaultLevels;
	mLogSetDefaultLogger(&_stdLogger.d);
}

void mLoggerPythonStdFilterSet(const char* category, int levels) {
	_ensureStdLogger();
	mLogFilterSet(_stdLogger.d.filter, category, levels);
}

void mLoggerPythonClearStdLogger(void) {
	mLogSetDefaultLogger(NULL);
	if (!_stdLoggerReady) {
		return;
	}
	mStandardLoggerDeinit(&_stdLogger);
	_stdLoggerReady = false;
}
