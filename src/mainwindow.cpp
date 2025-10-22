#include "inc/mainwindow.h"
#include "ui_mainwindow.h"


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , commManager(new CommunicationManager(this))
    , upgradeManager(new UpgradeManager(this))
    , isConnected(false)
{
    ui->setupUi(this);

    // 固定窗口大小，不允许调整
    setFixedSize(this->size());

    // 设置日志文件路径（主程序同目录）
    QString appDir = QCoreApplication::applicationDirPath();
    logFilePath = appDir + "/bootloader.log";

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

    // 连接升级管理器的信号
    connect(upgradeManager, &UpgradeManager::sendData, this, &MainWindow::sendData);
    connect(upgradeManager, &UpgradeManager::showInfo, this, &MainWindow::appendInfoDisplay);
    connect(upgradeManager, &UpgradeManager::progressUpdated, this, &MainWindow::onUpgradeProgressUpdated);
    connect(upgradeManager, &UpgradeManager::upgradeFinished, this, &MainWindow::onUpgradeFinished);

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
// 辅助函数：计算字符串显示宽度并填充到指定宽度（中文字符算2个宽度）
static QString padString(const QString &str, int targetWidth)
{
    int displayWidth = 0;
    for (const QChar &ch : str) {
        if (ch.unicode() > 0x7F) {  // 非ASCII字符（包括中文）
            displayWidth += 2;
        } else {
            displayWidth += 1;
        }
    }
    int paddingNeeded = targetWidth - displayWidth;
    return str + QString(paddingNeeded > 0 ? paddingNeeded : 0, ' ');
}

void MainWindow::handleDataReceived(const QByteArray &frame, quint8 slaveId, BootLoaderProtocol::MessageType msgType, BootLoaderProtocol::ResponseFlag flag, const QByteArray &payload)
{
    const QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    const QString typeDesc = BootLoaderProtocol::getMessageTypeDescription(msgType);
    const QString flagDesc = BootLoaderProtocol::getResponseDescription(flag);

    // 如果勾选了日志记录，写入详细信息到文件
    if (ui->checkBox_log->isChecked()) {
        // 限制显示前20个字节
        QByteArray displayData = frame.size() > 20 ? frame.left(20) : frame;
        QString hexData = QString::fromLatin1(displayData.toHex(' ').toUpper());
        if (frame.size() > 20) {
            hexData += " ...";
        }

        // 格式: [时间] | RX | ID=xx | TYPE=类型描述 | FLAG=标志描述 | DATA=十六进制
        // 手动填充以支持中文对齐（TYPE=24字符宽度，FLAG=34字符宽度）
        QString typePadded = padString(typeDesc, 24);
        QString flagPadded = padString(flagDesc, 34);

        QString logLine = QString("[%1] | RX | ID=%2 | TYPE=%3 | FLAG=%4 | DATA=%5")
            .arg(timestamp)
            .arg(slaveId, 2, 10, QChar('0'))
            .arg(typePadded)
            .arg(flagPadded)
            .arg(hexData);
        writeToLogFile(logLine);
    }

    // 如果正在升级流程中，处理响应（包括调试报文，用于重置超时计时器）
    if (upgradeManager->currentState() != UpgradeManager::UpgradeState::IDLE) {
        upgradeManager->handleResponse(msgType, flag, payload);
    }
}

void MainWindow::handleSerialError(const QString &errorMessage)
{
    const QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    const QString message = tr("串口错误：%1").arg(errorMessage);
    appendInfoDisplay(message);
    statusBar()->showMessage(message);
    ui->pushButton_LJ->setEnabled(true);
    QMessageBox::critical(this, tr("串口错误"), message);
}

void MainWindow::handleTcpError(const QString &errorMessage)
{
    const QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    const QString message = tr("网口错误：%1").arg(errorMessage);
    appendInfoDisplay(message);
    statusBar()->showMessage(message);
    ui->pushButton_LJ->setEnabled(true);
    QMessageBox::critical(this, tr("网口错误"), message);
}

void MainWindow::handleConnectionStateChanged(bool connected)
{
    const bool wasConnected = isConnected;
    isConnected = connected;
    ui->pushButton_LJ->setEnabled(true);

    const QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");

    if (connected) {
        QString statusMessage;
        if (commManager->getActiveLink() == CommunicationManager::LinkType::Serial) {
            QString portLabel = ui->portName->currentText();
            if (portLabel.isEmpty()) {
                portLabel = ui->portName->currentData().toString();
            }
            if (portLabel.isEmpty()) {
                portLabel = tr("串口");
            }
            statusMessage = tr("串口已连接: %1").arg(portLabel);
        } else {
            statusMessage = tr("网口已连接: %1:%2")
                                .arg(ui->lineEdit_IP->text().trimmed())
                                .arg(ui->lineEdit_port->text().trimmed());
        }

        appendInfoDisplay(statusMessage);
        applyConnectedState(true, statusMessage);
    } else {
        const QString statusMessage = wasConnected ? tr("连接已断开") : tr("连接未建立");
        appendInfoDisplay(statusMessage);
        applyConnectedState(false, statusMessage);
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
    if (parityText.contains(QLatin1String("Even"), Qt::CaseInsensitive) || parityText.contains(QString("偶"))) {
        parity = QSerialPort::EvenParity;
    } else if (parityText.contains(QLatin1String("Odd"), Qt::CaseInsensitive) || parityText.contains(QString("奇"))) {
        parity = QSerialPort::OddParity;
    } else if (parityText.contains(QLatin1String("Space"), Qt::CaseInsensitive)) {
        parity = QSerialPort::SpaceParity;
    } else if (parityText.contains(QLatin1String("Mark"), Qt::CaseInsensitive)) {
        parity = QSerialPort::MarkParity;
    }

    // 使用通信管理器打开串口
    if (commManager->openSerialPort(portName, baudRate, dataBits, stopBits, parity)) {
        const QString message = tr("正在连接串口: %1").arg(portName);
        appendInfoDisplay(QStringLiteral("[%1] %2")
                              .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"), message));
        statusBar()->showMessage(message);
        return true;
    }

    statusBar()->showMessage(tr("串口连接失败"));
    return false;
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
        const QString message = tr("正在连接: %1:%2").arg(address.toString()).arg(port);
        appendInfoDisplay(QStringLiteral("[%1] %2")
                              .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"), message));
        statusBar()->showMessage(message);
        return true;
    }

    statusBar()->showMessage(tr("网口连接失败"));
    return false;
}

// 关闭当前连接
void MainWindow::closeConnection()
{
    commManager->closeSerialPort();
    commManager->closeTcpConnection();
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
        // 如果勾选了日志记录,写入文件（但不显示到界面）
        if (ui->checkBox_log->isChecked()) {
            QByteArray displayData = data.size() > 20 ? data.left(20) : data;   // 限制显示前20个字节
            QString hexData = QString::fromLatin1(displayData.toHex(' ').toUpper());
            if (data.size() > 20) {
                hexData += " ...";
            }
            QString typeStr = description.isEmpty() ? tr("数据") : description;
            // 尝试从数据中解析ID（假设第3个字节是ID）
            quint8 deviceId = 0;
            if (data.size() >= 3) {
                deviceId = static_cast<quint8>(data[2]);
            }

            // 格式: [时间] | TX | ID=xx | TYPE=描述 | FLAG=(空) | DATA=十六进制
            // 手动填充以支持中文对齐（TYPE=24字符宽度，FLAG=34字符宽度）
            QString typePadded = padString(typeStr, 24);
            QString flagPadded = padString("", 34);

            QString logLine = QString("[%1] | TX | ID=%2 | TYPE=%3 | FLAG=%4 | DATA=%5")
                .arg(timestamp)
                .arg(deviceId, 2, 10, QChar('0'))
                .arg(typePadded)
                .arg(flagPadded)
                .arg(hexData);
            writeToLogFile(logLine);
        }
    } else {
        appendInfoDisplay(tr("发送失败"));
        if (ui->checkBox_log->isChecked()) {
            writeToLogFile(QString("[%1] | TX | 发送失败").arg(timestamp));
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

QString MainWindow::toPrintable(const QByteArray &data) const
{
    if (data.isEmpty()) {
        return tr("无数据");
    }

    static constexpr int maxBytes = 64;
    QString result;
    result.reserve(qMin(data.size(), maxBytes) * 3);

    int count = 0;
    for (unsigned char ch : data) {
        if (count >= maxBytes) {
            result.append(QStringLiteral(" ..."));
            break;
        }

        if (ch >= 0x20 && ch <= 0x7E) {
            result.append(QChar(ch));
        } else if (ch == '\r') {
            result.append(QStringLiteral("\\r"));
        } else if (ch == '\n') {
            result.append(QStringLiteral("\\n"));
        } else if (ch == '\t') {
            result.append(QStringLiteral("\\t"));
        } else {
            result.append(QStringLiteral("\\x%1")
                              .arg(static_cast<quint8>(ch), 2, 16, QLatin1Char('0'))
                              .toUpper());
        }

        ++count;
    }

    return result;
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
    ui->lineEdit_mode->setEnabled(serialSelected && !isConnected);

    ui->lineEdit_IP->setEnabled(!serialSelected && !isConnected);
    ui->lineEdit_port->setEnabled(!serialSelected && !isConnected);

    ui->lineEdit_size->setEnabled(!isConnected);

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

// 获取从机ID
quint8 MainWindow::getSlaveId() const
{
    if (ui->link->currentIndex() == 0) {
        // 串口模式，从mode获取
        bool ok = false;
        int id = ui->lineEdit_mode->text().toInt(&ok);
        return ok ? static_cast<quint8>(id) : 1;
    } else {
        // 网口模式，从IP最后一段获取
        QString ip = ui->lineEdit_IP->text();
        QStringList parts = ip.split('.');
        if (parts.size() == 4) {
            bool ok = false;
            int lastByte = parts[3].toInt(&ok);
            if (ok && lastByte >= 0 && lastByte <= 255) {
                return static_cast<quint8>(lastByte);
            }
        }
        return 1; // 默认值
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

    ui->pushButton_LJ->setEnabled(false);

    const bool serialSelected = (ui->link->currentIndex() == 0);
    const bool opened = serialSelected ? openSerialPort() : openTcpSocket();
    if (!opened) {
        applyConnectedState(false, tr("未连接"));
        ui->pushButton_LJ->setEnabled(true);
    }
}

// 升级
void MainWindow::on_pushButton_SJ_clicked()
{
    if (!isConnected) {
        QMessageBox::warning(this, tr("升级"), tr("请先连接设备！"));
        return;
    }

    // 获取数据包大小
    bool ok = false;
    int packetSize = ui->lineEdit_size->text().toInt(&ok);

    // 获取从机ID
    quint8 slaveId = getSlaveId();

    // 开始升级流程
    bool started = upgradeManager->startUpgrade(
        slaveId, packetSize,
        ui->checkBox_FPGA->isChecked(), ui->checkBox_DSP1->isChecked(),
        ui->checkBox_DSP2->isChecked(), ui->checkBox_ARM->isChecked(),
        ui->lineEdit_FPGA->text(), ui->lineEdit_DSP1->text(),
        ui->lineEdit_DSP2->text(), ui->lineEdit_ARM->text()
    );

    if (started) {
        ui->pushButton_SJ->setEnabled(false);
        ui->progressBar_DQ->setValue(0);
        ui->progressBar_ZT->setValue(0);
    }
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

// 日志窗口清屏
void MainWindow::on_pushButton_clicked()
{
    ui->info_display->setText("");
}


/* =============================== 升级管理器信号槽 ======================================= */
//升级进度更新
void MainWindow::onUpgradeProgressUpdated(int currentDevice, int totalDevice)
{
    ui->progressBar_DQ->setValue(currentDevice);
    ui->progressBar_ZT->setValue(totalDevice);
}

// 升级完成
void MainWindow::onUpgradeFinished(bool success, const QString &message)
{
    ui->pushButton_SJ->setEnabled(true);

    if (success) {
        ui->progressBar_ZT->setValue(100);
        statusBar()->showMessage(tr("升级成功"));
        QMessageBox::information(this, tr("升级"), tr("升级完成！\n%1").arg(message));
    } else {
        statusBar()->showMessage(tr("升级失败"));
        QMessageBox::critical(this, tr("升级失败"), message);
    }
}
