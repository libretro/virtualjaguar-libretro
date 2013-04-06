//
// mainwin.cpp - Qt-based GUI for Virtual Jaguar: Main Application Window
// by James Hammons
// (C) 2009 Underground Software
//
// JLH = James Hammons <jlhamm@acm.org>
//
// Who  When        What
// ---  ----------  -------------------------------------------------------------
// JLH  12/23/2009  Created this file
// JLH  12/20/2010  Added settings, menus & toolbars
// JLH  07/05/2011  Added CD BIOS functionality to GUI
//

// FIXED:
//
// - Add dbl click/enter to select in cart list, ESC to dimiss [DONE]
// - Autoscan/autoload all available BIOS from 'software' folder [DONE]
// - Add 1 key jumping in cartridge list (press 'R', jumps to carts starting with 'R', etc) [DONE]
// - Controller configuration [DONE]
//
// STILL TO BE DONE:
//
// - Remove SDL dependencies (sound, mainly) from Jaguar core lib
// - Fix inconsistency with trailing slashes in paths (eeproms needs one, software doesn't)
//
// SFDX CODE: 9XF9TUHFM2359

// Uncomment this for debugging...
//#define DEBUG
//#define DEBUGFOO			// Various tool debugging...
//#define DEBUGTP				// Toolpalette debugging...

#include "mainwin.h"

#include "SDL.h"
#include "app.h"
#include "about.h"
#include "configdialog.h"
#include "filepicker.h"
#include "gamepad.h"
#include "generaltab.h"
#include "glwidget.h"
#include "help.h"
#include "settings.h"
#include "version.h"
#include "debug/cpubrowser.h"
#include "debug/m68kdasmbrowser.h"
#include "debug/memorybrowser.h"
#include "debug/opbrowser.h"
#include "debug/riscdasmbrowser.h"

#include "dac.h"
#include "jaguar.h"
#include "tom.h"
#include "log.h"
#include "file.h"
#include "jagbios.h"
#include "jagcdbios.h"
#include "jagstub2bios.h"
#include "joystick.h"

// According to SebRmv, this header isn't seen on Arch Linux either... :-/
//#ifdef __GCCWIN32__
// Apparently on win32, usleep() is not pulled in by the usual suspects.
#include <unistd.h>
//#endif

// The way BSNES controls things is by setting a timer with a zero
// timeout, sleeping if not emulating anything. Seems there has to be a
// better way.

// It has a novel approach to plugging-in/using different video/audio/input
// methods, can we do something similar or should we just use the built-in
// QOpenGL?

// We're going to try to use the built-in OpenGL support and see how it goes.
// We'll make the VJ core modular so that it doesn't matter what GUI is in
// use, we can drop it in anywhere and use it as-is.

//MainWin::MainWin(QString filenameToRun): running(true), powerButtonOn(false),
MainWin::MainWin(bool autoRun): running(true), powerButtonOn(false),
	showUntunedTankCircuit(true), cartridgeLoaded(false), CDActive(false),
	//, alpineLoadSuccessful(false),
//	pauseForFileSelector(false), loadAndGo(false), plzDontKillMyComputer(false)
	pauseForFileSelector(false), loadAndGo(autoRun), plzDontKillMyComputer(false)
{
	for(int i=0; i<8; i++)
		keyHeld[i] = false;

	videoWidget = new GLWidget(this);
	setCentralWidget(videoWidget);
	setWindowIcon(QIcon(":/res/vj-icon.png"));

	QString title = QString(tr("Virtual Jaguar " VJ_RELEASE_VERSION ));

	if (vjs.hardwareTypeAlpine)
		title += QString(tr(" - Alpine Mode"));

	setWindowTitle(title);

	aboutWin = new AboutWindow(this);
	helpWin = new HelpWindow(this);
	filePickWin = new FilePickerWindow(this);
	memBrowseWin = new MemoryBrowserWindow(this);
	cpuBrowseWin = new CPUBrowserWindow(this);
	opBrowseWin = new OPBrowserWindow(this);
	m68kDasmBrowseWin = new M68KDasmBrowserWindow(this);
	riscDasmBrowseWin = new RISCDasmBrowserWindow(this);

	videoWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

	setUnifiedTitleAndToolBarOnMac(true);

	// Create actions

	quitAppAct = new QAction(tr("E&xit"), this);
//	quitAppAct->setShortcuts(QKeySequence::Quit);
//	quitAppAct->setShortcut(QKeySequence(tr("Alt+x")));
	quitAppAct->setShortcut(QKeySequence(tr("Ctrl+q")));
	quitAppAct->setStatusTip(tr("Quit Virtual Jaguar"));
	connect(quitAppAct, SIGNAL(triggered()), this, SLOT(close()));

	powerGreen.addFile(":/res/power-off.png", QSize(), QIcon::Normal, QIcon::Off);
	powerGreen.addFile(":/res/power-on-green.png", QSize(), QIcon::Normal, QIcon::On);
	powerRed.addFile(":/res/power-off.png", QSize(), QIcon::Normal, QIcon::Off);
	powerRed.addFile(":/res/power-on-red.png", QSize(), QIcon::Normal, QIcon::On);

//	powerAct = new QAction(QIcon(":/res/power.png"), tr("&Power"), this);
	powerAct = new QAction(powerGreen, tr("&Power"), this);
	powerAct->setStatusTip(tr("Powers Jaguar on/off"));
	powerAct->setCheckable(true);
	powerAct->setChecked(false);
//	powerAct->setDisabled(true);
	connect(powerAct, SIGNAL(triggered()), this, SLOT(TogglePowerState()));

	QIcon pauseIcon;
	pauseIcon.addFile(":/res/pause-off", QSize(), QIcon::Normal, QIcon::Off);
	pauseIcon.addFile(":/res/pause-on", QSize(), QIcon::Normal, QIcon::On);
//	pauseAct = new QAction(QIcon(":/res/pause.png"), tr("Pause"), this);
	pauseAct = new QAction(pauseIcon, tr("Pause"), this);
	pauseAct->setStatusTip(tr("Toggles the running state"));
	pauseAct->setCheckable(true);
	pauseAct->setDisabled(true);
	pauseAct->setShortcut(QKeySequence(tr("Esc")));
	connect(pauseAct, SIGNAL(triggered()), this, SLOT(ToggleRunState()));

	zoomActs = new QActionGroup(this);

	x1Act = new QAction(QIcon(":/res/zoom100.png"), tr("Zoom 100%"), zoomActs);
	x1Act->setStatusTip(tr("Set window zoom to 100%"));
	x1Act->setCheckable(true);
	connect(x1Act, SIGNAL(triggered()), this, SLOT(SetZoom100()));

	x2Act = new QAction(QIcon(":/res/zoom200.png"), tr("Zoom 200%"), zoomActs);
	x2Act->setStatusTip(tr("Set window zoom to 200%"));
	x2Act->setCheckable(true);
	connect(x2Act, SIGNAL(triggered()), this, SLOT(SetZoom200()));

	x3Act = new QAction(QIcon(":/res/zoom300.png"), tr("Zoom 300%"), zoomActs);
	x3Act->setStatusTip(tr("Set window zoom to 300%"));
	x3Act->setCheckable(true);
	connect(x3Act, SIGNAL(triggered()), this, SLOT(SetZoom300()));

	tvTypeActs = new QActionGroup(this);

	ntscAct = new QAction(QIcon(":/res/ntsc.png"), tr("NTSC"), tvTypeActs);
	ntscAct->setStatusTip(tr("Sets Jaguar to NTSC mode"));
	ntscAct->setCheckable(true);
	connect(ntscAct, SIGNAL(triggered()), this, SLOT(SetNTSC()));

	palAct = new QAction(QIcon(":/res/pal.png"), tr("PAL"), tvTypeActs);
	palAct->setStatusTip(tr("Sets Jaguar to PAL mode"));
	palAct->setCheckable(true);
	connect(palAct, SIGNAL(triggered()), this, SLOT(SetPAL()));

	blurAct = new QAction(QIcon(":/res/blur.png"), tr("Blur"), this);
	blurAct->setStatusTip(tr("Sets OpenGL rendering to GL_NEAREST"));
	blurAct->setCheckable(true);
	connect(blurAct, SIGNAL(triggered()), this, SLOT(ToggleBlur()));

	aboutAct = new QAction(QIcon(":/res/vj-icon.png"), tr("&About..."), this);
	aboutAct->setStatusTip(tr("Blatant self-promotion"));
	connect(aboutAct, SIGNAL(triggered()), this, SLOT(ShowAboutWin()));

	helpAct = new QAction(QIcon(":/res/vj-icon.png"), tr("&Contents..."), this);
	helpAct->setStatusTip(tr("Help is available, if you should need it"));
	connect(helpAct, SIGNAL(triggered()), this, SLOT(ShowHelpWin()));

	filePickAct = new QAction(QIcon(":/res/software.png"), tr("&Insert Cartridge..."), this);
	filePickAct->setStatusTip(tr("Insert a cartridge into Virtual Jaguar"));
	filePickAct->setShortcut(QKeySequence(tr("Ctrl+i")));
	connect(filePickAct, SIGNAL(triggered()), this, SLOT(InsertCart()));

	configAct = new QAction(QIcon(":/res/wrench.png"), tr("&Configure"), this);
	configAct->setStatusTip(tr("Configure options for Virtual Jaguar"));
	configAct->setShortcut(QKeySequence(tr("Ctrl+c")));
	connect(configAct, SIGNAL(triggered()), this, SLOT(Configure()));

	useCDAct = new QAction(QIcon(":/res/compact-disc.png"), tr("&Use CD Unit"), this);
	useCDAct->setStatusTip(tr("Use Jaguar Virtual CD unit"));
//	useCDAct->setShortcut(QKeySequence(tr("Ctrl+c")));
	useCDAct->setCheckable(true);
	connect(useCDAct, SIGNAL(triggered()), this, SLOT(ToggleCDUsage()));

	frameAdvanceAct = new QAction(QIcon(":/res/frame-advance.png"), tr("&Frame Advance"), this);
	frameAdvanceAct->setShortcut(QKeySequence(tr("F7")));
	connect(frameAdvanceAct, SIGNAL(triggered()), this, SLOT(FrameAdvance()));

	fullScreenAct = new QAction(QIcon(":/res/fullscreen.png"), tr("F&ull Screen"), this);
	fullScreenAct->setShortcut(QKeySequence(tr("F9")));
	fullScreenAct->setCheckable(true);
	connect(fullScreenAct, SIGNAL(triggered()), this, SLOT(ToggleFullScreen()));

	// Debugger Actions
	memBrowseAct = new QAction(QIcon(":/res/tool-memory.png"), tr("Memory Browser"), this);
	memBrowseAct->setStatusTip(tr("Shows the Jaguar memory browser window"));
//	memBrowseAct->setCheckable(true);
	connect(memBrowseAct, SIGNAL(triggered()), this, SLOT(ShowMemoryBrowserWin()));

	cpuBrowseAct = new QAction(QIcon(":/res/tool-cpu.png"), tr("CPU Browser"), this);
	cpuBrowseAct->setStatusTip(tr("Shows the Jaguar CPU browser window"));
//	memBrowseAct->setCheckable(true);
	connect(cpuBrowseAct, SIGNAL(triggered()), this, SLOT(ShowCPUBrowserWin()));

	opBrowseAct = new QAction(QIcon(":/res/tool-op.png"), tr("OP Browser"), this);
	opBrowseAct->setStatusTip(tr("Shows the Jaguar OP browser window"));
//	memBrowseAct->setCheckable(true);
	connect(opBrowseAct, SIGNAL(triggered()), this, SLOT(ShowOPBrowserWin()));

	m68kDasmBrowseAct = new QAction(QIcon(":/res/tool-68k-dis.png"), tr("68K Listing Browser"), this);
	m68kDasmBrowseAct->setStatusTip(tr("Shows the 68K disassembly browser window"));
//	memBrowseAct->setCheckable(true);
	connect(m68kDasmBrowseAct, SIGNAL(triggered()), this, SLOT(ShowM68KDasmBrowserWin()));

	riscDasmBrowseAct = new QAction(QIcon(":/res/tool-risc-dis.png"), tr("RISC Listing Browser"), this);
	riscDasmBrowseAct->setStatusTip(tr("Shows the RISC disassembly browser window"));
//	memBrowseAct->setCheckable(true);
	connect(riscDasmBrowseAct, SIGNAL(triggered()), this, SLOT(ShowRISCDasmBrowserWin()));

	// Misc. connections...
	connect(filePickWin, SIGNAL(RequestLoad(QString)), this, SLOT(LoadSoftware(QString)));
	connect(filePickWin, SIGNAL(FilePickerHiding()), this, SLOT(Unpause()));

	// Create menus & toolbars

	fileMenu = menuBar()->addMenu(tr("&Jaguar"));
	fileMenu->addAction(powerAct);
	fileMenu->addAction(pauseAct);
	fileMenu->addAction(frameAdvanceAct);
	fileMenu->addAction(filePickAct);
	fileMenu->addAction(useCDAct);
	fileMenu->addAction(configAct);
	fileMenu->addAction(quitAppAct);

	if (vjs.hardwareTypeAlpine)
	{
		debugMenu = menuBar()->addMenu(tr("&Debug"));
		debugMenu->addAction(memBrowseAct);
		debugMenu->addAction(cpuBrowseAct);
		debugMenu->addAction(opBrowseAct);
		debugMenu->addAction(m68kDasmBrowseAct);
		debugMenu->addAction(riscDasmBrowseAct);
	}

	helpMenu = menuBar()->addMenu(tr("&Help"));
	helpMenu->addAction(helpAct);
	helpMenu->addAction(aboutAct);

	toolbar = addToolBar(tr("Stuff"));
	toolbar->addAction(powerAct);
	toolbar->addAction(pauseAct);
	toolbar->addAction(filePickAct);
	toolbar->addAction(useCDAct);
	toolbar->addSeparator();
	toolbar->addAction(x1Act);
	toolbar->addAction(x2Act);
	toolbar->addAction(x3Act);
	toolbar->addSeparator();
	toolbar->addAction(ntscAct);
	toolbar->addAction(palAct);
	toolbar->addSeparator();
	toolbar->addAction(blurAct);
	toolbar->addAction(fullScreenAct);

	if (vjs.hardwareTypeAlpine)
	{
		debugbar = addToolBar(tr("&Debug"));
		debugbar->addAction(memBrowseAct);
		debugbar->addAction(cpuBrowseAct);
		debugbar->addAction(opBrowseAct);
		debugbar->addAction(m68kDasmBrowseAct);
		debugbar->addAction(riscDasmBrowseAct);
	}

	//	Create status bar
	statusBar()->showMessage(tr("Ready"));

	ReadSettings();

	// Do this in case original size isn't correct (mostly for the first-run case)
	ResizeMainWindow();

	// Set up timer based loop for animation...
	timer = new QTimer(this);
	connect(timer, SIGNAL(timeout()), this, SLOT(Timer()));

	// This isn't very accurate for NTSC: This is early by 40 msec per frame.
	// This is because it's discarding the 0.6666... on the end of the fraction.
	// Alas, 6 doesn't divide cleanly into 10. :-P
//Should we defer this until SyncUI? Probably.
//No, it doesn't work, because it uses setInterval() instead of start()...
//	timer->start(vjs.hardwareTypeNTSC ? 16 : 20);

	// We set this initially, to make VJ behave somewhat as it would if no
	// cart were inserted and the BIOS was set as active...
	jaguarCartInserted = true;
	WriteLog("Virtual Jaguar %s (Last full build was on %s %s)\n", VJ_RELEASE_VERSION, __DATE__, __TIME__);
	WriteLog("VJ: Initializing jaguar subsystem...\n");
	JaguarInit();
	memcpy(jagMemSpace + 0xE00000, jaguarBootROM, 0x20000);	// Use the stock BIOS

	// Check for filename passed in on the command line...
//	if (!filenameToRun.isEmpty())
	if (autoRun)
	{
//		loadAndGo = true;
		// Attempt to load/run the file the user passed in...
//		LoadSoftware(filenameToRun);
////		memcpy(jagMemSpace + 0xE00000, jaguarBootROM, 0x20000);	// Use the stock BIOS
		// Prevent the file scanner from running...
		return;
	}

	// Load up the default ROM if in Alpine mode:
	if (vjs.hardwareTypeAlpine)
	{
		bool romLoaded = JaguarLoadFile(vjs.alpineROMPath);

		// If regular load failed, try just a straight file load
		// (Dev only! I don't want people to start getting lazy with their releases again! :-P)
		if (!romLoaded)
			romLoaded = AlpineLoadFile(vjs.alpineROMPath);

		if (romLoaded)
			WriteLog("Alpine Mode: Successfully loaded file \"%s\".\n", vjs.alpineROMPath);
		else
			WriteLog("Alpine Mode: Unable to load file \"%s\"!\n", vjs.alpineROMPath);

		// Attempt to load/run the ABS file...
		LoadSoftware(vjs.absROMPath);
		memcpy(jagMemSpace + 0xE00000, jaguarDevBootROM2, 0x20000);	// Use the stub BIOS
		// Prevent the scanner from running...
		return;
	}
//	else
//		memcpy(jagMemSpace + 0xE00000, jaguarBootROM, 0x20000);	// Otherwise, use the stock BIOS

	// Run the scanner if nothing passed in and *not* Alpine mode...
	// NB: Really need to look into caching the info scanned in here...
	filePickWin->ScanSoftwareFolder(allowUnknownSoftware);
}


void MainWin::LoadFile(QString file)
{
	LoadSoftware(file);
}


void MainWin::SyncUI(void)
{
	// Set toolbar buttons/menus based on settings read in (sync the UI)...
	// (Really, this is to sync command line options passed in)
	blurAct->setChecked(vjs.glFilter);
	x1Act->setChecked(zoomLevel == 1);
	x2Act->setChecked(zoomLevel == 2);
	x3Act->setChecked(zoomLevel == 3);
//	running = powerAct->isChecked();
	ntscAct->setChecked(vjs.hardwareTypeNTSC);
	palAct->setChecked(!vjs.hardwareTypeNTSC);
	powerAct->setIcon(vjs.hardwareTypeNTSC ? powerRed : powerGreen);

	fullScreen = vjs.fullscreen;
	SetFullScreen(fullScreen);

	// Reset the timer to be what was set in the command line (if any):
//	timer->setInterval(vjs.hardwareTypeNTSC ? 16 : 20);
	timer->start(vjs.hardwareTypeNTSC ? 16 : 20);
}


void MainWin::closeEvent(QCloseEvent * event)
{
	JaguarDone();
// This should only be done by the config dialog
//	WriteSettings();
	WriteUISettings();
	event->accept(); // ignore() if can't close for some reason
}


void MainWin::keyPressEvent(QKeyEvent * e)
{
	// We ignore the Alt key for now, since it causes problems with the GUI
	if (e->key() == Qt::Key_Alt)
	{
		e->accept();
		return;
	}
/*
	if (e->key() == Qt::Key_F9)
	{
		ToggleFullScreen();
		return;
	}
*/
	HandleKeys(e, true);
}


void MainWin::keyReleaseEvent(QKeyEvent * e)
{
	// We ignore the Alt key for now, since it causes problems with the GUI
	if (e->key() == Qt::Key_Alt)
	{
		e->accept();
		return;
	}

	HandleKeys(e, false);
}


void MainWin::HandleKeys(QKeyEvent * e, bool state)
{
	enum { P1LEFT = 0, P1RIGHT, P1UP, P1DOWN, P2LEFT, P2RIGHT, P2UP, P2DOWN };
	// We kill bad key combos here, before they can get to the emulator...
	// This also kills the illegal instruction problem that cropped up in Rayman!
	// May want to do this by killing the old one instead of ignoring the new one...
	// Seems to work better that way...

// The problem with this approach is that it causes bad results because it doesn't do
// any checking of previous states. Need to come up with something better because this
// causes problems where the keyboard acts as if it were unresponsive. :-P
#if 0
	if ((e->key() == vjs.p1KeyBindings[BUTTON_L] && joypad_0_buttons[BUTTON_R])
		|| (e->key() == vjs.p1KeyBindings[BUTTON_R] && joypad_0_buttons[BUTTON_L])
		|| (e->key() == vjs.p1KeyBindings[BUTTON_U] && joypad_0_buttons[BUTTON_D])
		|| (e->key() == vjs.p1KeyBindings[BUTTON_D] && joypad_0_buttons[BUTTON_U]))
		return;
#else
#if 0
	if (e->key() == (int)vjs.p1KeyBindings[BUTTON_L] && joypad_0_buttons[BUTTON_R])
		joypad_0_buttons[BUTTON_R] = 0;
	if (e->key() == (int)vjs.p1KeyBindings[BUTTON_R] && joypad_0_buttons[BUTTON_L])
		joypad_0_buttons[BUTTON_L] = 0;
	if (e->key() == (int)vjs.p1KeyBindings[BUTTON_U] && joypad_0_buttons[BUTTON_D])
		joypad_0_buttons[BUTTON_D] = 0;
	if (e->key() == (int)vjs.p1KeyBindings[BUTTON_D] && joypad_0_buttons[BUTTON_U])
		joypad_0_buttons[BUTTON_U] = 0;

	if (e->key() == (int)vjs.p2KeyBindings[BUTTON_L] && joypad_1_buttons[BUTTON_R])
		joypad_1_buttons[BUTTON_R] = 0;
	if (e->key() == (int)vjs.p2KeyBindings[BUTTON_R] && joypad_1_buttons[BUTTON_L])
		joypad_1_buttons[BUTTON_L] = 0;
	if (e->key() == (int)vjs.p2KeyBindings[BUTTON_U] && joypad_1_buttons[BUTTON_D])
		joypad_1_buttons[BUTTON_D] = 0;
	if (e->key() == (int)vjs.p2KeyBindings[BUTTON_D] && joypad_1_buttons[BUTTON_U])
		joypad_1_buttons[BUTTON_U] = 0;
#else
//hrm, this still has sticky state problems... Ugh!
	// First, settle key states...
	if (e->key() == (int)vjs.p1KeyBindings[BUTTON_L])
		keyHeld[P1LEFT] = state;
	else if (e->key() == (int)vjs.p1KeyBindings[BUTTON_R])
		keyHeld[P1RIGHT] = state;
	else if (e->key() == (int)vjs.p1KeyBindings[BUTTON_U])
		keyHeld[P1UP] = state;
	else if (e->key() == (int)vjs.p1KeyBindings[BUTTON_D])
		keyHeld[P1DOWN] = state;
	else if (e->key() == (int)vjs.p2KeyBindings[BUTTON_L])
		keyHeld[P2LEFT] = state;
	else if (e->key() == (int)vjs.p2KeyBindings[BUTTON_R])
		keyHeld[P2RIGHT] = state;
	else if (e->key() == (int)vjs.p2KeyBindings[BUTTON_U])
		keyHeld[P2UP] = state;
	else if (e->key() == (int)vjs.p2KeyBindings[BUTTON_D])
		keyHeld[P2DOWN] = state;

	// Next, check for conflicts and bail out if there are any...
	if ((keyHeld[P1LEFT] && keyHeld[P1RIGHT])
		|| (keyHeld[P1UP] && keyHeld[P1DOWN])
		|| (keyHeld[P2LEFT] && keyHeld[P2RIGHT])
		|| (keyHeld[P2UP] && keyHeld[P2DOWN]))
		return;
#endif
#endif

	// No bad combos exist, let's stuff the emulator key buffers...!
	for(int i=BUTTON_FIRST; i<=BUTTON_LAST; i++)
	{
		if (e->key() == (int)vjs.p1KeyBindings[i])
//			joypad_0_buttons[i] = (uint8)state;
			joypad_0_buttons[i] = (state ? 0x01 : 0x00);

// Pad #2 is screwing up pad #1. Prolly a problem in joystick.cpp...
// So let's try to fix it there. :-P [DONE]
		if (e->key() == (int)vjs.p2KeyBindings[i])
//			joypad_1_buttons[i] = (uint8)state;
			joypad_1_buttons[i] = (state ? 0x01 : 0x00);
	}
}


void MainWin::HandleGamepads(void)
{
	Gamepad::Update();

	for(int i=BUTTON_FIRST; i<=BUTTON_LAST; i++)
	{
		if (vjs.p1KeyBindings[i] & (JOY_BUTTON | JOY_HAT))
			joypad_0_buttons[i] = (Gamepad::GetState(0, vjs.p1KeyBindings[i]) ? 0x01 : 0x00);

		if (vjs.p2KeyBindings[i] & (JOY_BUTTON | JOY_HAT))
			joypad_1_buttons[i] = (Gamepad::GetState(1, vjs.p2KeyBindings[i]) ? 0x01 : 0x00);
	}
}


void MainWin::Open(void)
{
}


void MainWin::Configure(void)
{
	// Call the configuration dialog and update settings
	ConfigDialog dlg(this);
	//ick.
	dlg.generalTab->useUnknownSoftware->setChecked(allowUnknownSoftware);

	if (dlg.exec() == false)
		return;

	QString before = vjs.ROMPath;
	QString alpineBefore = vjs.alpineROMPath;
	QString absBefore = vjs.absROMPath;
//	bool audioBefore = vjs.audioEnabled;
	bool audioBefore = vjs.DSPEnabled;
	dlg.UpdateVJSettings();
	QString after = vjs.ROMPath;
	QString alpineAfter = vjs.alpineROMPath;
	QString absAfter = vjs.absROMPath;
//	bool audioAfter = vjs.audioEnabled;
	bool audioAfter = vjs.DSPEnabled;

	bool allowOld = allowUnknownSoftware;
	//ick.
	allowUnknownSoftware = dlg.generalTab->useUnknownSoftware->isChecked();

	// We rescan the "software" folder if the user either changed the path or
	// checked/unchecked the "Allow unknown files" option in the config dialog.
	if ((before != after) || (allowOld != allowUnknownSoftware))
		filePickWin->ScanSoftwareFolder(allowUnknownSoftware);

	// If the "Alpine" ROM is changed, then let's load it...
	if (alpineBefore != alpineAfter)
	{
		if (!JaguarLoadFile(vjs.alpineROMPath) && !AlpineLoadFile(vjs.alpineROMPath))
		{
			// Oh crap, we couldn't get the file! Alert the media!
			QMessageBox msg;
			msg.setText(QString(tr("Could not load file \"%1\"!")).arg(vjs.alpineROMPath));
			msg.setIcon(QMessageBox::Warning);
			msg.exec();
		}
	}

	// If the "ABS" ROM is changed, then let's load it...
	if (absBefore != absAfter)
	{
		if (!JaguarLoadFile(vjs.absROMPath))
		{
			// Oh crap, we couldn't get the file! Alert the media!
			QMessageBox msg;
			msg.setText(QString(tr("Could not load file \"%1\"!")).arg(vjs.absROMPath));
			msg.setIcon(QMessageBox::Warning);
			msg.exec();
		}
	}

	// If the "Enable DSP" checkbox changed, then we have to re-init the DAC,
	// since it's running in the host audio IRQ...
	if (audioBefore != audioAfter)
	{
		DACDone();
		DACInit();
	}

	// Just in case we crash before a clean exit...
	WriteSettings();
}


//
// Here's the main emulator loop
//
void MainWin::Timer(void)
{
	if (!running)
		return;

	if (showUntunedTankCircuit)
	{
		// Some machines can't handle this, so we give them the option to disable it. :-)
		if (!plzDontKillMyComputer)
		{
			// Random hash & trash
			// We try to simulate an untuned tank circuit here... :-)
			for(uint32_t x=0; x<videoWidget->rasterWidth; x++)
			{
				for(uint32_t y=0; y<videoWidget->rasterHeight; y++)
				{
					videoWidget->buffer[(y * videoWidget->textureWidth) + x]
						= (rand() & 0xFF) << 8 | (rand() & 0xFF) << 16 | (rand() & 0xFF) << 24;
				}
			}
		}
	}
	else
	{
		// Otherwise, run the Jaguar simulation
		HandleGamepads();
		JaguarExecuteNew();
	}

	videoWidget->updateGL();
}


void MainWin::TogglePowerState(void)
{
	powerButtonOn = !powerButtonOn;
	running = true;

	// With the power off, we simulate white noise on the screen. :-)
	if (!powerButtonOn)
	{
		useCDAct->setDisabled(false);
		palAct->setDisabled(false);
		ntscAct->setDisabled(false);
		pauseAct->setChecked(false);
		pauseAct->setDisabled(true);
		showUntunedTankCircuit = true;
		// This is just in case the ROM we were playing was in a narrow or wide field mode,
		// so the untuned tank sim doesn't look wrong. :-)
		DACPauseAudioThread();
		TOMReset();
	}
	else
	{
		useCDAct->setDisabled(true);
		palAct->setDisabled(true);
		ntscAct->setDisabled(true);
		pauseAct->setChecked(false);
		pauseAct->setDisabled(false);
		showUntunedTankCircuit = false;

		// Otherwise, we prepare for running regular software...
		if (CDActive)
		{
// Should check for cartridgeLoaded here as well...!
// We can clear it when toggling CDActive on, so that when we power cycle it does the
// expected thing. Otherwise, if we use the file picker to insert a cart, we expect
// to run the cart! Maybe have a RemoveCart function that only works if the CD unit
// is active?
			setWindowTitle(QString("Virtual Jaguar " VJ_RELEASE_VERSION
				" - Now playing: Jaguar CD"));
		}

		WriteLog("GUI: Resetting Jaguar...\n");
		JaguarReset();
		DACPauseAudioThread(false);
	}
}


void MainWin::ToggleRunState(void)
{
	running = !running;

	if (!running)
	{
		for(uint32_t i=0; i<(uint32_t)(videoWidget->textureWidth * 256); i++)
		{
			uint32_t pixel = videoWidget->buffer[i];
			uint8_t r = (pixel >> 24) & 0xFF, g = (pixel >> 16) & 0xFF, b = (pixel >> 8) & 0xFF;
			pixel = ((r + g + b) / 3) & 0x00FF;
			videoWidget->buffer[i] = 0x000000FF | (pixel << 16) | (pixel << 8);
		}

		videoWidget->updateGL();
	}

	// Pause/unpause any running/non-running threads...
	DACPauseAudioThread(!running);
}


void MainWin::SetZoom100(void)
{
	zoomLevel = 1;
	ResizeMainWindow();
}


void MainWin::SetZoom200(void)
{
	zoomLevel = 2;
	ResizeMainWindow();
}


void MainWin::SetZoom300(void)
{
	zoomLevel = 3;
	ResizeMainWindow();
}


void MainWin::SetNTSC(void)
{
	powerAct->setIcon(powerRed);
	timer->setInterval(16);
	vjs.hardwareTypeNTSC = true;
	ResizeMainWindow();
	WriteSettings();
}


void MainWin::SetPAL(void)
{
	powerAct->setIcon(powerGreen);
	timer->setInterval(20);
	vjs.hardwareTypeNTSC = false;
	ResizeMainWindow();
	WriteSettings();
}


void MainWin::ToggleBlur(void)
{
	vjs.glFilter = !vjs.glFilter;
	WriteSettings();
}


void MainWin::ShowAboutWin(void)
{
	aboutWin->show();
}


void MainWin::ShowHelpWin(void)
{
	helpWin->show();
}


void MainWin::InsertCart(void)
{
	// If the emulator is running, we pause it here and unpause it later
	// if we dismiss the file selector without choosing anything
	if (running && powerButtonOn)
	{
		ToggleRunState();
		pauseForFileSelector = true;
	}

	filePickWin->show();
}


void MainWin::Unpause(void)
{
	// Here we unpause the emulator if it was paused when we went into the file selector
	if (pauseForFileSelector)
	{
		pauseForFileSelector = false;

		// Some nutter might have unpaused while in the file selector, so check for that
		if (!running)
			ToggleRunState();
	}
}


void MainWin::LoadSoftware(QString file)
{
	running = false;							// Prevent bad things(TM) from happening...
	pauseForFileSelector = false;				// Reset the file selector pause flag
	SET32(jaguarMainRAM, 0, 0x00200000);		// Set top of stack...
	cartridgeLoaded = JaguarLoadFile(file.toAscii().data());

	char * biosPointer = jaguarBootROM;

	if (vjs.hardwareTypeAlpine)
		biosPointer = jaguarDevBootROM2;

	memcpy(jagMemSpace + 0xE00000, biosPointer, 0x20000);

	powerAct->setDisabled(false);
	powerAct->setChecked(true);
	powerButtonOn = false;
	TogglePowerState();

	if (!vjs.hardwareTypeAlpine && !loadAndGo)
	{
		QString newTitle = QString("Virtual Jaguar " VJ_RELEASE_VERSION " - Now playing: %1")
			.arg(filePickWin->GetSelectedPrettyName());
		setWindowTitle(newTitle);
	}
}


void MainWin::ToggleCDUsage(void)
{
	CDActive = !CDActive;

#if 0
	if (CDActive)
	{
		powerAct->setDisabled(false);
	}
	else
	{
		powerAct->setDisabled(true);
	}
#else
	// Set up the Jaguar CD for execution, otherwise, clear memory
	if (CDActive)
		memcpy(jagMemSpace + 0x800000, jaguarCDBootROM, 0x40000);
	else
		memset(jagMemSpace + 0x800000, 0xFF, 0x40000);
#endif
}


void MainWin::FrameAdvance(void)
{
//printf("Frame Advance...\n");
	// Execute 1 frame, then exit (only useful in Pause mode)
	JaguarExecuteNew();
	videoWidget->updateGL();
}


void MainWin::SetFullScreen(bool state/*= true*/)
{
	if (state)
	{
		mainWinPosition = pos();
//		mainWinSize = size();
		menuBar()->hide();
		statusBar()->hide();
		showFullScreen();
		QRect r = QApplication::desktop()->availableGeometry();
//		double targetWidth = 320.0, targetHeight = (vjs.hardwareTypeNTSC ? 240.0 : 256.0);
		double targetWidth = (double)VIRTUAL_SCREEN_WIDTH,
			targetHeight = (double)(vjs.hardwareTypeNTSC ? VIRTUAL_SCREEN_HEIGHT_NTSC : VIRTUAL_SCREEN_HEIGHT_PAL);
		double aspectRatio = targetWidth / targetHeight;
		// NOTE: Really should check here to see which dimension constrains the other.
		//       Right now, we assume that height is the constraint.
		int newWidth = (int)(aspectRatio * (double)r.height());
		videoWidget->offset = (r.width() - newWidth) / 2;
		videoWidget->fullscreen = true;
		videoWidget->outputWidth = newWidth;

//		videoWidget->setFixedSize(newWidth, r.height());
		videoWidget->setFixedSize(r.width(), r.height());
		showFullScreen();
	}
	else
	{
		// Reset the video widget to windowed mode
		videoWidget->offset = 0;
		videoWidget->fullscreen = false;
		menuBar()->show();
		statusBar()->show();
		showNormal();
		ResizeMainWindow();
		move(mainWinPosition);
	}

	// For some reason, this doesn't work: If the emu is paused, toggling from
	// fullscreen to windowed (& vice versa) shows a white screen.
//	videoWidget->updateGL();
}


void MainWin::ToggleFullScreen(void)
{
	fullScreen = !fullScreen;
	SetFullScreen(fullScreen);
}


void MainWin::ShowMemoryBrowserWin(void)
{
	memBrowseWin->show();
	memBrowseWin->RefreshContents();
}


void MainWin::ShowCPUBrowserWin(void)
{
	cpuBrowseWin->show();
	cpuBrowseWin->RefreshContents();
}


void MainWin::ShowOPBrowserWin(void)
{
	opBrowseWin->show();
	opBrowseWin->RefreshContents();
}


void MainWin::ShowM68KDasmBrowserWin(void)
{
	m68kDasmBrowseWin->show();
	m68kDasmBrowseWin->RefreshContents();
}


void MainWin::ShowRISCDasmBrowserWin(void)
{
	riscDasmBrowseWin->show();
	riscDasmBrowseWin->RefreshContents();
}


void MainWin::ResizeMainWindow(void)
{
//	videoWidget->setFixedSize(zoomLevel * 320, zoomLevel * (vjs.hardwareTypeNTSC ? 240 : 256));
	videoWidget->setFixedSize(zoomLevel * VIRTUAL_SCREEN_WIDTH,
		zoomLevel * (vjs.hardwareTypeNTSC ? VIRTUAL_SCREEN_HEIGHT_NTSC : VIRTUAL_SCREEN_HEIGHT_PAL));
	show();

	for(int i=0; i<2; i++)
	{
		resize(0, 0);
		usleep(2000);
		QApplication::processEvents();
	}
}


#warning "!!! Need to check the window geometry to see if the positions are legal !!!"
// i.e., someone could drag it to another screen, close it, then disconnect that screen
void MainWin::ReadSettings(void)
{
	QSettings settings("Underground Software", "Virtual Jaguar");
	mainWinPosition = settings.value("pos", QPoint(200, 200)).toPoint();
	QSize size = settings.value("size", QSize(400, 400)).toSize();
	resize(size);
	move(mainWinPosition);
	QPoint pos = settings.value("cartLoadPos", QPoint(200, 200)).toPoint();
	filePickWin->move(pos);

	zoomLevel = settings.value("zoom", 2).toInt();
	allowUnknownSoftware = settings.value("showUnknownSoftware", false).toBool();

	vjs.useJoystick      = settings.value("useJoystick", false).toBool();
	vjs.joyport          = settings.value("joyport", 0).toInt();
	vjs.hardwareTypeNTSC = settings.value("hardwareTypeNTSC", true).toBool();
	vjs.frameSkip        = settings.value("frameSkip", 0).toInt();
	vjs.useJaguarBIOS    = settings.value("useJaguarBIOS", false).toBool();
	vjs.GPUEnabled       = settings.value("GPUEnabled", true).toBool();
	vjs.DSPEnabled       = settings.value("DSPEnabled", false).toBool();
	vjs.audioEnabled     = settings.value("audioEnabled", true).toBool();
	vjs.usePipelinedDSP  = settings.value("usePipelinedDSP", false).toBool();
	vjs.fullscreen       = settings.value("fullscreen", false).toBool();
	vjs.useOpenGL        = settings.value("useOpenGL", true).toBool();
	vjs.glFilter         = settings.value("glFilterType", 1).toInt();
	vjs.renderType       = settings.value("renderType", 0).toInt();
	vjs.allowWritesToROM = settings.value("writeROM", false).toBool();
//	strcpy(vjs.jagBootPath, settings.value("JagBootROM", "./bios/[BIOS] Atari Jaguar (USA, Europe).zip").toString().toAscii().data());
//	strcpy(vjs.CDBootPath, settings.value("CDBootROM", "./bios/jagcd.rom").toString().toAscii().data());
	strcpy(vjs.EEPROMPath, settings.value("EEPROMs", "./eeproms/").toString().toAscii().data());
	strcpy(vjs.ROMPath, settings.value("ROMs", "./software/").toString().toAscii().data());
	strcpy(vjs.alpineROMPath, settings.value("DefaultROM", "").toString().toAscii().data());
	strcpy(vjs.absROMPath, settings.value("DefaultABS", "").toString().toAscii().data());
WriteLog("MainWin: Paths\n");
//WriteLog("    jagBootPath = \"%s\"\n", vjs.jagBootPath);
//WriteLog("    CDBootPath  = \"%s\"\n", vjs.CDBootPath);
WriteLog("   EEPROMPath = \"%s\"\n", vjs.EEPROMPath);
WriteLog("      ROMPath = \"%s\"\n", vjs.ROMPath);
WriteLog("AlpineROMPath = \"%s\"\n", vjs.alpineROMPath);
WriteLog("   absROMPath = \"%s\"\n", vjs.absROMPath);
WriteLog("Pipelined DSP = %s\n", (vjs.usePipelinedDSP ? "ON" : "off"));

	// Keybindings in order of U, D, L, R, C, B, A, Op, Pa, 0-9, #, *
	vjs.p1KeyBindings[BUTTON_U] = settings.value("p1k_up", Qt::Key_S).toInt();
	vjs.p1KeyBindings[BUTTON_D] = settings.value("p1k_down", Qt::Key_X).toInt();
	vjs.p1KeyBindings[BUTTON_L] = settings.value("p1k_left", Qt::Key_A).toInt();
	vjs.p1KeyBindings[BUTTON_R] = settings.value("p1k_right", Qt::Key_D).toInt();
	vjs.p1KeyBindings[BUTTON_C] = settings.value("p1k_c", Qt::Key_J).toInt();
	vjs.p1KeyBindings[BUTTON_B] = settings.value("p1k_b", Qt::Key_K).toInt();
	vjs.p1KeyBindings[BUTTON_A] = settings.value("p1k_a", Qt::Key_L).toInt();
	vjs.p1KeyBindings[BUTTON_OPTION] = settings.value("p1k_option", Qt::Key_O).toInt();
	vjs.p1KeyBindings[BUTTON_PAUSE] = settings.value("p1k_pause", Qt::Key_P).toInt();
	vjs.p1KeyBindings[BUTTON_0] = settings.value("p1k_0", Qt::Key_0).toInt();
	vjs.p1KeyBindings[BUTTON_1] = settings.value("p1k_1", Qt::Key_1).toInt();
	vjs.p1KeyBindings[BUTTON_2] = settings.value("p1k_2", Qt::Key_2).toInt();
	vjs.p1KeyBindings[BUTTON_3] = settings.value("p1k_3", Qt::Key_3).toInt();
	vjs.p1KeyBindings[BUTTON_4] = settings.value("p1k_4", Qt::Key_4).toInt();
	vjs.p1KeyBindings[BUTTON_5] = settings.value("p1k_5", Qt::Key_5).toInt();
	vjs.p1KeyBindings[BUTTON_6] = settings.value("p1k_6", Qt::Key_6).toInt();
	vjs.p1KeyBindings[BUTTON_7] = settings.value("p1k_7", Qt::Key_7).toInt();
	vjs.p1KeyBindings[BUTTON_8] = settings.value("p1k_8", Qt::Key_8).toInt();
	vjs.p1KeyBindings[BUTTON_9] = settings.value("p1k_9", Qt::Key_9).toInt();
	vjs.p1KeyBindings[BUTTON_d] = settings.value("p1k_pound", Qt::Key_Minus).toInt();
	vjs.p1KeyBindings[BUTTON_s] = settings.value("p1k_star", Qt::Key_Equal).toInt();

	vjs.p2KeyBindings[BUTTON_U] = settings.value("p2k_up", Qt::Key_Up).toInt();
	vjs.p2KeyBindings[BUTTON_D] = settings.value("p2k_down", Qt::Key_Down).toInt();
	vjs.p2KeyBindings[BUTTON_L] = settings.value("p2k_left", Qt::Key_Left).toInt();
	vjs.p2KeyBindings[BUTTON_R] = settings.value("p2k_right", Qt::Key_Right).toInt();
	vjs.p2KeyBindings[BUTTON_C] = settings.value("p2k_c", Qt::Key_Z).toInt();
	vjs.p2KeyBindings[BUTTON_B] = settings.value("p2k_b", Qt::Key_X).toInt();
	vjs.p2KeyBindings[BUTTON_A] = settings.value("p2k_a", Qt::Key_C).toInt();
	vjs.p2KeyBindings[BUTTON_OPTION] = settings.value("p2k_option", Qt::Key_Apostrophe).toInt();
	vjs.p2KeyBindings[BUTTON_PAUSE] = settings.value("p2k_pause", Qt::Key_Return).toInt();
	vjs.p2KeyBindings[BUTTON_0] = settings.value("p2k_0", Qt::Key_0).toInt();
	vjs.p2KeyBindings[BUTTON_1] = settings.value("p2k_1", Qt::Key_1).toInt();
	vjs.p2KeyBindings[BUTTON_2] = settings.value("p2k_2", Qt::Key_2).toInt();
	vjs.p2KeyBindings[BUTTON_3] = settings.value("p2k_3", Qt::Key_3).toInt();
	vjs.p2KeyBindings[BUTTON_4] = settings.value("p2k_4", Qt::Key_4).toInt();
	vjs.p2KeyBindings[BUTTON_5] = settings.value("p2k_5", Qt::Key_5).toInt();
	vjs.p2KeyBindings[BUTTON_6] = settings.value("p2k_6", Qt::Key_6).toInt();
	vjs.p2KeyBindings[BUTTON_7] = settings.value("p2k_7", Qt::Key_7).toInt();
	vjs.p2KeyBindings[BUTTON_8] = settings.value("p2k_8", Qt::Key_8).toInt();
	vjs.p2KeyBindings[BUTTON_9] = settings.value("p2k_9", Qt::Key_9).toInt();
	vjs.p2KeyBindings[BUTTON_d] = settings.value("p2k_pound", Qt::Key_Slash).toInt();
	vjs.p2KeyBindings[BUTTON_s] = settings.value("p2k_star", Qt::Key_Asterisk).toInt();
}


void MainWin::WriteSettings(void)
{
	QSettings settings("Underground Software", "Virtual Jaguar");
	settings.setValue("pos", pos());
	settings.setValue("size", size());
	settings.setValue("cartLoadPos", filePickWin->pos());

	settings.setValue("zoom", zoomLevel);
	settings.setValue("showUnknownSoftware", allowUnknownSoftware);

	settings.setValue("useJoystick", vjs.useJoystick);
	settings.setValue("joyport", vjs.joyport);
	settings.setValue("hardwareTypeNTSC", vjs.hardwareTypeNTSC);
	settings.setValue("frameSkip", vjs.frameSkip);
	settings.setValue("useJaguarBIOS", vjs.useJaguarBIOS);
	settings.setValue("GPUEnabled", vjs.GPUEnabled);
	settings.setValue("DSPEnabled", vjs.DSPEnabled);
	settings.setValue("audioEnabled", vjs.audioEnabled);
	settings.setValue("usePipelinedDSP", vjs.usePipelinedDSP);
	settings.setValue("fullscreen", vjs.fullscreen);
	settings.setValue("useOpenGL", vjs.useOpenGL);
	settings.setValue("glFilterType", vjs.glFilter);
	settings.setValue("renderType", vjs.renderType);
	settings.setValue("writeROM", vjs.allowWritesToROM);
	settings.setValue("JagBootROM", vjs.jagBootPath);
	settings.setValue("CDBootROM", vjs.CDBootPath);
	settings.setValue("EEPROMs", vjs.EEPROMPath);
	settings.setValue("ROMs", vjs.ROMPath);
	settings.setValue("DefaultROM", vjs.alpineROMPath);
	settings.setValue("DefaultABS", vjs.absROMPath);

	settings.setValue("p1k_up", vjs.p1KeyBindings[BUTTON_U]);
	settings.setValue("p1k_down", vjs.p1KeyBindings[BUTTON_D]);
	settings.setValue("p1k_left", vjs.p1KeyBindings[BUTTON_L]);
	settings.setValue("p1k_right", vjs.p1KeyBindings[BUTTON_R]);
	settings.setValue("p1k_c", vjs.p1KeyBindings[BUTTON_C]);
	settings.setValue("p1k_b", vjs.p1KeyBindings[BUTTON_B]);
	settings.setValue("p1k_a", vjs.p1KeyBindings[BUTTON_A]);
	settings.setValue("p1k_option", vjs.p1KeyBindings[BUTTON_OPTION]);
	settings.setValue("p1k_pause", vjs.p1KeyBindings[BUTTON_PAUSE]);
	settings.setValue("p1k_0", vjs.p1KeyBindings[BUTTON_0]);
	settings.setValue("p1k_1", vjs.p1KeyBindings[BUTTON_1]);
	settings.setValue("p1k_2", vjs.p1KeyBindings[BUTTON_2]);
	settings.setValue("p1k_3", vjs.p1KeyBindings[BUTTON_3]);
	settings.setValue("p1k_4", vjs.p1KeyBindings[BUTTON_4]);
	settings.setValue("p1k_5", vjs.p1KeyBindings[BUTTON_5]);
	settings.setValue("p1k_6", vjs.p1KeyBindings[BUTTON_6]);
	settings.setValue("p1k_7", vjs.p1KeyBindings[BUTTON_7]);
	settings.setValue("p1k_8", vjs.p1KeyBindings[BUTTON_8]);
	settings.setValue("p1k_9", vjs.p1KeyBindings[BUTTON_9]);
	settings.setValue("p1k_pound", vjs.p1KeyBindings[BUTTON_d]);
	settings.setValue("p1k_star", vjs.p1KeyBindings[BUTTON_s]);

	settings.setValue("p2k_up", vjs.p2KeyBindings[BUTTON_U]);
	settings.setValue("p2k_down", vjs.p2KeyBindings[BUTTON_D]);
	settings.setValue("p2k_left", vjs.p2KeyBindings[BUTTON_L]);
	settings.setValue("p2k_right", vjs.p2KeyBindings[BUTTON_R]);
	settings.setValue("p2k_c", vjs.p2KeyBindings[BUTTON_C]);
	settings.setValue("p2k_b", vjs.p2KeyBindings[BUTTON_B]);
	settings.setValue("p2k_a", vjs.p2KeyBindings[BUTTON_A]);
	settings.setValue("p2k_option", vjs.p2KeyBindings[BUTTON_OPTION]);
	settings.setValue("p2k_pause", vjs.p2KeyBindings[BUTTON_PAUSE]);
	settings.setValue("p2k_0", vjs.p2KeyBindings[BUTTON_0]);
	settings.setValue("p2k_1", vjs.p2KeyBindings[BUTTON_1]);
	settings.setValue("p2k_2", vjs.p2KeyBindings[BUTTON_2]);
	settings.setValue("p2k_3", vjs.p2KeyBindings[BUTTON_3]);
	settings.setValue("p2k_4", vjs.p2KeyBindings[BUTTON_4]);
	settings.setValue("p2k_5", vjs.p2KeyBindings[BUTTON_5]);
	settings.setValue("p2k_6", vjs.p2KeyBindings[BUTTON_6]);
	settings.setValue("p2k_7", vjs.p2KeyBindings[BUTTON_7]);
	settings.setValue("p2k_8", vjs.p2KeyBindings[BUTTON_8]);
	settings.setValue("p2k_9", vjs.p2KeyBindings[BUTTON_9]);
	settings.setValue("p2k_pound", vjs.p2KeyBindings[BUTTON_d]);
	settings.setValue("p2k_star", vjs.p2KeyBindings[BUTTON_s]);
}


void MainWin::WriteUISettings(void)
{
	QSettings settings("Underground Software", "Virtual Jaguar");
	settings.setValue("pos", pos());
	settings.setValue("size", size());
	settings.setValue("cartLoadPos", filePickWin->pos());

	settings.setValue("zoom", zoomLevel);
}
