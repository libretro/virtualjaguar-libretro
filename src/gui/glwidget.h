// Implementation of OpenGL widget using Qt
//
// by James Hammons
// (C) 2010 Underground Software

#ifndef __GLWIDGET_H__
#define __GLWIDGET_H__

#include <QtGui>
#include <QGLWidget>

class GLWidget: public QGLWidget
{
	Q_OBJECT

	public:
		GLWidget(QWidget * parent = 0);
		~GLWidget();

//		QSize minimumSizeHint() const;
//		QSize sizeHint() const;

//	signals:
//		void clicked();

	protected:
		void initializeGL();
		void paintGL();
		void resizeGL(int width, int height);

//	private:
	public:
		GLuint texture;
		int textureWidth, textureHeight;

		uint32_t * buffer;
		unsigned rasterWidth, rasterHeight;

		bool synchronize;
		unsigned filter;
		int offset;
		bool fullscreen;
		int outputWidth;
};

#endif	// __GLWIDGET_H__
