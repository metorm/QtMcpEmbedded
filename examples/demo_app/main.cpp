#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDial>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLCDNumber>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
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
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <QtMcp.h>

/// Combo-box cell editor delegate for delegateTable: editing a cell opens a
/// QComboBox instead of a line edit.
class ComboBoxDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &) const override
    {
        auto *combo = new QComboBox(parent);
        combo->addItems({QStringLiteral("Red"), QStringLiteral("Green"),
                         QStringLiteral("Blue")});
        return combo;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        auto *combo = qobject_cast<QComboBox *>(editor);
        if (combo)
            combo->setCurrentText(index.data(Qt::EditRole).toString());
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        auto *combo = qobject_cast<QComboBox *>(editor);
        if (combo)
            model->setData(index, combo->currentText(), Qt::EditRole);
    }
};

/// Spin-box cell editor delegate for delegateTable.
class SpinBoxDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &) const override
    {
        auto *spin = new QSpinBox(parent);
        spin->setRange(0, 100);
        return spin;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        auto *spin = qobject_cast<QSpinBox *>(editor);
        if (spin)
            spin->setValue(index.data(Qt::EditRole).toInt());
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        auto *spin = qobject_cast<QSpinBox *>(editor);
        if (spin)
            model->setData(index, spin->value(), Qt::EditRole);
    }
};

/// Main window with an unsaved-changes guard: closing while dirty asks
/// "Save changes before quitting?" — a classic blocking scenario.
class DemoMainWindow : public QMainWindow
{
public:
    using QMainWindow::QMainWindow;

    void setDirty(bool dirty) { m_dirty = dirty; }

protected:
    void closeEvent(QCloseEvent *event) override
    {
        if (!m_dirty) {
            event->accept();
            return;
        }
        const QMessageBox::StandardButton choice = QMessageBox::question(
            this, QStringLiteral("Unsaved changes"),
            QStringLiteral("Save changes before quitting?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (choice == QMessageBox::Cancel) {
            event->ignore();
            return;
        }
        // Save/Discard both proceed; "Save" just pretends to save here.
        event->accept();
    }

private:
    bool m_dirty = false;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QtMcp::InstallOptions mcpOptions;
    mcpOptions.appName = QStringLiteral("QtMcp Demo");
    mcpOptions.instructions = QStringLiteral(
        "Demo app for the QtMcpEmbedded probe. Tab 'Basic': applyButton sets statusLabel "
        "to 'Applied'; nameEdit mirrors into mirrorLabel; volumeSlider/volumeSpin/"
        "volumeProgress are synced; levelDial drives levelLcd; lockedButton is disabled "
        "until unlockCheck is checked; dialogButton opens a modal SampleDialog; "
        "warnButton opens a modal QMessageBox. Tab 'Views' requires nameEdit to be "
        "non-empty (the switch is rejected otherwise); itemList selection drives "
        "listDetailLabel; itemTable cells sum into sumLabel. Quitting with unsaved "
        "changes asks Save/Discard/Cancel.");
    QtMcp::install(mcpOptions);
    QtMcp::postMessage(QStringLiteral("demo_app started"));

    DemoMainWindow window;
    window.setObjectName(QStringLiteral("MainWindow"));
    window.setWindowTitle(QStringLiteral("QtMcp Demo"));

    auto *tabs = new QTabWidget;
    tabs->setObjectName(QStringLiteral("mainTabs"));

    // ------------------------------------------------ Tab 1: basic controls
    auto *basicTab = new QWidget;
    basicTab->setObjectName(QStringLiteral("basicTab"));
    auto *basicLayout = new QVBoxLayout(basicTab);

    auto *statusLabel = new QLabel(QStringLiteral("Ready"));
    statusLabel->setObjectName(QStringLiteral("statusLabel"));

    auto *applyButton = new QPushButton(QStringLiteral("Apply"));
    applyButton->setObjectName(QStringLiteral("applyButton"));
    applyButton->setToolTip(QStringLiteral("Apply the current settings (sets status to Applied)"));
    QObject::connect(applyButton, &QPushButton::clicked, statusLabel, [statusLabel, &window]() {
        statusLabel->setText(QStringLiteral("Applied"));
        window.setDirty(false); // applying saves
        QtMcp::postMessage(QStringLiteral("applyButton clicked, statusLabel := Applied"));
    });

    auto *nameEdit = new QLineEdit;
    nameEdit->setObjectName(QStringLiteral("nameEdit"));
    nameEdit->setPlaceholderText(QStringLiteral("Enter name"));
    nameEdit->setToolTip(QStringLiteral("输入名字，会实时镜像到 Mirror 标签"));
    auto *mirrorLabel = new QLabel;
    mirrorLabel->setObjectName(QStringLiteral("mirrorLabel"));
    // linkage: typing mirrors into the label; editing also marks the document dirty
    QObject::connect(nameEdit, &QLineEdit::textChanged, mirrorLabel, &QLabel::setText);
    QObject::connect(nameEdit, &QLineEdit::textChanged, &window,
                     [&window]() { window.setDirty(true); });

    auto *notesEdit = new QPlainTextEdit(QStringLiteral("line1\nline2"));
    notesEdit->setObjectName(QStringLiteral("notesEdit"));
    notesEdit->setMaximumHeight(60);

    auto *modeCombo = new QComboBox;
    modeCombo->setObjectName(QStringLiteral("modeCombo"));
    modeCombo->addItems({QStringLiteral("Alpha"), QStringLiteral("Beta"), QStringLiteral("Gamma")});
    auto *modeLabel = new QLabel(QStringLiteral("Mode: Alpha"));
    modeLabel->setObjectName(QStringLiteral("modeLabel"));
    // linkage: combo selection drives the label
    QObject::connect(modeCombo, &QComboBox::currentTextChanged, modeLabel,
                     [modeLabel](const QString &text) {
                         modeLabel->setText(QStringLiteral("Mode: ") + text);
                     });

    auto *weightSpin = new QDoubleSpinBox;
    weightSpin->setObjectName(QStringLiteral("weightSpin"));
    weightSpin->setRange(0.0, 100.0);
    weightSpin->setValue(1.5);

    // linkage: slider <-> spinbox two-way sync, both drive the progress bar
    auto *volumeSlider = new QSlider(Qt::Horizontal);
    volumeSlider->setObjectName(QStringLiteral("volumeSlider"));
    volumeSlider->setRange(0, 100);
    volumeSlider->setToolTip(QStringLiteral("Drag to set volume; syncs with spin box and progress bar"));
    auto *volumeSpin = new QSpinBox;
    volumeSpin->setObjectName(QStringLiteral("volumeSpin"));
    volumeSpin->setRange(0, 100);
    auto *volumeProgress = new QProgressBar;
    volumeProgress->setObjectName(QStringLiteral("volumeProgress"));
    volumeProgress->setRange(0, 100);
    QObject::connect(volumeSlider, &QSlider::valueChanged, volumeSpin, &QSpinBox::setValue);
    QObject::connect(volumeSpin, QOverload<int>::of(&QSpinBox::valueChanged),
                     volumeSlider, &QSlider::setValue);
    QObject::connect(volumeSlider, &QSlider::valueChanged,
                     volumeProgress, &QProgressBar::setValue);

    // linkage: dial drives the LCD number
    auto *levelDial = new QDial;
    levelDial->setObjectName(QStringLiteral("levelDial"));
    levelDial->setRange(0, 10);
    levelDial->setMaximumHeight(60);
    auto *levelLcd = new QLCDNumber;
    levelLcd->setObjectName(QStringLiteral("levelLcd"));
    levelLcd->setMaximumHeight(40);
    levelLcd->display(0);
    QObject::connect(levelDial, &QDial::valueChanged, levelLcd,
                     [levelLcd](int v) { levelLcd->display(v); });

    auto *agreeCheckBox = new QCheckBox(QStringLiteral("I agree"));
    agreeCheckBox->setObjectName(QStringLiteral("agreeCheckBox"));

    // linkage: checkbox enables/disables the group; radios drive a label
    auto *advancedCheck = new QCheckBox(QStringLiteral("Advanced"));
    advancedCheck->setObjectName(QStringLiteral("advancedCheck"));
    advancedCheck->setChecked(true);
    auto *advancedGroup = new QGroupBox(QStringLiteral("Advanced options"));
    advancedGroup->setObjectName(QStringLiteral("advancedGroup"));
    auto *optionRadio1 = new QRadioButton(QStringLiteral("Option 1"));
    optionRadio1->setObjectName(QStringLiteral("optionRadio1"));
    optionRadio1->setChecked(true);
    auto *optionRadio2 = new QRadioButton(QStringLiteral("Option 2"));
    optionRadio2->setObjectName(QStringLiteral("optionRadio2"));
    auto *optionLabel = new QLabel(QStringLiteral("Option: 1"));
    optionLabel->setObjectName(QStringLiteral("optionLabel"));
    QObject::connect(advancedCheck, &QCheckBox::toggled, advancedGroup, &QWidget::setEnabled);
    QObject::connect(optionRadio1, &QRadioButton::toggled, optionLabel, [optionLabel](bool on) {
        if (on)
            optionLabel->setText(QStringLiteral("Option: 1"));
    });
    QObject::connect(optionRadio2, &QRadioButton::toggled, optionLabel, [optionLabel](bool on) {
        if (on)
            optionLabel->setText(QStringLiteral("Option: 2"));
    });
    auto *groupLayout = new QHBoxLayout(advancedGroup);
    groupLayout->addWidget(optionRadio1);
    groupLayout->addWidget(optionRadio2);
    groupLayout->addWidget(optionLabel);

    auto *dialogButton = new QPushButton(QStringLiteral("Open Dialog"));
    dialogButton->setObjectName(QStringLiteral("dialogButton"));
    QObject::connect(dialogButton, &QPushButton::clicked, &window, [&window]() {
        QDialog dialog(&window);
        dialog.setObjectName(QStringLiteral("SampleDialog"));
        dialog.setWindowTitle(QStringLiteral("Sample Dialog"));

        auto *dialogLayout = new QVBoxLayout(&dialog);
        auto *dialogEdit = new QLineEdit;
        dialogEdit->setObjectName(QStringLiteral("dialogEdit"));
        auto *okButton = new QPushButton(QStringLiteral("OK"));
        okButton->setObjectName(QStringLiteral("okButton"));
        QObject::connect(okButton, &QPushButton::clicked, &dialog, &QDialog::accept);
        dialogLayout->addWidget(dialogEdit);
        dialogLayout->addWidget(okButton);

        dialog.exec();
    });

    basicLayout->addWidget(statusLabel);
    basicLayout->addWidget(applyButton);

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Name"), nameEdit);
    form->addRow(QStringLiteral("Mirror"), mirrorLabel);
    form->addRow(QStringLiteral("Mode"), modeCombo);
    form->addRow(QStringLiteral("Mode label"), modeLabel);
    form->addRow(QStringLiteral("Weight"), weightSpin);
    basicLayout->addLayout(form);
    basicLayout->addWidget(notesEdit);

    auto *volumeRow = new QHBoxLayout;
    volumeRow->addWidget(volumeSlider);
    volumeRow->addWidget(volumeSpin);
    basicLayout->addLayout(volumeRow);
    basicLayout->addWidget(volumeProgress);

    auto *dialRow = new QHBoxLayout;
    dialRow->addWidget(levelDial);
    dialRow->addWidget(levelLcd);
    basicLayout->addLayout(dialRow);

    basicLayout->addWidget(agreeCheckBox);
    basicLayout->addWidget(advancedCheck);
    basicLayout->addWidget(advancedGroup);
    basicLayout->addWidget(dialogButton);

    // file & directory dialogs (native suppressed by the probe) — exercise
    // qt_file_dialog: result path lands in statusLabel
    auto *fileButton = new QPushButton(QStringLiteral("Open File"));
    fileButton->setObjectName(QStringLiteral("fileButton"));
    QObject::connect(fileButton, &QPushButton::clicked, &window, [statusLabel, &window]() {
        const QString f = QFileDialog::getOpenFileName(&window, QStringLiteral("Pick a file"));
        statusLabel->setText(f.isEmpty() ? QStringLiteral("file cancelled")
                                         : QStringLiteral("file: %1").arg(f));
    });
    basicLayout->addWidget(fileButton);

    auto *dirButton = new QPushButton(QStringLiteral("Pick Directory"));
    dirButton->setObjectName(QStringLiteral("dirButton"));
    QObject::connect(dirButton, &QPushButton::clicked, &window, [statusLabel, &window]() {
        const QString d = QFileDialog::getExistingDirectory(&window, QStringLiteral("Pick a dir"));
        statusLabel->setText(d.isEmpty() ? QStringLiteral("dir cancelled")
                                         : QStringLiteral("dir: %1").arg(d));
    });
    basicLayout->addWidget(dirButton);

    // warning message box (modal exec) — a classic blocking scenario
    auto *warnButton = new QPushButton(QStringLiteral("Show Warning"));
    warnButton->setObjectName(QStringLiteral("warnButton"));
    QObject::connect(warnButton, &QPushButton::clicked, &window, [&window]() {
        QMessageBox::warning(&window, QStringLiteral("Warning"),
                             QStringLiteral("Something needs your attention."));
    });
    basicLayout->addWidget(warnButton);

    // linkage: checkbox unlocks an initially-disabled button
    auto *unlockCheck = new QCheckBox(QStringLiteral("Unlock feature"));
    unlockCheck->setObjectName(QStringLiteral("unlockCheck"));
    auto *lockedButton = new QPushButton(QStringLiteral("Locked Action"));
    lockedButton->setObjectName(QStringLiteral("lockedButton"));
    lockedButton->setEnabled(false);
    lockedButton->setToolTip(QStringLiteral("勾选“Unlock feature”后此按钮才会启用"));
    auto *lockedLabel = new QLabel(QStringLiteral("Not run"));
    lockedLabel->setObjectName(QStringLiteral("lockedLabel"));
    QObject::connect(unlockCheck, &QCheckBox::toggled, lockedButton, &QWidget::setEnabled);
    QObject::connect(lockedButton, &QPushButton::clicked, lockedLabel, [lockedLabel]() {
        lockedLabel->setText(QStringLiteral("Clicked"));
    });
    basicLayout->addWidget(unlockCheck);
    basicLayout->addWidget(lockedButton);
    basicLayout->addWidget(lockedLabel);

    // Validation: switching to the Views tab requires a name. A rejected switch
    // reverts asynchronously so set_property callers see the readback value.
    QObject::connect(tabs, &QTabWidget::currentChanged, &window,
                     [&window, tabs, nameEdit](int index) {
                         if (index == 1 && nameEdit->text().isEmpty()) {
                             window.statusBar()->showMessage(
                                 QStringLiteral("Name required before switching to Views"),
                                 3000);
                             QTimer::singleShot(0, tabs,
                                                [tabs]() { tabs->setCurrentIndex(0); });
                         }
                     });

    tabs->addTab(basicTab, QStringLiteral("Basic"));

    // ------------------------------------------------- Tab 2: item views
    auto *viewsTab = new QWidget;
    viewsTab->setObjectName(QStringLiteral("viewsTab"));
    auto *viewsLayout = new QHBoxLayout(viewsTab);

    auto *listColumn = new QVBoxLayout;
    auto *itemList = new QListWidget;
    itemList->setObjectName(QStringLiteral("itemList"));
    itemList->addItems({QStringLiteral("Apple"), QStringLiteral("Banana"),
                        QStringLiteral("Cherry")});
    auto *listDetailLabel = new QLabel(QStringLiteral("Selected: none"));
    listDetailLabel->setObjectName(QStringLiteral("listDetailLabel"));
    // linkage: list selection drives the detail label
    QObject::connect(itemList, &QListWidget::currentTextChanged, listDetailLabel,
                     [listDetailLabel](const QString &text) {
                         listDetailLabel->setText(QStringLiteral("Selected: ") + text);
                     });
    listColumn->addWidget(itemList);
    listColumn->addWidget(listDetailLabel);

    auto *treeColumn = new QVBoxLayout;
    auto *itemTree = new QTreeWidget;
    itemTree->setObjectName(QStringLiteral("itemTree"));
    itemTree->setHeaderLabels({QStringLiteral("Name")});
    auto *fruits = new QTreeWidgetItem(itemTree, {QStringLiteral("Fruits")});
    auto *appleItem = new QTreeWidgetItem(fruits, {QStringLiteral("Apple")});
    appleItem->setCheckState(0, Qt::Unchecked);
    auto *bananaItem = new QTreeWidgetItem(fruits, {QStringLiteral("Banana")});
    bananaItem->setCheckState(0, Qt::Unchecked);
    auto *vegetables = new QTreeWidgetItem(itemTree, {QStringLiteral("Vegetables")});
    auto *carrotItem = new QTreeWidgetItem(vegetables, {QStringLiteral("Carrot")});
    carrotItem->setCheckState(0, Qt::Unchecked);
    fruits->setExpanded(true);

    auto *treeDetailLabel = new QLabel(QStringLiteral("Tree: none"));
    treeDetailLabel->setObjectName(QStringLiteral("treeDetailLabel"));
    // linkages: selection / double click / right-click context request
    QObject::connect(itemTree, &QTreeWidget::currentItemChanged, treeDetailLabel,
                     [treeDetailLabel](QTreeWidgetItem *current, QTreeWidgetItem *) {
                         treeDetailLabel->setText(current
                                                      ? QStringLiteral("Tree: ") + current->text(0)
                                                      : QStringLiteral("Tree: none"));
                     });
    QObject::connect(itemTree, &QTreeWidget::itemDoubleClicked, treeDetailLabel,
                     [treeDetailLabel](QTreeWidgetItem *item, int) {
                         treeDetailLabel->setText(QStringLiteral("Double: ") + item->text(0));
                     });
    itemTree->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(itemTree, &QTreeWidget::customContextMenuRequested, treeDetailLabel,
                     [treeDetailLabel, itemTree](const QPoint &pos) {
                         if (QTreeWidgetItem *item = itemTree->itemAt(pos))
                             treeDetailLabel->setText(QStringLiteral("Context: ")
                                                      + item->text(0));
                     });
    treeColumn->addWidget(itemTree);
    treeColumn->addWidget(treeDetailLabel);

    auto *tableColumn = new QVBoxLayout;
    auto *itemTable = new QTableWidget(3, 3);
    itemTable->setObjectName(QStringLiteral("itemTable"));
    int n = 1;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            itemTable->setItem(r, c, new QTableWidgetItem(QString::number(n++)));
    auto *sumLabel = new QLabel;
    sumLabel->setObjectName(QStringLiteral("sumLabel"));
    // linkage: any cell edit recomputes the sum
    const auto recomputeSum = [itemTable, sumLabel]() {
        double sum = 0;
        for (int r = 0; r < itemTable->rowCount(); ++r)
            for (int c = 0; c < itemTable->columnCount(); ++c)
                if (QTableWidgetItem *it = itemTable->item(r, c))
                    sum += it->text().toDouble();
        sumLabel->setText(QStringLiteral("Sum: %1").arg(sum));
    };
    recomputeSum();
    QObject::connect(itemTable, &QTableWidget::cellChanged, sumLabel,
                     [recomputeSum](int, int) { recomputeSum(); });
    tableColumn->addWidget(itemTable);
    tableColumn->addWidget(sumLabel);

    // Table with non-text cell editors: combo-box column + spin-box column.
    auto *delegateTable = new QTableWidget(2, 2);
    delegateTable->setObjectName(QStringLiteral("delegateTable"));
    delegateTable->setItem(0, 0, new QTableWidgetItem(QStringLiteral("Red")));
    delegateTable->setItem(0, 1, new QTableWidgetItem(QStringLiteral("5")));
    delegateTable->setItem(1, 0, new QTableWidgetItem(QStringLiteral("Green")));
    delegateTable->setItem(1, 1, new QTableWidgetItem(QStringLiteral("10")));
    delegateTable->setItemDelegateForColumn(0, new ComboBoxDelegate(delegateTable));
    delegateTable->setItemDelegateForColumn(1, new SpinBoxDelegate(delegateTable));
    auto *delegateLabel = new QLabel;
    delegateLabel->setObjectName(QStringLiteral("delegateLabel"));
    const auto updateDelegateLabel = [delegateTable, delegateLabel]() {
        delegateLabel->setText(QStringLiteral("delegates: %1/%2 %3/%4")
                                   .arg(delegateTable->item(0, 0)->text(),
                                        delegateTable->item(0, 1)->text(),
                                        delegateTable->item(1, 0)->text(),
                                        delegateTable->item(1, 1)->text()));
    };
    updateDelegateLabel();
    QObject::connect(delegateTable, &QTableWidget::cellChanged, delegateLabel,
                     [updateDelegateLabel](int, int) { updateDelegateLabel(); });
    tableColumn->addWidget(delegateTable);
    tableColumn->addWidget(delegateLabel);

    viewsLayout->addLayout(listColumn);
    viewsLayout->addLayout(treeColumn);
    viewsLayout->addLayout(tableColumn);
    tabs->addTab(viewsTab, QStringLiteral("Views"));

    window.setCentralWidget(tabs);

    // ---------------------------------------------------- toolbar and menus
    auto *toolBar = window.addToolBar(QStringLiteral("Main"));
    toolBar->setObjectName(QStringLiteral("mainToolBar"));
    QAction *toolApplyAction = toolBar->addAction(QStringLiteral("Apply"));
    toolApplyAction->setObjectName(QStringLiteral("toolApplyAction"));
    // linkage: toolbar action behaves exactly like the Apply button
    QObject::connect(toolApplyAction, &QAction::triggered, applyButton, &QPushButton::click);

    QMenu *fileMenu = window.menuBar()->addMenu(QStringLiteral("&File"));
    fileMenu->setObjectName(QStringLiteral("fileMenu"));
    QAction *quitAction = fileMenu->addAction(QStringLiteral("&Quit"));
    quitAction->setObjectName(QStringLiteral("quitAction"));
    // Goes through closeEvent: unsaved changes trigger the save/discard/cancel prompt.
    QObject::connect(quitAction, &QAction::triggered, &window, &QWidget::close);

    window.resize(800, 640);
    window.show();

    return app.exec();
}
