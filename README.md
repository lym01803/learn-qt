# LearnQt: 文件存储占用查看工具 (File Storage Usage Viewer)

LearnQt 是一个基于现代 C++ (C++23) 和 Qt6/QML 构建的高性能跨平台文件存储占用查看工具。它致力于提供极其流畅的用户体验，即使在全盘扫描和面对海量文件列表时，UI 依然保持非阻塞和高响应性。

## ✨ 核心特性 (Features)

*   **实时动态分析**: 在后台执行文件系统扫描的同时，实时反馈当前已扫描到的目录大小。
*   **极致流畅体验**: 采用异步非阻塞架构，繁重的 IO 扫描和业务逻辑都在后台线程中完成，主 UI 线程零卡顿。
*   **海量文件支撑**: 深度定制 `QAbstractListModel`，轻松应对包含数万文件的目录展示。
*   **智能模糊搜索**: 内置基于评分机制的异步模糊搜索，动态快速过滤当前目录内容。
*   **平滑路径导航**: 支持多级目录的深度钻取与历史路径的快速回退。

## 🛠 技术亮点 (Technical Highlights)

*   **严格的 MVVM 架构**:
    *   **视图分离**: 通过 Qt QML 引擎实现界面渲染，与 C++ 业务逻辑完全解耦。
    *   **快照模式 (Snapshot)**: 设计了线程安全的数据快照机制.
*   **精细的并发控制**:
    *   **Actor 模式**: 在 ViewModel 与 View 的交互中，利用 Actor 模式串行化快照指针的读写，优雅地消除多线程环境下的竞态条件。
    *   **细粒度锁与信号量**: 在底层的目录节点更新 (Model 层) 中，使用细粒度锁来确保并发扫描任务的资源安全。
    *   **UI 节流 (Throttling)**: 实现节流机制控制从后台向 UI 发送更新信号的频率，防止事件洪流导致界面挂起。
*   **跨平台 CI/CD**: 配置了 GitHub Actions 工作流，支持 macOS 和 Windows 双平台的自动化构建。

## 🏗 架构图解 (Architecture)

简要架构数据流：
```text
[后台扫描协程] --- (更新) ---> [DataModel / 细粒度锁]
                                     |
                             (节流、定期获取快照)
                                     v
                           [生成 Snapshot (快照数据)]
                                     |
                           (通过 Actor 交换快照指针)
                                     v
[ViewModel (UI 数据绑定)] <--- (自动发送更新信号) ---> [QML View (界面渲染)]
```

## 🚀 编译与运行 (Build & Run)

### 环境依赖

*   **C++ 编译器**: 支持 C++23 标准的编译器 (如 GCC 11+, Clang 14+, MSVC 19.30+)。
*   **CMake**: 3.21 或更高版本。
*   **Qt**: Qt 6.10+ (包含 qtdeclarative 等核心模块)。

### 构建步骤

安装 Qt 6.11.1 依赖

```bash
git clone --recursive https://github.com/lym01803/LearnQt.git
cd LearnQt

cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

cmake --build build --config Release
```

参见 `.github/workflows`
