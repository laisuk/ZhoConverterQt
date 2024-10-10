#include "mainwindow.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
	QApplication::setStyle("WindowsVista");
    MainWindow w;
    w.show();
    return QApplication::exec();
}
