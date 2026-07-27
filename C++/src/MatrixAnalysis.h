#ifndef CARTON_MATRIX_ANALYSIS_H
#define CARTON_MATRIX_ANALYSIS_H

#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <vector>

#include "straw.h"

namespace MatrixAnalysis {

enum class SimilarityMetric {
    Pearson,
    Cosine
};

struct DenseMatrix {
    int width = 0;
    int height = 0;
    int resolution = 1;
    qint64 x0 = 0;
    qint64 y0 = 0;
    std::vector<float> values;
    std::vector<bool> present;
    float minimum = 0.0f;
    float maximum = 0.0f;
    QString error;

    bool valid() const {
        return error.isEmpty() && width > 0 && height > 0 &&
               values.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    }
};

struct PolarPixel {
    int dx = 0;
    int dy = 0;
    double radius = 0.0;
    double angle = 0.0;
    float value = 0.0f;
    bool present = false;
};

DenseMatrix makeDense(const std::vector<contactRecord>& records, int resolution,
                      qint64 x0, qint64 x1, qint64 y0, qint64 y1,
                      bool reflectIntra, int maximumBins = 1024);

std::vector<PolarPixel> bullseye(const std::vector<contactRecord>& records, int resolution,
                                qint64 centerX, qint64 centerY, int radiusBins,
                                bool reflectIntra);

std::vector<float> virtual4C(const std::vector<contactRecord>& records, int resolution,
                            qint64 anchor, qint64 targetStart, qint64 targetEnd,
                            bool reflectIntra, bool anchorOnX = true);

// Filters a diagonal strip and, when necessary, collapses contacts into a
// screen-shaped bucket grid. The result never exceeds maxRecords.
std::vector<contactRecord> boundedRotatedRecords(const std::vector<contactRecord>& records,
                                                 int resolution,
                                                 qint64 viewStart, qint64 viewEnd,
                                                 qint64 maxDistance,
                                                 int pixelWidth, int pixelHeight,
                                                 int maxRecords);

// Calculates a symmetric row-by-row similarity matrix over a bounded local
// context. Pearson follows Juicebox semantics: the supplied values are O/E and
// each row is mean-centered before correlation. Only one triangle is returned;
// intrachromosomal rendering reflects it when appropriate.
std::vector<contactRecord> similarity(const std::vector<contactRecord>& records,
                                      SimilarityMetric metric, int resolution,
                                      qint64 contextStart, qint64 contextEnd,
                                      qint64 outputX0, qint64 outputX1,
                                      qint64 outputY0, qint64 outputY1,
                                      int maximumBins = 1800);

DenseMatrix process(const DenseMatrix& input, const QString& operation,
                    double parameter = 1.0, double threshold = 0.0);

QStringList supportedOperations();

} // namespace MatrixAnalysis

#endif
