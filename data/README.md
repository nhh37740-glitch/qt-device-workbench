# 公开实测数据驱动的设备回放

来源：[MIT 发布的 Intel Berkeley Research Lab 数据](http://db.csail.mit.edu/labdata/labdata.html)。原始数据来自实验室传感器；本程序把保存的记录依次通过 TCP 发出，模拟设备工作。它不会生成新的测量，也不接入实验室实时设备。

本演示选取节点 1 按记录时间排序的前 4096 条完整、有限且符合通信范围的记录。温度为 °C，湿度为相对湿度 %，电压为 V。没有插值、缩放或裁剪数值；不符合范围的原始记录不会送入模拟设备，筛选数量见 provenance.json。

- `intel-lab-mote1.csv`：程序读取的六列记录。
- `intel-lab-mote1.source.txt`：所选记录在原始文件中的完整行，保留源 epoch、光照及原始小数。
- `provenance.json`：原始下载地址、完整压缩包 SHA256、筛选规则、时间约定、贡献者和文件校验值。

原始时区未注明。为保留原始钟表时间，通信使用“将该钟表时间视作 UTC”的毫秒编码，显示时也采用相同约定；这不表示原始测量发生于 UTC。源时间的微秒精度截取到毫秒；新的传输序号不是源 epoch。回放间隔控制发送速度，不代表原始采样时间间隔；数据结尾停止，不循环或补造记录。

发布者允许使用和转载数据，并要求致谢。感谢 Peter Bodik、Wei Hong、Carlos Guestrin、Sam Madden、Mark Paskin 和 Romain Thibaux，以及相关实验室和 TinyOS 团队。数据许可独立于本项目的 MIT 代码许可；公开数据自身可能包含噪声和异常。

验证随包数据：`python scripts/import_public_data.py`。
从完整源包重建：从来源页下载 `data.txt.gz`，执行 `python scripts/import_public_data.py --archive path/to/data.txt.gz`。只有与已核实 SHA256 相同的原始压缩包才被接受。
