#include "dashboard.hpp"

#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QNetworkDatagram>
#include <QPlainTextEdit>
#include <QStatusBar>
#include <QTimer>
#include <QUdpSocket>
#include <QVBoxLayout>

#include "gauge.hpp"

namespace {
constexpr qint64 kStaleMs = 3000;

const char* fmiText(int fmi) {
    switch (fmi) {
        case 0: return "above normal (most severe)";
        case 1: return "below normal (most severe)";
        case 2: return "erratic / intermittent";
        case 3: return "voltage above normal";
        case 4: return "voltage below normal";
        case 16: return "above normal (moderate)";
        case 18: return "below normal (moderate)";
        default: return "see J1939-73";
    }
}
QString spnText(int spn) {
    switch (spn) {
        case 110: return "Engine Coolant Temperature";
        case 175: return "Engine Oil Temperature";
        case 190: return "Engine Speed";
        case 84: return "Wheel-Based Vehicle Speed";
        default: return QString("SPN %1").arg(spn);
    }
}
}  // namespace

Dashboard::Dashboard(quint16 port, QWidget* parent) : QMainWindow(parent), socket_(new QUdpSocket(this)) {
    setWindowTitle("J1939 Vehicle Bus Monitor");

    rpm_ = new Gauge("Engine Speed", "rpm", 0, 3000);
    rpm_->setZones(2500, 2800);
    speed_ = new Gauge("Vehicle Speed", "km/h", 0, 140);
    coolant_ = new Gauge("Coolant Temp", "\u00B0C", 40, 130);
    coolant_->setZones(100, 110);  // matches the monitor's default thresholds

    banner_ = new QLabel;
    banner_->setAlignment(Qt::AlignCenter);
    banner_->setMinimumHeight(56);
    QFont bf = banner_->font();
    bf.setPointSize(16);
    bf.setBold(true);
    banner_->setFont(bf);
    setStateBanner("Normal", "waiting for data");

    dtcs_ = new QListWidget;
    events_ = new QPlainTextEdit;
    events_->setReadOnly(true);
    events_->setMaximumBlockCount(500);

    auto* gauges = new QHBoxLayout;
    gauges->addWidget(rpm_);
    gauges->addWidget(speed_);
    gauges->addWidget(coolant_);

    auto* dtcBox = new QGroupBox("Active DTCs (DM1)");
    (new QVBoxLayout(dtcBox))->addWidget(dtcs_);
    auto* evBox = new QGroupBox("State transitions");
    (new QVBoxLayout(evBox))->addWidget(events_);
    auto* bottom = new QHBoxLayout;
    bottom->addWidget(dtcBox, 1);
    bottom->addWidget(evBox, 1);

    auto* central = new QWidget;
    auto* root = new QVBoxLayout(central);
    root->addWidget(banner_);
    root->addLayout(gauges, 3);
    root->addLayout(bottom, 2);
    setCentralWidget(central);

    status_ = new QLabel;
    statusBar()->addWidget(status_);
    if (!socket_->bind(QHostAddress::LocalHost, port))
        status_->setText(QString("could not bind UDP port %1").arg(port));
    else
        status_->setText(QString("listening on udp://127.0.0.1:%1").arg(port));

    connect(socket_, &QUdpSocket::readyRead, this, &Dashboard::readDatagrams);
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &Dashboard::checkStale);
    timer->start(250);
    resize(900, 640);
}

void Dashboard::readDatagrams() {
    while (socket_->hasPendingDatagrams()) {
        const QNetworkDatagram dg = socket_->receiveDatagram();
        const QJsonDocument doc = QJsonDocument::fromJson(dg.data());
        if (!doc.isObject()) continue;  // ignore anything malformed
        const QJsonObject o = doc.object();
        const QString type = o.value("type").toString();
        ++received_;
        if (type == "signal") handleSignal(o);
        else if (type == "dm1") handleDm1(o);
        else if (type == "state") handleState(o);
    }
    status_->setText(QString("listening on udp://127.0.0.1:%1  |  %2 messages")
                         .arg(socket_->localPort()).arg(received_));
}

void Dashboard::handleSignal(const QJsonObject& o) {
    if (!gotData_) {
        gotData_ = true;
        setStateBanner("Normal", "receiving data");
    }
    const int spn = o.value("spn").toInt();
    const double v = o.value("value").toDouble();
    lastSeenMs_[spn] = QDateTime::currentMSecsSinceEpoch();
    if (spn == 190) rpm_->setValue(v);
    else if (spn == 84) speed_->setValue(v);
    else if (spn == 110) coolant_->setValue(v);
}

void Dashboard::handleDm1(const QJsonObject& o) {
    dtcs_->clear();
    const QJsonArray arr = o.value("dtcs").toArray();
    if (arr.isEmpty()) {
        dtcs_->addItem("No active faults");
        return;
    }
    for (const auto& d : arr) {
        const QJsonObject dtc = d.toObject();
        const int spn = dtc.value("spn").toInt(), fmi = dtc.value("fmi").toInt();
        dtcs_->addItem(QString("SPN %1 / FMI %2\n    %3: %4 (x%5)")
                           .arg(spn).arg(fmi).arg(spnText(spn), fmiText(fmi))
                           .arg(dtc.value("oc").toInt()));
    }
}

void Dashboard::handleState(const QJsonObject& o) {
    gotData_ = true;
    const QString to = o.value("to").toString(), reason = o.value("reason").toString();
    setStateBanner(to, reason);
    log(QString("%1 -> %2   %3").arg(o.value("from").toString(), to, reason));
}

void Dashboard::setStateBanner(const QString& state, const QString& reason) {
    QString bg = "#2E7D32";  // Normal
    if (state == "Warning") bg = "#E8A317";
    else if (state == "Fault") bg = "#C62828";
    else if (state == "Recovery") bg = "#1565C0";
    banner_->setStyleSheet(QString("background:%1; color:white; border-radius:6px; padding:6px;").arg(bg));
    banner_->setText(QString("%1  -  %2").arg(state.toUpper(), reason));
}

void Dashboard::log(const QString& line) {
    events_->appendPlainText(QDateTime::currentDateTime().toString("hh:mm:ss.zzz  ") + line);
}

void Dashboard::checkStale() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    auto stale = [&](int spn) { return lastSeenMs_.contains(spn) && now - lastSeenMs_[spn] > kStaleMs; };
    rpm_->setStale(stale(190));
    speed_->setStale(stale(84));
    coolant_->setStale(stale(110));
}
