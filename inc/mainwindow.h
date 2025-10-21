#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSerialPort>

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void handleConnectButton();
    void handleUpgradeButton();
    void handleSerialReadyRead();
    void handleSerialError(QSerialPort::SerialPortError error);

    void on_pushButton_FPGA_clicked();

    void on_pushButton_DSP1_clicked();

    void on_pushButton_DSP2_clicked();

    void on_pushButton_ARM_clicked();

    void on_pushButton_LJ_clicked();

    void on_pushButton_SJ_clicked();

    void on_link_currentIndexChanged(int index);

private:
    void populateSerialPorts();
    bool openSerialPort();
    void closeSerialPort();
    void setSerialControlsEnabled(bool enabled);
    void appendLog(const QString &text);
    QString toPrintable(const QByteArray &data) const;

    Ui::MainWindow *ui;
    QSerialPort serialPort;
    bool isConnected;
};

#endif // MAINWINDOW_H
