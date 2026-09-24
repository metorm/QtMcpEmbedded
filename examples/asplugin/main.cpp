#include "MainWindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_GENERIC_PLUGINS", "qtmcp");
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return QApplication::exec();
}
