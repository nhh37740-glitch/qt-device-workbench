# 设备测量、显示、保存与转发工具

三个独立 Windows 程序：`device-simulator.exe` 产生测量值，`device-workbench.exe` 接收、显示、保存和转发，`result-receiver.exe` 接收转发结果。
这是原创 Qt/C++ 项目，参考成熟上位机的通用工作流程；不是 Serial Studio 分支。

上位机四个模块分别执行四种职责：接收并处理数据、显示和操作（含曲线）、保存和读取记录、向下游发送结果。每个业务模块以独立 DLL 交付，主程序仅连接接口。
上位机有四条工作线：接收处理、界面、文件、下游推送。跨线程用 Qt 排队交付值副本，程序之间用 TCP 一行一条 JSON 消息。默认只连接本机。

## 启动二进制交付

解压交付 ZIP；不需要安装 Qt SDK或编译器。在 `programs` 下的三个目录分别启动模拟设备、下游接收程序和上位机。
双击交付包的 `start-demo.cmd`，会启动三个程序并开始回放、保存和转发。关闭界面后，启动器会关闭自己启动的两个后台服务。也可以运行 `start-demo.ps1`；默认端口分别为 9101、9102。

## 1.1.0 界面与公开数据

界面以温度、湿度、电压和曲线为中心，把连接、文件、下游和故障设置放入侧边区域；状态与详细日志分开展示。窗口适配检查覆盖 1020×720、1100×780 和 1280×860。曲线只在收到数据或改变布局时重绘。

模拟设备默认使用 [MIT 发布的 Intel Berkeley 实验室公开记录](http://db.csail.mit.edu/labdata/labdata.html)：4096 条温度、湿度和电压实测记录。本项目模拟的是发送这些记录的设备；界面明确标注实测数据回放。回放间隔仅控制发送速度，记录时间来自原始文件；数据末尾停止发送。

[数据来源与复核方法](data/README.md)解释选择规则、贡献者、许可、原始时区未注明的处理及校验值。原始选中行也随源码保留，不把传输序号误称为原始采样序号。

`device-simulator --data path/to/records.csv` 可指定同格式数据。缺失或无效文件会启动失败。`--synthetic` 是明确选择旧正弦数据的通信测试模式，与 `--data` 不能同时使用。

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
