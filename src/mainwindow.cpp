#include "inc/mainwindow.h"
#include "ui_mainwindow.h"


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , commManager(new CommunicationManager(this))
    , isConnected(false)
{
    ui->setupUi(this);

    // 固定窗口大小，不允许调整
    setFixedSize(this->size());

    // 设置日志文件路径（主程序同目录）
    QString appDir = QCoreApplication::applicationDirPath();
    logFilePath = appDir + "/bootloader_log.txt";

    // 枚举可用串口
    populateSerialPorts();
    ui->link->setCurrentIndex(0);
    ui->progressBar_DQ->setValue(0);
    ui->progressBar_ZT->setValue(0);

    // 连接通信管理器的信号
    connect(commManager, &CommunicationManager::dataReceived, this, &MainWindow::handleDataReceived);
    connect(commManager, &CommunicationManager::serialError, this, &MainWindow::handleSerialError);
    connect(commManager, &CommunicationManager::tcpError, this, &MainWindow::handleTcpError);
    connect(commManager, &CommunicationManager::connectionStateChanged, this, &MainWindow::handleConnectionStateChanged);

    updateUiForLinkSelection(ui->link->currentIndex());
    statusBar()->showMessage(tr("未连接"));

    // 在日志文件中记录程序启动
    writeToLogFile(QString("========================================"));
    writeToLogFile(QString("程序启动: %1").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss")));
    writeToLogFile(QString("========================================"));
}

MainWindow::~MainWindow()
{
    closeConnection();
    delete ui;
}

/* ===============================  通信函数 ======================================= */
void MainWindow::handleDataReceived(const QByteArray &frame, quint8 slaveId,  BootLoaderProtocol::MessageType, BootLoaderProtocol::ResponseFlag flag, const QByteArray &/*payload*/)
{
    const QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");

    // info_display 只显示 ResponseFlag 的描述
    QString flagStr = BootLoaderProtocol::getResponseDescription(flag);
    appendInfoDisplay(QStringLiteral("[%1] 接收: %2").arg(timestamp, flagStr));

    // 如果勾选了日志记录，写入详细信息
    if (ui->checkBox_log->isChecked()) {
        // 限制显示前20个字节
        QByteArray displayData = frame.size() > 20 ? frame.left(20) : frame;
        QString hexData = QString::fromLatin1(displayData.toHex(' ').toUpper());
        if (frame.size() > 20) {
            hexData += " ...";
        }

        // 格式: [时间] | RX | ID=xx | TYPE=描述 | DATA=十六进制
        writeToLogFile(QStringLiteral("[%1] | RX | ID=%2 | TYPE=%3 | DATA=%4")
            .arg(timestamp)
            .arg(slaveId, 2, 10, QLatin1Char('0'))
            .arg(flagStr.leftJustified(16, ' ', true))
            .arg(hexData));
    }
}

void MainWindow::handleSerialError(const QString &errorMessage)
{
    const QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    const QString message = tr("串口错误：%1").arg(errorMessage);
    appendInfoDisplay(QStringLiteral("[%1] 错误: %2").arg(timestamp, message));
    QMessageBox::critical(this, tr("串口错误"), message);
}

void MainWindow::handleTcpError(const QString &errorMessage)
{
    const QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    const QString message = tr("网口错误：%1").arg(errorMessage);
    appendInfoDisplay(QStringLiteral("[%1] 错误: %2").arg(timestamp, message));
    QMessageBox::critical(this, tr("网口错误"), message);
}

void MainWindow::handleConnectionStateChanged(bool connected)
{
    isConnected = connected;
    if (!connected) {
        applyConnectedState(false, tr("未连接"));
    }
}

// 枚举可用串口
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
        appendInfoDisplay(tr("未检测到可用串口"));
    } else if (!previousSelection.isEmpty()) {
        const int index = ui->portName->findData(previousSelection);
        if (index >= 0) {
            ui->portName->setCurrentIndex(index);
        }
    }
}

// 打开串口连接
bool MainWindow::openSerialPort()
{
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

    // 从UI获取串口参数
    const int baudRate = ui->baudRate->currentText().toInt();

    const QString dataBitsText = ui->dataBits->currentText();
    QSerialPort::DataBits dataBits = QSerialPort::Data8;
    if (dataBitsText.contains(QLatin1String("5"))) {
        dataBits = QSerialPort::Data5;
    } else if (dataBitsText.contains(QLatin1String("6"))) {
        dataBits = QSerialPort::Data6;
    } else if (dataBitsText.contains(QLatin1String("7"))) {
        dataBits = QSerialPort::Data7;
    }

    const QString stopBitsText = ui->stopBits->currentText();
    QSerialPort::StopBits stopBits = QSerialPort::OneStop;
    if (stopBitsText.contains(QLatin1String("1.5"))) {
        stopBits = QSerialPort::OneAndHalfStop;
    } else if (stopBitsText.contains(QLatin1String("2"))) {
        stopBits = QSerialPort::TwoStop;
    }

    const QString parityText = ui->parity->currentText();
    QSerialPort::Parity parity = QSerialPort::NoParity;
    if (parityText.contains(QLatin1String("Even"), Qt::CaseInsensitive) || parityText.contains(QStringLiteral("偶"))) {
        parity = QSerialPort::EvenParity;
    } else if (parityText.contains(QLatin1String("Odd"), Qt::CaseInsensitive) || parityText.contains(QStringLiteral("奇"))) {
        parity = QSerialPort::OddParity;
    } else if (parityText.contains(QLatin1String("Space"), Qt::CaseInsensitive)) {
        parity = QSerialPort::SpaceParity;
    } else if (parityText.contains(QLatin1String("Mark"), Qt::CaseInsensitive)) {
        parity = QSerialPort::MarkParity;
    }

    // 使用通信管理器打开串口
    if (commManager->openSerialPort(portName, baudRate, dataBits, stopBits, parity)) {
        const QString message = tr("串口已连接: %1").arg(portName);
        appendInfoDisplay(QStringLiteral("[%1] %2").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"), message));
        applyConnectedState(true, message);
        return true;
    } else {
        return false;
    }
}

// 打开网口连接
bool MainWindow::openTcpSocket()
{
    const QString host = ui->lineEdit_IP->text().trimmed();
    const QString portText = ui->lineEdit_port->text().trimmed();

    QHostAddress address;
    if (!address.setAddress(host)) {
        QMessageBox::warning(this, tr("警告"), tr("IP 地址无效。"));
        return false;
    }

    bool ok = false;
    const quint16 port = portText.toUShort(&ok);
    if (!ok || port == 0) {
        QMessageBox::warning(this, tr("警告"), tr("端口号无效。"));
        return false;
    }

    // 使用通信管理器打开网口
    if (commManager->openTcpConnection(host, port)) {
        const QString message = tr("网口已连接: %1:%2").arg(address.toString()).arg(port);
        appendInfoDisplay(QStringLiteral("[%1] %2").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"), message));
        applyConnectedState(true, message);
        return true;
    } else {
        return false;
    }
}

// 关闭当前连接
void MainWindow::closeConnection()
{
    if (!isConnected) {
        return;
    }

    // 使用通信管理器关闭连接
    if (commManager->getActiveLink() == CommunicationManager::LinkType::Serial) {
        commManager->closeSerialPort();
        const QString message = tr("串口已断开");
        appendInfoDisplay(QStringLiteral("[%1] %2").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"), message));
        applyConnectedState(false, message);
    } else {
        commManager->closeTcpConnection();
        const QString message = tr("网口已断开");
        appendInfoDisplay(QStringLiteral("[%1] %2").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"), message));
        applyConnectedState(false, message);
    }
}

// 发送数据（串口或网口）
void MainWindow::sendData(const QByteArray &data, const QString &description)
{
    if (!isConnected || data.isEmpty()) {
        return;
    }

    const QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");

    // 使用通信管理器发送数据
    qint64 bytesWritten = commManager->sendData(data);

    if (bytesWritten > 0) {
        // 显示到 info_display
        if (!description.isEmpty()) {
            appendInfoDisplay(QStringLiteral("[%1] 发送: %2").arg(timestamp, description));
        }

        // 如果勾选了日志记录，写入文件
        if (ui->checkBox_log->isChecked()) {
            QByteArray displayData = data.size() > 20 ? data.left(20) : data;   // 限制显示前20个字节
            QString hexData = QString::fromLatin1(displayData.toHex(' '));
            if (data.size() > 20) {
                hexData += " ...";
            }
            QString typeStr = description.isEmpty() ? tr("数据") : description;
            // 尝试从数据中解析ID（假设第3个字节是ID）
            quint8 deviceId = 0;
            if (data.size() >= 3) {
                deviceId = static_cast<quint8>(data[2]);
            }

            // 格式: [时间] | TX | ID=xx | TYPE=描述 | DATA=十六进制
            writeToLogFile(QStringLiteral("[%1] | TX | ID=%2 | TYPE=%3 | DATA=%4")
                .arg(timestamp)
                .arg(deviceId, 2, 10, QLatin1Char('0'))
                .arg(typeStr.leftJustified(16, ' ', true))
                .arg(hexData));
        }
    } else {
        appendInfoDisplay(QStringLiteral("[%1] 发送失败").arg(timestamp));
        if (ui->checkBox_log->isChecked()) {
            writeToLogFile(QStringLiteral("[%1] | TX | 发送失败").arg(timestamp));
        }
    }
}


/* ===============================  功能函数 ======================================= */
// 固件文件选择函数
void MainWindow::selectFirmwareFile(QLineEdit *lineEdit, QCheckBox *checkBox, const QString &title, const QString &filter)
{
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        title,
        lineEdit->text(),
        filter);

    if (!filePath.isEmpty()) {
        lineEdit->setText(filePath);
        checkBox->setChecked(true);
    }
}

// 根据连接状态，刷新按键文本
void MainWindow::applyConnectedState(bool connected, const QString &statusText)
{
    isConnected = connected;
    ui->pushButton_LJ->setText(connected ? tr("断开") : tr("连接"));
    ui->link->setEnabled(!connected);
    updateUiForLinkSelection(ui->link->currentIndex());

    if (!statusText.isEmpty()) {
        statusBar()->showMessage(statusText);
    } else {
        statusBar()->showMessage(connected ? tr("已连接") : tr("未连接"));
    }
}

// 根据连接状态，关闭某些控件
void MainWindow::updateUiForLinkSelection(int index)
{
    const bool serialSelected = (index == 0);
    if (!isConnected) {
        commManager->setActiveLink(serialSelected ? CommunicationManager::LinkType::Serial : CommunicationManager::LinkType::Ethernet);
    }

    ui->portName->setEnabled(serialSelected && !isConnected);
    ui->baudRate->setEnabled(serialSelected && !isConnected);
    ui->dataBits->setEnabled(serialSelected && !isConnected);
    ui->stopBits->setEnabled(serialSelected && !isConnected);
    ui->parity->setEnabled(serialSelected && !isConnected);
    ui->mode->setEnabled(serialSelected && !isConnected);

    ui->lineEdit_IP->setEnabled(!serialSelected && !isConnected);
    ui->lineEdit_port->setEnabled(!serialSelected && !isConnected);

    ui->size->setEnabled(!isConnected);

    if (serialSelected && ui->portName->count() == 0) {
        populateSerialPorts();  // 枚举可用串口
    }
}

// 窗口追加信息
void MainWindow::appendInfoDisplay(const QString &text)
{
    QTextCursor cursor = ui->info_display->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text + QLatin1Char('\n'));
    ui->info_display->setTextCursor(cursor);
    ui->info_display->ensureCursorVisible();
}

// 写入日志文件
void MainWindow::writeToLogFile(const QString &text)
{
    if (!ui->checkBox_log->isChecked()) {
        return;
    }
    QFile file(logFilePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        out << text << "\n";
        file.close();
    }
}


/* ===============================  按键函数 ======================================= */
// 选择 FPGA 固件文件
void MainWindow::on_pushButton_FPGA_clicked()
{
    selectFirmwareFile(ui->lineEdit_FPGA, ui->checkBox_FPGA,
                      tr("选择 FPGA 文件"),
                      tr("FPGA 文件 (*.rbf *.bin);;所有文件 (*.*)"));
}

// 选择 DSP1 固件文件
void MainWindow::on_pushButton_DSP1_clicked()
{
    selectFirmwareFile(ui->lineEdit_DSP1, ui->checkBox_DSP1,
                      tr("选择 DSP1 文件"),
                      tr("DSP 文件 (*.hex *.bin);;所有文件 (*.*)"));
}

// 选择 DSP2 固件文件
void MainWindow::on_pushButton_DSP2_clicked()
{
    selectFirmwareFile(ui->lineEdit_DSP2, ui->checkBox_DSP2,
                      tr("选择 DSP2 文件"),
                      tr("DSP 文件 (*.hex *.bin);;所有文件 (*.*)"));
}

// 选择 ARM 固件文件
void MainWindow::on_pushButton_ARM_clicked()
{
    selectFirmwareFile(ui->lineEdit_ARM, ui->checkBox_ARM,
                      tr("选择 ARM 文件"),
                      tr("ARM 文件 (*.hex *.bin);;所有文件 (*.*)"));
}

// 连接、断开
void MainWindow::on_pushButton_LJ_clicked()
{
    if (isConnected) {
        closeConnection();
        return;
    }
    const bool serialSelected = (ui->link->currentIndex() == 0);
    const bool opened = serialSelected ? openSerialPort() : openTcpSocket();
    if (!opened) {
        applyConnectedState(false, tr("未连接"));
    }
}

// 升级
void MainWindow::on_pushButton_SJ_clicked()
{

}

// 通信方式选择
void MainWindow::on_link_currentIndexChanged(int index)
{
    if (isConnected) {
        return;
    }
    updateUiForLinkSelection(index);
    if (index == 0) {
        statusBar()->showMessage(tr("已选择串口模式"));
    } else {
        statusBar()->showMessage(tr("已选择网口模式"));
    }
}
