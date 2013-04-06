//
// mainwin.h: Header file
//
// by James Hammons
// (C) 2010 Underground Software
//

#ifndef __MAINWIN_H__
#define __MAINWIN_H__

//Hrm. uh??? I thought this wasn't the way to do this stuff...???
#include <QtGui>

// Forward declarations
class GLWidget;
class AboutWindow;
class HelpWindow;
class FilePickerWindow;
class MemoryBrowserWindow;
class CPUBrowserWindow;
class OPBrowserWindow;
class M68KDasmBrowserWindow;
class RISCDasmBrowserWindow;

class MainWin: public QMainWindow
{
	// All Qt apps require this macro for signal/slot functionality to work
	Q_OBJECT

	public:
//		MainWin(QString);
		MainWin(bool);
		void LoadFile(QString);
		void SyncUI(void);

	protected:
		void closeEvent(QCloseEvent *);
		void keyPressEvent(QKeyEvent *);
		void keyReleaseEvent(QKeyEvent *);

	private slots:
		void Open(void);
 		void Configure(void);
		void Timer(void);
		void TogglePowerState(void);
		void ToggleRunState(void);
		void SetZoom100(void);
		void SetZoom200(void);
		void SetZoom300(void);
		void SetNTSC(void);
		void SetPAL(void);
		void ToggleBlur(void);
		void ShowAboutWin(void);
		void ShowHelpWin(void);
		void InsertCart(void);
		void Unpause(void);
		void LoadSoftware(QString);
		void ToggleCDUsage(void);
		void FrameAdvance(void);
		void ToggleFullScreen(void);

		void ShowMemoryBrowserWin(void);
		void ShowCPUBrowserWin(void);
		void ShowOPBrowserWin(void);
		void ShowM68KDasmBrowserWin(void);
		void ShowRISCDasmBrowserWin(void);

	private:
		void HandleKeys(QKeyEvent *, bool);
		void HandleGamepads(void);
		void SetFullScreen(bool state = true);
		void ResizeMainWindow(void);
		void ReadSettings(void);
		void WriteSettings(void);
		void WriteUISettings(void);

//	public:
		GLWidget * videoWidget;
		AboutWindow * aboutWin;
		HelpWindow * helpWin;
		FilePickerWindow * filePickWin;
		MemoryBrowserWindow * memBrowseWin;
		CPUBrowserWindow * cpuBrowseWin;
		OPBrowserWindow * opBrowseWin;
		M68KDasmBrowserWindow * m68kDasmBrowseWin;
		RISCDasmBrowserWindow * riscDasmBrowseWin;
		QTimer * timer;
		bool running;
		int zoomLevel;
		bool powerButtonOn;
		bool showUntunedTankCircuit;
		bool cartridgeLoaded;
		bool allowUnknownSoftware;
		bool CDActive;
//		bool alpineLoadSuccessful;
		bool pauseForFileSelector;
		bool loadAndGo;
		bool keyHeld[8];
		bool fullScreen;
	public:
		bool plzDontKillMyComputer;
	private:
		QPoint mainWinPosition;
//		QSize mainWinSize;
		QMenu * fileMenu;
		QMenu * helpMenu;
		QMenu * debugMenu;
		QToolBar * toolbar;
		QToolBar * debugbar;

		QActionGroup * zoomActs;
		QActionGroup * tvTypeActs;

		QAction * quitAppAct;
		QAction * powerAct;
		QAction * pauseAct;
		QAction * x1Act;
		QAction * x2Act;
		QAction * x3Act;
		QAction * ntscAct;
		QAction * palAct;
		QAction * blurAct;
		QAction * aboutAct;
		QAction * helpAct;
		QAction * filePickAct;
		QAction * configAct;
		QAction * useCDAct;
		QAction * frameAdvanceAct;
		QAction * fullScreenAct;

		QAction * memBrowseAct;
		QAction * cpuBrowseAct;
		QAction * opBrowseAct;
		QAction * m68kDasmBrowseAct;
		QAction * riscDasmBrowseAct;

		QIcon powerGreen;
		QIcon powerRed;
};

#endif	// __MAINWIN_H__
