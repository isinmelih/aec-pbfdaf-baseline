#ifdef HAVE_QT

#include <QApplication>
#include "MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    MainWindow w;
    w.resize(1000, 800);
    w.show();
    
    return app.exec();
}

#else

int main(int argc, char *argv[]) {
    return 0;
}

#endif // HAVE_QT
