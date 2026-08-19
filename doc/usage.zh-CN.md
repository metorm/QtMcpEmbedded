# 工具使用参考

[English](usage.md) | **中文**

面向给 AI 写指令或手工调工具的读者。这些语义也体现在 `initialize` 响应的
`instructions` 里。

## 工具一览（21 个）

| 工具 | 说明 |
|---|---|
| `qt_snapshot` | widget 树结构化快照（含 ref、tooltip、勾选/隐藏/禁用标记；内联 list/tree/table 条目及点击坐标） |
| `qt_find_widget` | 按 objectName/类名/文本/模式查找控件（类名匹配忽略 C++ 命名空间，`CDockWidget` 可命中 `ads::CDockWidget`） |
| `qt_widget_details` | 控件或 tree 条目的完整属性（含控件挂载的 QAction 列表，可发现 Ribbon 组等容器上可触发的动作） |
| `qt_object_tree` / `qt_list_windows` | QObject 树 / 顶层窗口（每个窗口带 ref 与标题，可直接用于截图等后续操作） |
| `qt_active_popup` | 当前模态/弹出窗口（标题、文本、全部按钮的可点击 ref）——处理 messagebox、保存确认等阻塞弹窗的入口 |
| `qt_screenshot` | 窗口或控件截图（PNG/JPEG，基于真实渲染结果） |
| `qt_click` | 点击（widget 与 tree item 均可；双击、右键含上下文菜单、修饰键、坐标；`row`/`col` 直达任意 item view（QListView/QTableView/QTreeView/QTableWidget）的单元格，屏外自动滚入；`item_text` 按文本命中条目，树形自动展开） |
| `qt_file_dialog` | 处理当前活动的 QFileDialog：填路径并确认/取消（打开/保存/选目录均可）。探针 install() 时设置 `Qt::AA_DontUseNativeDialogs`，之后创建的文件对话框都是 Qt 控件形态、可被驱动 |
| `qt_drag` | 拖拽（源 widget → 目标 widget；路径上移动真实光标以兼容读取 `QCursor::pos()` 的拖拽实现，如 ADS dock 重排） |
| `qt_type_text` / `qt_key_press` | 文本输入 / 按键（含焦点管理） |
| `qt_set_property` | 写属性（含写后读回 `value`，可发现被校验逻辑拒绝的写入；支持 tree item 伪属性 expanded/checked/selected/text） |
| `qt_invoke_slot` | 调用槽/invokable 方法（≤4 参数；方法名可裸写或带签名，如 `toggleView` / `toggleView(bool)`） |
| `qt_get_text` | 提取文本（widget 或 tree item；隐藏控件也可读；对 item view 转储模型文本，行以换行、列以制表符分隔） |
| `qt_trigger_action` | 触发 QAction（菜单/工具栏；按文本匹配时递归搜索子菜单条目） |
| `qt_wait_for` | 等待条件（widget_visible / window_count_changed / property_equals；超时报告最后观测值） |
| `qt_batch` | 一次往返顺序执行多步，失败即停 |
| `qt_debug_message` | 读取 Qt 内部消息（qDebug/qWarning 等）环形缓冲 |
| `qt_host_messages` | 读取宿主通过 `QtMcp::postMessage()` 主动推送的消息（读后清空暂存区，不会重复送达；宿主不推送则恒为空） |
| `qt_app_commands` | 宿主自定义命令（`QtMcp::registerCommand` 注册）快照：每条 `{name, description, available, reason}`，可用性为实时评估 |

行为约定：操作类工具采用**异步事件投递**（不会在模态 `exec()` 上死锁）；对隐藏/禁用/被模态阻断的目标**快速失败**并给出原因（`force=true` 可绕过）；ref 全局单调编号，跨 snapshot 稳定，绝不静默重绑（包括宿主分配器复用已销毁控件地址的情形：旧 ref 一律明确报错，不会指向新控件）。

不能承诺的行为边界见 [pitfalls.zh-CN.md](pitfalls.zh-CN.md)。
