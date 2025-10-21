#include "inc/mainwindow.h"
#include "ui_mainwindow.h"

#include <QDateTime>
#include <QMessageBox>
#include <QSerialPortInfo>
#include <QStatusBar>
#include <QTextCursor>

MainWindow::MainWindow(QWidget *parent): QMainWindow(parent), ui(new Ui::MainWindow), isConnected(false)
{
    ui->setupUi(this);

    populateSerialPorts();
    ui->link->setCurrentIndex(0);
    ui->progressBar_DQ->setValue(0);
    ui->progressBar_ZT->setValue(0);

    connect(&serialPort, &QSerialPort::readyRead, this, &MainWindow::handleSerialReadyRead);
    connect(&serialPort, &QSerialPort::errorOccurred, this, &MainWindow::handleSerialError);

    statusBar()->showMessage(tr("未连接串口"));
}

MainWindow::~MainWindow()
{
    if (serialPort.isOpen()) {
        serialPort.close();
    }
    delete ui;
}

// ==========================================================================
void MainWindow::handleConnectButton()
{
    if (isConnected) {
        closeSerialPort();
    } else {
        openSerialPort();
    }
}

void MainWindow::handleUpgradeButton()
{
    QMessageBox::information(this, tr("提示"), tr("升级功能尚未实现。"));
}

void MainWindow::handleSerialReadyRead()
{
    const QByteArray data = serialPort.readAll();
    if (data.isEmpty()) {
        return;
    }

    const QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    appendLog(QString("[%1] 接收到 %2 字节").arg(timestamp).arg(data.size()));
    appendLog(QString("  ASCII: %1").arg(toPrintable(data)));
    appendLog(QString("  HEX: %1").arg(QString::fromLatin1(data.toHex(' ').toUpper())));
}

void MainWindow::handleSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError) {
        return;
    }

    QString errorDetail = serialPort.errorString();
    if (errorDetail.isEmpty()) {
        switch (error) {
        case QSerialPort::DeviceNotFoundError:
            errorDetail = tr("设备未找到");
            break;
        case QSerialPort::PermissionError:
            errorDetail = tr("权限不足或端口被占用");
            break;
        case QSerialPort::OpenError:
            errorDetail = tr("串口打开失败");
            break;
        case QSerialPort::WriteError:
            errorDetail = tr("写入数据失败");
            break;
        case QSerialPort::ReadError:
            errorDetail = tr("读取数据失败");
            break;
        case QSerialPort::ResourceError:
            errorDetail = tr("串口资源不可用或设备被移除");
            break;
        case QSerialPort::UnsupportedOperationError:
            errorDetail = tr("执行了不支持的操作");
            break;
        case QSerialPort::TimeoutError:
            errorDetail = tr("串口操作超时");
            break;
        default:
            errorDetail = tr("未知错误 (代码 %1)").arg(static_cast<int>(error));
            break;
        }
    }

    const QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    const QString message = tr("串口错误：%1").arg(errorDetail);
    appendLog(QString("[%1] %2").arg(timestamp, message));

    if (serialPort.isOpen()) {
        QMessageBox::critical(this, tr("串口错误"), message);
        closeSerialPort();
    }
}

void MainWindow::populateSerialPorts()
{
    const QString previousSelection = ui->portName->currentData().toString();
    ui->portName->clear();

    const auto ports = QSerialPortInfo::availablePorts();
    for (const auto &info : ports) {
        QString label = info.portName();
        if (!info.description().isEmpty()) {
            label += QStringLiteral(" (%1)").arg(info.description());
        }
        ui->portName->addItem(label, info.portName());
    }

    if (ui->portName->count() == 0) {
        appendLog(tr("未检测到可用串口。"));
    } else if (!previousSelection.isEmpty()) {
        const int index = ui->portName->findData(previousSelection);
        if (index >= 0) {
            ui->portName->setCurrentIndex(index);
        }
    }
}

bool MainWindow::openSerialPort()
{
    if (ui->link->currentText() != QStringLiteral("串口")) {
        QMessageBox::information(this, tr("提示"), tr("当前仅支持串口连接，请选择串口方式。"));
        return false;
    }

    if (ui->portName->count() == 0) {
        populateSerialPorts();
    }

    QString portName = ui->portName->currentData().toString();
    if (portName.isEmpty()) {
        portName = ui->portName->currentText().trimmed();
    }

    if (portName.isEmpty()) {
        QMessageBox::warning(this, tr("警告"), tr("没有可用的串口。"));
        return false;
    }

    serialPort.setPortName(portName);

    if (!serialPort.open(QIODevice::ReadWrite)) {
        const QString message = tr("串口打开失败：%1").arg(serialPort.errorString());
        QMessageBox::critical(this, tr("错误"), message);
        appendLog(QString("[%1] %2").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"), message));
        return false;
    }

    auto guardOnFailure = [this]() {
        serialPort.close();
        isConnected = false;
        setSerialControlsEnabled(true);
        ui->pushButton_LJ->setText(tr("连接"));
        statusBar()->showMessage(tr("未连接串口"));
    };

    const int baudRate = ui->baudRate->currentText().toInt();
    if (!serialPort.setBaudRate(baudRate)) {
        const QString message = tr("设置波特率失败：%1").arg(serialPort.errorString());
        guardOnFailure();
        QMessageBox::critical(this, tr("错误"), message);
        appendLog(QString("[%1] %2").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"), message));
        return false;
    }

    const int dataBitsValue = ui->dataBits->currentText().toInt();
    QSerialPort::DataBits dataBits = QSerialPort::Data8;
    switch (dataBitsValue) {
    case 5: dataBits = QSerialPort::Data5; break;
    case 6: dataBits = QSerialPort::Data6; break;
    case 7: dataBits = QSerialPort::Data7; break;
    case 8: dataBits = QSerialPort::Data8; break;
    default: break;
    }
    if (!serialPort.setDataBits(dataBits)) {
        const QString message = tr("设置数据位失败：%1").arg(serialPort.errorString());
        guardOnFailure();
        QMessageBox::critical(this, tr("错误"), message);
        appendLog(QString("[%1] %2").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"), message));
        return false;
    }

    const int stopBitsValue = ui->stopBits->currentText().toInt();
    const QSerialPort::StopBits stopBits = (stopBitsValue == 2) ? QSerialPort::TwoStop : QSerialPort::OneStop;
    if (!serialPort.setStopBits(stopBits)) {
        const QString message = tr("设置停止位失败：%1").arg(serialPort.errorString());
        guardOnFailure();
        QMessageBox::critical(this, tr("错误"), message);
        appendLog(QString("[%1] %2").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"), message));
        return false;
    }

    const QString parityText = ui->parity->currentText();
    QSerialPort::Parity parity = QSerialPort::NoParity;
    if (parityText == QLatin1String("EvenParity")) {
        parity = QSerialPort::EvenParity;
    } else if (parityText == QLatin1String("OddParity")) {
        parity = QSerialPort::OddParity;
    }
    if (!serialPort.setParity(parity)) {
        const QString message = tr("设置校验位失败：%1").arg(serialPort.errorString());
        guardOnFailure();
        QMessageBox::critical(this, tr("错误"), message);
        appendLog(QString("[%1] %2").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"), message));
        return false;
    }

    serialPort.setFlowControl(QSerialPort::NoFlowControl);
    serialPort.clear();

    isConnected = true;
    setSerialControlsEnabled(false);
    ui->pushButton_LJ->setText(tr("断开"));
    const QString successMessage = tr("串口已连接: %1").arg(portName);
    statusBar()->showMessage(successMessage);
    appendLog(QString("[%1] %2").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"), successMessage));

    return true;
}

void MainWindow::closeSerialPort()
{
    QString portName = serialPort.portName();
    if (serialPort.isOpen()) {
        serialPort.close();
    }

    isConnected = false;
    setSerialControlsEnabled(true);
    ui->pushButton_LJ->setText(tr("连接"));
    statusBar()->showMessage(tr("未连接串口"));

    if (!portName.isEmpty()) {
        appendLog(QString("[%1] 已断开 %2").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"), portName));
    } else {
        appendLog(QString("[%1] 已断开串口").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss")));
    }
}

void MainWindow::setSerialControlsEnabled(bool enabled)
{
    ui->portName->setEnabled(enabled);
    ui->baudRate->setEnabled(enabled);
    ui->dataBits->setEnabled(enabled);
    ui->stopBits->setEnabled(enabled);
    ui->parity->setEnabled(enabled);
    ui->link->setEnabled(enabled);
}

void MainWindow::appendLog(const QString &text)
{
    QTextCursor cursor = ui->comLog->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text + '\n');
    ui->comLog->setTextCursor(cursor);
    ui->comLog->ensureCursorVisible();
}

QString MainWindow::toPrintable(const QByteArray &data) const
{
    QString result;
    result.reserve(data.size() * 2);

    for (unsigned char ch : data) {
        if (ch >= 0x20 && ch <= 0x7E) {
            result.append(QChar(ch));
        } else if (ch == '\r') {
            result.append(QStringLiteral("\\r"));
        } else if (ch == '\n') {
            result.append(QStringLiteral("\\n"));
        } else if (ch == '\t') {
            result.append(QStringLiteral("\\t"));
        } else {
            result.append(QStringLiteral("\\x%1").arg(ch, 2, 16, QLatin1Char('0')).toUpper());
        }
    }

    return result;
}

// 自动绑定槽函数处理
void MainWindow::on_pushButton_FPGA_clicked()
{

}

void MainWindow::on_pushButton_DSP1_clicked()
{

}

void MainWindow::on_pushButton_DSP2_clicked()
{

}

void MainWindow::on_pushButton_ARM_clicked()
{

}

void MainWindow::on_pushButton_LJ_clicked()
{

}

void MainWindow::on_pushButton_SJ_clicked()
{

}

void MainWindow::on_link_currentIndexChanged(int index)
{

}

// 其他功能函数
