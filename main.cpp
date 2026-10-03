#include "mainwindow.h"

#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    QApplication::setOrganizationName("Laisuk");
    QApplication::setApplicationName("ZhoConverterQt");
    QApplication::setApplicationVersion("1.0.0");

    QApplication::setStyle("WindowsVista");

    MainWindow w;
    w.show();

    return QApplication::exec();
}