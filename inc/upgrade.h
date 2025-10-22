#ifndef UPGRADE_H
#define UPGRADE_H

#include <QObject>
#include <QTimer>
#include <QByteArray>
#include <QList>
#include "protocol.h"

class MainWindow; // 前向声明

/**
 * @brief 升级管理器 - 处理固件升级流程
 */
class UpgradeManager : public QObject
{
    Q_OBJECT

public:
    // 升级状态枚举
    enum class UpgradeState {
        IDLE,                    // 空闲状态
        WAIT_UPGRADE_REQUEST,    // 等待升级请求回复
        WAIT_SYSTEM_RESET,       // 等待系统复位回复
        WAIT_UPGRADE_COMMAND,    // 等待升级指令回复
        WAIT_UPGRADE_DATA,       // 等待升级数据回复
        WAIT_UPGRADE_END,        // 等待升级结束回复
        WAIT_TOTAL_END,          // 等待总体结束回复
        UPGRADE_SUCCESS,         // 升级成功
        UPGRADE_FAILED           // 升级失败
    };

    // 设备类型枚举
    enum class DeviceType {
        FPGA,
        DSP1,
        DSP2,
        ARM
    };

    // 固件信息结构
    struct FirmwareInfo {
        QString filePath;
        QByteArray fileData;
        quint32 fileSize;
        quint16 packetCount;
        quint16 fileCRC;
        quint16 currentPacket;
        quint16 packetSize;
        DeviceType deviceType;
    };

    explicit UpgradeManager(MainWindow *parent);
    ~UpgradeManager();

    // 启动升级
    bool startUpgrade(quint8 slaveId, int packetSize,
                     bool upgradeFPGA, bool upgradeDSP1,
                     bool upgradeDSP2, bool upgradeARM,
                     const QString &fpgaPath, const QString &dsp1Path,
                     const QString &dsp2Path, const QString &armPath);

    // 处理接收到的响应
    void handleResponse(BootLoaderProtocol::MessageType msgType,
                       BootLoaderProtocol::ResponseFlag flag,
                       const QByteArray &payload);

    // 获取当前状态
    UpgradeState currentState() const { return upgradeState; }

    // 停止升级
    void stopUpgrade();

signals:
    // 需要发送数据
    void sendData(const QByteArray &data, const QString &description);

    // 显示信息
    void showInfo(const QString &text);

    // 升级进度更新
    void progressUpdated(int currentDevice, int totalDevice);

    // 升级完成
    void upgradeFinished(bool success, const QString &message);

private slots:
    void onTimeout();

private:
    // 准备固件文件
    bool prepareFirmware(int packetSize,
                        bool upgradeFPGA, bool upgradeDSP1,
                        bool upgradeDSP2, bool upgradeARM,
                        const QString &fpgaPath, const QString &dsp1Path,
                        const QString &dsp2Path, const QString &armPath);

    // 发送各个阶段的报文
    void sendUpgradeRequest();
    void sendSystemReset();
    void startDeviceUpgrade(DeviceType device);
    void sendUpgradeCommand();
    void sendUpgradeData();
    void sendUpgradeEnd();
    void sendTotalEnd();

    // 辅助函数
    void upgradeComplete(bool success, const QString &message);
    void resetState();
    void updateProgress();

    MainWindow *mainWindow;
    BootLoaderProtocol protocol;

    // 升级状态
    UpgradeState upgradeState;
    QList<FirmwareInfo> firmwareList;
    int currentFirmwareIndex;
    quint8 slaveId;
    int retryCount;
    QTimer *upgradeTimer;
    int totalPackets;
    int sentPackets;
};

#endif // UPGRADE_H
