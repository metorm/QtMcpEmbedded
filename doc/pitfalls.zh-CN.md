# 要避免的问题与已知边界

[English](pitfalls.md) | **中文**

## 不能承诺的行为边界

agent 使用工具时必须知晓：

- **事件投递即返回，不保证效果**。`qt_click`/`qt_type_text`/`qt_key_press` 把事件
  投进宿主事件队列就返回成功；目标是否真的响应（按钮被点中、文本被接受），要用
  `qt_wait_for` / `qt_get_text` / 属性读回来断言。不要用固定 sleep 代替条件等待。
- **宿主 GUI 线程阻塞时一切免谈**。所有工具都运行在宿主事件循环里；宿主做长耗时
  同步操作（重计算、阻塞 IO）期间，请求会排队到其恢复。探针无法绕过，也没有超时兜底。
- **ref 只在控件存活期间有效**。对话框、动态页面销毁后旧 ref 报 "ref not found"，
  必须重新 `qt_find_widget`——哪怕新对话框看起来和前一个一模一样。
- **坐标点击（`position`）假设布局静止**。窗口缩放、面板展开/折叠、内容滚动都会使
  坐标失效；能按 ref/item/row/col 寻址就不要用坐标。纯自绘组件（如 Qtitan Grid 的
  行、QGraphicsScene 条目）没有子控件 ref，坐标（或专用场景工具）是唯一途径。
- **截图是离屏渲染（`QWidget::grab`）**，不依赖窗口在屏幕上可见；但对绕过 Qt 绘制链
  的控件（原生子窗口、OpenGL/DirectX 直绘、Qtitan Grid 的 GraphicControl）可能得到
  空白/底色——此时改截其父容器或顶层窗口。
- **`qt_wait_for` / `qt_batch` 的等待不是无副作用的**：等待期间宿主的事件循环照常
  运转（定时器、动画、网络回调都会执行），界面状态可能自行变化。
- **`qt_drag` 会移动物理鼠标光标**（兼容读 `QCursor::pos()` 的拖拽实现），执行期间
  不要动鼠标；Wayland 上不可用。
- **模态语义按 Qt 规则**：存在应用模态窗口时，对窗口外控件的操作被拒绝
  （`force=true` 可绕过，但事件仍会被 Qt 的模态过滤器丢弃——force 只对"隐藏/禁用"
  类守卫真正有效）。
- **只有 Qt 控件树内的界面可被感知**。探针的内省与操作建立在 QObject/QWidget 树和
  Qt 事件队列上：经其他渠道创建的界面——直接调 Win32 API 的对话框
  （`GetOpenFileName`/`IFileDialog` 等）、嵌入的原生子窗口（HWND/CWnd）、QtWebEngine
  的页面 DOM、OpenGL/DirectX 直绘内容——不会出现在快照里，也收不到合成事件。Qt 自带
  的文件/颜色/字体对话框默认在 Windows 上是**操作系统原生窗口**，本属此类；探针在
  `install()` 时设置 `Qt::AA_DontUseNativeDialogs` 把它们静默切换为 Qt 控件实现
  （宿主无需改任何代码，`qt_file_dialog` 即可驱动），但该属性管不到绕过 Qt 对话框类、
  直接调用 OS API 的代码路径——那些需要宿主自行改造，否则对 agent 不可见。
- **单会话**：一个探针实例同一时刻只服务一个 MCP 会话。详见
  [client.zh-CN.md](client.zh-CN.md#客户端接入claude-code-等) 的完整说明与建议做法。

## offscreen 平台的 Qt 层级坑

离屏平台有两个 Qt 层级的坑，库内 `HeadlessCompat` 已做变通（仅在检测到
offscreen/minimal 时启用，桌面平台不受影响）：

- **Windows 下 QMessageBox 崩溃**（Qt 5.15 bug）：`QMessageBox::showEvent` 无条件调用
  `qt_getWindowsSystemMenu()`，而 offscreen/minimal 的 `platformNativeInterface()` 为
  `nullptr`，静态函数 `QMessageBox::warning()` 等一弹即段错误。守卫拦截发往
  QMessageBox 的 Show 事件并用公开 API 复现其行为（跳过仅装饰性的系统菜单项调整）。
- **焦点死区**：offscreen 在窗口 show 时会激活它，但模态对话框关闭后不会重新激活父
  窗口，此后 `setFocus()` 静默失效、无 ref 的键盘操作无处投递。守卫在活动的瞬态窗口
  隐藏时重新激活其父窗口。

## 自定义命令注意事项

- **命名规则**：禁止 `qt_` 前缀（保留给内置工具），禁止与已有工具/命令重名；违规
  返回 `false` 并 `qWarning`。应选用有区分度的应用专属前缀。
- **client 缓存 tools/list**：多数 MCP client 只拉取一次工具列表，client 连接之后
  注册的命令对它可能不可见。建议用两阶段启动（`autoStart=false`，注册完毕后
  `startServer()`），让首次连接即拿到完整工具表；开服之后再注册的命令仍然合法，下次
  `tools/list` 拉取即可见。
- **handler 跑在 GUI 线程**：handler 与可用性检查都运行在宿主 GUI 线程的事件循环里，
  耗时任务不要在 handler 里同步执行（会冻结界面和 MCP）。应启动后台任务后立即返回，
  进度通过 `QtMcp::postMessage()` 上报，agent 用 `qt_host_messages` 轮询。
- **跨线程注册乐观返回**：install 之后从非 GUI 线程调用 `registerCommand()`，注册会
  排队投递到 GUI 线程，返回值是乐观的——重名只会在服务端以 `qWarning` 形式暴露；若
  排队调用执行前探针已析构、或 context 对象已销毁，注册会被静默丢弃。需要可靠反馈时
  请在 GUI 线程注册。
