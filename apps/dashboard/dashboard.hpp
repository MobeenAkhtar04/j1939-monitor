#pragma once
#include <QDateTime>
#include <QHash>
#include <QMainWindow>

class Gauge;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QUdpSocket;
class QJsonObject;

// Listens for JSON datagrams from j1939_monitor and renders them live.
class Dashboard : public QMainWindow {
    Q_OBJECT
public:
    explicit Dashboard(quint16 port, QWidget* parent = nullptr);

private slots:
    void readDatagrams();
    void checkStale();

private:
    void handleSignal(const QJsonObject& o);
    void handleDm1(const QJsonObject& o);
    void handleState(const QJsonObject& o);
    void setStateBanner(const QString& state, const QString& reason);
    void log(const QString& line);

    QUdpSocket* socket_;
    Gauge* rpm_;
    Gauge* speed_;
    Gauge* coolant_;
    QLabel* banner_;
    QLabel* status_;
    QListWidget* dtcs_;
    QPlainTextEdit* events_;
    QHash<int, qint64> lastSeenMs_;  // SPN -> last update (for stale gauges)
    quint64 received_ = 0;
    bool gotData_ = false;
};
