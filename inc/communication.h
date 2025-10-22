#ifndef COMMUNICATIONMANAGER_H
#define COMMUNICATIONMANAGER_H

#include <QObject>
#include <QSerialPort>
#include <QTcpSocket>
#include <QByteArray>
#include <QString>

#include "protocol.h"

class CommunicationManager : public QObject
{
    Q_OBJECT

public:
    enum class LinkType {
        Serial,
        Ethernet
    };

    explicit CommunicationManager(QObject *parent = nullptr);
    ~CommunicationManager();

    // 串口操作
    bool openSerialPort(const QString &portName, qint32 baudRate,
                       QSerialPort::DataBits dataBits,
                       QSerialPort::StopBits stopBits,
                       QSerialPort::Parity parity);
    void closeSerialPort();
    bool isSerialPortOpen() const;

    // 网口操作
    bool openTcpConnection(const QString &host, quint16 port);
    void closeTcpConnection();
    bool isTcpConnected() const;

    // 发送数据
    qint64 sendData(const QByteArray &data);

    // 获取当前连接类型
    LinkType getActiveLink() const { return activeLink; }
    void setActiveLink(LinkType type) { activeLink = type; }

    // 协议访问
    BootLoaderProtocol& getProtocol() { return protocol; }

signals:
    // 数据接收信号
    void dataReceived(const QByteArray &frame, quint8 slaveId,
                     BootLoaderProtocol::MessageType msgType,
                     BootLoaderProtocol::ResponseFlag flag,
                     const QByteArray &payload);

    // 错误信号
    void serialError(const QString &errorMessage);
    void tcpError(const QString &errorMessage);

    // 连接状态变化
    void connectionStateChanged(bool connected);

private slots:
    // 串口数据接收
    void handleSerialReadyRead();
    void handleSerialError(QSerialPort::SerialPortError error);

    // 网口数据接收
    void handleTcpReadyRead();
    void handleTcpError(QAbstractSocket::SocketError error);

private:
    QSerialPort serialPort;
    QTcpSocket tcpSocket;
    BootLoaderProtocol protocol;
    LinkType activeLink;

    // 处理接收到的数据（共用逻辑）
    void processReceivedData(const QByteArray &data);
};

#endif // COMMUNICATIONMANAGER_H
