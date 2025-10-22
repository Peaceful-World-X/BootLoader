#include "inc/mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 设置编码为UTF-8，确保中文正确显示
    QApplication::setApplicationName("BootLoader");

    MainWindow w;
    w.show();

    return a.exec();
}
