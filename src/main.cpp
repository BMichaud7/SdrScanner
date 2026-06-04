/*
========================================================================
Project: OpenRFStack
Author:  Brendan Michaud
Year:    2026
Part of OpenRFStack (https://github.com/OpenRFStack)

Licensed under the Personal Use License.
Do not use for commercial, organizational, or military purposes.
Contact author for permission: https://github.com/OpenRFStack
========================================================================
*/
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

/*
========================================================================
End of file — OpenRFStack
Subject to Personal Use License
https://github.com/OpenRFStack
========================================================================
*/
