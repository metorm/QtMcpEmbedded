# 在 Qt-Advanced-Docking-System 中嵌入 QtMcpEmbedded

本文档记录把 QtMcpEmbedded(MCP 探针)嵌入
[Qt-Advanced-Docking-System](https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System)
(下称 ADS)全部示例/演示程序的完整步骤,以及在 ADS 这类高动态界面上使用
`qt_*` 工具链的实测经验。

**适用版本**:针对 2026-08-16 下载的 ADS master 源码(zip 内记录的上游 commit
`89f8640516ec567cdc9e1026a8c23b7d8b798c0a`)。在该版本上,嵌入后全部 11 个程序
(10 个 examples + demo)通过了本目录两个测试脚本的全部检查。ADS 上游结构变动
不大时,本步骤同样适用;如 `main.cpp` 或目标名有变化,按第 1 节的手动步骤调整即可。

## 目录内容

| 文件 | 说明 |
|---|---|
| `embed_qtmcp.patch` | 对 ADS 源码的全部改动(12 个文件),`patch -p1 < embed_qtmcp.patch` 即可应用 |
| `ads_test.py` | 基线测试套件:对任一 ADS 程序做全套 MCP 冒烟(快照/截图/dock 内省/文本读写/浮动/batch/wait_for) |
| `ads_deep.py` | 深度场景测试:demo 的菜单/工具栏/状态保存恢复/autohide 侧栏、DeleteOnCloseTest 的动态创建与文本往返、AutoHideExample 的图钉-侧栏-overlay 流程 |

两个脚本只依赖 MCP 接口,不限定进程内实现;改动均在 ADS 侧的"简单嵌入代码"
范围内,不触碰 ADS 的任何逻辑。

## 1. 嵌入步骤

### 1.1 应用补丁(推荐)

在 ADS 源码根目录:

```bash
patch -p1 < embed_qtmcp.patch
```

### 1.2 手动改动(与补丁等价,共两处)

**(a) 顶层 `CMakeLists.txt` 末尾追加**(利用 ADS 目标名已知的事实,一次性把探针
链进全部示例;设了 `QTMCP_SOURCE_DIR` 才生效,不设则 ADS 构建完全不受影响):

```cmake
set(QTMCP_SOURCE_DIR "" CACHE PATH "Path to the QtMcpEmbedded sources")
if(QTMCP_SOURCE_DIR)
    add_subdirectory(${QTMCP_SOURCE_DIR} ${CMAKE_BINARY_DIR}/qtmcp_embedded)
    foreach(_qtmcp_target
            SimpleExample HideShowExample SidebarExample DeleteOnCloseTest
            CentralWidgetExample AutoHideExample AutoHideDragNDropExample
            EmptyDockAreaExample DockInDockExample ConfigFlagsExample
            AdvancedDockingSystemDemo)
        if(TARGET ${_qtmcp_target})
            target_link_libraries(${_qtmcp_target} PRIVATE QtMcpEmbedded)
        endif()
    endforeach()
endif()
```

**(b) 每个 `examples/*/main.cpp` 与 `demo/main.cpp` 加两行**:

```cpp
#include <QtMcp.h>
// ...
QApplication a(argc, argv);
QtMcp::install();   // 未设 QT_MCP_PROBE=1 时是零开销 no-op
```

注意 `demo/main.cpp` 里有一行 `qInstallMessageHandler(myMessageOutput)`:
`QtMcp::install()` 必须放在它**之后**(MessageLog 会链接到前一个 handler;
顺序反了 demo 的 handler 会把探针的覆盖掉,`qt_messages` 失效)。

### 1.3 构建

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=<Qt前缀,如 C:/devlibs/Qt/5.15.2/msvc2019_64> \
      -DADS_VERSION=4.3.1 \
      -DQTMCP_SOURCE_DIR=<qt-mcp-embedded 仓库路径>
cmake --build build
```

两个坑,都是 ADS 构建系统本身的:

- **`-DADS_VERSION` 必须显式给**。ADS 顶层 CMakeLists 默认用 `git describe`
  推导版本号;zip 下载的源码没有 `.git`,不给就 configure 失败。版本号取
  major.minor.patch 格式即可。
- 产物在 `build/x64/bin/`(Qt5 下库名 `qtadvanceddocking-qt5.dll`)。要双击运行,
  对每个 exe 跑 `windeployqt --release <exe>`。

## 2. 运行测试脚本

脚本依赖 `client/` 的 uv 环境(`mcp` 包),不依赖被测程序的内部实现:

```bash
# 基线套件:自动启动程序(QT_MCP_PROBE=1)、跑完自动杀掉
uv run --project <本仓库>/client python ads_test.py SimpleExample
uv run --project <本仓库>/client python ads_test.py AdvancedDockingSystemDemo --port 9150

# 深度场景:demo / deleteonclose / autohide 三个场景
uv run --project <本仓库>/client python ads_deep.py demo
uv run --project <本仓库>/client python ads_deep.py deleteonclose
uv run --project <本仓库>/client python ads_deep.py autohide
```

- exe 定位:默认取 `<本仓库>/tmp/Qt-Advanced-Docking-System-master/build/x64/bin`;
  用 `--exe-dir <目录>` 或环境变量 `ADS_BIN_DIR` 覆盖。
- 截图与快照 JSON 输出到本目录 `shots/`。
- 脚本以非零码退出表示有断言失败;`[FAIL]` 行给出原因。

## 3. 给调用方 agent 的注意事项(实测经验)

定位与内省:

- **ADS 类都在 `ads::` 命名空间**,且绝大多数控件 `objectName` 为空。用
  `qt_find_widget` 的 `class_name`(支持不带命名空间的写法,如 `CDockWidget`)
  加 `visible_only:false` 枚举,再配合 `root_ref` 在某个区域子树里精确定位
  (例如:先找 `CDockWidgetTab`,再在其子树里找 `CElidingLabel` 匹配标题文本)。
- **`qt_list_windows` 顺序不保证主窗口在前**:demo 启动即带一个浮动日历窗
  (`CFloatingDockContainer`),它可能排在主窗口前面。给"主窗口"截图时,按
  `class`/`title` 选出 `CMainWindow` 的 ref 再截,不要用 `full_window:true`
  (它取第一个可见顶层窗口)。
- 快照对 ADS 结构覆盖良好:dock 区域的 tab、图钉/关闭/分离按钮都带 tooltip
  (`tip: Pin Active Tab` 等),可以靠 tooltip 识别按钮用途。

操作:

- **隐藏 tab 页里的控件 `isVisible() == false`**,操作守卫会拒绝点击/输入并提示
  `force=true`——这是正常行为。要么先 `qt_invoke_slot setAsCurrentTab` 把它所在的
  tab 切到前台,要么只对可见控件操作;只读操作(`qt_get_text`/`qt_widget_details`)
  对隐藏控件照常用。
- **切换 docking 状态的两条路径**:
  (a) 槽调用——`CDockWidget` 的 `toggleView(bool)`(关/开)、`setAsCurrentTab()`
  (切当前 tab)、`setFloating()`(转浮动)、`setAutoHide(bool)` 都是槽,
  `qt_invoke_slot` 直接调;
  (b) `qt_drag`——把 `CDockWidgetTab` 拖到目标 `CDockAreaWidget` 上,完成
  dock 重排。**注意 `qt_drag` 会真实移动物理光标**(ADS 拖拽读 `QCursor::pos()`,
  纯合成事件驱动不了),跑这类测试时不要动鼠标。
- **`CDockManager::addDockWidget` 不是槽**,无法 `qt_invoke_slot` 按区域代码精确
  放置;用 `qt_drag` 达到等价效果。
- 表格编辑:`qt_click` 对任意 `QTableWidget` 支持 `row`/`col` 直达单元格(屏外
  单元格自动滚入);双击开编辑器后按编辑器类型操作——文本框用 `qt_type_text`,
  下拉框用 `qt_set_property currentText`,数字框对其内部 line edit 输入,
  Return 提交。ADS demo 工具栏的 "Create Floating Table" 可以现场造一个表格练手。
- 菜单触发:`qt_trigger_action` 对菜单栏按文本匹配会**递归进子菜单**
  (如 `View > Label 4` 直接传 `"Label 4"` 即可);工具栏按钮同样是 action。
- demo 的 QCalendarWidget 年份是 `QToolButton` 而不是 spinbox(spinbox 存在但被
  Qt 隐藏),这不是探针的误判;隐藏控件的信息都能读,只是不能点。

## 4. 依赖的探针能力(版本注意)

以下能力是在 ADS 实测中驱动出来的改进,本目录脚本依赖它们;用早于这些改进的
QtMcpEmbedded 版本跑 ADS 会遇到对应失败:

1. `qt_list_windows` 条目带 `ref`/`title`(多顶层窗口可寻址);
2. `qt_find_widget` 的 `class_name` 忽略 C++ 命名空间;
3. `qt_invoke_slot` 容忍带签名的方法名(`toggleView(bool)`);
4. `qt_trigger_action` 递归匹配子菜单;
5. `qt_click` 的 `row`/`col` 表格单元格定位;
6. `qt_drag` 拖拽工具。

## 5. 已知边界

- 拖拽在 Wayland 上不适用(ADS 在该平台走原生 `QDrag`,事件注入无法驱动);
  Windows/X11 路径是纯 Qt 事件 + `QCursor::pos()`,`qt_drag` 已验证可用。
- 浮动窗"再停靠回去"没有槽可调,用 `qt_drag` 拖回目标区域。
- 探针端口绑定 `127.0.0.1`,同一时刻一个端口只能跑一个被测程序;批量测试请
  串行或用 `--port` 区分。
