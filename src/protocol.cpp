// protocol.cpp
#include "inc/protocol.h"

/**
 * @brief 构造函数
 */
BootLoaderProtocol::BootLoaderProtocol(QObject *parent)
    : QObject(parent)
{
}

/**
 * @brief 计算CRC16-MODBUS校验值
 */
quint16 BootLoaderProtocol::calculateCRC16(const QByteArray &data)
{
    quint16 crc = 0xFFFF;

    for (int i = 0; i < data.size(); i++) {
        crc ^= static_cast<quint8>(data[i]);
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc;
}

/**
 * @brief 构建上位机报文帧
 */
QByteArray BootLoaderProtocol::buildMasterFrame(quint8 slaveId, MessageType type, ResponseFlag flag, const QByteArray &payload)
{
    QByteArray frame;
    quint16 length = 8 + payload.size();  // 报文总长度

    // 帧头
    frame.append(static_cast<char>(MASTER_HEADER1));
    frame.append(static_cast<char>(MASTER_HEADER2));

    // 下位机ID
    frame.append(static_cast<char>(slaveId));

    // 长度（高位在前）
    frame.append(static_cast<char>((length >> 8) & 0xFF));
    frame.append(static_cast<char>(length & 0xFF));

    // 报文类型
    frame.append(static_cast<char>(type));

    // 应答标识
    frame.append(static_cast<char>(flag));

    // 命令数据
    frame.append(payload);

    // 计算CRC（从ID开始）
    QByteArray crcData = frame.mid(2);
    quint16 crc = calculateCRC16(crcData);

    // CRC（高位在前）
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    frame.append(static_cast<char>(crc & 0xFF));

    return frame;
}

/**
 * @brief 构建下位机报文帧
 */
QByteArray BootLoaderProtocol::buildSlaveFrame(quint8 slaveId, MessageType type, ResponseFlag flag, const QByteArray &payload)
{
    QByteArray frame;
    quint16 length = 8 + payload.size();

    // 帧头（下位机是0x55 0xAA）
    frame.append(static_cast<char>(SLAVE_HEADER1));
    frame.append(static_cast<char>(SLAVE_HEADER2));

    // ID
    frame.append(static_cast<char>(slaveId));

    // 长度（高位在前）
    frame.append(static_cast<char>((length >> 8) & 0xFF));
    frame.append(static_cast<char>(length & 0xFF));

    // 报文类型
    frame.append(static_cast<char>(type));

    // 应答标识
    frame.append(static_cast<char>(flag));

    // 命令数据
    frame.append(payload);

    // 计算CRC
    QByteArray crcData = frame.mid(2);
    quint16 crc = calculateCRC16(crcData);

    // CRC（高位在前）
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    frame.append(static_cast<char>(crc & 0xFF));

    return frame;
}

// ============= 上位机发送接口实现 =============

QByteArray BootLoaderProtocol::buildUpgradeRequest(quint8 slaveId, const UpgradeFlags &flags)
{
    QByteArray payload;
    payload.append(static_cast<char>(flags.toByte()));
    return buildMasterFrame(slaveId, MessageType::UPGRADE_REQUEST, ResponseFlag::REQUEST_FLAG, payload);
}

QByteArray BootLoaderProtocol::buildSystemReset(quint8 slaveId)
{
    QByteArray payload;
    payload.append(static_cast<char>(0x00));
    return buildMasterFrame(slaveId, MessageType::SYSTEM_RESET, ResponseFlag::REQUEST_FLAG, payload);
}

QByteArray BootLoaderProtocol::buildUpgradeCommand(quint8 slaveId, MessageType type, quint32 fileSize, quint16 packetCount, quint16 fileCRC)
{
    QByteArray payload;

    // 文件大小（高字节在前）
    payload.append(static_cast<char>((fileSize >> 24) & 0xFF));
    payload.append(static_cast<char>((fileSize >> 16) & 0xFF));
    payload.append(static_cast<char>((fileSize >> 8) & 0xFF));
    payload.append(static_cast<char>(fileSize & 0xFF));

    // 数据包总数（高字节在前）
    payload.append(static_cast<char>((packetCount >> 8) & 0xFF));
    payload.append(static_cast<char>(packetCount & 0xFF));

    // 文件CRC16（高字节在前）
    payload.append(static_cast<char>((fileCRC >> 8) & 0xFF));
    payload.append(static_cast<char>(fileCRC & 0xFF));

    return buildMasterFrame(slaveId, type, ResponseFlag::REQUEST_FLAG, payload);
}

QByteArray BootLoaderProtocol::buildUpgradeData(quint8 slaveId, MessageType type, quint16 packetNum, const QByteArray &data)
{
    QByteArray payload;

    // 数据包序号（高字节在前）
    payload.append(static_cast<char>((packetNum >> 8) & 0xFF));
    payload.append(static_cast<char>(packetNum & 0xFF));

    // 升级文件内容
    payload.append(data);

    return buildMasterFrame(slaveId, type, ResponseFlag::REQUEST_FLAG, payload);
}

QByteArray BootLoaderProtocol::buildUpgradeEnd(quint8 slaveId, MessageType type)
{
    QByteArray payload;
    payload.append(static_cast<char>(0x00));
    return buildMasterFrame(slaveId, type, ResponseFlag::REQUEST_FLAG, payload);
}

QByteArray BootLoaderProtocol::buildTotalEnd(quint8 slaveId)
{
    QByteArray payload;
    payload.append(static_cast<char>(0x00));
    return buildMasterFrame(slaveId, MessageType::TOTAL_END, ResponseFlag::REQUEST_FLAG, payload);
}

// ============= 下位机发送接口实现 =============

QByteArray BootLoaderProtocol::buildResponse(quint8 slaveId, MessageType type, ResponseFlag flag, const QByteArray &data)
{
    return buildSlaveFrame(slaveId, type, flag, data);
}

QByteArray BootLoaderProtocol::buildDebugInfo(quint8 slaveId, ResponseFlag flag)
{
    QByteArray payload;
    payload.append(static_cast<char>(0x00));
    return buildSlaveFrame(slaveId, MessageType::DEBUG_INFO, flag, payload);
}

// ============= 接收解析接口实现 =============

QList<QByteArray> BootLoaderProtocol::parseReceivedData(const QByteArray &data)
{
    QList<QByteArray> frames;

    // 将新数据追加到接收缓冲区
    m_receiveBuffer.append(data);

    // 查找并提取完整帧
    while (m_receiveBuffer.size() >= 5) {
        bool foundHeader = false;
        int headerPos = 0;

        // 查找帧头
        for (int i = 0; i < m_receiveBuffer.size() - 1; i++) {
            quint8 byte1 = static_cast<quint8>(m_receiveBuffer[i]);
            quint8 byte2 = static_cast<quint8>(m_receiveBuffer[i + 1]);

            if ((byte1 == MASTER_HEADER1 && byte2 == MASTER_HEADER2) ||
                (byte1 == SLAVE_HEADER1 && byte2 == SLAVE_HEADER2)) {
                foundHeader = true;
                headerPos = i;
                break;
            }
        }

        if (!foundHeader) {
            m_receiveBuffer.clear();
            break;
        }

        // 删除帧头之前的无效数据
        if (headerPos > 0) {
            m_receiveBuffer.remove(0, headerPos);
        }

        // 检查长度字段是否完整
        if (m_receiveBuffer.size() < 5) {
            break;
        }

        // 获取报文长度
        quint16 length = (static_cast<quint8>(m_receiveBuffer[3]) << 8) |
                         static_cast<quint8>(m_receiveBuffer[4]);
        quint16 totalLength = length + 4;  // 加上2字节帧头和2字节CRC

        // 检查完整帧是否接收完毕
        if (m_receiveBuffer.size() < totalLength) {
            break;
        }

        // 提取完整帧
        QByteArray frame = m_receiveBuffer.left(totalLength);
        m_receiveBuffer.remove(0, totalLength);

        frames.append(frame);
    }

    return frames;
}

bool BootLoaderProtocol::parseFrame(const QByteArray &frame, quint8 &slaveId, MessageType &type, ResponseFlag &flag, QByteArray &payload)
{
    if (frame.size() < 10) {
        return false;
    }

    // 验证CRC
    QByteArray crcData = frame.mid(2, frame.size() - 4);
    quint16 calculatedCRC = calculateCRC16(crcData);
    quint16 receivedCRC = (static_cast<quint8>(frame[frame.size() - 2]) << 8) |
                          static_cast<quint8>(frame[frame.size() - 1]);

    if (calculatedCRC != receivedCRC) {
        return false;
    }

    // 解析字段
    slaveId = static_cast<quint8>(frame[2]);
    type = static_cast<MessageType>(frame[5]);
    flag = static_cast<ResponseFlag>(frame[6]);

    // 提取命令数据
    if (frame.size() > 10) {
        payload = frame.mid(7, frame.size() - 9);
    } else {
        payload.clear();
    }

    return true;
}

/* ============= 指令描述匹配 ============= */
// 应答标识
QString BootLoaderProtocol::getResponseDescription(ResponseFlag flag)
{
    switch (flag) {
        case ResponseFlag::SUCCESS: return "命令执行成功";
        case ResponseFlag::FAILED: return "命令执行失败";
        case ResponseFlag::CRC_ERROR: return "数据校验错误";
        case ResponseFlag::TIMEOUT: return "接收超时";
        case ResponseFlag::ALLOW_UPGRADE: return "允许升级";
        case ResponseFlag::FORBID_UPGRADE: return "禁止升级";
        case ResponseFlag::EXIT_UPGRADE: return "退出升级流程";
        case ResponseFlag::UNLOCK_SUCCESS: return "解锁成功";
        case ResponseFlag::UNLOCK_FAILED: return "解锁失败";
        case ResponseFlag::PREPARE_ERASE: return "准备擦除Flash";
        case ResponseFlag::ERASE_SUCCESS: return "擦除Flash成功";
        case ResponseFlag::ERASE_FAILED: return "擦除Flash失败";
        case ResponseFlag::RESTART_SUCCESS: return "重启成功";
        case ResponseFlag::RESTART_FAILED: return "重启失败";
        case ResponseFlag::UPGRADE_END: return "升级结束，所有数据包发送成功";
        case ResponseFlag::SIZE_ERROR: return "升级失败，数据大小出错";
        case ResponseFlag::DATA_CRC_ERROR: return "升级失败，数据校验错误";
        case ResponseFlag::FPGA_CHECK_PASS: return "FPGA配置文件自检通过";
        case ResponseFlag::FPGA_FILE_DAMAGED: return "FPGA配置文件损坏";
        case ResponseFlag::FPGA_READY: return "FPGA就绪，等待配置";
        case ResponseFlag::FPGA_STATUS_ERROR: return "FPGA状态异常";
        case ResponseFlag::FPGA_LOAD_COMPLETE: return "FPGA配置加载完成";
        case ResponseFlag::FPGA_CONFIG_SUCCESS: return "FPGA配置成功";
        case ResponseFlag::START_APP: return "启动应用程序";
        case ResponseFlag::DSP_VERSION: return "DSP版本号";
        case ResponseFlag::FLASH_WRITE_FAILED: return "Flash数据写入失败";
        case ResponseFlag::FPGA_CONFIG_FAILED: return "FPGA配置失败";
        case ResponseFlag::FPGA_FLAG_WRITE_FAILED: return "写FPGA固件标志位失败";
        case ResponseFlag::PACKET_SIZE_EXCEED: return "数据包大小超限";
        case ResponseFlag::REQUEST_FLAG: return "请求标识";
        default: return QString("未知响应(0x%1)").arg(static_cast<quint8>(flag), 2, 16, QChar('0'));
    }
}

// 报文类型
QString BootLoaderProtocol::getMessageTypeDescription(MessageType type)
{
    switch (type) {
        case MessageType::UPGRADE_REQUEST: return "升级请求";
        case MessageType::SYSTEM_RESET: return "系统复位";
        case MessageType::ARM_COMMAND: return "ARM升级命令";
        case MessageType::ARM_DATA: return "ARM升级数据";
        case MessageType::ARM_END: return "ARM升级结束";
        case MessageType::FPGA_COMMAND: return "FPGA升级命令";
        case MessageType::FPGA_DATA: return "FPGA升级数据";
        case MessageType::FPGA_END: return "FPGA升级结束";
        case MessageType::DSP1_COMMAND: return "DSP1升级命令";
        case MessageType::DSP1_DATA: return "DSP1升级数据";
        case MessageType::DSP1_END: return "DSP1升级结束";
        case MessageType::DSP2_COMMAND: return "DSP2升级命令";
        case MessageType::DSP2_DATA: return "DSP2升级数据";
        case MessageType::DSP2_END: return "DSP2升级结束";
        case MessageType::TOTAL_END: return "总体结束";
        case MessageType::DEBUG_INFO: return "调试信息";
        default: return QString("未知类型(0x%1)").arg(static_cast<quint8>(type), 2, 16, QChar('0'));
    }
}