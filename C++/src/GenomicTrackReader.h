#ifndef CARTON_GENOMIC_TRACK_READER_H
#define CARTON_GENOMIC_TRACK_READER_H

#include <QColor>
#include <QMap>
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

struct GenomicInteraction {
    QString chr1;
    qint64 start1 = 0;
    qint64 end1 = 0;
    QString chr2;
    qint64 start2 = 0;
    qint64 end2 = 0;
    QString name;
    double value = 1.0;
    QColor color = QColor("#111111");
    QMap<QString, QString> attributes;
};

struct GenomicInteractionReadResult {
    QVector<GenomicInteraction> interactions;
    QString format;
    QString warning;
};

struct GenomicCytoband {
    QString chr;
    qint64 start = 0;
    qint64 end = 0;
    QString name;
    QString stain;
};

struct GenomicCytobandReadResult {
    QVector<GenomicCytoband> cytobands;
    QString warning;
};

GenomicTrackReadResult readGenomicTrack(const QString& pathOrUrl);
GenomicInteractionReadResult readGenomicInteractions(const QString& pathOrUrl);
GenomicCytobandReadResult readGenomicCytobands(const QString& pathOrUrl);

#endif
