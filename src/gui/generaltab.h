#ifndef __GENERALTAB_H__
#define __GENERALTAB_H__

#include <QtGui>

class GeneralTab: public QWidget
{
	Q_OBJECT

	public:
		GeneralTab(QWidget * parent = 0);
		~GeneralTab();

	public:
		QLineEdit * edit1;
		QLineEdit * edit2;
		QLineEdit * edit3;
		QLineEdit * edit4;

		QCheckBox * useBIOS;
		QCheckBox * useGPU;
		QCheckBox * useDSP;
//		QCheckBox * useHostAudio;
		QCheckBox * useFullScreen;
		QCheckBox * useUnknownSoftware;
};

#endif	// __GENERALTAB_H__
