# 谁交给谁，以及接口实例

## 程序之间

模拟设备在127.0.0.1:9101接收连接，上位机主动连接它；下游在127.0.0.1:9102接收连接，上位机推送线程主动连接它。每条消息是UTF8的一行JSON，用换行判定完整消息；最大64KiB，支持分两次收到一条、一次收到多条。实例见examples/messages.ndjson。

测量值必含设备名、递增编号、测量时间、温度、湿度、电压。操作要求必含请求编号；设备回同一个编号的确认。下游只有验证并保存后才确认设备名和测量编号；上位机未收到确认就保留记录并重试，下游对重试去重。

## 模块和线程之间

公开接口全部定义在include/workbench/contracts.h，二进制实例由各DLL的wb_create_*导出工厂创建。

| 发出方 → 接收方 | 交接内容 | 调用及执行位置 |
|---|---|---|
| 模拟设备 → 接收处理 | 原始消息 | TCP，到接收处理线程 |
| 接收处理 → 前端 | Sample值副本 | sampleReady → showSample，界面线程 |
| 接收处理 → 保存记录 | Sample值副本 | sampleReady → append，文件线程 |
| 接收处理 → 下游推送 | Sample值副本 | sampleReady → enqueue，推送线程 |
| 前端 → 接收处理 | 连接、开始、停止、故障设置 | connectRequested/startRequested/stopRequested/simulationRequested → 接收线程对应槽 |
| 前端 → 保存记录 | 文件路径、停止保存、读取历史 | recordRequested/recordStopRequested/historyRequested → 文件线程 |
| 前端 → 下游推送 | 下游地址和端口 | downstreamRequested → connectSink，推送线程 |
| 保存记录 → 前端 | 历史记录和写入结果 | historyReady → showHistory；status/error → showStatus，界面线程 |
| 下游推送 → 前端 | 已确认编号、待确认数量、连接状态 | delivered/backlogChanged/status/error → 界面线程 |
| 推送队列满 → 接收处理 | 停止采集要求 | error含queue_full → stopMeasurements，接收线程 |

所有跨线程连接明确指定Qt::QueuedConnection；模块内同一线程的直接调用不需要队列。线程退出前在所属线程调用shutdown，再quit/wait。前端的画曲线属于前端模块，不独立交付另一个可视化模块。

## 二进制使用实例

examples/binary_consumer.cpp通过公开头文件、contracts导入库和QLibrary加载七个模块DLL，执行真实模拟采集、绘图接口、CSV保存及读取、下游确认。examples/CMakeLists.txt可以只链接交付SDK，无需任何模块实现源码。模块的module.json记录工厂、版本、依赖与运行时位置。

## 有限范围

接收端每次TCP重新连接建立新的去重会话，可接受重启设备重新从1计数。模拟器自身在同一进程内跨重连保持编号递增。下游按deviceId+sequence在当前进程内去重，因此设备进程重启时须使用新deviceId或同时重启下游；生产扩展应给记录增加启动会话标识，当前不宣称跨设备重启的永久唯一性。
记录最多读取100000条；曲线保留最多1000点；下游待确认最多256条，满则停止采集。下游离线补送只保证本次上位机进程内，退出时仍未确认会显示错误，不宣称持久化发送队列。
