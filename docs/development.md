# 开发与打包

## 目录

| 目录 | 内容 |
| --- | --- |
| `mho_assist/` | 界面、方向识别、Windows 输入与运行统计，模板随包放在 `templates/` 子目录 |
| `scripts/` | 离线回放、模板提取、PyInstaller 配置和发布打包 |
| `tests/` | 单元测试、脱敏图像素材和可手动运行的 Windows 输入检查 |
| `integrations/` | QtScrcpy 键位示例与可选的 Android 光标组件 |
| `docs/` | 安装、配置和[版本沿革](history.md) |

`python -m mho_assist` 从源码启动。`python assist.py` 和旧 CMD 启动器仍可使用。已有 `release_v*/ArrowAssist/` 的本地工作区保留 CMD 对这些版本的优先启动，源码运行继续使用仓库根目录的 `settings.json` 和 `data/`。

## 测试与离线回放

安装根目录的 `requirements.txt` 后，从根目录运行以下命令。回放从指定视频生成 `analysis/replay.json` 和箭头裁图；`--auto` 使用窗口自动定位。模板提取针对原始 1080 × 484 参考视频，会覆盖仓库内的识别模板。

```powershell
python -m unittest discover -s tests -v
python -m mho_assist --smoke-test
python -m scripts.replay "D:\videos\reference.mp4" --auto
python -m scripts.prepare_templates --help
```

`python tests/native_input_check.py` 仅向其创建的前台测试窗口发送方向键，用于手动验证 Windows 输入。

## 生成 Windows x64 便携包

在 Windows 的干净虚拟环境中使用 Python 3.11 或以上的 x64 版，打包脚本会在受限 PATH 下从中文路径解压并运行识别与界面启动检查。

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r scripts/requirements-build.txt
.\.venv\Scripts\python.exe -m unittest discover -s tests -v
.\.venv\Scripts\python.exe -m scripts.build_release --version v5.0.0
```

在 `dist/` 中取得 ZIP 和对应的 SHA-256 文件。压缩包内的 `licenses/` 列出附带的运行库和版本。

## 可选集成

需要给自定义 QtScrcpy 构建独立识别进程时，可运行 `python -m PyInstaller scripts/freeze/DirectionWorker.spec`。识别进程从标准输入读取逐行 JSON 请求并通过标准输出返回结果。

`integrations/android_pointer/` 包含自定义 QtScrcpy 手机光标服务。使用 Windows JDK 和 Android SDK 的 API 35 平台及 Build Tools，在根目录运行以下命令。输出 APK 及开发签名只在本地生成。

```powershell
python integrations/android_pointer/build.py --android-jar "$env:ANDROID_HOME/platforms/android-35/android.jar" --build-tools "$env:ANDROID_HOME/build-tools/35.0.0" --jdk-bin "$env:JAVA_HOME/bin"
```
