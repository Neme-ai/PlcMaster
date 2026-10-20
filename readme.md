# PlcMaster

基于 Qt Widgets 与 Snap7 的 Windows x64 PLC 调试工具，当前版本以 47 个转台/导轨预置点位为主，包含独立 M5.0 故障指示、手动单个位写入与可调 FeedingDog 脉冲。

## 当前功能

- 47 个预置 M 位，支持分组和搜索；名称、地址及中文说明见 `points.txt`。
- 顶部固定显示只读 M5.0：1 为故障、0 为无故障，未连接或读取失败为未知；不计入 47 点。
- 刷新周期 5–5000 ms，默认 200 ms；显示实测刷新间隔及读取耗时。
- 写入默认关闭。前 20 个控制点可手动写 0/1，后 27 个反馈点需单独解锁；M5.0 始终禁止写入。
- 采用 S7 单个位写入，记录原值、目标值、发送结果及读回值。ERR_RESET 为普通位，不自动复位。
- FeedingDog（M22.1）周期 5–600000 ms，高/低电平各至少 1 ms，默认周期 1000 ms、高电平 100 ms；不自动修改 EnableDog。
- 脉冲运行时其他点位仍可写入，仅 FeedingDog 自身禁止手动覆盖；反馈位仍需解锁。
- 写入或读回失败时不自动重试，重连不自动恢复脉冲；正常停止时尝试将可能为高的 FeedingDog 归零并记录结果。

## 构建和运行

需要 Windows x64、Qt 5.12.10 MSVC2017_64、Visual Studio Build Tools 2019（v142）及 Windows SDK。仓库包含原项目使用的 x64 Snap7 头文件、导入库和 DLL。

请将仓库克隆到英文路径，例如 `C:\Projects\PlcMaster`。Qt Creator 打开根目录 `PlcQtDebugTool.pro`，选择对应 x64 Kit 后编译运行。不要使用旧版本的 `.pro.user` 配置。

PowerShell 构建：

```powershell
.\tools\build.ps1 -Configuration Release -DeployRelease
# Qt 路径不同时：
.\tools\build.ps1 -Configuration Release -DeployRelease -QtDir 'C:\Qt\5.12.10\msvc2017_64'
```

默认 Qt 位置为 `C:\Qt\Qt5.12.10\5.12.10\msvc2017_64`；可用 `-VisualStudioDir` 指定 VS2019 安装目录。
独立运行包生成在 `.build/package-时间戳`。运行其中的 `PlcQtDebugTool.exe`，输入 PLC IP 并点击连接。

## 网络与使用

界面默认 PLC IP 为 `192.168.3.2`，请按现场实际地址修改。直连时电脑有线网卡应与 PLC 同网段；本项目已在 `192.168.3.100/24` 连接 `192.168.3.2` 的环境验证读取。
使用 VPN TUN 时应确认 PLC 流量走有线网卡，不能仅凭 ping 判断 S7 通信正常。

选中点位后开启写入模式，核对地址再写 0/1；信号方向是点表说明，不代表所有反馈值都能持久改变，PLC 程序可能覆盖反馈位。
操作日志保存在运行程序目录的 `logs/`。日志目录不可写时会禁用后续普通写入。

5 ms 是可配置下限，不是硬实时保证。网络通信、PLC 扫描、日志和手动读写均会影响实际脉冲；耗时超限时程序停止脉冲。断网、断电或强制终止时不能保证最后一次高电平归零。

## 测试与验收边界

```powershell
.\tests\run-tests.ps1
```

离线测试通过模拟 S7 接口运行，不连接 PLC，覆盖写入权限、单个位保留相邻位、反馈解锁、M5.0 只读、脉冲控制、异常处理及实测计时。测试结果写入 `tests/test-results.txt`（不提交）。
2026-10-09 已完成现场只读连接和监控检查；现场写入、设备物理含义及真实脉冲波形未验收。`--ui-test` 为离线界面检查；`--smoke-test` 会连接默认 PLC 进行短时只读检查，不属于离线测试。

## 项目结构

- `main.cpp`、`mainwindow.*`：界面、显示与交互
- `plcworker.*`：通信线程、监控、写入和脉冲
- `plcsnap7client.*`：Snap7 接口封装
- `points.txt`、`points.qrc`：内置 47 点表及资源
- `tools/`：构建、部署
- `tests/`：离线模拟回归测试
- `thirdparty/snap7/`：第三方依赖；其许可独立于本仓库 LICENSE

当前源码替换旧版本；旧源码仍可通过 Git 提交历史查看。构建缓存、机器配置、现场日志及本地历史运行包不纳入仓库。

2026-10-20 仓库同步验证：当前仓库副本 Release 编译通过，42 项离线回归测试通过；本次未连接或写入现场 PLC。
