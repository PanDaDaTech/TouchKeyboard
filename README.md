# 触摸键盘 (Touch Keyboard)

> 专为 Windows 11 / 10 及 WinPE 离线环境打造的**极轻量级、原生 Win32 C++ 触控键盘**。
> 无任何重型 UI 框架依赖，内存占用仅 **~3MB**，二进制文件仅 **~30KB**，支持 4K 高分屏自适应与九宫格十字滑选！

---

## 核心亮点 (Key Features)

- **零延迟极速响应**：原生 Win32 GDI 双缓冲绘制，按压 0ms 瞬间直出上屏；退格与方向键支持高频连发 (Auto-Repeat)。
- **4K 高分屏 & 矢量自适应**：原生适配 1080P / 2K / 4K（125%~250% DPI 缩放），支持 8 方向边框自由拖拽拉大拉小，按键与字号全矢量等比放缩。
- **独创九宫格十字 5 向选字**：按住数字键秒出 **[中 / 上 / 右 / 下 / 左]** 5 向大按键卡片，滑动即可秒速上屏；支持【短按/长按】模式快速切换。
- **智能焦点感应自动呼出**：深入透视 Caret 闪烁光标，精准识别 QQ、微信、Chrome、Notepad、Office 等输入框，点击秒级自动滑出，离焦自动收回。
- **物理键盘对齐 & 输入法一键切**：严格区分左右 Shift；短按模式下点击右 Shift 发送硬件级 `VK_RSHIFT` 瞬间脉冲，秒切微软拼音/搜狗中英文！
- **深色 / 浅色主题切换**：支持深色、浅色两套主题，支持跟随系统自动切换，也可通过命令行参数强制指定。
- **完美兼容微软拼音 / 五笔输入法**：按键通过 `SendInput` 虚拟键码 (VK) + 正确扫描码发送，完整经过 TSF 组合管线，拼音/五笔组字无障碍。
- **极致轻量与兼容**：兼容 Windows XP ~ Windows 11，完美适配 WinPE 维护环境，支持系统托盘常驻与后台静默运行。

---

## 命令行参数说明 (CLI Parameters)

支持丰富的启动参数，方便集成到 WinPE 启动脚本、第三方 Shell 或快捷方式中：

| 参数 | 含义说明 |
| :--- | :--- |
| `-show` | 启动时直接弹出显示键盘 |
| `-hide` / `-min` / `-tray` | 启动后静默隐藏到右下角系统托盘 |
| `-touchonly` | **触摸屏专属模式**（非触摸设备启动自动静默退出，不占用任何内存） |
| `-auto` | 默认开启"点击编辑框自动呼出"功能 |
| `-noauto` | 默认关闭"自动呼出"功能 |
| `-short` | 默认开启"短按触发"模式 |
| `-9key` / `-t9` | 启动默认切换为**九宫格**触摸模式 |
| `-full` / `-qwerty` | 启动默认切换为**全键盘**模式 |
| `-dark` | 强制使用**深色**主题 |
| `-light` | 强制使用**浅色**主题 |
| `-theme:system` | 主题跟随系统自动切换（默认行为） |

### 常用启动示例：
```bat
:: 1. 触摸屏设备静默自启（驻留托盘，点击输入框自动弹显）
UI_TouchKeyboard_x64.exe -hide -touchonly

:: 2. 默认以九宫格短按模式直接显示
UI_TouchKeyboard_x64.exe -9key -short -show

:: 3. 强制浅色主题 + 全键盘
UI_TouchKeyboard_x64.exe -light -full -show
```

---

## 九宫格选字与快捷技巧

- **四周环绕划字**：在九宫格模式下按住数字键（如 `2 abc2`），向 **[上/右/下/左/中]** 手指滑动松开，即可直接输入对应的字符/数字。
- **中英文一键切换**：点击顶栏切换为 `[ 短按:开 ]` 后，点击 `Shift` 键将直接给操作系统发送物理右 Shift 脉冲，瞬间切换输入法中英文状态。
- **自由放缩**：鼠标/手指按住键盘 4 周任意边框或角落拖动，即可像 Win11 原生键盘一样自由调整键盘大小。

---

## 编译指南 (Build Instructions)

本项目采用纯 Win32 API 编写，无第三方运行时依赖。

- **编译器**：MSVC (Visual Studio 2019 / 2022)
- **本地编译**：直接运行根目录下的 `build_cpp.bat` 即可一键生成 x86 / x64 双架构二进制程序。
- **CI 构建**：项目已集成 GitHub Actions，推送代码后自动构建并生成 Release 产物，详见 `.github/workflows/build.yml`。

### XP 兼容依赖（自动下载）

构建脚本依赖 [YY-Thunks](https://github.com/Chuyu-Team/YY-Thunks)（WinXP API 桩）和 [VC-LTL](https://github.com/Chuyu-Team/VC-LTL)（静态 CRT 链接）以实现 Windows XP 兼容。

- 若本地未找到这两个依赖，`build_cpp.bat` 会**自动从 NuGet 下载**到 `deps/` 目录（YY-Thunks 1.2.2 + VC-LTL 5.3.1）。
- 也可通过环境变量 `TOOLCHAIN_ROOT` 指定自定义路径（如 `set TOOLCHAIN_ROOT=D:\MyTools`），脚本会在 `%TOOLCHAIN_ROOT%\YY-Thunks\` 和 `%TOOLCHAIN_ROOT%\VC-LTL\` 下查找。
- GitHub Actions 中由 workflow 自动下载，无需手动配置。

---

## 开源协议 (License)

本项目基于 [GPL-3.0-or-later](LICENSE) 开源。