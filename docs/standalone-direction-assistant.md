# MHO Assist · 独立方向助手

Windows 上的《怪物猎人：旅人》跳舞辅助工具。它从前台游戏或投屏画面读取箭头序列，启用后按顺序发送方向键，并通过箭头变黄判断游戏反馈。

[独立助手源码 v5.0.0](https://github.com/iCalculate/MHO-assist/tree/v5.0.0) · [QtScrcpy 配置](qtscrcpy.md) · [源码与打包](https://github.com/iCalculate/MHO-assist/blob/main/docs/development.md)

## 安装与首次使用

1. 在 Releases 的 **Assets** 下载 `MHO-Assist-v5.0.0-windows-x64.zip`。将整个压缩包解压到可写目录，打开 `MHO-Assist\ArrowAssist.exe`。程序自带运行依赖，`_internal` 文件夹需与 EXE 放在一起。
2. 打开游戏窗口，或先用投屏软件连接手机。使用 QtScrcpy 时，按[配置指南](qtscrcpy.md)导入随包自带的键位示例。先在游戏中手动按 ↑ ↓ ← →，确认四个方向都对应正确。
3. 在助手下拉列表选择游戏或投屏窗口，点击 **启动预览**。找不到窗口时点 **刷新**，也可用 **框选** 框住画面，确保底部箭头序列和右侧四个方向按钮完整可见。框选中心要落在接收按键的窗口内。
4. 切回目标窗口，让跳舞画面处于前台且无遮挡。等助手识别到完整序列后按 **F8** 启用自动输入。游戏识别到按键时，对应箭头会变黄。

| 快捷键 | 操作 |
| --- | --- |
| **F8** | 在目标窗口切换自动输入和暂停 |
| **F9** | 立即暂停输入与连续刷 |
| **F10** | 切回已选的目标窗口 |

## 设置与排查

- **能识别但游戏没反应** · 检查投屏窗口是否在前台、键位映射是否启用，以及手动按方向键时游戏是否响应。统计里的「已确认」来自游戏内的黄色反馈。
- **一直在定位** · 放大投屏画面，确保右侧四键和底部序列都在画面内，点击 **重新定位**。固定框选随窗户移动后需重新框选。
- **偶尔漏键** · 在 **设置** 中适当增加按住和间隔，重新点击 **启动预览** 应用，再按 F8。可启用「未确认则暂停」观察漏键。
- **连续刷** · 首次启动默认关闭。手动验证 `Shift → [ → ] → ]` 可完成重开后，再打开 **连续刷** 并按 F8。结算或菜单页面无法确认时，助手会暂停流程。映射和页面布局见 [QtScrcpy 配置](qtscrcpy.md)。

## 数据与换电脑

便携版把设置保存在 EXE 旁的 `settings.json`，把历史、日志和诊断保存在 `data/`。换版本前关闭助手，再把旧数据复制到新版 EXE 所在目录。若从旧 `release_v5` 迁移，`settings.json` 在旧 EXE 旁，`data/` 在原项目根目录。

换电脑时，在 **投屏工具 → 配置 QtScrcpy 路径** 选择新电脑上的 `QtScrcpy.exe`，再选一次目标窗口。首次运行会自动创建所需目录。

## 从源码运行

在 Windows 的项目根目录运行。发布包使用 Python 3.12 x64 构建。

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
.\.venv\Scripts\python.exe -m mho_assist
```

源码和识别模板在 `mho_assist/`，打包及离线工具在 `scripts/`，测试和脱敏素材在 `tests/`，可选组件在 `integrations/`。测试、离线回放和发布包构建命令见[开发文档](https://github.com/iCalculate/MHO-assist/blob/main/docs/development.md)。
