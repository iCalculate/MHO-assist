# 在新电脑上配置 QtScrcpy

## 连接手机与导入键位

1. 下载并解压 QtScrcpy 的 Windows 版。在 Android 上打开 USB 调试，通过 USB 连接并允许这台电脑调试。启动 QtScrcpy，在设备列表选择手机并开启投屏。 [QtScrcpy 官方下载](https://github.com/barry-ran/QtScrcpy/releases)
2. 从助手的解压目录找到 [MHO-Dance.json](../integrations/qtscrcpy/MHO-Dance.json)，复制到 `QtScrcpy.exe` 所在目录的 `keymap/`。在 QtScrcpy 面板刷新脚本，选择 **MHO-Dance** 并应用到当前投屏。 [QtScrcpy 官方键位说明](https://github.com/barry-ran/QtScrcpy/blob/dev/docs/KeyMapDes_zh.md)
3. 点一下手机投屏画面，按一次 **Ctrl** 进入该示例的自定义映射模式。进入跳舞界面，用电脑上的 ↑ ↓ ← → 分别确认游戏右侧四个按钮响应。退出或重新进入投屏时以 QtScrcpy 的当前映射状态为准。
4. 在助手里手动选择显示手机画面的窗口，点击 **启动预览**。也可以在 **投屏工具 → 配置 QtScrcpy 路径** 保存 `QtScrcpy.exe`，以后通过 **打开 / 连接 QtScrcpy** 启动。运行多个投屏时手动选择所需窗口。回到投屏窗口用 **F8** 启用助手。

## 调整触点

示例的触点按约 **1080 × 484** 的横屏游戏画面标定。手机比例、游戏 UI 布局或语言不同，须在当前设备上核对。编辑 `MHO-Dance.json` 中对应按键的 `pos`，用触点在**手机画面内**的像素位置除以画面宽高得到 `x`、`y`，原点为左上角，计算时排除投屏窗口的标题栏和黑边。保存后在 QtScrcpy 刷新并重新应用。

| JSON 键名 | 电脑按键 | 游戏目标 |
| --- | --- | --- |
| `Key_Up`, `Key_Down`, `Key_Left`, `Key_Right` | ↑ ↓ ← → | 跳舞画面右侧四个按钮 |
| `Key_Shift` | Shift | 从结算回到可匹配页面 |
| `Key_BracketLeft` | [ | 点「立即匹配」 |
| `Key_BracketRight` | ] | 依次确认重开的两个页面。两处确认按钮需可用同一触点 |

只使用方向识别时，保持助手的 **连续刷** 开关关闭即可。启用前先按 **F9** 暂停助手，手动走一遍 `Shift → [ → ] → ]`。完成后，助手的连续刷会在识别到结算或可匹配页面时核验各步页面变化。示例对应中文游戏页面。

桌面「恢复游戏控制」「清理手机残留触点」「编辑按键位置」等入口沿用已有的本机工具。新设备可直接在 QtScrcpy 里重连和编辑映射。
