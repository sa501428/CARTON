#ifndef CARTON_GENOMIC_TRACK_READER_H
#define CARTON_GENOMIC_TRACK_READER_H

#include <QColor>
#include <QString>
#include <QVector>

struct GenomicTrackFeature {
    QString chr;
    qint64 start = 0;
    qint64 end = 0;
    QString name;
    double value = 1.0;
    QColor color = QColor("#4b7bec");
};

struct GenomicTrackReadResult {
    QVector<GenomicTrackFeature> features;
    QString format;
    QString warning;
};

GenomicTrackReadResult readGenomicTrack(const QString& pathOrUrl);

#endif
