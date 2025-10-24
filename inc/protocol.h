// BootLoaderProtocol.h
#ifndef BOOTLOADER_PROTOCOL_H
#define BOOTLOADER_PROTOCOL_H

#include <QObject>
#include <QByteArray>
#include <QString>

/**
 * @brief BootLoader协议通信类 - 纯协议实现
 */
class BootLoaderProtocol : public QObject {
    Q_OBJECT

public:
    // 报文类型枚举
    enum class MessageType : quint8 {
        UPGRADE_REQUEST = 0x01,      // 升级请求报文
        SYSTEM_RESET = 0x02,         // 系统复位命令

        ARM_COMMAND = 0x03,          // ARM升级命令
        ARM_DATA = 0x04,             // ARM升级数据
        ARM_END = 0x05,              // ARM升级结束

        FPGA_COMMAND = 0x06,         // FPGA升级命令
        FPGA_DATA = 0x07,            // FPGA升级数据
        FPGA_END = 0x09,             // FPGA升级结束

        DSP1_COMMAND = 0x0A,         // DSP1升级命令
        DSP1_DATA = 0x0B,            // DSP1升级数据
        DSP1_END = 0x0C,             // DSP1升级结束

        DSP2_COMMAND = 0x0D,         // DSP2升级命令
        DSP2_DATA = 0x0E,            // DSP2升级数据
        DSP2_END = 0x0F,             // DSP2升级结束

        TOTAL_END = 0x10,            // 总体结束
        DEBUG_INFO = 0x1F            // 调试信息显示
    };

    // 应答标识枚举
    enum class ResponseFlag : quint8 {
        SUCCESS = 0x00,              // 命令执行成功
        FAILED = 0x01,               // 命令执行失败

        CRC_ERROR = 0x02,            // 数据校验错误
        TIMEOUT = 0x03,              // 接收超时
        ALLOW_UPGRADE = 0x04,        // 允许升级
        FORBID_UPGRADE = 0x05,       // 禁止升级
        EXIT_UPGRADE = 0x06,         // 退出升级流程

        UNLOCK_SUCCESS = 0x07,       // 解锁成功
        UNLOCK_FAILED = 0x08,        // 解锁失败
        PREPARE_ERASE = 0x09,        // 准备擦除Flash
        ERASE_SUCCESS = 0x0A,        // 擦除Flash成功
        ERASE_FAILED = 0x0B,         // 擦除Flash失败

        RESTART_SUCCESS = 0x0C,      // 重启成功
        RESTART_FAILED = 0x0D,       // 重启失败
        UPGRADE_END = 0x0E,          // 升级结束
        SIZE_ERROR = 0x0F,           // 升级失败，数据大小出错
        DATA_CRC_ERROR = 0x10,       // 升级失败，数据校验错误

        FPGA_CHECK_PASS = 0x11,      // FPGA配置文件自检通过
        FPGA_FILE_DAMAGED = 0x12,    // FPGA配置文件损坏
        FPGA_READY = 0x13,           // FPGA就绪
        FPGA_STATUS_ERROR = 0x14,    // FPGA状态异常
        FPGA_LOAD_COMPLETE = 0x15,   // FPGA配置加载完成
        FPGA_CONFIG_SUCCESS = 0x16,  // FPGA配置成功

        START_APP = 0x17,            // 启动应用程序
        DSP_VERSION = 0x18,          // DSP版本号
        FLASH_WRITE_FAILED = 0x19,   // Flash数据写入失败
        FPGA_CONFIG_FAILED = 0x20,   // FPGA配置失败
        FPGA_FLAG_WRITE_FAILED = 0x21, // 写FPGA固件标志位失败
        PACKET_SIZE_EXCEED = 0x22,   // 数据包大小超限

        START_PROGRAM_FPGA = 0x23,   // 开始编程FPGA
        RESERVED_0x24 = 0x24,        // 预留
        RESERVED_0x25 = 0x25,        // 预留
        RESERVED_0x26 = 0x26,        // 预留
        RESERVED_0x27 = 0x27,        // 预留
        RESERVED_0x28 = 0x28,        // 预留
        RESERVED_0x29 = 0x29,        // 预留
        RESERVED_0x2A = 0x2A,        // 预留
        RESERVED_0x2B = 0x2B,        // 预留
        RESERVED_0x2C = 0x2C,        // 预留
        RESERVED_0x2D = 0x2D,        // 预留
        RESERVED_0x2E = 0x2E,        // 预留
        RESERVED_0x2F = 0x2F,        // 预留

        REQUEST_FLAG = 0xFE          // 请求标识
    };

    // 升级目标标识
    struct UpgradeFlags {
        bool fpga;
        bool dsp1;
        bool dsp2;
        bool arm;

        UpgradeFlags() : fpga(false), dsp1(false), dsp2(false), arm(false) {}

        quint8 toByte() const {
            return (fpga ? 0x01 : 0x00) |
                   (dsp1 ? 0x02 : 0x00) |
                   (dsp2 ? 0x04 : 0x00) |
                   (arm ? 0x08 : 0x00);
        }
    };

    explicit BootLoaderProtocol(QObject *parent = nullptr);

    // ============= 上位机发送接口 =============

    /**
     * @brief 构建升级请求报文
     * @param slaveId 下位机ID
     * @param flags 升级标识
     * @return 完整报文
     */
    QByteArray buildUpgradeRequest(quint8 slaveId, const UpgradeFlags &flags);

    /**
     * @brief 构建系统复位报文
     */
    QByteArray buildSystemReset(quint8 slaveId);

    /**
     * @brief 构建升级命令报文
     * @param slaveId 下位机ID
     * @param type 报文类型（ARM_COMMAND/FPGA_COMMAND/DSP1_COMMAND/DSP2_COMMAND）
     * @param fileSize 文件大小（字节）
     * @param packetCount 数据包总数
     * @param fileCRC 文件CRC16校验值
     */
    QByteArray buildUpgradeCommand(quint8 slaveId, MessageType type,
                                   quint32 fileSize, quint16 packetCount, quint16 fileCRC);

    /**
     * @brief 构建升级数据包报文
     * @param slaveId 下位机ID
     * @param type 报文类型（ARM_DATA/FPGA_DATA/DSP1_DATA/DSP2_DATA）
     * @param packetNum 数据包序号（从1开始）
     * @param data 数据内容
     */
    QByteArray buildUpgradeData(quint8 slaveId, MessageType type,
                                quint16 packetNum, const QByteArray &data);

    /**
     * @brief 构建升级结束报文
     * @param slaveId 下位机ID
     * @param type 报文类型（ARM_END/FPGA_END/DSP1_END/DSP2_END）
     */
    QByteArray buildUpgradeEnd(quint8 slaveId, MessageType type);

    /**
     * @brief 构建总体结束报文
     */
    QByteArray buildTotalEnd(quint8 slaveId);

    // ============= 下位机发送接口 =============

    /**
     * @brief 构建响应报文
     */
    QByteArray buildResponse(quint8 slaveId, MessageType type,
                            ResponseFlag flag, const QByteArray &data = QByteArray());

    /**
     * @brief 构建调试信息报文
     */
    QByteArray buildDebugInfo(quint8 slaveId, ResponseFlag flag);

    // ============= 接收解析接口 =============

    /**
     * @brief 解析接收到的数据
     * @param data 接收到的数据
     * @return 解析出的完整帧列表
     */
    QList<QByteArray> parseReceivedData(const QByteArray &data);

    /**
     * @brief 解析单个完整帧
     * @param frame 完整帧数据
     * @param slaveId 输出：下位机ID
     * @param type 输出：报文类型
     * @param flag 输出：应答标识
     * @param payload 输出：命令数据
     * @return 解析是否成功
     */
    bool parseFrame(const QByteArray &frame, quint8 &slaveId, MessageType &type,
                   ResponseFlag &flag, QByteArray &payload);

    // ============= 工具函数 =============

    /**
     * @brief 计算CRC16-MODBUS校验值
     */
    static quint16 calculateCRC16(const QByteArray &data);

    /**
     * @brief 获取响应标识描述
     */
    static QString getResponseDescription(ResponseFlag flag);

    /**
     * @brief 获取消息类型描述
     */
    static QString getMessageTypeDescription(MessageType type);

private:
    // 帧头常量
    static constexpr quint8 MASTER_HEADER1 = 0xAA;  // 上位机帧头1
    static constexpr quint8 MASTER_HEADER2 = 0x55;  // 上位机帧头2
    static constexpr quint8 SLAVE_HEADER1 = 0x55;   // 下位机帧头1
    static constexpr quint8 SLAVE_HEADER2 = 0xAA;   // 下位机帧头2

    /**
     * @brief 构建上位机报文帧
     */
    QByteArray buildMasterFrame(quint8 slaveId, MessageType type, ResponseFlag flag, const QByteArray &payload);

    /**
     * @brief 构建下位机报文帧
     */
    QByteArray buildSlaveFrame(quint8 slaveId, MessageType type, ResponseFlag flag, const QByteArray &payload);

    QByteArray m_receiveBuffer;  // 接收缓冲区
};

#endif // BOOTLOADER_PROTOCOL_H

