# BootLoader - 嵌入式固件升级工具

[![Qt Version](https://img.shields.io/badge/Qt-6.8.1-green.svg)](https://www.qt.io/)
[![C++](https://img.shields.io/badge/C++-17-blue.svg)](https://isocpp.org/)
[![Platform](https://img.shields.io/badge/platform-Windows-lightgrey.svg)](https://www.microsoft.com/windows)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

> 基于 Qt 6.8.1 开发的嵌入式固件升级工具，支持串口/网口双模式，可对 FPGA、DSP、ARM 等多种设备进行固件升级。

## ✨ 特性

- 🔌 **双通信模式** - 支持串口（RS232/USB-TTL）和网口（TCP）
- 🎯 **多设备支持** - FPGA、DSP1、DSP2、ARM 四种设备类型
- 📦 **实时进度** - 显示升级进度和详细状态信息
- 📝 **日志记录** - 完整的通信日志，便于调试和问题排查
- 🔄 **智能重试** - 自动超时检测和重传机制
<img width="1055" height="819" alt="image" src="https://github.com/user-attachments/assets/3f495990-18b1-497a-b281-ed3bb668ccc4" />

---

## 项目结构

```
BootLoader/
├── bin/                              # 可执行文件目录
│   ├── BootLoader.exe                # 编译后的可执行程序
│   ├── BootLoader 使用说明.md         # 软件使用手册
│   └── bootloader.log                # 运行日志文件（自动生成）
│
├── doc/                              # 文档目录
│   └── BootLoader流程协议解析.md      # 通信协议详细说明
│
├── gui/                              # 界面设计文件
│   └── mainwindow.ui                 # Qt Designer UI 文件
│
├── inc/                              # 头文件目录
│   ├── communication.h               # 通信管理器类
│   ├── mainwindow.h                  # 主窗口类
│   ├── protocol.h                    # 协议解析类
│   └── upgrade.h                     # 升级管理器类
│
├── src/                              # 源文件目录
│   ├── communication.cpp             # 串口/TCP 通信实现
│   ├── main.cpp                      # 程序入口（含试用期验证）
│   ├── mainwindow.cpp                # 主窗口实现
│   ├── protocol.cpp                  # 协议编码/解码实现
│   └── upgrade.cpp                   # 升级状态机实现
│
├── test/                             # 测试工具目录
│   ├── test_TCP.py                   # TCP 网口测试服务器（模拟下位机）
│   └── test_COM.py                   # 串口测试服务器（模拟下位机）
│
├── BootLoader.pro                    # Qt 项目文件
├── README.md                         # 项目说明文档（本文件）
└── .gitignore                        # Git 忽略配置

```

---

## 核心模块说明

### 1. 通信模块 (`communication.cpp/h`)
负责底层串口和 TCP 通信

### 2. 协议模块 (`protocol.cpp/h`)
实现 BootLoader 协议的编码和解析

### 3. 升级管理模块 (`upgrade.cpp/h`)
管理整个升级流程状态机

### 4. 主窗口模块 (`mainwindow.cpp/h`)
提供用户交互界面

---

## 编译和构建

### 开发环境要求
- **Qt 版本**：6.8.1 或更高
- **编译器**：MinGW 64-bit（Windows）
- **IDE**：Qt Creator 13.0+ 或 Visual Studio 2019+
- **CMake**：3.16+ （可选）

---

## 测试方法

项目提供了两个 Python 测试脚本，用于模拟下位机响应，便于在没有实际硬件时进行功能测试。

### 测试工具说明

#### 1. TCP 网口测试 (`test_TCP.py`)
模拟通过 TCP 网络连接的下位机。

#### 2. 串口测试 (`test_COM.py`)
模拟通过串口连接的下位机。

---

## 协议说明

详细的通信协议请参考：[doc/BootLoader流程协议解析.md](doc/BootLoader流程协议解析.md)

### 协议要点
- **帧头**：上位机 `0xAA 0x55`，下位机 `0x55 0xAA`
- **长度**：大端序 2 字节，表示整帧长度
- **报文类型**：区分请求、数据、结束等不同阶段
- **应答标识**：上位机固定 `0xFE`，下位机表示执行状态
- **CRC**：CRC16-MODBUS，高位在前

### 升级流程
```
上位机                                     下位机
  |                                          |
  |--- 0x01 升级请求 ----------------------->|
  |<-- 0x01 允许升级 (FLAG=0x04) ------------|
  |                                          |
  |--- 0x02 系统复位 ----------------------->|
  |<-- 0x02 重启成功 (FLAG=0x0C) ------------|
  |                                          |
  |--- 0x06 FPGA升级指令 -------------------->|
  |<-- 0x06 擦除Flash成功 (FLAG=0x0A) --------|
  |                                          |
  |--- 0x07 FPGA数据包 1 -------------------->|
  |<-- 0x07 命令执行成功 (FLAG=0x00) ---------|
  |--- 0x07 FPGA数据包 2 -------------------->|
  |<-- 0x07 命令执行成功 ---------------------|
  |    ... (循环发送所有数据包)                |
  |                                          |
  |--- 0x09 FPGA升级结束 -------------------->|
  |<-- 0x09 所有数据包发送成功 (FLAG=0x0E) ---|
  |                                          |
  |    ... (DSP1, DSP2, ARM 同样流程)         |
  |                                          |
  |--- 0x10 总体结束 ------------------------>|
  |<-- 0x10 命令执行成功 (FLAG=0x00) ---------|
  |                                          |
```

---

## 故障排查

### 常见问题

| 问题 | 可能原因 | 解决方法 |
|------|---------|---------|
| 串口打开失败 | 端口被占用 | 关闭其他串口工具，重新插拔设备 |
| TCP 连接超时 | IP 或端口错误 | 检查 IP 配置，使用 `ping` 测试 |
| CRC 校验失败 | 通信干扰或数据损坏 | 降低波特率，减小数据包大小 |
| 升级超时 | 设备响应慢或未响应 | 检查设备状态，重启设备 |

### 调试建议
1. **启用日志**：勾选日志功能，便于事后分析
2. **降低速度**：减小数据包大小（如 512），降低传输速率
3. **隔离测试**：先用测试脚本验证通信，再测试真实设备
4. **检查硬件**：确认线缆质量、接口牢固、供电稳定

---

## 开发指南

### 添加新设备类型
1. 在 `protocol.h` 中添加新的报文类型常量
2. 在 `upgrade.cpp` 中的设备列表添加新设备
3. 在 `mainwindow.ui` 中添加对应的复选框和文件选择控件
4. 更新状态机逻辑，添加新设备的升级流程

### 修改通信参数
- **串口波特率**：修改 `communication.cpp` 中的 `openSerial()` 函数
- **TCP 端口**：修改界面默认值和 `openTcp()` 函数
- **超时时间**：修改 `upgrade.cpp` 中的超时定时器时长

### 扩展协议
如需支持新的应答标识或报文类型，需同步修改：
1. `protocol.h` - 添加常量定义
2. `protocol.cpp` - 更新描述映射函数
3. `upgrade.cpp` - 更新状态机处理逻辑

---
