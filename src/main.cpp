#include "MainWindow.hpp"
#include <QApplication>

int main(int argc, char* argv[]) {
    qRegisterMetaType<Signal>();
    qRegisterMetaType<QVector<Signal>>();

    QApplication app(argc, argv);
    app.setApplicationName("SDR Band Scanner");
    app.setOrganizationName("SdrProject");

    MainWindow w;
    w.show();
    return app.exec();
}
