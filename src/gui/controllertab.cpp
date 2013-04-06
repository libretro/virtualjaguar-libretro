//
// controllertab.cpp: "Controller" tab on the config dialog
//
// Part of the Virtual Jaguar Project
// (C) 2011 Underground Software
// See the README and GPLv3 files for licensing and warranty information
//
// JLH = James Hammons <jlhamm@acm.org>
//
// WHO  WHEN        WHAT
// ---  ----------  ------------------------------------------------------------
// JLH  06/23/2011  Created this file
// JLH  07/20/2011  Fixed a bunch of stuff
//

#include "controllertab.h"

#include "controllerwidget.h"
#include "joystick.h"
#include "keygrabber.h"


ControllerTab::ControllerTab(QWidget * parent/*= 0*/): QWidget(parent)
{
	controllerWidget = new ControllerWidget(this);
	redefineAll = new QPushButton(tr("Define All Inputs"));

	QVBoxLayout * layout = new QVBoxLayout;
	layout->addWidget(controllerWidget);
	layout->addWidget(redefineAll);
	setLayout(layout);

	connect(redefineAll, SIGNAL(clicked()), this, SLOT(DefineAllKeys()));
}


ControllerTab::~ControllerTab()
{
}


void ControllerTab::DefineAllKeys(void)
{
//	char jagButtonName[21][10] = { "Up", "Down", "Left", "Right",
//		"*", "7", "4", "1", "0", "8", "5", "2", "#", "9", "6", "3",
//		"A", "B", "C", "Option", "Pause" };
	int orderToDefine[21] = { 0, 1, 2, 3, 18, 17, 16, 20, 19, 7, 11, 15, 6, 10, 14, 5, 9, 13, 8, 4, 12 };
	KeyGrabber keyGrab(this);

	for(int i=BUTTON_FIRST; i<=BUTTON_LAST; i++)
	{
//		keyGrab.SetText(jagButtonName[orderToDefine[i]]);
		keyGrab.SetKeyText(orderToDefine[i]);
		keyGrab.exec();
		int key = keyGrab.key;

		if (key == Qt::Key_Escape)
			break;

		// Otherwise, populate the appropriate spot in the settings & update screen...
		controllerWidget->keys[orderToDefine[i]] = key;
		controllerWidget->update();
	}
}

