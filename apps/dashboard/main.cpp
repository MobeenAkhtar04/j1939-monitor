#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

#include "dashboard.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("j1939_dashboard");

    QCommandLineParser parser;
    parser.setApplicationDescription("Live dashboard for j1939_monitor (listens for UDP JSON events)");
    parser.addHelpOption();
    QCommandLineOption portOpt({"p", "port"}, "UDP port to listen on (default 9000).", "port", "9000");
    parser.addOption(portOpt);
    QCommandLineOption shotOpt("screenshot", "Save a PNG of the window after --after ms, then exit.", "file");
    QCommandLineOption afterOpt("after", "Delay before --screenshot (default 5000 ms).", "ms", "5000");
    parser.addOption(shotOpt);
    parser.addOption(afterOpt);
    parser.process(app);

    Dashboard d(static_cast<quint16>(parser.value(portOpt).toUShort()));
    d.show();
    if (parser.isSet(shotOpt)) {
        QTimer::singleShot(parser.value(afterOpt).toInt(), &app, [&] {
            d.grab().save(parser.value(shotOpt));
            app.quit();
        });
    }
    return app.exec();
}
