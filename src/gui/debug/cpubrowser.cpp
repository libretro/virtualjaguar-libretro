//
// cpubrowser.cpp - Jaguar CPU browser
//
// by James Hammons
// (C) 2012 Underground Software
//
// JLH = James Hammons <jlhamm@acm.org>
//
// Who  When        What
// ---  ----------  -------------------------------------------------------------
// JLH  08/14/2012  Created this file
//

// STILL TO DO:
//

#include "cpubrowser.h"
//#include "memory.h"
#include "m68000/m68kinterface.h"
#include "dsp.h"
#include "gpu.h"


CPUBrowserWindow::CPUBrowserWindow(QWidget * parent/*= 0*/): QWidget(parent, Qt::Dialog),
//	layout(new QVBoxLayout), text(new QTextBrowser),
	layout(new QVBoxLayout), text(new QLabel),
	refresh(new QPushButton(tr("Refresh"))),
	memBase(0)
{
	setWindowTitle(tr("CPU Browser"));

	// Need to set the size as well...
//	resize(560, 480);

	QFont fixedFont("Lucida Console", 8, QFont::Normal);
	text->setFont(fixedFont);
////	layout->setSizeConstraint(QLayout::SetFixedSize);
	setLayout(layout);

	layout->addWidget(text);
	layout->addWidget(refresh);

	connect(refresh, SIGNAL(clicked()), this, SLOT(RefreshContents()));
}


void CPUBrowserWindow::RefreshContents(void)
{
	char string[1024], buf[64];
	QString s;

	// 68K
	uint32_t m68kPC = m68k_get_reg(NULL, M68K_REG_PC);
	uint32_t m68kSR = m68k_get_reg(NULL, M68K_REG_SR);
	sprintf(string, "PC: %06X&nbsp;&nbsp;SR: %04X<br><br>", m68kPC, m68kSR);
	s += QString(string);

	uint32_t m68kA0 = m68k_get_reg(NULL, M68K_REG_A0);
	uint32_t m68kA1 = m68k_get_reg(NULL, M68K_REG_A1);
	uint32_t m68kA2 = m68k_get_reg(NULL, M68K_REG_A2);
	uint32_t m68kA3 = m68k_get_reg(NULL, M68K_REG_A3);
	sprintf(string, "A0: %08X&nbsp;&nbsp;A1: %08X&nbsp;&nbsp;A2: %08X&nbsp;&nbsp;A3: %08X<br>", m68kA0, m68kA1, m68kA2, m68kA3);
	s += QString(string);

	uint32_t m68kA4 = m68k_get_reg(NULL, M68K_REG_A4);
	uint32_t m68kA5 = m68k_get_reg(NULL, M68K_REG_A5);
	uint32_t m68kA6 = m68k_get_reg(NULL, M68K_REG_A6);
	uint32_t m68kA7 = m68k_get_reg(NULL, M68K_REG_A7);
	sprintf(string, "A4: %08X&nbsp;&nbsp;A5: %08X&nbsp;&nbsp;A6: %08X&nbsp;&nbsp;A7: %08X<br><br>", m68kA4, m68kA5, m68kA6, m68kA7);
	s += QString(string);

	uint32_t m68kD0 = m68k_get_reg(NULL, M68K_REG_D0);
	uint32_t m68kD1 = m68k_get_reg(NULL, M68K_REG_D1);
	uint32_t m68kD2 = m68k_get_reg(NULL, M68K_REG_D2);
	uint32_t m68kD3 = m68k_get_reg(NULL, M68K_REG_D3);
	sprintf(string, "D0: %08X&nbsp;&nbsp;D1: %08X&nbsp;&nbsp;D2: %08X&nbsp;&nbsp;D3: %08X<br>", m68kD0, m68kD1, m68kD2, m68kD3);
	s += QString(string);

	uint32_t m68kD4 = m68k_get_reg(NULL, M68K_REG_D4);
	uint32_t m68kD5 = m68k_get_reg(NULL, M68K_REG_D5);
	uint32_t m68kD6 = m68k_get_reg(NULL, M68K_REG_D6);
	uint32_t m68kD7 = m68k_get_reg(NULL, M68K_REG_D7);
	sprintf(string, "D4: %08X&nbsp;&nbsp;D5: %08X&nbsp;&nbsp;D6: %08X&nbsp;&nbsp;D7: %08X<br><br>", m68kD4, m68kD5, m68kD6, m68kD7);
	s += QString(string);

	// GPU
	sprintf(string, "GPU PC: %06X&nbsp;&nbsp;FLAGS: %08X<br><br>", GPUReadLong(0xF02010), GPUReadLong(0xF02000));
	s += QString(string);

	// DSP
	sprintf(string, "DSP PC: %06X&nbsp;&nbsp;FLAGS: %08X<br><br>", DSPReadLong(0xF1A110), DSPReadLong(0xF1A100));
	s += QString(string);

	text->clear();
	text->setText(s);
}


void CPUBrowserWindow::keyPressEvent(QKeyEvent * e)
{
	if (e->key() == Qt::Key_Escape)
		hide();
	else if (e->key() == Qt::Key_PageUp)
	{
		memBase -= 480;

		if (memBase < 0)
			memBase = 0;

		RefreshContents();
	}
	else if (e->key() == Qt::Key_PageDown)
	{
		memBase += 480;

		if (memBase > (0x200000 - 480))
			memBase = 0x200000 - 480;

		RefreshContents();
	}
	else if (e->key() == Qt::Key_Up || e->key() == Qt::Key_Minus)
	{
		memBase -= 16;

		if (memBase < 0)
			memBase = 0;

		RefreshContents();
	}
	else if (e->key() == Qt::Key_Down || e->key() == Qt::Key_Equal)
	{
		memBase += 16;

		if (memBase > (0x200000 - 480))
			memBase = 0x200000 - 480;

		RefreshContents();
	}
}
