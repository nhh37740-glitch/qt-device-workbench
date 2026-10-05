# 设备测量、显示、保存与转发工具

三个独立 Windows 程序：`device-simulator.exe` 产生测量值，`device-workbench.exe` 接收、显示、保存和转发，`result-receiver.exe` 接收转发结果。
这是原创 Qt/C++ 项目，参考成熟上位机的通用工作流程；不是 Serial Studio 分支。

上位机四个模块分别执行四种职责：接收并处理数据、显示和操作（含曲线）、保存和读取记录、向下游发送结果。每个业务模块以独立 DLL 交付，主程序仅连接接口。
上位机有四条工作线：接收处理、界面、文件、下游推送。跨线程用 Qt 排队交付值副本，程序之间用 TCP 一行一条 JSON 消息。默认只连接本机。

## 启动二进制交付

解压交付 ZIP；不需要安装 Qt SDK或编译器。在 `programs` 下的三个目录分别启动模拟设备、下游接收程序和上位机。
也可以运行交付包的 `start-demo.ps1`，它会启动三个程序。前端选择连接设备、开始测量、保存记录、连接下游。默认端口分别为 9101、9102。

## 编译与验证

需要 MSVC 2022 x64、Qt 6.8+ (Core/Network/Widgets/Test)、CMake 3.24+、Python 3。项目源码不含预装工具链。

```powershell
./scripts/build.ps1 -QtRoot 'X:/path/to/Qt/6.8.3/msvc2022_64'
./scripts/test.ps1 -QtRoot 'X:/path/to/Qt/6.8.3/msvc2022_64'
./scripts/package.ps1 -QtRoot 'X:/path/to/Qt/6.8.3/msvc2022_64'
```

接口及功能边界见 [architecture](docs/architecture.md)，实例见 [messages](examples/messages.ndjson)。模块级测试与三个真实进程的端到端测试均须通过才可交付。

## 交付边界

下游断线后保留当前进程内未确认的记录，重连后补送；接收方去重后确认。退出并重启上位机不恢复未确认队列。队列达到256条会拒收新记录并停止采集，界面显示错误。
本项目实现模拟设备与网络通信，尚不接入真实串口硬件。图形界面与模拟设备均为真实 Windows 原生二进制。

版权所有 2026 Qt Device Workbench contributors。MIT 许可；随包 Qt 动态库遵循其第三方许可，见交付包 licenses。
