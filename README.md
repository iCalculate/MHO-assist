# MHO QtScrcpy

面向《怪物猎人：旅人》的 Windows 安卓投屏工具，基于 QtScrcpy 4.2.1。方向助手直接读取投屏视频帧，在同一个投屏窗口内定位、识别并输入方向序列。游戏键位、按键编辑器、顶部诊断和手机光标都在整包内。

**[下载 Windows x64 整包](https://github.com/iCalculate/MHO-assist/releases/latest)** · [源码与构建](https://github.com/iCalculate/MHO-assist/blob/main/docs/build-qtscrcpy.md)

## 在新电脑上启动

1. 在 Releases 的 **Assets** 下载 `MHO-QtScrcpy-v4.2.1-mho.4-windows-x64.zip`，完整解压到可写目录。双击 `MHO-QtScrcpy\QtScrcpy.exe`。
2. 在 Android 手机上开启 **USB 调试**。通过 USB 数据线连接电脑，解锁手机并确认调试授权。在 QtScrcpy 刷新设备，选中手机并启动投屏。
3. 在主控窗口的键位脚本列表选 **Generic-Alt**，点击 **应用**。回到投屏窗口按 **Ctrl** 切换为游戏映射。用 **W/A/S/D** 和方向键先手动确认触点位置。
4. 游戏界面或手机比例变化时，点投屏窗口顶部的 **按键配置**。可以抓取当前手机画面当作背景，调整触点并保存。回 QtScrcpy 刷新脚本并重新应用，再按 Ctrl 恢复映射。

发布包内的 `keymap/Generic-Alt.json` 包含当前使用的配置，包括鼠标中键轮盘和 V 键拖动。已有安装可在 **打开键位目录** 中备份旧文件，再复制新版 `Generic-Alt.json` 并刷新、重新应用脚本。

## 用鼠标选锁定轮盘

在 **按键配置** 里抓取游戏画面，把 **鼠标拖轮盘** 拖到手机上按住即可打开轮盘的位置。默认绑定鼠标中键。保存当前配置后，在主控窗口重新应用键位脚本，按 **Ctrl** 开启映射。

按住中键并移动鼠标来选择部位，松开即向游戏提交选中的方向，随后继续用鼠标转视角。拖动距离不足时可提高 **最大拖动距离**，鼠标移动太慢或太快时可调整 **拖动灵敏度**。

## 投屏窗口里的方向助手

在游戏跳舞界面点击顶部 **方向助手** 或按 **F8**。助手会根据右侧四方向按钮自动定位序列区域。点 **调整范围** 可以拖动或缩放识别框，退出编辑后会按设备记忆范围。点 **自动定位** 恢复自动识别区域。**F9** 可停止，失焦和调整范围时也会停止输入。

顶部面板显示 FPS、丢帧、发送队列、界面延迟、触点和历史按键。通过主控窗口的 **显示帧率** 选项控制面板显示。**手机光标** 用于普通鼠标模式，手机端首次使用时按提示授予悬浮窗权限。

## 钓鱼助手

在钓点点击投屏工具栏的 **钓鱼助手** 后，可以切到其他窗口或最小化投屏。钓点退出后，完成卡会显示本轮数量、鱼名和捕获时间。工具栏旁的箭头可以打开 **渔获记录**。运行依赖见[钓鱼助手构建说明](docs/build-qtscrcpy.md#钓鱼助手)。

## 配置与迁移

- 主控窗口的设置和已应用的键位文件在 `%APPDATA%\QtScrcpy\QtScrcpy\` 下。可以用设置里的 **打开键位目录** 定位到当前实际使用的脚本。
- 换电脑时可复制自己调好的 `Generic-Alt.json`，或从随包示例重新调整。识别框存在 EXE 同目录的 `direction-assist.ini`，按手机分别记录。
- 设备列表为空时，检查 USB 数据线及手机上的调试授权，再刷新。按键无反应时，确认脚本已应用、投屏窗口有焦点且已用 Ctrl 打开映射。

## 仓库结构

- `qtscrcpy/` 保存修改后的 QtScrcpy 和 QtScrcpyCore C++ 源码及原生测试。
- `mho_assist/` 提供方向识别进程、模板和独立助手源码。
- `integrations/` 保存键位编辑器及 Android 光标源码。
- `scripts/` 提供构建和打包工具，`tests/` 包含脱敏测试素材。

早期独立工具的使用说明见 [独立方向助手](https://github.com/iCalculate/MHO-assist/blob/main/docs/standalone-direction-assistant.md)。QtScrcpy 源码基于其 [上游 v4.2.1](https://github.com/barry-ran/QtScrcpy/tree/v4.2.1)，键位编辑器基于 [ScrcpyKeyMapper](https://github.com/w4po/ScrcpyKeyMapper)。
