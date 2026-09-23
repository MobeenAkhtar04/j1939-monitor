#include "gauge.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>

namespace {
constexpr double kStartDeg = 225.0;  // dial runs clockwise from 225 deg to -45 deg
constexpr double kSpanDeg = 270.0;
}  // namespace

Gauge::Gauge(QString title, QString unit, double min, double max, QWidget* parent)
    : QWidget(parent), title_(std::move(title)), unit_(std::move(unit)), min_(min), max_(max) {
    setMinimumSize(180, 180);
}

void Gauge::setZones(double warning, double fault) {
    warning_ = warning;
    fault_ = fault;
    update();
}

void Gauge::setValue(double v) {
    value_ = v;
    hasValue_ = true;
    stale_ = false;
    update();
}

void Gauge::setStale(bool stale) {
    if (stale_ != stale) {
        stale_ = stale;
        update();
    }
}

double Gauge::toAngle(double v) const {
    const double f = qBound(0.0, (v - min_) / (max_ - min_), 1.0);
    return kStartDeg - f * kSpanDeg;
}

void Gauge::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int side = qMin(width(), height()) - 12;
    const QRectF dial((width() - side) / 2.0, (height() - side) / 2.0, side, side);
    const QPointF c = dial.center();
    const double r = side / 2.0;
    const QPalette pal = palette();

    // base arc
    const double band = r * 0.10;
    QRectF arcRect = dial.adjusted(band, band, -band, -band);
    p.setPen(QPen(pal.color(QPalette::Mid), band, Qt::SolidLine, Qt::FlatCap));
    p.drawArc(arcRect, int(kStartDeg * 16), int(-kSpanDeg * 16));

    // warning / fault bands
    auto zone = [&](double from, double to, QColor col) {
        const double a0 = toAngle(from), a1 = toAngle(to);
        p.setPen(QPen(col, band, Qt::SolidLine, Qt::FlatCap));
        p.drawArc(arcRect, int(a0 * 16), int((a1 - a0) * 16));
    };
    if (warning_ >= 0) zone(warning_, fault_ >= 0 ? fault_ : max_, QColor(0xE8, 0xA3, 0x17));
    if (fault_ >= 0) zone(fault_, max_, QColor(0xD6, 0x3B, 0x30));

    // ticks + labels
    p.setPen(QPen(pal.color(QPalette::Text), 1.5));
    QFont small = font();
    small.setPointSizeF(qMax(7.0, r * 0.075));
    p.setFont(small);
    for (int i = 0; i <= 10; ++i) {
        const double v = min_ + (max_ - min_) * i / 10.0;
        const double a = qDegreesToRadians(toAngle(v));
        const QPointF dir(qCos(a), -qSin(a));
        p.drawLine(c + dir * (r * 0.70), c + dir * (r * 0.78));
        if (i % 2 == 0) {
            const QPointF lp = c + dir * (r * 0.58);
            p.drawText(QRectF(lp.x() - 25, lp.y() - 10, 50, 20), Qt::AlignCenter, QString::number(v, 'f', 0));
        }
    }

    // needle
    const QColor needle = stale_ || !hasValue_ ? pal.color(QPalette::Mid) : QColor(0xD6, 0x3B, 0x30);
    const double a = qDegreesToRadians(toAngle(hasValue_ ? value_ : min_));
    const QPointF tip = c + QPointF(qCos(a), -qSin(a)) * (r * 0.72);
    p.setPen(QPen(needle, qMax(2.0, r * 0.03), Qt::SolidLine, Qt::RoundCap));
    p.drawLine(c, tip);
    p.setBrush(needle);
    p.setPen(Qt::NoPen);
    p.drawEllipse(c, r * 0.05, r * 0.05);

    // readout
    QFont big = font();
    big.setPointSizeF(qMax(10.0, r * 0.16));
    big.setBold(true);
    p.setFont(big);
    p.setPen(stale_ ? pal.color(QPalette::Mid) : pal.color(QPalette::Text));
    const QString text = !hasValue_ ? "--" : stale_ ? "STALE" : QString::number(value_, 'f', 0);
    p.drawText(QRectF(c.x() - r * 0.6, c.y() + r * 0.50, 1.2 * r, r * 0.28), Qt::AlignCenter, text);
    p.setFont(small);
    p.drawText(QRectF(c.x() - r, c.y() + r * 0.80, 2 * r, r * 0.2), Qt::AlignCenter, title_ + " (" + unit_ + ")");
}
