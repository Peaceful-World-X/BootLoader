#include "inc/upgrade.h"
#include "inc/mainwindow.h"
#include <QFile>
#include <QMessageBox>
#include <limits>

UpgradeManager::UpgradeManager(MainWindow *parent)
    : QObject(parent)
    , mainWindow(parent)
    , upgradeState(UpgradeState::IDLE)
    , currentFirmwareIndex(-1)
    , slaveId(0)
    , retryCount(0)
    , upgradeTimer(new QTimer(this))
    , totalPackets(0)
    , sentPackets(0)
{
    // 设置10秒超时
    upgradeTimer->setInterval(10000);
    connect(upgradeTimer, &QTimer::timeout, this, &UpgradeManager::onTimeout);
}

UpgradeManager::~UpgradeManager()
{
    stopUpgrade();
}

/**
 * @brief 启动升级流程
 */
bool UpgradeManager::startUpgrade(quint8 slaveId, int packetSize, bool upgradeFPGA, bool upgradeDSP1, bool upgradeDSP2, bool upgradeARM, const QString &fpgaPath, const QString &dsp1Path, const QString &dsp2Path, const QString &armPath)
{
    if (upgradeState != UpgradeState::IDLE) {
        emit showInfo(tr(">>> 升级正在进行中..."));
        return false;
    }

    // 准备固件文件
    if (!prepareFirmware(packetSize, upgradeFPGA, upgradeDSP1, upgradeDSP2, upgradeARM,
                        fpgaPath, dsp1Path, dsp2Path, armPath)) {
        return false;
    }

    // 保存从机ID
    this->slaveId = slaveId;
    currentFirmwareIndex = -1;

    emit showInfo(tr("========================================"));
    emit showInfo(tr(">>> 开始升级流程"));

    // 发送升级请求
    sendUpgradeRequest();

    return true;
}

/**
 * @brief 准备固件文件
 */
bool UpgradeManager::prepareFirmware(int packetSize, bool upgradeFPGA, bool upgradeDSP1, bool upgradeDSP2, bool upgradeARM, const QString &fpgaPath, const QString &dsp1Path, const QString &dsp2Path, const QString &armPath)
{
    firmwareList.clear();
    totalPackets = 0;
    sentPackets = 0;

    if (packetSize <= 0 || packetSize > 4096) {
        emit showInfo(tr(">>> 错误：数据包大小无效！"));
        return false;
    }

    // 按照升级顺序：FPGA -> DSP1 -> DSP2 -> ARM
    struct DeviceConfig {
        DeviceType type;
        bool enabled;
        QString path;
        QString name;
    };

    QList<DeviceConfig> devices = {
        {DeviceType::FPGA, upgradeFPGA, fpgaPath, "FPGA"},
        {DeviceType::DSP1, upgradeDSP1, dsp1Path, "DSP1"},
        {DeviceType::DSP2, upgradeDSP2, dsp2Path, "DSP2"},
        {DeviceType::ARM, upgradeARM, armPath, "ARM"}
    };

    for (const auto &dev : devices) {
        if (!dev.enabled) {
            continue;
        }

        if (dev.path.isEmpty()) {
            emit showInfo(tr(">>> 错误：请选择 %1 固件文件！").arg(dev.name));
            return false;
        }

        QFile file(dev.path);
        if (!file.open(QIODevice::ReadOnly)) {
            emit showInfo(tr(">>> 错误：无法打开 %1 固件文件：%2").arg(dev.name, file.errorString()));
            return false;
        }

        FirmwareInfo info;
        info.filePath = dev.path;
        info.fileData = file.readAll();
        info.fileSize = info.fileData.size();
        info.deviceType = dev.type;
        info.currentPacket = 0;

        if (info.fileSize == 0) {
            emit showInfo(tr(">>> 错误：%1 固件文件为空！").arg(dev.name));
            return false;
        }

        info.packetSize = static_cast<quint16>(packetSize);

        // 计算数据包总数
        const quint32 computedPacketCount = (info.fileSize + packetSize - 1) / packetSize;
        if (computedPacketCount == 0 ||
            computedPacketCount > std::numeric_limits<quint16>::max()) {
            emit showInfo(tr(">>> 错误：%1 固件需要的数据包数量超出协议限制！").arg(dev.name));
            return false;
        }
        info.packetCount = static_cast<quint16>(computedPacketCount);

        // 计算文件CRC16
        info.fileCRC = BootLoaderProtocol::calculateCRC16(info.fileData);

        file.close();

        firmwareList.append(info);
        totalPackets += info.packetCount;

        emit showInfo(tr("加载 %1 固件: %2 字节, %3 包, CRC16=0x%4")
            .arg(dev.name)
            .arg(info.fileSize)
            .arg(info.packetCount)
            .arg(info.fileCRC, 4, 16, QLatin1Char('0')));
    }

    if (firmwareList.isEmpty()) {
        emit showInfo(tr(">>> 错误：请至少选择一个固件文件！"));
        return false;
    }

    return true;
}

/**
 * @brief 发送升级请求报文
 */
void UpgradeManager::sendUpgradeRequest()
{
    upgradeState = UpgradeState::WAIT_UPGRADE_REQUEST;

    BootLoaderProtocol::UpgradeFlags flags;
    for (const auto &fw : firmwareList) {
        switch (fw.deviceType) {
            case DeviceType::FPGA: flags.fpga = true; break;
            case DeviceType::DSP1: flags.dsp1 = true; break;
            case DeviceType::DSP2: flags.dsp2 = true; break;
            case DeviceType::ARM: flags.arm = true; break;
        }
    }

    QByteArray request = protocol.buildUpgradeRequest(slaveId, flags);
    emit sendData(request, tr("发送升级请求"));

    upgradeTimer->start();
}

/**
 * @brief 发送系统复位报文
 */
void UpgradeManager::sendSystemReset()
{
    upgradeState = UpgradeState::WAIT_SYSTEM_RESET;

    QByteArray reset = protocol.buildSystemReset(slaveId);
    emit sendData(reset, tr("发送系统复位命令"));

    upgradeTimer->start();
}

/**
 * @brief 开始设备升级
 */
void UpgradeManager::startDeviceUpgrade(DeviceType device)
{
    // 查找对应的固件
    currentFirmwareIndex = -1;
    for (int i = 0; i < firmwareList.size(); ++i) {
        if (firmwareList[i].deviceType == device) {
            currentFirmwareIndex = i;
            break;
        }
    }

    if (currentFirmwareIndex < 0) {
        // 该设备没有固件，跳到下一个设备
        DeviceType nextDevice = DeviceType::FPGA;
        switch (device) {
            case DeviceType::FPGA: nextDevice = DeviceType::DSP1; break;
            case DeviceType::DSP1: nextDevice = DeviceType::DSP2; break;
            case DeviceType::DSP2: nextDevice = DeviceType::ARM; break;
            case DeviceType::ARM:
                // 所有设备升级完成
                sendTotalEnd();
                return;
        }
        startDeviceUpgrade(nextDevice);
        return;
    }

    QString deviceName;
    switch (device) {
        case DeviceType::FPGA: deviceName = "FPGA"; break;
        case DeviceType::DSP1: deviceName = "DSP1"; break;
        case DeviceType::DSP2: deviceName = "DSP2"; break;
        case DeviceType::ARM: deviceName = "ARM"; break;
    }

    emit showInfo(tr("\n>>> 准备升级 %1").arg(deviceName));

    firmwareList[currentFirmwareIndex].currentPacket = 0;

    sendUpgradeCommand();
}

/**
 * @brief 发送升级指令报文
 */
void UpgradeManager::sendUpgradeCommand()
{
    if (currentFirmwareIndex < 0 || currentFirmwareIndex >= firmwareList.size()) {
        upgradeComplete(false, tr("内部错误：固件索引无效"));
        return;
    }

    upgradeState = UpgradeState::WAIT_UPGRADE_COMMAND;

    const FirmwareInfo &fw = firmwareList[currentFirmwareIndex];

    BootLoaderProtocol::MessageType cmdType = BootLoaderProtocol::MessageType::FPGA_COMMAND;
    switch (fw.deviceType) {
        case DeviceType::FPGA: cmdType = BootLoaderProtocol::MessageType::FPGA_COMMAND; break;
        case DeviceType::DSP1: cmdType = BootLoaderProtocol::MessageType::DSP1_COMMAND; break;
        case DeviceType::DSP2: cmdType = BootLoaderProtocol::MessageType::DSP2_COMMAND; break;
        case DeviceType::ARM: cmdType = BootLoaderProtocol::MessageType::ARM_COMMAND; break;
        default: break;
    }

    QByteArray command = protocol.buildUpgradeCommand(slaveId, cmdType,
                                                     fw.fileSize, fw.packetCount, fw.fileCRC);
    emit sendData(command, tr("发送升级指令"));

    upgradeTimer->start();
}

/**
 * @brief 发送升级数据包
 */
void UpgradeManager::sendUpgradeData()
{
    if (currentFirmwareIndex < 0 || currentFirmwareIndex >= firmwareList.size()) {
        upgradeComplete(false, tr("内部错误：固件索引无效"));
        return;
    }

    upgradeState = UpgradeState::WAIT_UPGRADE_DATA;

    FirmwareInfo &fw = firmwareList[currentFirmwareIndex];

    if (fw.packetSize == 0 || fw.packetCount == 0) {
        upgradeComplete(false, tr("内部错误：数据包参数无效"));
        return;
    }

    const int packetSize = fw.packetSize;
    const int offset = fw.currentPacket * packetSize;
    const int remaining = static_cast<int>(fw.fileSize) - offset;

    if (remaining <= 0) {
        upgradeComplete(false, tr("内部错误：数据包偏移无效"));
        return;
    }

    const int dataSize = qMin(packetSize, remaining);
    const quint16 packetNum = fw.currentPacket + 1; // 从1开始

    QByteArray packetData = fw.fileData.mid(offset, dataSize);

    BootLoaderProtocol::MessageType dataType = BootLoaderProtocol::MessageType::FPGA_DATA;
    switch (fw.deviceType) {
        case DeviceType::FPGA: dataType = BootLoaderProtocol::MessageType::FPGA_DATA; break;
        case DeviceType::DSP1: dataType = BootLoaderProtocol::MessageType::DSP1_DATA; break;
        case DeviceType::DSP2: dataType = BootLoaderProtocol::MessageType::DSP2_DATA; break;
        case DeviceType::ARM: dataType = BootLoaderProtocol::MessageType::ARM_DATA; break;
        default: break;
    }

    QByteArray data = protocol.buildUpgradeData(slaveId, dataType, packetNum, packetData);
    emit sendData(data, tr("发送数据包 %1/%2").arg(packetNum).arg(fw.packetCount));

    upgradeTimer->start();
}

/**
 * @brief 发送升级结束报文
 */
void UpgradeManager::sendUpgradeEnd()
{
    if (currentFirmwareIndex < 0 || currentFirmwareIndex >= firmwareList.size()) {
        upgradeComplete(false, tr("内部错误：固件索引无效"));
        return;
    }

    upgradeState = UpgradeState::WAIT_UPGRADE_END;

    const FirmwareInfo &fw = firmwareList[currentFirmwareIndex];

    BootLoaderProtocol::MessageType endType = BootLoaderProtocol::MessageType::FPGA_END;
    switch (fw.deviceType) {
        case DeviceType::FPGA: endType = BootLoaderProtocol::MessageType::FPGA_END; break;
        case DeviceType::DSP1: endType = BootLoaderProtocol::MessageType::DSP1_END; break;
        case DeviceType::DSP2: endType = BootLoaderProtocol::MessageType::DSP2_END; break;
        case DeviceType::ARM: endType = BootLoaderProtocol::MessageType::ARM_END; break;
        default: break;
    }

    QByteArray end = protocol.buildUpgradeEnd(slaveId, endType);
    emit sendData(end, tr("发送升级结束"));

    upgradeTimer->start();
}

/**
 * @brief 发送总体结束报文
 */
void UpgradeManager::sendTotalEnd()
{
    upgradeState = UpgradeState::WAIT_TOTAL_END;

    QByteArray totalEnd = protocol.buildTotalEnd(slaveId);
    emit sendData(totalEnd, tr("发送总体结束"));

    upgradeTimer->start();
}

/**
 * @brief 处理接收到的响应
 */
void UpgradeManager::handleResponse(BootLoaderProtocol::MessageType msgType, BootLoaderProtocol::ResponseFlag flag, const QByteArray &payload)
{
    // 重置超时
    upgradeTimer->stop();
    retryCount = 0;

    switch (upgradeState) {
        case UpgradeState::WAIT_UPGRADE_REQUEST:
            if (msgType == BootLoaderProtocol::MessageType::UPGRADE_REQUEST) {
                if (flag == BootLoaderProtocol::ResponseFlag::ALLOW_UPGRADE &&
                    !payload.isEmpty() && payload[0] == 0x00) {
                    emit showInfo(tr(">>> 设备允许升级"));
                    sendSystemReset();
                } else {
                    upgradeComplete(false, tr("设备禁止升级或状态异常"));
                }
            }
            break;

        case UpgradeState::WAIT_SYSTEM_RESET:
            if (msgType == BootLoaderProtocol::MessageType::SYSTEM_RESET) {
                if (flag == BootLoaderProtocol::ResponseFlag::RESTART_SUCCESS &&
                    !payload.isEmpty() && payload[0] == 0x00) {
                    emit showInfo(tr(">>> 系统重启成功"));
                    startDeviceUpgrade(DeviceType::FPGA);
                } else {
                    upgradeComplete(false, tr("系统重启失败"));
                }
            }
            break;

        case UpgradeState::WAIT_UPGRADE_COMMAND:
            {
                if (currentFirmwareIndex < 0) break;

                const FirmwareInfo &fw = firmwareList[currentFirmwareIndex];
                BootLoaderProtocol::MessageType expectedType = BootLoaderProtocol::MessageType::FPGA_COMMAND;
                switch (fw.deviceType) {
                    case DeviceType::FPGA: expectedType = BootLoaderProtocol::MessageType::FPGA_COMMAND; break;
                    case DeviceType::DSP1: expectedType = BootLoaderProtocol::MessageType::DSP1_COMMAND; break;
                    case DeviceType::DSP2: expectedType = BootLoaderProtocol::MessageType::DSP2_COMMAND; break;
                    case DeviceType::ARM: expectedType = BootLoaderProtocol::MessageType::ARM_COMMAND; break;
                    default: break;
                }

                if (msgType == expectedType) {
                    // 处理准备擦除Flash标志（0x09）
                    if (flag == BootLoaderProtocol::ResponseFlag::PREPARE_ERASE) {
                        emit showInfo(tr(">>> 准备擦除Flash..."));
                        // 继续等待擦除成功的消息，不改变状态
                    }
                    // 处理擦除成功标志（0x0A）
                    else if (flag == BootLoaderProtocol::ResponseFlag::ERASE_SUCCESS &&
                        !payload.isEmpty() && payload[0] == 0x00) {
                        emit showInfo(tr(">>> 擦除Flash成功，开始传输数据"));
                        sendUpgradeData();
                    }
                    else {
                        const QString reason = BootLoaderProtocol::getResponseDescription(flag);
                        upgradeComplete(false, tr("擦除Flash失败：%1").arg(reason));
                    }
                }
            }
            break;

        case UpgradeState::WAIT_UPGRADE_DATA:
            {
                if (currentFirmwareIndex < 0) break;

                FirmwareInfo &fw = firmwareList[currentFirmwareIndex];
                BootLoaderProtocol::MessageType expectedType = BootLoaderProtocol::MessageType::FPGA_DATA;
                switch (fw.deviceType) {
                    case DeviceType::FPGA: expectedType = BootLoaderProtocol::MessageType::FPGA_DATA; break;
                    case DeviceType::DSP1: expectedType = BootLoaderProtocol::MessageType::DSP1_DATA; break;
                    case DeviceType::DSP2: expectedType = BootLoaderProtocol::MessageType::DSP2_DATA; break;
                    case DeviceType::ARM: expectedType = BootLoaderProtocol::MessageType::ARM_DATA; break;
                    default: break;
                }

                if (msgType == expectedType) {
                    if (flag == BootLoaderProtocol::ResponseFlag::SUCCESS) {
                        if (payload.size() < 5) {
                            upgradeComplete(false, tr("数据传输失败：应答长度异常"));
                            return;
                        }

                        const quint8 status = static_cast<quint8>(payload[0]);
                        const quint16 packetNum = static_cast<quint16>((static_cast<quint8>(payload[1]) << 8) |
                                                                       static_cast<quint8>(payload[2]));
                        const quint16 receivedCount = static_cast<quint16>((static_cast<quint8>(payload[3]) << 8) |
                                                                            static_cast<quint8>(payload[4]));
                        const quint16 expectedPacket = fw.currentPacket + 1;

                        if (status != 0x00) {
                            upgradeComplete(false, tr("数据传输失败：目标设备上报错误状态"));
                            return;
                        }

                        if (packetNum != expectedPacket) {
                            upgradeComplete(false, tr("数据传输失败：包序号不匹配 (期望 %1, 实际 %2)")
                                                           .arg(expectedPacket)
                                                           .arg(packetNum));
                            return;
                        }

                        if (receivedCount < packetNum || receivedCount > fw.packetCount) {
                            upgradeComplete(false, tr("数据传输失败：目标设备接收计数异常"));
                            return;
                        }

                        fw.currentPacket++;
                        sentPackets++;

                        updateProgress();

                        if (fw.currentPacket < fw.packetCount) {
                            sendUpgradeData();
                        } else {
                            emit showInfo(tr(">>> 所有数据包发送完成"));
                            sendUpgradeEnd();
                        }
                    } else {
                        const QString reason = failureMessageForFlag(flag);
                        upgradeComplete(false, tr("数据传输失败：%1").arg(reason));
                        return;
                    }
                }
            }
            break;

        case UpgradeState::WAIT_UPGRADE_END:
            {
                if (currentFirmwareIndex < 0) break;

                const FirmwareInfo &fw = firmwareList[currentFirmwareIndex];
                BootLoaderProtocol::MessageType expectedType = BootLoaderProtocol::MessageType::FPGA_END;
                switch (fw.deviceType) {
                    case DeviceType::FPGA: expectedType = BootLoaderProtocol::MessageType::FPGA_END; break;
                    case DeviceType::DSP1: expectedType = BootLoaderProtocol::MessageType::DSP1_END; break;
                    case DeviceType::DSP2: expectedType = BootLoaderProtocol::MessageType::DSP2_END; break;
                    case DeviceType::ARM: expectedType = BootLoaderProtocol::MessageType::ARM_END; break;
                    default: break;
                }

                if (msgType == expectedType) {
                    const bool successFlag = (flag == BootLoaderProtocol::ResponseFlag::SUCCESS ||
                                              flag == BootLoaderProtocol::ResponseFlag::UPGRADE_END ||
                                              flag == BootLoaderProtocol::ResponseFlag::FPGA_CONFIG_SUCCESS);

                    if (successFlag) {
                        if (!payload.isEmpty() && static_cast<quint8>(payload[0]) == 0x00) {
                            emit showInfo(tr(">>> 设备升级完成"));

                            // 升级下一个设备
                            DeviceType nextDevice = DeviceType::FPGA;
                            switch (fw.deviceType) {
                                case DeviceType::FPGA: nextDevice = DeviceType::DSP1; break;
                                case DeviceType::DSP1: nextDevice = DeviceType::DSP2; break;
                                case DeviceType::DSP2: nextDevice = DeviceType::ARM; break;
                                case DeviceType::ARM:
                                    sendTotalEnd();
                                    return;
                            }
                            startDeviceUpgrade(nextDevice);
                        } else {
                            upgradeComplete(false, tr("设备升级校验失败：目标设备状态异常"));
                            return;
                        }
                    } else {
                        const QString reason = failureMessageForFlag(flag);
                        upgradeComplete(false, tr("设备升级失败：%1").arg(reason));
                        return;
                    }
                }
            }
            break;

        case UpgradeState::WAIT_TOTAL_END:
            if (msgType == BootLoaderProtocol::MessageType::TOTAL_END) {
                if (flag == BootLoaderProtocol::ResponseFlag::SUCCESS) {
                    if (!payload.isEmpty() && static_cast<quint8>(payload[0]) == 0x00) {
                        upgradeComplete(true, tr("所有设备升级成功"));
                    } else {
                        upgradeComplete(false, tr("总体结束失败：目标设备状态异常"));
                    }
                } else {
                    const QString reason = failureMessageForFlag(flag);
                    upgradeComplete(false, tr("总体结束失败：%1").arg(reason));
                }
                return;
            }
            break;

        default:
            break;
    }

    // 继续等待可能的后续响应
    if (upgradeState != UpgradeState::IDLE &&
        upgradeState != UpgradeState::UPGRADE_SUCCESS &&
        upgradeState != UpgradeState::UPGRADE_FAILED) {
        upgradeTimer->start();
    }
}

/**
 * @brief 超时处理
 */
void UpgradeManager::onTimeout()
{
    upgradeTimer->stop();

    retryCount++;

    if (retryCount <= 3) {
        emit showInfo(tr(">>> 通信超时，第 %1 次重发...").arg(retryCount));

        // 根据当前状态重发相应的报文
        switch (upgradeState) {
            case UpgradeState::WAIT_UPGRADE_REQUEST:
                sendUpgradeRequest();
                break;
            case UpgradeState::WAIT_SYSTEM_RESET:
                sendSystemReset();
                break;
            case UpgradeState::WAIT_UPGRADE_COMMAND:
                sendUpgradeCommand();
                break;
            case UpgradeState::WAIT_UPGRADE_DATA:
                sendUpgradeData();
                break;
            case UpgradeState::WAIT_UPGRADE_END:
                sendUpgradeEnd();
                break;
            case UpgradeState::WAIT_TOTAL_END:
                sendTotalEnd();
                break;
            default:
                break;
        }
    } else {
        upgradeComplete(false, tr("通信超时，目标无响应，请检查设备状态"));
    }
}

/**
 * @brief 升级完成
 */
void UpgradeManager::upgradeComplete(bool success, const QString &message)
{
    upgradeTimer->stop();

    if (success) {
        upgradeState = UpgradeState::UPGRADE_SUCCESS;
        emit showInfo(tr("\n>>> 升级完成！%1").arg(message));
        emit showInfo(tr("========================================"));
    } else {
        upgradeState = UpgradeState::UPGRADE_FAILED;
        emit showInfo(tr("\n>>> 升级失败：%1").arg(message));
        emit showInfo(tr("========================================"));
    }

    emit upgradeFinished(success, message);

    resetState();
}

/**
 * @brief 重置状态
 */
void UpgradeManager::resetState()
{
    upgradeState = UpgradeState::IDLE;
    currentFirmwareIndex = -1;
    retryCount = 0;
    upgradeTimer->stop();
}

/**
 * @brief 更新进度
 */
void UpgradeManager::updateProgress()
{
    if (currentFirmwareIndex < 0 || currentFirmwareIndex >= firmwareList.size()) {
        return;
    }

    const FirmwareInfo &fw = firmwareList[currentFirmwareIndex];

    // 计算当前设备进度
    int currentProgress = fw.packetCount > 0 ? (fw.currentPacket * 100) / fw.packetCount : 0;

    // 计算总体进度
    int totalProgress = totalPackets > 0 ? (sentPackets * 100) / totalPackets : 0;

    emit progressUpdated(currentProgress, totalProgress);
}

QString UpgradeManager::failureMessageForFlag(BootLoaderProtocol::ResponseFlag flag) const
{
    switch (flag) {
        case BootLoaderProtocol::ResponseFlag::FAILED:
            return tr("命令执行失败");
        case BootLoaderProtocol::ResponseFlag::CRC_ERROR:
        case BootLoaderProtocol::ResponseFlag::DATA_CRC_ERROR:
            return tr("数据校验错误");
        case BootLoaderProtocol::ResponseFlag::TIMEOUT:
            return tr("接收超时");
        case BootLoaderProtocol::ResponseFlag::FORBID_UPGRADE:
            return tr("禁止升级");
        case BootLoaderProtocol::ResponseFlag::ERASE_FAILED:
            return tr("擦除Flash失败");
        case BootLoaderProtocol::ResponseFlag::RESTART_FAILED:
            return tr("重启失败");
        case BootLoaderProtocol::ResponseFlag::SIZE_ERROR:
            return tr("数据大小出错");
        case BootLoaderProtocol::ResponseFlag::FLASH_WRITE_FAILED:
            return tr("Flash数据写入失败");
        case BootLoaderProtocol::ResponseFlag::FPGA_CONFIG_FAILED:
            return tr("FPGA配置失败");
        case BootLoaderProtocol::ResponseFlag::FPGA_FILE_DAMAGED:
            return tr("FPGA配置文件损坏");
        case BootLoaderProtocol::ResponseFlag::FPGA_STATUS_ERROR:
            return tr("FPGA状态异常");
        case BootLoaderProtocol::ResponseFlag::FPGA_FLAG_WRITE_FAILED:
            return tr("写FPGA固件标志位失败");
        case BootLoaderProtocol::ResponseFlag::PACKET_SIZE_EXCEED:
            return tr("数据包大小超限");
        default:
            return BootLoaderProtocol::getResponseDescription(flag);
    }
}

/**
 * @brief 停止升级
 */
void UpgradeManager::stopUpgrade()
{
    if (upgradeState != UpgradeState::IDLE) {
        upgradeTimer->stop();
        emit showInfo(tr(">>> 升级已取消"));
        resetState();
    }
}
