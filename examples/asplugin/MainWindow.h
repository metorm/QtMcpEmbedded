#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QJsonObject>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QCheckBox;
class QRadioButton;
class QSpinBox;
class QSlider;
class QProgressBar;
class QComboBox;
class QListWidget;
class QTableWidget;
class QLabel;
class QTimer;
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT
    Q_PROPERTY(QString customStatus READ customStatus WRITE setCustomStatus NOTIFY customStatusChanged)

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    QString customStatus() const { return m_customStatus; }
    void setCustomStatus(const QString &status);

    // Q_INVOKABLE slots for qt_invoke_slot testing
    Q_INVOKABLE QString echo(const QString &message);
    Q_INVOKABLE int addNumbers(int a, int b);
    Q_INVOKABLE bool toggleFlag(bool flag);
    Q_INVOKABLE QJsonObject getAppInfo();

public slots:
    void setStatus(const QString &text);
    void resetForm();
    void appendLog(const QString &msg);

signals:
    void customStatusChanged(const QString &status);

private slots:
    void onSubmitClicked();
    void onResetClicked();
    void onTriggerDelayClicked();
    void onOpenCustomDialog();
    void onShowMessageBox();
    void onOpenFileDialog();
    void onActionAbout();

private:
    void setupUiManual();
    void createMenusAndActions();

    Ui::MainWindow *ui;
    QString m_customStatus;

    // Tab 1: Controls
    QLineEdit *m_nameInput = nullptr;
    QLineEdit *m_passwordInput = nullptr;
    QPlainTextEdit *m_notesEdit = nullptr;
    QLabel *m_echoLabel = nullptr;
    QPushButton *m_btnSubmit = nullptr;
    QPushButton *m_btnReset = nullptr;
    QPushButton *m_btnDelay = nullptr;
    QCheckBox *m_chkAgree = nullptr;
    QRadioButton *m_rbOptionA = nullptr;
    QRadioButton *m_rbOptionB = nullptr;
    QSpinBox *m_spinValue = nullptr;
    QSlider *m_sliderVolume = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QComboBox *m_comboCity = nullptr;

    // Tab 2: Views
    QListWidget *m_itemList = nullptr;
    QLabel *m_labelSelectedItem = nullptr;
    QPushButton *m_btnAddItem = nullptr;
    QTableWidget *m_tableData = nullptr;

    // Tab 3: Dialogs
    QPushButton *m_btnOpenDialog = nullptr;
    QPushButton *m_btnShowMsgBox = nullptr;
    QPushButton *m_btnOpenFileDialog = nullptr;
    QLabel *m_labelDialogResult = nullptr;

    // Logging & Status
    QPlainTextEdit *m_logViewer = nullptr;
    QLabel *m_statusLabel = nullptr;
    QTimer *m_delayTimer = nullptr;
};

#endif // MAINWINDOW_H
