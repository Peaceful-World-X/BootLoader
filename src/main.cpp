#include "inc/mainwindow.h"
#include <QApplication>
#include <QDate>
#include <QMessageBox>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QApplication::setApplicationName("BootLoader");

    // 产品保护：检查使用期限（2025年10月26日）
    QDate expirationDate(2025, 10, 26);
    QDate currentDate = QDate::currentDate();

    if (currentDate > expirationDate) {
        QMessageBox::critical(nullptr, "产品已过期",
            QString("此产品试用期已于 %1 到期，无法继续使用。\n请联系供应商获取正式版本。")
            .arg(expirationDate.toString("yyyy年MM月dd日")));
        return 1;  // 退出程序
    }

    MainWindow w;
    w.show();
    return a.exec();
}
