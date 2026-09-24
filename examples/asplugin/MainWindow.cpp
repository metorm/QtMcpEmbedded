#include "MainWindow.h"
#include "ui_MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_customStatus(QStringLiteral("Initial"))
{
    ui->setupUi(this);
    setWindowTitle(QStringLiteral("QtMcp Test Suite - asplugin"));
    resize(860, 680);

    setupUiManual();
    createMenusAndActions();

    appendLog(QStringLiteral("程序启动完成，就绪。"));
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::setCustomStatus(const QString &status)
{
    if (m_customStatus != status) {
        m_customStatus = status;
        emit customStatusChanged(m_customStatus);
        appendLog(QStringLiteral("Q_PROPERTY customStatus changed: %1").arg(m_customStatus));
    }
}

QString MainWindow::echo(const QString &message)
{
    const QString result = QStringLiteral("Echo: %1").arg(message);
    appendLog(QStringLiteral("Q_INVOKABLE echo called with: %1").arg(message));
    return result;
}

int MainWindow::addNumbers(int a, int b)
{
    const int sum = a + b;
    appendLog(QStringLiteral("Q_INVOKABLE addNumbers(%1, %2) = %3").arg(a).arg(b).arg(sum));
    return sum;
}

bool MainWindow::toggleFlag(bool flag)
{
    const bool inverted = !flag;
    appendLog(QStringLiteral("Q_INVOKABLE toggleFlag(%1) = %2").arg(flag).arg(inverted));
    return inverted;
}

QJsonObject MainWindow::getAppInfo()
{
    appendLog(QStringLiteral("Q_INVOKABLE getAppInfo called."));
    return QJsonObject{
        {QStringLiteral("appName"), QStringLiteral("asplugin")},
        {QStringLiteral("version"), QStringLiteral("1.0.0")},
        {QStringLiteral("framework"), QStringLiteral("Qt5")},
        {QStringLiteral("customStatus"), m_customStatus}
    };
}

void MainWindow::setStatus(const QString &text)
{
    if (m_statusLabel)
        m_statusLabel->setText(text);
    appendLog(QStringLiteral("Status updated: %1").arg(text));
}

void MainWindow::resetForm()
{
    onResetClicked();
}

void MainWindow::appendLog(const QString &msg)
{
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz"));
    const QString entry = QStringLiteral("[%1] %2").arg(timestamp, msg);
    if (m_logViewer)
        m_logViewer->appendPlainText(entry);
    qDebug().noquote() << entry;
}

void MainWindow::setupUiManual()
{
    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("centralWidgetRoot"));
    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(8);

    auto *tabs = new QTabWidget(central);
    tabs->setObjectName(QStringLiteral("mainTabs"));

    // =========================================================================
    // Tab 1: 基础控件 (Controls)
    // =========================================================================
    auto *tabControls = new QWidget;
    tabControls->setObjectName(QStringLiteral("tabControls"));
    auto *controlsLayout = new QHBoxLayout(tabControls);

    // Left Column: Inputs
    auto *boxInputs = new QGroupBox(QStringLiteral("文本输入 (Inputs)"), tabControls);
    boxInputs->setObjectName(QStringLiteral("boxInputs"));
    auto *inputForm = new QFormLayout(boxInputs);

    m_nameInput = new QLineEdit(boxInputs);
    m_nameInput->setObjectName(QStringLiteral("nameInput"));
    m_nameInput->setPlaceholderText(QStringLiteral("请输入用户名"));
    inputForm->addRow(QStringLiteral("用户名 (&N):"), m_nameInput);

    m_passwordInput = new QLineEdit(boxInputs);
    m_passwordInput->setObjectName(QStringLiteral("passwordInput"));
    m_passwordInput->setEchoMode(QLineEdit::Password);
    m_passwordInput->setPlaceholderText(QStringLiteral("请输入密码"));
    inputForm->addRow(QStringLiteral("密码 (&P):"), m_passwordInput);

    m_notesEdit = new QPlainTextEdit(boxInputs);
    m_notesEdit->setObjectName(QStringLiteral("notesEdit"));
    m_notesEdit->setPlaceholderText(QStringLiteral("请输入多行测试备注..."));
    m_notesEdit->setMaximumHeight(90);
    inputForm->addRow(QStringLiteral("备注 (&M):"), m_notesEdit);

    m_echoLabel = new QLabel(QStringLiteral("回显: [空]"), boxInputs);
    m_echoLabel->setObjectName(QStringLiteral("echoLabel"));
    m_echoLabel->setStyleSheet(QStringLiteral("color: #0066cc; font-weight: bold;"));
    inputForm->addRow(QStringLiteral("实时回显:"), m_echoLabel);

    connect(m_nameInput, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_echoLabel->setText(QStringLiteral("回显: %1").arg(text.isEmpty() ? QStringLiteral("[空]") : text));
    });

    // Right Column: Options, Sliders, Buttons
    auto *rightColWidget = new QWidget(tabControls);
    auto *rightColLayout = new QVBoxLayout(rightColWidget);
    rightColLayout->setContentsMargins(0, 0, 0, 0);

    auto *boxOptions = new QGroupBox(QStringLiteral("选择与数值 (Options & Sliders)"), rightColWidget);
    boxOptions->setObjectName(QStringLiteral("boxOptions"));
    auto *optionsLayout = new QVBoxLayout(boxOptions);

    m_chkAgree = new QCheckBox(QStringLiteral("同意测试协议 (Agree)"), boxOptions);
    m_chkAgree->setObjectName(QStringLiteral("chkAgree"));
    optionsLayout->addWidget(m_chkAgree);

    auto *rbLayout = new QHBoxLayout;
    m_rbOptionA = new QRadioButton(QStringLiteral("模式 A"), boxOptions);
    m_rbOptionA->setObjectName(QStringLiteral("rbOptionA"));
    m_rbOptionA->setChecked(true);
    m_rbOptionB = new QRadioButton(QStringLiteral("模式 B"), boxOptions);
    m_rbOptionB->setObjectName(QStringLiteral("rbOptionB"));
    rbLayout->addWidget(m_rbOptionA);
    rbLayout->addWidget(m_rbOptionB);
    optionsLayout->addLayout(rbLayout);

    m_comboCity = new QComboBox(boxOptions);
    m_comboCity->setObjectName(QStringLiteral("comboCity"));
    m_comboCity->addItems({QStringLiteral("Beijing"), QStringLiteral("Shanghai"),
                           QStringLiteral("Shenzhen"), QStringLiteral("Guangzhou")});
    optionsLayout->addWidget(m_comboCity);

    auto *sliderLayout = new QHBoxLayout;
    m_spinValue = new QSpinBox(boxOptions);
    m_spinValue->setObjectName(QStringLiteral("spinValue"));
    m_spinValue->setRange(0, 100);
    m_spinValue->setValue(25);

    m_sliderVolume = new QSlider(Qt::Horizontal, boxOptions);
    m_sliderVolume->setObjectName(QStringLiteral("sliderVolume"));
    m_sliderVolume->setRange(0, 100);
    m_sliderVolume->setValue(25);

    sliderLayout->addWidget(m_sliderVolume);
    sliderLayout->addWidget(m_spinValue);
    optionsLayout->addLayout(sliderLayout);

    m_progressBar = new QProgressBar(boxOptions);
    m_progressBar->setObjectName(QStringLiteral("progressBar"));
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(25);
    optionsLayout->addWidget(m_progressBar);

    connect(m_sliderVolume, &QSlider::valueChanged, m_spinValue, &QSpinBox::setValue);
    connect(m_spinValue, QOverload<int>::of(&QSpinBox::valueChanged), m_sliderVolume, &QSlider::setValue);
    connect(m_sliderVolume, &QSlider::valueChanged, m_progressBar, &QProgressBar::setValue);

    // Buttons
    auto *boxButtons = new QGroupBox(QStringLiteral("动作按钮 (Actions)"), rightColWidget);
    boxButtons->setObjectName(QStringLiteral("boxButtons"));
    auto *buttonsLayout = new QHBoxLayout(boxButtons);

    m_btnSubmit = new QPushButton(QStringLiteral("提交 (Submit)"), boxButtons);
    m_btnSubmit->setObjectName(QStringLiteral("btnSubmit"));
    buttonsLayout->addWidget(m_btnSubmit);

    m_btnReset = new QPushButton(QStringLiteral("重置 (Reset)"), boxButtons);
    m_btnReset->setObjectName(QStringLiteral("btnReset"));
    buttonsLayout->addWidget(m_btnReset);

    m_btnDelay = new QPushButton(QStringLiteral("延时变更 (WaitFor)"), boxButtons);
    m_btnDelay->setObjectName(QStringLiteral("btnDelay"));
    buttonsLayout->addWidget(m_btnDelay);

    connect(m_btnSubmit, &QPushButton::clicked, this, &MainWindow::onSubmitClicked);
    connect(m_btnReset, &QPushButton::clicked, this, &MainWindow::onResetClicked);
    connect(m_btnDelay, &QPushButton::clicked, this, &MainWindow::onTriggerDelayClicked);

    rightColLayout->addWidget(boxOptions);
    rightColLayout->addWidget(boxButtons);

    controlsLayout->addWidget(boxInputs, 1);
    controlsLayout->addWidget(rightColWidget, 1);
    tabs->addTab(tabControls, QStringLiteral("基础控件 (Controls)"));

    // =========================================================================
    // Tab 2: 列表与表格 (Views & Data)
    // =========================================================================
    auto *tabViews = new QWidget;
    tabViews->setObjectName(QStringLiteral("tabViews"));
    auto *viewsLayout = new QHBoxLayout(tabViews);

    // ListWidget
    auto *boxList = new QGroupBox(QStringLiteral("列表测试 (QListWidget)"), tabViews);
    boxList->setObjectName(QStringLiteral("boxList"));
    auto *listLayout = new QVBoxLayout(boxList);

    m_itemList = new QListWidget(boxList);
    m_itemList->setObjectName(QStringLiteral("itemList"));
    m_itemList->addItems({QStringLiteral("Alpha item"), QStringLiteral("Beta item"),
                          QStringLiteral("Gamma item"), QStringLiteral("Delta item")});
    listLayout->addWidget(m_itemList);

    m_labelSelectedItem = new QLabel(QStringLiteral("选中项: 无"), boxList);
    m_labelSelectedItem->setObjectName(QStringLiteral("labelSelectedItem"));
    listLayout->addWidget(m_labelSelectedItem);

    m_btnAddItem = new QPushButton(QStringLiteral("添加项 (&Add)"), boxList);
    m_btnAddItem->setObjectName(QStringLiteral("btnAddItem"));
    listLayout->addWidget(m_btnAddItem);

    connect(m_itemList, &QListWidget::currentTextChanged, this, [this](const QString &text) {
        m_labelSelectedItem->setText(QStringLiteral("选中项: %1").arg(text));
        appendLog(QStringLiteral("List item selected: %1").arg(text));
    });
    connect(m_btnAddItem, &QPushButton::clicked, this, [this]() {
        const QString newItem = QStringLiteral("New Item %1").arg(m_itemList->count() + 1);
        m_itemList->addItem(newItem);
        appendLog(QStringLiteral("Added list item: %1").arg(newItem));
    });

    // TableWidget
    auto *boxTable = new QGroupBox(QStringLiteral("表格测试 (QTableWidget)"), tabViews);
    boxTable->setObjectName(QStringLiteral("boxTable"));
    auto *tableLayout = new QVBoxLayout(boxTable);

    m_tableData = new QTableWidget(4, 3, boxTable);
    m_tableData->setObjectName(QStringLiteral("tableData"));
    m_tableData->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("Name"), QStringLiteral("Score")});
    m_tableData->horizontalHeader()->setStretchLastSection(true);

    const char *names[] = {"Alice", "Bob", "Charlie", "David"};
    const int scores[] = {95, 88, 92, 79};
    for (int row = 0; row < 4; ++row) {
        m_tableData->setItem(row, 0, new QTableWidgetItem(QString::number(101 + row)));
        m_tableData->setItem(row, 1, new QTableWidgetItem(QString::fromUtf8(names[row])));
        m_tableData->setItem(row, 2, new QTableWidgetItem(QString::number(scores[row])));
    }
    tableLayout->addWidget(m_tableData);

    viewsLayout->addWidget(boxList, 1);
    viewsLayout->addWidget(boxTable, 1);
    tabs->addTab(tabViews, QStringLiteral("数据视图 (Views)"));

    // =========================================================================
    // Tab 3: 弹窗与对话框 (Dialogs & Popups)
    // =========================================================================
    auto *tabDialogs = new QWidget;
    tabDialogs->setObjectName(QStringLiteral("tabDialogs"));
    auto *dialogsLayout = new QVBoxLayout(tabDialogs);

    auto *boxDialogs = new QGroupBox(QStringLiteral("对话框与弹窗测试"), tabDialogs);
    boxDialogs->setObjectName(QStringLiteral("boxDialogs"));
    auto *boxDialogsLayout = new QVBoxLayout(boxDialogs);

    m_btnOpenDialog = new QPushButton(QStringLiteral("打开自定义对话框 (Custom Dialog)"), boxDialogs);
    m_btnOpenDialog->setObjectName(QStringLiteral("btnOpenDialog"));
    boxDialogsLayout->addWidget(m_btnOpenDialog);

    m_btnShowMsgBox = new QPushButton(QStringLiteral("弹出消息提示框 (QMessageBox)"), boxDialogs);
    m_btnShowMsgBox->setObjectName(QStringLiteral("btnShowMsgBox"));
    boxDialogsLayout->addWidget(m_btnShowMsgBox);

    m_btnOpenFileDialog = new QPushButton(QStringLiteral("弹出文件选择框 (QFileDialog)"), boxDialogs);
    m_btnOpenFileDialog->setObjectName(QStringLiteral("btnOpenFileDialog"));
    boxDialogsLayout->addWidget(m_btnOpenFileDialog);

    m_labelDialogResult = new QLabel(QStringLiteral("弹窗返回结果: 尚未触发"), boxDialogs);
    m_labelDialogResult->setObjectName(QStringLiteral("labelDialogResult"));
    m_labelDialogResult->setStyleSheet(QStringLiteral("color: #2b7d2b; font-weight: bold;"));
    boxDialogsLayout->addWidget(m_labelDialogResult);
    boxDialogsLayout->addStretch();

    connect(m_btnOpenDialog, &QPushButton::clicked, this, &MainWindow::onOpenCustomDialog);
    connect(m_btnShowMsgBox, &QPushButton::clicked, this, &MainWindow::onShowMessageBox);
    connect(m_btnOpenFileDialog, &QPushButton::clicked, this, &MainWindow::onOpenFileDialog);

    dialogsLayout->addWidget(boxDialogs);
    tabs->addTab(tabDialogs, QStringLiteral("弹窗与对话框 (Dialogs)"));

    mainLayout->addWidget(tabs, 3);

    // =========================================================================
    // Activity Log Panel
    // =========================================================================
    auto *boxLog = new QGroupBox(QStringLiteral("操作与运行日志 (Activity Log)"), central);
    boxLog->setObjectName(QStringLiteral("boxLog"));
    auto *logLayout = new QVBoxLayout(boxLog);
    logLayout->setContentsMargins(6, 6, 6, 6);

    m_logViewer = new QPlainTextEdit(boxLog);
    m_logViewer->setObjectName(QStringLiteral("logViewer"));
    m_logViewer->setReadOnly(true);
    m_logViewer->setMaximumHeight(140);
    logLayout->addWidget(m_logViewer);

    auto *logBtnLayout = new QHBoxLayout;
    auto *btnClearLog = new QPushButton(QStringLiteral("清空日志"), boxLog);
    btnClearLog->setObjectName(QStringLiteral("btnClearLog"));
    connect(btnClearLog, &QPushButton::clicked, m_logViewer, &QPlainTextEdit::clear);
    logBtnLayout->addStretch();
    logBtnLayout->addWidget(btnClearLog);
    logLayout->addLayout(logBtnLayout);

    mainLayout->addWidget(boxLog, 1);

    setCentralWidget(central);

    // Status bar
    m_statusLabel = new QLabel(QStringLiteral("就绪 (Ready)"), this);
    m_statusLabel->setObjectName(QStringLiteral("statusLabel"));
    statusBar()->addWidget(m_statusLabel);
}

void MainWindow::createMenusAndActions()
{
    // File Menu
    auto *menuFile = menuBar()->addMenu(QStringLiteral("文件 (&File)"));
    menuFile->setObjectName(QStringLiteral("menuFile"));

    auto *actionOpenFile = menuFile->addAction(QStringLiteral("打开 (&Open)..."));
    actionOpenFile->setObjectName(QStringLiteral("actionOpenFile"));
    actionOpenFile->setShortcut(QKeySequence::Open);
    connect(actionOpenFile, &QAction::triggered, this, &MainWindow::onOpenFileDialog);

    auto *actionSaveFile = menuFile->addAction(QStringLiteral("保存 (&Save)"));
    actionSaveFile->setObjectName(QStringLiteral("actionSaveFile"));
    actionSaveFile->setShortcut(QKeySequence::Save);
    connect(actionSaveFile, &QAction::triggered, this, [this]() {
        appendLog(QStringLiteral("Action triggered: 保存 (Save)"));
        setStatus(QStringLiteral("已执行保存动作"));
    });

    menuFile->addSeparator();

    auto *actionExit = menuFile->addAction(QStringLiteral("退出 (&Exit)"));
    actionExit->setObjectName(QStringLiteral("actionExit"));
    connect(actionExit, &QAction::triggered, this, &QWidget::close);

    // Edit Menu
    auto *menuEdit = menuBar()->addMenu(QStringLiteral("编辑 (&Edit)"));
    menuEdit->setObjectName(QStringLiteral("menuEdit"));

    auto *actionClearLog = menuEdit->addAction(QStringLiteral("清空日志 (&Clear Log)"));
    actionClearLog->setObjectName(QStringLiteral("actionClearLog"));
    connect(actionClearLog, &QAction::triggered, m_logViewer, &QPlainTextEdit::clear);

    auto *actionResetForm = menuEdit->addAction(QStringLiteral("重置表单 (&Reset Form)"));
    actionResetForm->setObjectName(QStringLiteral("actionResetForm"));
    connect(actionResetForm, &QAction::triggered, this, &MainWindow::resetForm);

    // Help Menu
    auto *menuHelp = menuBar()->addMenu(QStringLiteral("帮助 (&Help)"));
    menuHelp->setObjectName(QStringLiteral("menuHelp"));

    auto *actionAbout = menuHelp->addAction(QStringLiteral("关于 (&About)..."));
    actionAbout->setObjectName(QStringLiteral("actionAbout"));
    connect(actionAbout, &QAction::triggered, this, &MainWindow::onActionAbout);
}

void MainWindow::onSubmitClicked()
{
    const QString name = m_nameInput ? m_nameInput->text() : QString();
    const bool agree = m_chkAgree && m_chkAgree->isChecked();
    const QString city = m_comboCity ? m_comboCity->currentText() : QString();
    const int val = m_spinValue ? m_spinValue->value() : 0;

    const QString summary = QStringLiteral("提交表单: 用户名='%1', 同意=%2, 城市='%3', 数值=%4")
                                .arg(name)
                                .arg(agree ? QStringLiteral("是") : QStringLiteral("否"))
                                .arg(city)
                                .arg(val);
    appendLog(summary);
    setStatus(QStringLiteral("提交成功: %1").arg(name.isEmpty() ? QStringLiteral("匿名") : name));
}

void MainWindow::onResetClicked()
{
    if (m_nameInput) m_nameInput->clear();
    if (m_passwordInput) m_passwordInput->clear();
    if (m_notesEdit) m_notesEdit->clear();
    if (m_chkAgree) m_chkAgree->setChecked(false);
    if (m_rbOptionA) m_rbOptionA->setChecked(true);
    if (m_comboCity) m_comboCity->setCurrentIndex(0);
    if (m_sliderVolume) m_sliderVolume->setValue(25);
    appendLog(QStringLiteral("表单已重置为初始状态。"));
    setStatus(QStringLiteral("已重置"));
}

void MainWindow::onTriggerDelayClicked()
{
    appendLog(QStringLiteral("延时计时器已启动 (1.2秒)..."));
    setStatus(QStringLiteral("等待延时状态更新..."));
    if (!m_delayTimer) {
        m_delayTimer = new QTimer(this);
        m_delayTimer->setSingleShot(true);
        connect(m_delayTimer, &QTimer::timeout, this, [this]() {
            setStatus(QStringLiteral("Delayed Condition Met!"));
            appendLog(QStringLiteral("延时状态满足: Delayed Condition Met!"));
        });
    }
    m_delayTimer->start(1200);
}

void MainWindow::onOpenCustomDialog()
{
    appendLog(QStringLiteral("打开自定义模态对话框..."));
    auto *dlg = new QDialog(this);
    dlg->setObjectName(QStringLiteral("CustomDialog"));
    dlg->setWindowTitle(QStringLiteral("测试自定义对话框"));
    dlg->resize(320, 160);

    auto *layout = new QVBoxLayout(dlg);
    auto *infoLabel = new QLabel(QStringLiteral("请输入测试输入:"), dlg);
    auto *input = new QLineEdit(dlg);
    input->setObjectName(QStringLiteral("dialogInput"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    buttons->setObjectName(QStringLiteral("dialogButtons"));
    connect(buttons, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    layout->addWidget(infoLabel);
    layout->addWidget(input);
    layout->addWidget(buttons);

    if (dlg->exec() == QDialog::Accepted) {
        const QString text = input->text();
        m_labelDialogResult->setText(QStringLiteral("弹窗返回结果: 确认, 内容='%1'").arg(text));
        appendLog(QStringLiteral("自定义对话框确认提交: %1").arg(text));
    } else {
        m_labelDialogResult->setText(QStringLiteral("弹窗返回结果: 取消"));
        appendLog(QStringLiteral("自定义对话框已取消。"));
    }
    dlg->deleteLater();
}

void MainWindow::onShowMessageBox()
{
    appendLog(QStringLiteral("弹出测试 QMessageBox..."));
    QMessageBox::information(this, QStringLiteral("提示消息"),
                             QStringLiteral("这是一个用于测试 MCP 弹窗交互的消息框。"));
    m_labelDialogResult->setText(QStringLiteral("弹窗返回结果: QMessageBox 已确认"));
    appendLog(QStringLiteral("QMessageBox 已确认关闭。"));
}

void MainWindow::onOpenFileDialog()
{
    appendLog(QStringLiteral("弹出文件选择对话框 (QFileDialog)..."));
    const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("选择测试文件"),
                                                     QString(), QStringLiteral("All Files (*.*)"));
    if (!file.isEmpty()) {
        m_labelDialogResult->setText(QStringLiteral("选择的文件: %1").arg(file));
        appendLog(QStringLiteral("QFileDialog 已选中文件: %1").arg(file));
    } else {
        m_labelDialogResult->setText(QStringLiteral("文件选择已取消"));
        appendLog(QStringLiteral("QFileDialog 已取消。"));
    }
}

void MainWindow::onActionAbout()
{
    appendLog(QStringLiteral("触发 关于 (About) 对话框..."));
    QMessageBox::about(this, QStringLiteral("关于 QtMcp asplugin"),
                       QStringLiteral("QtMcpEmbedded 全功能验收测试程序 (v1.0.0)\n"
                                      "支持 21 个 MCP 工具的端到端调用与测试。"));
}
