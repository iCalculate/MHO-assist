# 构建 Windows x64 整包

构建 QtScrcpy 使用 MSVC x64 开发者命令行、CMake、Ninja 和 **Qt 5.15.2 MSVC x64**。Python 3.11+ 用于打包内嵌的识别进程。Qt 运行库从解压后的上游 QtScrcpy v4.2.1 Windows x64 包取得，解压目录应直接含 `Qt5Core.dll`。 [上游 Windows 运行包](https://github.com/barry-ran/QtScrcpy/releases/tag/v4.2.1)

发布版使用保留 H.264 解码与 MP4 录制的 FFmpeg 4.4.2 LGPL 动态库。下载并解压对应源码后，在继承 MSVC x64 环境的 MSYS2 shell 中运行以下命令。PATH 中需有 GNU Make 和 NASM。 [FFmpeg 源码](https://ffmpeg.org/releases/ffmpeg-4.4.2.tar.xz) · [FFmpeg 的 MSVC 构建说明](https://ffmpeg.org/platform.html#Microsoft-Visual-C_002b_002b-or-Intel-C_002b_002b-Compiler-for-Windows)

```bash
bash scripts/build_ffmpeg.sh /c/Build/ffmpeg-4.4.2 /c/Build/ffmpeg-sdk
```

```powershell
python -m pip install -r scripts/requirements-build.txt
python -m scripts.build_qtscrcpy --qt "C:\Qt\5.15.2\msvc2019_64" --runtime "C:\Build\QtScrcpy-win-x64-v4.2.1" --ffmpeg "C:\Build\ffmpeg-sdk" --pointer-apk "C:\Build\qtscrcpy-pointer.apk" --version v4.2.1-mho.5
```

`--pointer-apk` 指向已签名的 Android 光标组件，可用本仓库的 [Android 构建说明](development.md#可选集成)生成。发布包的 APK 与已有魔改版使用同一已签名的构建。私有签名文件留在开发机。

首次构建自动从固定的 QtScrcpyCore 提交获取 ADB 和 Android 投屏服务端。构建完成后，脚本在干净目录里解压产物，验证 QtScrcpy 主窗口、键位读取和保存、WASD 与触点释放、顶部面板布局，以及投屏帧到内嵌识别进程的方向输入。可发布的 ZIP 和 SHA-256 校验文件在 `dist/`。

## 组件

QtScrcpy 主程序直接负责投屏、映射与输入。`DirectionWorker/DirectionWorker.exe` 是它启动的识别进程，通过逐行 JSON 接收投屏画面并返回方向。在投屏窗口点击「按键配置」时，同一进程在本机回环地址提供独立的编辑会话。Qt 窗口通过选中的设备序列号请求实时截图，编辑后保存到当前用户的 `Generic-Alt.json`。

## 钓鱼助手

连接手机后，在投屏中进入钓点，点击工具栏的「钓鱼助手」。按钮旁的箭头可以选择鱼竿、鱼饵和查看「渔获记录」。启动后可切到其他窗口或最小化投屏。回到投屏后点击助手按钮或按 Esc 可以暂停。

助手从中文横屏投屏帧识别落点、鱼标和收线仪表。连续看不到鱼标后会退出钓点，完成卡显示本轮数量、鱼名和捕获时间。捕获记录保存在当前 QtScrcpy 配置目录的 `fishing-history.json`，关闭应用后仍可查看。

识别进程需要可从 PATH 启动的 Python 3.10 或更新版本，以及 `opencv-python` 和 `numpy`。Windows 构建使用支持 C++/WinRT 的 Windows SDK。鱼名可根据结算画面匹配，其他名称使用 Windows 的简体中文 OCR。没有读出名字时，记录仍保留并显示「鱼名未识别」。

```powershell
python -m pip install opencv-python numpy
python integrations/fishing/worker.py --replay "path/to/recording.mp4"
```

## 录制训练数据

连接手机前勾选 **record screen + inputs**，选择 **MP4**，保持投屏显示开启。结束时断开投屏或正常退出程序。同一目录下会生成视频和同名的 `.inputs.jsonl`，例如 `session.mp4` 与 `session.mp4.inputs.jsonl`。

| 记录 | 常用字段 |
| --- | --- |
| `keymap` | `name`、`keymap_revision` 和完整的 `mapping`。每次应用映射会写入一条新版本。 |
| `key`、`mouse`、`wheel` | 按下、松开、自动重复、按住时长、鼠标位置和滚轮增量。用 `press_seq` 把同一次按下、重复、松开或取消关联起来；真实松开有 `hold_us`，失焦或录制结束时仍按住的操作记为 `cancel`。`handled_locally` 标记在投屏窗口内处理的快捷键。 |
| `control` | 发往手机的触摸、Android 按键或其他控制指令。触摸记录包含指针 ID、动作和坐标。 `accepted` 表示本地控制 socket 接收了完整消息。 `source: assistant` 标记助手生成的触控。 |
| `frame` | `frame_index`、`capture_us`、封装后的视频时间 `video_us` 和帧时长 `duration_us`。 |
| `preview` | `frame_index` 和 `capture_us` 标识交付给 Qt 预览的帧，`host_us` 是交付时间。 |
| `end` | `video_complete`、已写入帧数 `frame_count` 和收到的帧数 `received_frame_count`。 |

训练时用输入记录的 `preview_frame_index` 查找同一文件内的 `frame.frame_index`，再通过 `frame.video_us` 从视频取帧。这是处理输入时最近交付给 Qt 预览的帧，`preview_age_us` 给出该帧已交付多久。较旧的记录或预览滞后时，可用 `preview_capture_us` 关联 `frame.capture_us`。后台录制可通过 `received_frame_index` 关联最近收到的帧。首帧到达前的输入仍有 `host_us`，收到帧后可用 `video_estimate_us` 查看以首帧到达为零点的时间。用于训练前检查 `end.video_complete` 和帧关联是否有效。所有 `*_us` 字段的单位是微秒，需要按采样时间排序时用 `host_us`，时间相同时用 `seq`。
