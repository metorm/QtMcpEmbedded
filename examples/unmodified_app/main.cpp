#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>

int main(int argc, char *argv[])
{
    // Completely standard Qt application: NO QtMcp includes, NO QtMcp::install().
    QApplication app(argc, argv);

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("Unmodified Qt App (Plugin Target)"));
    window.setObjectName(QStringLiteral("unmodifiedMainWindow"));

    auto *central = new QWidget(&window);
    auto *layout = new QVBoxLayout(central);

    auto *infoLabel = new QLabel(QStringLiteral("This app has ZERO QtMcp code. Driven purely via QGenericPlugin."), central);
    infoLabel->setObjectName(QStringLiteral("infoLabel"));
    layout->addWidget(infoLabel);

    auto *nameRow = new QHBoxLayout();
    auto *nameLabel = new QLabel(QStringLiteral("Name:"), central);
    auto *nameEdit = new QLineEdit(central);
    nameEdit->setObjectName(QStringLiteral("nameEdit"));
    nameEdit->setPlaceholderText(QStringLiteral("Type something..."));
    nameRow->addWidget(nameLabel);
    nameRow->addWidget(nameEdit);
    layout->addLayout(nameRow);

    auto *statusLabel = new QLabel(QStringLiteral("Status: Initial"), central);
    statusLabel->setObjectName(QStringLiteral("statusLabel"));
    layout->addWidget(statusLabel);

    auto *applyButton = new QPushButton(QStringLiteral("Apply Changes"), central);
    applyButton->setObjectName(QStringLiteral("applyButton"));
    layout->addWidget(applyButton);

    QObject::connect(applyButton, &QPushButton::clicked, [nameEdit, statusLabel]() {
        statusLabel->setText(QStringLiteral("Status: Hello, ") + nameEdit->text());
    });

    window.setCentralWidget(central);
    window.resize(450, 220);
    window.show();

    return app.exec();
}
