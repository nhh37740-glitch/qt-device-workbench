# 模块负责人、二进制和验收

所有子代理均通过实际 collaboration.spawn_agent 创建，返回结果后由主代理编译、审查和集成验证；每个模块只有一位子代理写入。

| 负责人 | 拥有的模块 | 二进制 |
|---|---|---|
| device_simulation | 消息拆包与编码、模拟设备 | wb_wire_protocol.dll、wb_device_simulator.dll、device-simulator.exe |
| receive_and_display | 接收并检查数字、前端（含画曲线） | wb_device_source.dll、wb_frontend.dll |
| save_and_forward | 保存和读取、向下游发送、下游接收 | wb_record_store.dll、wb_result_push.dll、wb_result_receiver.dll、result-receiver.exe |
| 主代理 | 固定公共接口、连接四线程、构建、Git/GitHub、交付及独立验证 | wb_contracts.dll、device-workbench.exe、交付ZIP和SDK |

## 验收矩阵

| 验证对象 | 检查内容 | 证据 |
|---|---|---|
| 公共数据接口 | 必填字段、范围、整数、非法类型、输出不被失败请求改变 | contracts_test |
| 消息格式DLL | 部分消息、多条消息、非法JSON、超长消息后恢复 | wire_protocol_test |
| 模拟设备DLL | 开始停止、请求确认、参数检查、拆分发送、错误、延迟、断线和编号 | device_simulator_test |
| 接收处理DLL | 真正TCP接收、检查数字、重复过滤、重连、确认超时、线程归属 | device_source_test |
| 前端DLL | 所有按钮参数、数值、历史、曲线点数上限、实际绘制 | frontend_test |
| 记录DLL | 小数精度、带逗号/换行/引号的字段、错误文件、历史验证、线程归属 | record_store_test |
| 推送DLL | 确认匹配、超时补送、去重、未启用时不积压、队列上限、线程归属 | result_push_test |
| 下游DLL | 消息拆分合并、字段检查、重试去重、保存后才确认、文件错误 | result_receiver_test |
| 三个真实程序 | 正常接收/显示/保存/推送一致、故障拆包、坏消息过滤、设备重连、延迟、下游迟启动、错误参数、队列满停采、保存失败不阻挡推送 | process_test.py，9个场景 |
| 二进制独立运行 | 各程序目录自带DLL/Qt/VC运行库，PATH中移除SDK/构建目录，重复9个进程场景 | delivery-evidence |
| 模块独立二进制接口 | 只用交付头文件和contracts导入库重新编译调用者，动态加载七个业务DLL并完成真实数据链路 | examples/binary_consumer.cpp |
| 交付完整性 | 真正Windows x64 EXE/DLL、导入库、工厂调用、运行依赖、许可、所有文件SHA256 | scripts/audit_delivery.py、manifest.json |

## 交付布局

programs/<程序名>/：可执行文件及完整运行依赖；每个程序另有独立ZIP。
modules/<模块名>/：独立DLL、导入库、公开头文件、版本/依赖说明；每个模块另有独立ZIP。
shared/bin/：SDK使用的共同运行依赖；模块ZIP依赖此目录及module.json列出的其他模块。
shared/sdk/：无需模块实现源码即可链接的头文件与导入库。
test-evidence/：模块测试报告、源码构建进程测试摘要、二进制独立运行摘要、模块DLL调用证据。
licenses/：项目及Qt许可和软件成分说明。

不把测试通过视为产品完整性的替代：必须实际启动EXE，比较保存/推送的每个数值，检查界面图片可读，并调用各DLL导出接口。
