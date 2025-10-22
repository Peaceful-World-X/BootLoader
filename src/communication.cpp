#include "inc/communication.h"
#include <QDateTime>

CommunicationManager::CommunicationManager(QObject *parent)
    : QObject(parent)
    , serialPort()
    , tcpSocket()
    , protocol()
    , activeLink(LinkType::Serial)
{
    // 连接串口信号
    connect(&serialPort, &QSerialPort::readyRead, this, &CommunicationManager::handleSerialReadyRead);
    connect(&serialPort, &QSerialPort::errorOccurred, this, &CommunicationManager::handleSerialError);

    // 连接网口信号
    connect(&tcpSocket, &QTcpSocket::connected, this, &CommunicationManager::handleTcpConnected);
    connect(&tcpSocket, &QTcpSocket::readyRead, this, &CommunicationManager::handleTcpReadyRead);
    connect(&tcpSocket, &QTcpSocket::errorOccurred, this, &CommunicationManager::handleTcpError);
}

CommunicationManager::~CommunicationManager()
{
    closeSerialPort();
    closeTcpConnection();
}

// ========================================================================
// 串口操作
// ========================================================================

bool CommunicationManager::openSerialPort(const QString &portName, qint32 baudRate, QSerialPort::DataBits dataBits, QSerialPort::StopBits stopBits, QSerialPort::Parity parity)
{
    if (serialPort.isOpen()) {
        closeSerialPort();
    }

    serialPort.setPortName(portName);
    serialPort.setBaudRate(baudRate);
    serialPort.setDataBits(dataBits);
    serialPort.setStopBits(stopBits);
    serialPort.setParity(parity);
    serialPort.setFlowControl(QSerialPort::NoFlowControl);

    if (serialPort.open(QIODevice::ReadWrite)) {
        activeLink = LinkType::Serial;
        emit connectionStateChanged(true);
        return true;
    } else {
        emit serialError(serialPort.errorString());
        return false;
    }
}

void CommunicationManager::closeSerialPort()
{
    if (serialPort.isOpen()) {
        serialPort.clear();
        serialPort.close();
        emit connectionStateChanged(false);
    }
}

bool CommunicationManager::isSerialPortOpen() const
{
    return serialPort.isOpen();
}

// ========================================================================
// 网口操作
// ========================================================================

bool CommunicationManager::openTcpConnection(const QString &host, quint16 port)
{
    if (tcpSocket.state() == QAbstractSocket::ConnectedState ||
        tcpSocket.state() == QAbstractSocket::ConnectingState) {
        tcpSocket.abort();
    }

    tcpSocket.connectToHost(host, port);
    return true; // 连接结果通过信号异步通知
}

void CommunicationManager::closeTcpConnection()
{
    if (tcpSocket.state() == QAbstractSocket::ConnectedState) {
        tcpSocket.disconnectFromHost();
        if (tcpSocket.state() != QAbstractSocket::UnconnectedState) {
            tcpSocket.waitForDisconnected(1000);
        }
        emit connectionStateChanged(false);
    } else if (tcpSocket.state() == QAbstractSocket::ConnectingState) {
        tcpSocket.abort();
        emit connectionStateChanged(false);
    }
}

bool CommunicationManager::isTcpConnected() const
{
    return tcpSocket.state() == QAbstractSocket::ConnectedState;
}

// ========================================================================
// 串口数据接收处理
// ========================================================================

void CommunicationManager::handleSerialReadyRead()
{
    const QByteArray data = serialPort.readAll();
    if (!data.isEmpty()) {
        processReceivedData(data);
    }
}

void CommunicationManager::handleSerialError(QSerialPort::SerialPortError error)
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

    emit serialError(errorDetail);
    closeSerialPort();
}

// ========================================================================
// 网口数据接收处理
// ========================================================================

void CommunicationManager::handleTcpConnected()
{
    activeLink = LinkType::Ethernet;
    emit connectionStateChanged(true);
}

void CommunicationManager::handleTcpReadyRead()
{
    const QByteArray data = tcpSocket.readAll();
    if (!data.isEmpty()) {
        processReceivedData(data);
    }
}

void CommunicationManager::handleTcpError(QAbstractSocket::SocketError error)
{
    if (error == QAbstractSocket::UnknownSocketError && tcpSocket.errorString().isEmpty()) {
        return;
    }

    const QString errorDetail = tcpSocket.errorString();
    emit tcpError(errorDetail);

    if (tcpSocket.state() == QAbstractSocket::ConnectedState ||
        error == QAbstractSocket::RemoteHostClosedError) {
        closeTcpConnection();
    } else {
        emit connectionStateChanged(false);
    }
}

// ========================================================================
// 接收数据处理（共用逻辑）
// ========================================================================

void CommunicationManager::processReceivedData(const QByteArray &data)
{
    // 使用协议解析接收到的数据
    QList<QByteArray> frames = protocol.parseReceivedData(data);

    for (const QByteArray &frame : frames) {
        quint8 slaveId;
        BootLoaderProtocol::MessageType msgType;
        BootLoaderProtocol::ResponseFlag flag;
        QByteArray payload;

        if (protocol.parseFrame(frame, slaveId, msgType, flag, payload)) {
            // 发送信号，让UI层处理
            emit dataReceived(frame, slaveId, msgType, flag, payload);
        }
    }
}

// ========================================================================
// 数据发送（共用逻辑）
// ========================================================================

qint64 CommunicationManager::sendData(const QByteArray &data)
{
    if (data.isEmpty()) {
        return 0;
    }

    qint64 bytesWritten = 0;

    if (activeLink == LinkType::Serial && serialPort.isOpen()) {
        bytesWritten = serialPort.write(data);
        serialPort.flush();
    } else if (activeLink == LinkType::Ethernet && tcpSocket.state() == QAbstractSocket::ConnectedState) {
        bytesWritten = tcpSocket.write(data);
        tcpSocket.flush();
    }

    return bytesWritten;
}