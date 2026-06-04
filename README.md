# 4G 嵌入式无线通信控制项目

本项目是一个基于 **STM32** 微控制器和 **FreeRTOS** 实时操作系统的嵌入式开发项目，主要实现通过 4G 无线通信模块进行数据传输与远程控制功能。

## 🛠️ 开发环境与技术栈

- **核心处理器**: STM32 (如 STM32F1 系列)
- **实时操作系统**: FreeRTOS
- **配置工具**: STM32CubeMX
- **集成开发环境 (IDE)**: Keil MDK-ARM (V5)
- **外设与通信**: 4G 模块 (通过 UART/AT 指令控制)、GPIO、定时器等

## 📂 目录结构说明

```text
4g_project/
├── 4g_project.ioc         # STM32CubeMX 配置文件
├── .mxproject             # CubeMX 项目生成元数据
├── ignore.gitignore       # 针对本工程的 Git 忽略规则
├── Code/                  # ⭐ 自定义应用层代码（包含业务逻辑、4G 模块驱动与数据解析等）
├── Core/                  # MCU 底层核心代码（包含 CubeMX 生成的 main.c 及中断服务函数等）
├── Drivers/               # STM32 HAL 库及硬件底层驱动
├── MDK-ARM/               # Keil MDK 编译工程目录及启动文件
└── Middlewares/           # 中间件，包含 FreeRTOS 源码及配置文件
