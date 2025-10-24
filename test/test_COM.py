#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
BootLoader测试服务器 - 串口模式
模拟下位机通过串口响应BootLoader协议
"""

import serial
import serial.tools.list_ports
import struct
import time
from datetime import datetime

class BootLoaderSerialServer:
    """BootLoader协议串口测试服务器"""

    # 协议常量
    MASTER_HEADER = b'\xAA\x55'
    SLAVE_HEADER = b'\x55\xAA'

    # 报文类型
    MSG_UPGRADE_REQUEST = 0x01
    MSG_SYSTEM_RESET = 0x02
    MSG_ARM_COMMAND = 0x03
    MSG_ARM_DATA = 0x04
    MSG_ARM_END = 0x05
    MSG_FPGA_COMMAND = 0x06
    MSG_FPGA_DATA = 0x07
    MSG_FPGA_END = 0x09
    MSG_DSP1_COMMAND = 0x0A
    MSG_DSP1_DATA = 0x0B
    MSG_DSP1_END = 0x0C
    MSG_DSP2_COMMAND = 0x0D
    MSG_DSP2_DATA = 0x0E
    MSG_DSP2_END = 0x0F
    MSG_TOTAL_END = 0x10
    MSG_DEBUG_INFO = 0x1F

    # 应答标识
    FLAG_SUCCESS = 0x00
    FLAG_ALLOW_UPGRADE = 0x04
    FLAG_ERASE_SUCCESS = 0x0A
    FLAG_RESTART_SUCCESS = 0x0C
    FLAG_UPGRADE_END = 0x0E
    FLAG_REQUEST = 0xFE

    def __init__(self, port='COM2', baudrate=115200):
        """
        初始化串口服务器
        :param port: 串口名称 (Windows: COM1, COM2...; Linux: /dev/ttyUSB0, /dev/ttyS0...)
        :param baudrate: 波特率
        """
        self.port = port
        self.baudrate = baudrate
        self.serial_conn = None
        self.running = False
        self.received_packets = 0
        self.expected_file_size = 0
        self.expected_packet_count = 0

    def calculate_crc16(self, data):
        """计算CRC16-MODBUS校验"""
        crc = 0xFFFF
        for byte in data:
            crc ^= byte
            for _ in range(8):
                if crc & 0x0001:
                    crc = (crc >> 1) ^ 0xA001
                else:
                    crc >>= 1
        return crc

    def parse_frame(self, data):
        """解析报文帧"""
        if len(data) < 10:
            return None

        # 检查帧头
        if data[0:2] != self.MASTER_HEADER:
            print(f"[错误] 无效帧头: {data[0:2].hex()}")
            return None

        slave_id = data[2]
        length = (data[3] << 8) | data[4]
        msg_type = data[5]
        flag = data[6]

        # 验证长度
        if len(data) != length:
            print(f"[错误] 长度不匹配: 期望{length}, 实际{len(data)}")
            return None

        # 提取payload和CRC
        payload = data[7:-2]
        # 接收到的CRC：低位在前，高位在后
        received_crc = data[-2] | (data[-1] << 8)

        # 验证CRC
        crc_data = data[:-2]
        calculated_crc = self.calculate_crc16(crc_data)

        if calculated_crc != received_crc:
            print(f"[错误] CRC校验失败: 期望0x{calculated_crc:04X}, 实际0x{received_crc:04X}")
            return None

        return {
            'slave_id': slave_id,
            'msg_type': msg_type,
            'flag': flag,
            'payload': payload,
            'length': length
        }

    def build_response(self, msg_type, flag, payload=b'\x00'):
        """构建响应报文"""
        slave_id = 0x01  # 模拟从机ID

        # 计算长度
        length = 10 + len(payload) - 1  # 去掉默认的1字节payload

        # 构建帧
        frame = bytearray()
        frame.extend(self.SLAVE_HEADER)
        frame.append(slave_id)
        frame.append((length >> 8) & 0xFF)
        frame.append(length & 0xFF)
        frame.append(msg_type)
        frame.append(flag)
        frame.extend(payload)

        # 计算CRC（低位在前，高位在后）
        crc = self.calculate_crc16(frame)
        frame.append(crc & 0xFF)           # 低位
        frame.append((crc >> 8) & 0xFF)    # 高位

        return bytes(frame)

    def handle_upgrade_request(self, frame_info):
        """处理升级请求"""
        payload = frame_info['payload']
        upgrade_flags = payload[0] if payload else 0

        print(f"[请求] 升级请求 - FPGA:{bool(upgrade_flags & 0x01)} DSP1:{bool(upgrade_flags & 0x02)} DSP2:{bool(upgrade_flags & 0x04)} ARM:{bool(upgrade_flags & 0x08)}")

        # 允许升级
        return self.build_response(self.MSG_UPGRADE_REQUEST, self.FLAG_ALLOW_UPGRADE, b'\x00')

    def handle_system_reset(self, frame_info):
        """处理系统复位"""
        print(f"[请求] 系统复位")

        # 模拟重启延迟
        time.sleep(0.5)

        # 重启成功
        return self.build_response(self.MSG_SYSTEM_RESET, self.FLAG_RESTART_SUCCESS, b'\x00')

    def handle_upgrade_command(self, frame_info):
        """处理升级指令"""
        payload = frame_info['payload']

        if len(payload) >= 8:
            file_size = struct.unpack('>I', payload[0:4])[0]
            packet_count = struct.unpack('>H', payload[4:6])[0]
            file_crc = struct.unpack('>H', payload[6:8])[0]

            self.expected_file_size = file_size
            self.expected_packet_count = packet_count
            self.received_packets = 0

            device_name = {
                self.MSG_ARM_COMMAND: "ARM",
                self.MSG_FPGA_COMMAND: "FPGA",
                self.MSG_DSP1_COMMAND: "DSP1",
                self.MSG_DSP2_COMMAND: "DSP2"
            }.get(frame_info['msg_type'], "未知")

            print(f"[指令] 升级{device_name} - 文件大小:{file_size}字节, 包数:{packet_count}, CRC:0x{file_crc:04X}")

        # 模拟擦除Flash
        time.sleep(0.3)

        # 擦除成功
        return self.build_response(frame_info['msg_type'], self.FLAG_ERASE_SUCCESS, b'\x00')

    def handle_upgrade_data(self, frame_info):
        """处理升级数据"""
        payload = frame_info['payload']

        if len(payload) >= 2:
            packet_num = struct.unpack('>H', payload[0:2])[0]
            data = payload[2:]

            self.received_packets += 1

            # 每10包打印一次进度
            if packet_num % 10 == 0 or packet_num == self.expected_packet_count:
                progress = (self.received_packets * 100) // self.expected_packet_count if self.expected_packet_count > 0 else 0
                print(f"[数据] 包序号:{packet_num}/{self.expected_packet_count} 数据大小:{len(data)}字节 进度:{progress}%")

            # 构建响应payload: status(1) + packet_num(2) + received_count(2)
            response_payload = struct.pack('>BHH', 0x00, packet_num, self.received_packets)

            return self.build_response(frame_info['msg_type'], self.FLAG_SUCCESS, response_payload)

        return None

    def handle_upgrade_end(self, frame_info):
        """处理升级结束"""
        print(f"[结束] 升级结束 - 共接收{self.received_packets}个数据包")

        # 升级结束成功
        return self.build_response(frame_info['msg_type'], self.FLAG_UPGRADE_END, b'\x00')

    def handle_total_end(self, frame_info):
        """处理总体结束"""
        print(f"[完成] 总体结束")

        return self.build_response(self.MSG_TOTAL_END, self.FLAG_SUCCESS, b'\x00')

    def process_data(self):
        """处理接收的数据"""
        buffer = bytearray()

        try:
            while self.running:
                # 检查是否有数据可读
                if self.serial_conn.in_waiting > 0:
                    data = self.serial_conn.read(self.serial_conn.in_waiting)
                    buffer.extend(data)

                    # 查找并处理完整帧
                    while len(buffer) >= 5:
                        # 查找帧头
                        header_pos = -1
                        for i in range(len(buffer) - 1):
                            if buffer[i:i+2] == self.MASTER_HEADER:
                                header_pos = i
                                break

                        if header_pos < 0:
                            buffer.clear()
                            break

                        # 删除帧头之前的数据
                        if header_pos > 0:
                            buffer = buffer[header_pos:]

                        # 检查长度字段
                        if len(buffer) < 5:
                            break

                        length = (buffer[3] << 8) | buffer[4]

                        # 等待完整帧
                        if len(buffer) < length:
                            break

                        # 提取完整帧
                        frame = bytes(buffer[:length])
                        buffer = buffer[length:]

                        # 显示接收的数据
                        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
                        print(f"\n[{timestamp}] 接收帧 ({len(frame)}字节): {frame.hex(' ').upper()}")

                        # 解析帧
                        frame_info = self.parse_frame(frame)
                        if not frame_info:
                            continue

                        # 根据报文类型处理
                        response = None
                        msg_type = frame_info['msg_type']

                        if msg_type == self.MSG_UPGRADE_REQUEST:
                            response = self.handle_upgrade_request(frame_info)
                        elif msg_type == self.MSG_SYSTEM_RESET:
                            response = self.handle_system_reset(frame_info)
                        elif msg_type in [self.MSG_ARM_COMMAND, self.MSG_FPGA_COMMAND,
                                         self.MSG_DSP1_COMMAND, self.MSG_DSP2_COMMAND]:
                            response = self.handle_upgrade_command(frame_info)
                        elif msg_type in [self.MSG_ARM_DATA, self.MSG_FPGA_DATA,
                                         self.MSG_DSP1_DATA, self.MSG_DSP2_DATA]:
                            response = self.handle_upgrade_data(frame_info)
                        elif msg_type in [self.MSG_ARM_END, self.MSG_FPGA_END,
                                         self.MSG_DSP1_END, self.MSG_DSP2_END]:
                            response = self.handle_upgrade_end(frame_info)
                        elif msg_type == self.MSG_TOTAL_END:
                            response = self.handle_total_end(frame_info)

                        # 发送响应
                        if response:
                            timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
                            print(f"[{timestamp}] 发送响应 ({len(response)}字节): {response.hex(' ').upper()}")
                            self.serial_conn.write(response)
                            self.serial_conn.flush()

                else:
                    # 没有数据时短暂休眠避免CPU占用过高
                    time.sleep(0.01)

        except Exception as e:
            print(f"[错误] 处理数据时出错: {e}")

    def start(self):
        """启动串口服务器"""
        self.running = True

        try:
            # 打开串口
            self.serial_conn = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=1.0,
                write_timeout=1.0
            )

            print("=" * 60)
            print(f"BootLoader串口测试服务器已启动")
            print(f"串口: {self.port}")
            print(f"波特率: {self.baudrate}")
            print(f"启动时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
            print("=" * 60)
            print("等待数据...\n")

            # 在主线程中处理数据
            self.process_data()

        except serial.SerialException as e:
            print(f"[错误] 串口打开失败: {e}")
            print(f"\n可用串口列表:")
            list_serial_ports()
        except Exception as e:
            print(f"[错误] 服务器启动失败: {e}")
        finally:
            if self.serial_conn and self.serial_conn.is_open:
                self.serial_conn.close()
            print("\n服务器已停止")

    def stop(self):
        """停止服务器"""
        self.running = False
        if self.serial_conn and self.serial_conn.is_open:
            self.serial_conn.close()


def list_serial_ports():
    """列出所有可用的串口"""
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("  未找到可用串口")
    else:
        for port in ports:
            print(f"  {port.device} - {port.description}")


def main():
    """主函数"""
    import sys

    # 默认参数
    port = 'COM2'
    baudrate = 115200

    # 解析命令行参数
    if len(sys.argv) > 1:
        port = sys.argv[1]
    if len(sys.argv) > 2:
        try:
            baudrate = int(sys.argv[2])
        except ValueError:
            print(f"无效波特率: {sys.argv[2]}")
            return

    # 如果请求列出串口
    if port.lower() in ['list', 'ls', '-l']:
        print("可用串口:")
        list_serial_ports()
        return

    server = BootLoaderSerialServer(port, baudrate)

    try:
        server.start()
    except KeyboardInterrupt:
        print("\n\n收到退出信号...")
        server.stop()


if __name__ == '__main__':
    main()
