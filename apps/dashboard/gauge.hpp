#pragma once
#include <QWidget>

// Circular analog gauge drawn with QPainter. Optional warning/fault zones are
// painted as coloured bands on the dial.
class Gauge : public QWidget {
    Q_OBJECT
public:
    Gauge(QString title, QString unit, double min, double max, QWidget* parent = nullptr);

    void setZones(double warning, double fault);  // values at which bands start
    void setValue(double v);
    void setStale(bool stale);                     // grey out when data stops arriving

    QSize sizeHint() const override { return {220, 220}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    double toAngle(double v) const;

    QString title_, unit_;
    double min_, max_;
    double value_ = 0;
    bool hasValue_ = false;
    bool stale_ = false;
    double warning_ = -1, fault_ = -1;
};
