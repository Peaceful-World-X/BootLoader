#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QFileDialog>
#include <QHostAddress>
#include <QMessageBox>
#include <QSerialPortInfo>
#include <QStatusBar>
#include <QTextCursor>
#include <QDir>
#include <QCoreApplication>
#include <QLineEdit>
#include <QCheckBox>

#include "communication.h"
#include "protocol.h"
#include "upgrade.h"

namespace Ui {
class MainWindow;
}

class UpgradeManager; // 前向声明

class MainWindow : public QMainWindow
{
    Q_OBJECT // 启用元对象系统

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    // 通信管理器信号槽
    void handleDataReceived(const QByteArray &frame, quint8 slaveId, BootLoaderProtocol::MessageType msgType, BootLoaderProtocol::ResponseFlag flag, const QByteArray &payload);
    void handleSerialError(const QString &errorMessage);
    void handleTcpError(const QString &errorMessage);
    void handleConnectionStateChanged(bool connected);

    // UI按钮槽函数
    void on_pushButton_FPGA_clicked();
    void on_pushButton_DSP1_clicked();
    void on_pushButton_DSP2_clicked();
    void on_pushButton_ARM_clicked();
    void on_pushButton_LJ_clicked();
    void on_pushButton_SJ_clicked();
    void on_link_currentIndexChanged(int index);

    // 升级管理器信号槽
    void onUpgradeProgressUpdated(int currentDevice, int totalDevice);
    void onUpgradeFinished(bool success, const QString &message);

private:
    void populateSerialPorts();
    bool openSerialPort();
    bool openTcpSocket();
    void closeConnection();
    void applyConnectedState(bool connected, const QString &statusText = QString());
    void updateUiForLinkSelection(int index);
    void appendInfoDisplay(const QString &text);
    void writeToLogFile(const QString &text);
    void sendData(const QByteArray &data, const QString &description = QString());
    QString toPrintable(const QByteArray &data) const;
    void selectFirmwareFile(QLineEdit *lineEdit, QCheckBox *checkBox, const QString &title, const QString &filter);
    quint8 getSlaveId() const;

    Ui::MainWindow *ui;
    CommunicationManager *commManager;
    UpgradeManager *upgradeManager;
    bool isConnected;
    QString logFilePath;
};

#endif // MAINWINDOW_H
