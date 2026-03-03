/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "RFUView.h"

#include <QMessageBox>

#include "CoreController.h"
#include "Window.h"

using namespace QGBA;

RFUView::RFUView(Window* window, QWidget* parent)
	: QDialog(parent, Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint)
	, m_window(window)
{
	m_ui.setupUi(this);

	connect(window, &QObject::destroyed, this, &QWidget::close);
	connect(m_ui.connect, &QAbstractButton::clicked, this, &RFUView::attach);
	connect(m_ui.disconnect, &QAbstractButton::clicked, this, &RFUView::detach);

	updateAttached();
}

void RFUView::attach() {
	bool reset = m_ui.doReset->isChecked();
	if (!m_window->controller()) {
		m_window->bootBIOS();
		reset = false;
		if (!m_window->controller() || m_window->controller()->platform() != mPLATFORM_GBA) {
			return;
		}
	}

	m_controller = m_window->controller();
	CoreController::Interrupter interrupter(m_controller);
	m_controller->attachRFU();
	connect(m_controller.get(), &CoreController::stopping, this, &RFUView::detach);
	interrupter.resume();

	if (!m_controller->isRFUConnected()) {
		QMessageBox* fail = new QMessageBox(QMessageBox::Warning, tr("Couldn't Connect"),
		                                   tr("Could not attach the RFU adapter."),
		                                   QMessageBox::Ok);
		fail->setAttribute(Qt::WA_DeleteOnClose);
		fail->show();
	} else if (reset) {
		m_controller->reset();
	}

	updateAttached();
}

void RFUView::detach() {
	if (m_controller) {
		m_controller->detachRFU();
		m_controller.reset();
	}
	updateAttached();
}

void RFUView::updateAttached() {
	bool attached = m_window->controller() && m_window->controller()->isRFUConnected();
	m_ui.connect->setDisabled(attached);
	m_ui.disconnect->setEnabled(attached);
	m_ui.doReset->setDisabled(attached);
}
