#include "GenomicTrackReader.h"

#include <QFileInfo>
#include <QUrl>

#include <igv/igv.hpp>

#include <cstddef>
#include <exception>
#include <string>

namespace {
constexpr std::size_t kMaxResidentRecords = 1000000;

std::string utf8(const QString& value) {
    const QByteArray encoded = value.toUtf8();
    return {encoded.constData(), static_cast<std::size_t>(encoded.size())};
}

QString sourceBaseName(const QString& pathOrUrl) {
    const QUrl url = QUrl::fromUserInput(pathOrUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.path();
    const QString baseName = QFileInfo(path).baseName();
    return baseName.isEmpty() ? QStringLiteral("track") : baseName;
}

QString cartonFormat(igv::Format format) {
    switch (format) {
        case igv::Format::bed: return QStringLiteral("bed");
        case igv::Format::bedgraph: return QStringLiteral("bedGraph");
        case igv::Format::wig: return QStringLiteral("wig");
        case igv::Format::bigwig: return QStringLiteral("bigWig");
        case igv::Format::bigbed: return QStringLiteral("bigBed");
        case igv::Format::bedpe: return QStringLiteral("bedpe");
        default: return QString::fromUtf8(igv::format_name(format).data(),
                                          static_cast<qsizetype>(igv::format_name(format).size()));
    }
}

QColor parseColor(const std::string& value, const QColor& fallback) {
    const QString token = QString::fromUtf8(value);
    QColor color(token);
    if (color.isValid()) return color;
    const QStringList rgb = token.split(QLatin1Char(','));
    if (rgb.size() != 3) return fallback;
    bool redOk = false, greenOk = false, blueOk = false;
    const int red = rgb[0].toInt(&redOk);
    const int green = rgb[1].toInt(&greenOk);
    const int blue = rgb[2].toInt(&blueOk);
    return redOk && greenOk && blueOk ? QColor(red, green, blue) : fallback;
}

QString cappedWarning(const QString& kind) {
    return QStringLiteral("%1 was capped at %2 records to keep memory bounded.")
        .arg(kind)
        .arg(kMaxResidentRecords);
}
}

GenomicTrackReadResult readGenomicTrack(const QString& pathOrUrl) {
    GenomicTrackReadResult result;
    try {
        const igv::Resource resource{.uri = utf8(pathOrUrl)};
        const igv::Format format = igv::detect_format(resource);
        result.format = cartonFormat(format);
        const QString defaultName = sourceBaseName(pathOrUrl);
        igv::AnyReader reader = igv::open_reader(resource);

        if (auto* featureReader = std::get_if<std::unique_ptr<igv::FeatureReader>>(&reader)) {
            auto batch = (*featureReader)->read_all(kMaxResidentRecords);
            result.features.reserve(static_cast<qsizetype>(batch.records.size()));
            for (const igv::Feature& record : batch.records) {
                GenomicTrackFeature feature;
                feature.chr = QString::fromUtf8(record.interval.contig);
                feature.start = record.interval.start;
                feature.end = record.interval.end;
                feature.name = record.name.empty() ? defaultName : QString::fromUtf8(record.name);
                feature.value = record.score.value_or(1.0);
                if (record.color) feature.color = parseColor(*record.color, feature.color);
                result.features.push_back(std::move(feature));
            }
            if (batch.truncated) result.warning = cappedWarning(QStringLiteral("Track"));
            return result;
        }

        if (auto* signalReader = std::get_if<std::unique_ptr<igv::SignalReader>>(&reader)) {
            auto batch = (*signalReader)->read_all(kMaxResidentRecords);
            result.features.reserve(static_cast<qsizetype>(batch.records.size()));
            for (const igv::SignalValue& record : batch.records) {
                result.features.push_back({
                    QString::fromUtf8(record.interval.contig), record.interval.start, record.interval.end,
                    defaultName, record.value, QColor("#4b7bec")});
            }
            if (batch.truncated) result.warning = cappedWarning(QStringLiteral("Track"));
            return result;
        }

        result.warning = QStringLiteral("%1 is not a feature or signal track.").arg(pathOrUrl);
    } catch (const std::exception& error) {
        result.warning = QString::fromUtf8(error.what());
    }
    return result;
}

GenomicInteractionReadResult readGenomicInteractions(const QString& pathOrUrl) {
    GenomicInteractionReadResult result;
    try {
        const igv::Resource resource{.uri = utf8(pathOrUrl)};
        const igv::Format format = igv::detect_format(resource);
        result.format = cartonFormat(format);
        const QString defaultName = sourceBaseName(pathOrUrl);
        igv::AnyReader reader = igv::open_reader(resource);
        auto* interactionReader = std::get_if<std::unique_ptr<igv::InteractionReader>>(&reader);
        if (interactionReader == nullptr) {
            result.warning = QStringLiteral("%1 is not an interaction file.").arg(pathOrUrl);
            return result;
        }

        auto batch = (*interactionReader)->read_all(kMaxResidentRecords);
        result.interactions.reserve(static_cast<qsizetype>(batch.records.size()));
        for (const igv::Interaction& record : batch.records) {
            GenomicInteraction interaction;
            interaction.chr1 = QString::fromUtf8(record.first.contig);
            interaction.start1 = record.first.start;
            interaction.end1 = record.first.end;
            interaction.chr2 = QString::fromUtf8(record.second.contig);
            interaction.start2 = record.second.start;
            interaction.end2 = record.second.end;
            interaction.name = record.name.empty() ? defaultName : QString::fromUtf8(record.name);
            interaction.value = record.score.value_or(1.0);
            for (const auto& [key, value] : record.attributes) {
                interaction.attributes.insert(QString::fromUtf8(key), QString::fromUtf8(value));
            }
            if (const auto color = record.attributes.find("color"); color != record.attributes.end()) {
                interaction.color = parseColor(color->second, interaction.color);
            }
            result.interactions.push_back(std::move(interaction));
        }
        if (batch.truncated) result.warning = cappedWarning(QStringLiteral("Interaction file"));
    } catch (const std::exception& error) {
        result.warning = QString::fromUtf8(error.what());
    }
    return result;
}

GenomicCytobandReadResult readGenomicCytobands(const QString& pathOrUrl) {
    GenomicCytobandReadResult result;
    try {
        const igv::Resource resource{.uri = utf8(pathOrUrl), .format = "cytoband"};
        auto reader = igv::open_cytoband(resource);
        auto batch = reader->read_all(kMaxResidentRecords);
        result.cytobands.reserve(static_cast<qsizetype>(batch.records.size()));
        for (const igv::Feature& record : batch.records) {
            const auto stain = record.attributes.find("stain");
            result.cytobands.push_back({
                QString::fromUtf8(record.interval.contig), record.interval.start, record.interval.end,
                QString::fromUtf8(record.name),
                stain == record.attributes.end() ? QStringLiteral("gneg") : QString::fromUtf8(stain->second)});
        }
        if (batch.truncated) result.warning = cappedWarning(QStringLiteral("Cytoband file"));
    } catch (const std::exception& error) {
        result.warning = QString::fromUtf8(error.what());
    }
    return result;
}
