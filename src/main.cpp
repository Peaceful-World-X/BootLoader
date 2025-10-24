#include "inc/mainwindow.h"
#include <QApplication>
#include <QDate>
#include <QMessageBox>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QApplication::setApplicationName("BootLoader");
    MainWindow w;
    w.show();
    return a.exec();
}
