//
// keygrabber.cpp - Widget to grab a key and dismiss itself
//
// by James Hammons
// (C) 2011 Underground Software
//
// JLH = James Hammons <jlhamm@acm.org>
//
// Who  When        What
// ---  ----------  -------------------------------------------------------------
// JLH  07/18/2011  Created this file
//

#include "keygrabber.h"
#include "gamepad.h"


KeyGrabber::KeyGrabber(QWidget * parent/*= 0*/): QDialog(parent),
	label(new QLabel), timer(new QTimer), buttonDown(false)
{
//	label = new QLabel(this);
	QVBoxLayout * mainLayout = new QVBoxLayout;
	mainLayout->addWidget(label);
	setLayout(mainLayout);
	setWindowTitle(tr("Grab"));
	connect(timer, SIGNAL(timeout()), this, SLOT(CheckGamepad()));
	timer->setInterval(100);
	timer->start();

	// Will this make Mac OSX work???
	setFocusPolicy(Qt::StrongFocus);
}


KeyGrabber::~KeyGrabber()
{
	timer->stop();
}


void KeyGrabber::SetKeyText(int keyNum)
{
	char jagButtonName[21][10] = { "Up", "Down", "Left", "Right",
		"*", "7", "4", "1", "0", "8", "5", "2", "#", "9", "6", "3",
		"A", "B", "C", "Option", "Pause" };

	QString text = QString(tr("Press key for \"%1\"<br>(ESC to cancel)"))
		.arg(QString(jagButtonName[keyNum]));
	label->setText(text);
}


void KeyGrabber::keyPressEvent(QKeyEvent * e)
{
	key = e->key();

	// Since this is problematic, we don't allow this key...
	if (key != Qt::Key_Alt)
		accept();
}


void KeyGrabber::CheckGamepad(void)
{
	// How do we determine which joystick it is, if more than one?
	// Possibly by a combobox selecting the stick you want to configure...
	Gamepad::Update();

	if (!buttonDown)
	{
		button = Gamepad::CheckButtonPressed();

		if  (button == -1)
			return;

		buttonDown = true;
	}
	else
	{
		if (Gamepad::CheckButtonPressed() == button)
			return;

		key = button;
		accept();
		buttonDown = false;
	}
}

