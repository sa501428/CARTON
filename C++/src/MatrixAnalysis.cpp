#include "MatrixAnalysis.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_map>

namespace {
constexpr double kPi = 3.14159265358979323846;

std::size_t matrixIndex(int x, int y, int width) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
}

float sampleClamped(const MatrixAnalysis::DenseMatrix& matrix, int x, int y) {
    x = std::clamp(x, 0, matrix.width - 1);
    y = std::clamp(y, 0, matrix.height - 1);
    return matrix.values[matrixIndex(x, y, matrix.width)];
}

void updateRange(MatrixAnalysis::DenseMatrix& matrix) {
    if (matrix.values.empty()) {
        matrix.minimum = matrix.maximum = 0.0f;
        return;
    }
    matrix.minimum = std::numeric_limits<float>::infinity();
    matrix.maximum = -std::numeric_limits<float>::infinity();
    for (float value : matrix.values) {
        if (!std::isfinite(value)) continue;
        matrix.minimum = std::min(matrix.minimum, value);
        matrix.maximum = std::max(matrix.maximum, value);
    }
    if (!std::isfinite(matrix.minimum)) matrix.minimum = 0.0f;
    if (!std::isfinite(matrix.maximum)) matrix.maximum = matrix.minimum;
}

MatrixAnalysis::DenseMatrix outputLike(const MatrixAnalysis::DenseMatrix& input) {
    MatrixAnalysis::DenseMatrix output = input;
    output.values.assign(input.values.size(), 0.0f);
    output.present.assign(input.values.size(), true);
    output.error.clear();
    return output;
}

std::vector<double> gaussianKernel(double sigma) {
    sigma = std::clamp(sigma, 0.25, 20.0);
    const int radius = std::max(1, static_cast<int>(std::ceil(sigma * 3.0)));
    std::vector<double> kernel(static_cast<std::size_t>(radius * 2 + 1));
    double sum = 0.0;
    for (int i = -radius; i <= radius; ++i) {
        const double value = std::exp(-(i * i) / (2.0 * sigma * sigma));
        kernel[static_cast<std::size_t>(i + radius)] = value;
        sum += value;
    }
    for (double& value : kernel) value /= sum;
    return kernel;
}

MatrixAnalysis::DenseMatrix gaussianBlur(const MatrixAnalysis::DenseMatrix& input, double sigma) {
    MatrixAnalysis::DenseMatrix horizontal = outputLike(input);
    MatrixAnalysis::DenseMatrix output = outputLike(input);
    const std::vector<double> kernel = gaussianKernel(sigma);
    const int radius = static_cast<int>(kernel.size() / 2);
    for (int y = 0; y < input.height; ++y) {
        for (int x = 0; x < input.width; ++x) {
            double sum = 0.0;
            for (int k = -radius; k <= radius; ++k)
                sum += sampleClamped(input, x + k, y) * kernel[static_cast<std::size_t>(k + radius)];
            horizontal.values[matrixIndex(x, y, input.width)] = static_cast<float>(sum);
        }
    }
    for (int y = 0; y < input.height; ++y) {
        for (int x = 0; x < input.width; ++x) {
            double sum = 0.0;
            for (int k = -radius; k <= radius; ++k)
                sum += sampleClamped(horizontal, x, y + k) * kernel[static_cast<std::size_t>(k + radius)];
            output.values[matrixIndex(x, y, input.width)] = static_cast<float>(sum);
        }
    }
    updateRange(output);
    return output;
}

MatrixAnalysis::DenseMatrix morphology(const MatrixAnalysis::DenseMatrix& input, int radius, bool dilate) {
    MatrixAnalysis::DenseMatrix output = outputLike(input);
    radius = std::clamp(radius, 1, 20);
    for (int y = 0; y < input.height; ++y) {
        for (int x = 0; x < input.width; ++x) {
            float value = dilate ? -std::numeric_limits<float>::infinity()
                                 : std::numeric_limits<float>::infinity();
            for (int ky = -radius; ky <= radius; ++ky) {
                for (int kx = -radius; kx <= radius; ++kx) {
                    const float candidate = sampleClamped(input, x + kx, y + ky);
                    value = dilate ? std::max(value, candidate) : std::min(value, candidate);
                }
            }
            output.values[matrixIndex(x, y, input.width)] = value;
        }
    }
    updateRange(output);
    return output;
}

MatrixAnalysis::DenseMatrix gaborFilter(const MatrixAnalysis::DenseMatrix& input,
                                        double sigma, double angle) {
    MatrixAnalysis::DenseMatrix horizontalCos = outputLike(input);
    MatrixAnalysis::DenseMatrix horizontalSin = outputLike(input);
    MatrixAnalysis::DenseMatrix output = outputLike(input);
    const int radius = std::max(2, static_cast<int>(std::ceil(sigma * 3.0)));
    const int kernelSize = radius * 2 + 1;
    const double wavelength = std::max(2.0, sigma * 4.0);
    const double frequency = 2.0 * kPi / wavelength;
    std::vector<double> gaussian(static_cast<std::size_t>(kernelSize));
    std::vector<double> xCos(static_cast<std::size_t>(kernelSize));
    std::vector<double> xSin(static_cast<std::size_t>(kernelSize));
    std::vector<double> yCos(static_cast<std::size_t>(kernelSize));
    std::vector<double> ySin(static_cast<std::size_t>(kernelSize));
    for (int offset = -radius; offset <= radius; ++offset) {
        const std::size_t index = static_cast<std::size_t>(offset + radius);
        gaussian[index] = std::exp(-(offset * offset) / (2.0 * sigma * sigma));
        xCos[index] = std::cos(frequency * std::cos(angle) * offset);
        xSin[index] = std::sin(frequency * std::cos(angle) * offset);
        yCos[index] = std::cos(frequency * std::sin(angle) * offset);
        ySin[index] = std::sin(frequency * std::sin(angle) * offset);
    }

    // An isotropic rotated Gabor kernel is the sum of two separable kernels:
    // GxGy(cosX cosY - sinX sinY). This reduces work from O(radius²) to O(radius).
    for (int y = 0; y < input.height; ++y) {
        for (int x = 0; x < input.width; ++x) {
            double cosine = 0.0;
            double sine = 0.0;
            for (int offset = -radius; offset <= radius; ++offset) {
                const std::size_t index = static_cast<std::size_t>(offset + radius);
                const double sample = sampleClamped(input, x + offset, y) * gaussian[index];
                cosine += sample * xCos[index];
                sine += sample * xSin[index];
            }
            horizontalCos.values[matrixIndex(x, y, input.width)] = static_cast<float>(cosine);
            horizontalSin.values[matrixIndex(x, y, input.width)] = static_cast<float>(sine);
        }
    }
    for (int y = 0; y < input.height; ++y) {
        for (int x = 0; x < input.width; ++x) {
            double value = 0.0;
            for (int offset = -radius; offset <= radius; ++offset) {
                const std::size_t index = static_cast<std::size_t>(offset + radius);
                const double envelope = gaussian[index];
                value += envelope *
                         (sampleClamped(horizontalCos, x, y + offset) * yCos[index] -
                          sampleClamped(horizontalSin, x, y + offset) * ySin[index]);
            }
            output.values[matrixIndex(x, y, input.width)] = static_cast<float>(value);
        }
    }
    updateRange(output);
    return output;
}
}

namespace MatrixAnalysis {

DenseMatrix makeDense(const std::vector<contactRecord>& records, int resolution,
                      qint64 x0, qint64 x1, qint64 y0, qint64 y1,
                      bool reflectIntra, int maximumBins) {
    DenseMatrix matrix;
    matrix.resolution = std::max(1, resolution);
    matrix.x0 = (x0 / matrix.resolution) * matrix.resolution;
    matrix.y0 = (y0 / matrix.resolution) * matrix.resolution;
    const qint64 width = (std::max<qint64>(matrix.x0 + 1, x1) - matrix.x0 +
                          matrix.resolution - 1) / matrix.resolution;
    const qint64 height = (std::max<qint64>(matrix.y0 + 1, y1) - matrix.y0 +
                           matrix.resolution - 1) / matrix.resolution;
    maximumBins = std::clamp(maximumBins, 1, 1024);
    if (width <= 0 || height <= 0 || width > maximumBins || height > maximumBins) {
        matrix.error = QStringLiteral("Region is %1 × %2 bins; the processing limit is %3 × %3.")
                           .arg(width).arg(height).arg(maximumBins);
        return matrix;
    }
    matrix.width = static_cast<int>(width);
    matrix.height = static_cast<int>(height);
    matrix.values.assign(static_cast<std::size_t>(matrix.width) *
                             static_cast<std::size_t>(matrix.height),
                         0.0f);
    matrix.present.assign(matrix.values.size(), false);
    auto assign = [&](qint64 genomeX, qint64 genomeY, float value) {
        const int x = static_cast<int>((genomeX - matrix.x0) / matrix.resolution);
        const int y = static_cast<int>((genomeY - matrix.y0) / matrix.resolution);
        if (x < 0 || x >= matrix.width || y < 0 || y >= matrix.height) return;
        const std::size_t index = matrixIndex(x, y, matrix.width);
        matrix.values[index] = value;
        matrix.present[index] = true;
    };
    for (const contactRecord& record : records) {
        assign(record.binX, record.binY, record.counts);
        if (reflectIntra && record.binX != record.binY)
            assign(record.binY, record.binX, record.counts);
    }
    updateRange(matrix);
    return matrix;
}

std::vector<PolarPixel> bullseye(const std::vector<contactRecord>& records, int resolution,
                                qint64 centerX, qint64 centerY, int radiusBins,
                                bool reflectIntra) {
    resolution = std::max(1, resolution);
    radiusBins = std::clamp(radiusBins, 1, 256);
    centerX = (centerX / resolution) * resolution;
    centerY = (centerY / resolution) * resolution;
    const qint64 startX = centerX - static_cast<qint64>(radiusBins) * resolution;
    const qint64 endX = centerX + static_cast<qint64>(radiusBins + 1) * resolution;
    const qint64 startY = centerY - static_cast<qint64>(radiusBins) * resolution;
    const qint64 endY = centerY + static_cast<qint64>(radiusBins + 1) * resolution;
    const DenseMatrix dense = makeDense(records, resolution, startX, endX, startY, endY,
                                        reflectIntra, radiusBins * 2 + 1);
    std::vector<PolarPixel> pixels;
    if (!dense.valid()) return pixels;
    pixels.reserve(static_cast<std::size_t>((radiusBins * 2 + 1) * (radiusBins * 2 + 1)));
    for (int dy = -radiusBins; dy <= radiusBins; ++dy) {
        for (int dx = -radiusBins; dx <= radiusBins; ++dx) {
            const double radius = std::hypot(static_cast<double>(dx), static_cast<double>(dy));
            if (radius > radiusBins + 0.001) continue;
            const int x = dx + radiusBins;
            const int y = dy + radiusBins;
            const std::size_t index = matrixIndex(x, y, dense.width);
            pixels.push_back({dx, dy, radius, std::atan2(static_cast<double>(dy), static_cast<double>(dx)),
                              dense.values[index], dense.present[index]});
        }
    }
    return pixels;
}

std::vector<float> virtual4C(const std::vector<contactRecord>& records, int resolution,
                            qint64 anchor, qint64 targetStart, qint64 targetEnd,
                            bool reflectIntra, bool anchorOnX) {
    resolution = std::max(1, resolution);
    anchor = (anchor / resolution) * resolution;
    targetStart = (targetStart / resolution) * resolution;
    const int bins = std::max(0, static_cast<int>((targetEnd - targetStart + resolution - 1) / resolution));
    std::vector<float> values(static_cast<std::size_t>(bins), 0.0f);
    auto assign = [&](qint64 anchorCoordinate, qint64 targetCoordinate, float value) {
        if (anchorCoordinate != anchor || targetCoordinate < targetStart || targetCoordinate >= targetEnd) return;
        const int index = static_cast<int>((targetCoordinate - targetStart) / resolution);
        if (index >= 0 && index < bins) values[static_cast<std::size_t>(index)] = value;
    };
    for (const contactRecord& record : records) {
        if (anchorOnX) assign(record.binX, record.binY, record.counts);
        else assign(record.binY, record.binX, record.counts);
        if (reflectIntra) {
            if (anchorOnX) assign(record.binY, record.binX, record.counts);
            else assign(record.binX, record.binY, record.counts);
        }
    }
    return values;
}

std::vector<contactRecord> boundedRotatedRecords(const std::vector<contactRecord>& records,
                                                 int resolution,
                                                 qint64 viewStart, qint64 viewEnd,
                                                 qint64 maxDistance,
                                                 int pixelWidth, int pixelHeight,
                                                 int maxRecords) {
    resolution = std::max(1, resolution);
    viewEnd = std::max(viewStart + 1, viewEnd);
    maxDistance = std::max<qint64>(1, maxDistance);
    pixelWidth = std::max(1, pixelWidth);
    pixelHeight = std::max(1, pixelHeight);
    maxRecords = std::clamp(maxRecords, 1, 1000000);

    const auto isVisible = [=](const contactRecord& record) {
        const qint64 x = record.binX;
        const qint64 y = record.binY;
        const qint64 distance = std::abs(y - x);
        const qint64 midpoint = (x + y) / 2;
        return distance <= maxDistance + resolution &&
               midpoint + resolution >= viewStart && midpoint < viewEnd;
    };

    std::vector<contactRecord> output;
    output.reserve(std::min<std::size_t>(records.size(), static_cast<std::size_t>(maxRecords)));
    bool requiresAggregation = false;
    for (const contactRecord& record : records) {
        if (!isVisible(record)) continue;
        if (output.size() >= static_cast<std::size_t>(maxRecords)) {
            requiresAggregation = true;
            break;
        }
        output.push_back(record);
    }
    if (!requiresAggregation) return output;

    output.clear();
    int columns = pixelWidth;
    int rows = pixelHeight;
    const qint64 requestedBuckets = static_cast<qint64>(columns) * rows;
    if (requestedBuckets > maxRecords) {
        const double aspect = std::max(0.01, pixelWidth / static_cast<double>(pixelHeight));
        columns = std::clamp(static_cast<int>(std::floor(std::sqrt(maxRecords * aspect))),
                             1, std::min(pixelWidth, maxRecords));
        rows = std::clamp(maxRecords / columns, 1, pixelHeight);
    }
    while (static_cast<qint64>(columns) * rows > maxRecords) {
        if (columns >= rows && columns > 1) --columns;
        else if (rows > 1) --rows;
        else break;
    }

    std::unordered_map<quint64, contactRecord> buckets;
    buckets.reserve(static_cast<std::size_t>(columns) * static_cast<std::size_t>(rows));
    const double span = static_cast<double>(viewEnd - viewStart);
    for (const contactRecord& record : records) {
        if (!isVisible(record)) continue;
        const qint64 x = record.binX;
        const qint64 y = record.binY;
        const double midpoint = (static_cast<double>(x) + y) * 0.5;
        const double distance = std::abs(static_cast<double>(y) - x);
        const double normalizedX = std::clamp((midpoint - viewStart) / span, 0.0, 1.0);
        const double normalizedY = std::clamp(distance / static_cast<double>(maxDistance), 0.0, 1.0);
        const int column = std::min(columns - 1, static_cast<int>(normalizedX * columns));
        const int row = std::min(rows - 1, static_cast<int>(normalizedY * rows));
        const quint64 key = (static_cast<quint64>(static_cast<quint32>(row)) << 32U) |
                            static_cast<quint32>(column);
        const auto found = buckets.find(key);
        if (found == buckets.end()) {
            buckets.emplace(key, record);
        } else if (std::abs(record.counts) > std::abs(found->second.counts)) {
            found->second = record;
        }
    }

    output.reserve(buckets.size());
    for (const auto& [key, record] : buckets) {
        Q_UNUSED(key);
        output.push_back(record);
    }
    return output;
}

std::vector<contactRecord> similarity(const std::vector<contactRecord>& records,
                                      SimilarityMetric metric, int resolution,
                                      qint64 contextStart, qint64 contextEnd,
                                      qint64 outputX0, qint64 outputX1,
                                      qint64 outputY0, qint64 outputY1,
                                      int maximumBins) {
    resolution = std::max(1, resolution);
    contextStart = std::max<qint64>(0, (contextStart / resolution) * resolution);
    contextEnd = ((std::max(contextStart + 1, contextEnd) + resolution - 1) / resolution) * resolution;
    const int bins = static_cast<int>((contextEnd - contextStart) / resolution);
    if (bins <= 0 || bins > std::max(1, maximumBins)) return {};

    auto indexFor = [contextStart, resolution](qint64 position) {
        return static_cast<int>((position - contextStart) / resolution);
    };

    // Input .hic blocks are sparse and may contain either triangle. Restore
    // symmetry into sparse rows, then sort/deduplicate in-place to avoid a
    // dense bins² input allocation and hash-table overhead per row.
    using SparseEntry = std::pair<int, double>;
    std::vector<std::vector<SparseEntry>> rows(static_cast<std::size_t>(bins));
    for (const contactRecord& record : records) {
        if (!std::isfinite(record.counts)) continue;
        const int x = indexFor(record.binX);
        const int y = indexFor(record.binY);
        if (x < 0 || x >= bins || y < 0 || y >= bins) continue;
        rows[static_cast<std::size_t>(y)].emplace_back(x, record.counts);
        if (x != y) rows[static_cast<std::size_t>(x)].emplace_back(y, record.counts);
    }

    std::vector<double> sums(static_cast<std::size_t>(bins), 0.0);
    std::vector<double> sumsOfSquares(static_cast<std::size_t>(bins), 0.0);
    for (int row = 0; row < bins; ++row) {
        auto& sparseRow = rows[static_cast<std::size_t>(row)];
        std::sort(sparseRow.begin(), sparseRow.end(),
                  [](const SparseEntry& a, const SparseEntry& b) { return a.first < b.first; });
        std::size_t write = 0;
        for (const SparseEntry& entry : sparseRow) {
            if (write > 0 && sparseRow[write - 1].first == entry.first) {
                sparseRow[write - 1].second = entry.second;
            } else {
                sparseRow[write++] = entry;
            }
        }
        sparseRow.resize(write);
        for (const auto& [column, value] : sparseRow) {
            Q_UNUSED(column);
            sums[static_cast<std::size_t>(row)] += value;
            sumsOfSquares[static_cast<std::size_t>(row)] += value * value;
        }
    }

    const int xStart = std::clamp(indexFor(outputX0), 0, bins);
    const int xEnd = std::clamp(
        static_cast<int>((outputX1 - contextStart + resolution - 1) / resolution), 0, bins);
    const int yStart = std::clamp(indexFor(outputY0), 0, bins);
    const int yEnd = std::clamp(
        static_cast<int>((outputY1 - contextStart + resolution - 1) / resolution), 0, bins);
    if (xEnd <= xStart || yEnd <= yStart) return {};

    // Canonicalize the requested rectangle into unique upper-triangle pairs.
    // The second pass only adds reflected pairs not already covered by the
    // first, avoiding both a hash set and duplicate similarity calculations.
    std::vector<quint64> pairs;
    pairs.reserve(static_cast<std::size_t>(xEnd - xStart) * static_cast<std::size_t>(yEnd - yStart));
    for (int y = yStart; y < yEnd; ++y) {
        for (int x = xStart; x < xEnd; ++x) {
            if (x < y) continue;
            pairs.push_back((static_cast<quint64>(static_cast<quint32>(x)) << 32U) |
                            static_cast<quint32>(y));
        }
    }
    for (int y = yStart; y < yEnd; ++y) {
        for (int x = xStart; x < xEnd; ++x) {
            if (y <= x) continue;
            const bool reflectedAlreadyPresent =
                y >= xStart && y < xEnd && x >= yStart && x < yEnd;
            if (reflectedAlreadyPresent) continue;
            pairs.push_back((static_cast<quint64>(static_cast<quint32>(y)) << 32U) |
                            static_cast<quint32>(x));
        }
    }

    auto sparseDot = [&rows](int a, int b) {
        const auto& left = rows[static_cast<std::size_t>(a)];
        const auto& right = rows[static_cast<std::size_t>(b)];
        std::size_t i = 0;
        std::size_t j = 0;
        double dot = 0.0;
        while (i < left.size() && j < right.size()) {
            if (left[i].first < right[j].first) {
                ++i;
            } else if (right[j].first < left[i].first) {
                ++j;
            } else {
                dot += left[i].second * right[j].second;
                ++i;
                ++j;
            }
        }
        return dot;
    };

    std::vector<contactRecord> output;
    output.reserve(pairs.size());
    for (const quint64 key : pairs) {
        const int a = static_cast<int>(key >> 32U);
        const int b = static_cast<int>(key & 0xffffffffU);
        const double dot = sparseDot(a, b);
        double value = 0.0;
        if (metric == SimilarityMetric::Pearson) {
            const double count = static_cast<double>(bins);
            const double numerator = dot - sums[static_cast<std::size_t>(a)] *
                                               sums[static_cast<std::size_t>(b)] / count;
            const double varianceA = sumsOfSquares[static_cast<std::size_t>(a)] -
                                     sums[static_cast<std::size_t>(a)] *
                                         sums[static_cast<std::size_t>(a)] / count;
            const double varianceB = sumsOfSquares[static_cast<std::size_t>(b)] -
                                     sums[static_cast<std::size_t>(b)] *
                                         sums[static_cast<std::size_t>(b)] / count;
            const double denominator = std::sqrt(std::max(0.0, varianceA) * std::max(0.0, varianceB));
            if (denominator <= 0.0) continue;
            value = std::clamp(numerator / denominator, -1.0, 1.0);
        } else {
            const double denominator =
                std::sqrt(sumsOfSquares[static_cast<std::size_t>(a)] *
                          sumsOfSquares[static_cast<std::size_t>(b)]);
            if (denominator <= 0.0) continue;
            value = std::clamp(dot / denominator, -1.0, 1.0);
        }
        contactRecord record;
        record.binX = static_cast<int32_t>(contextStart + static_cast<qint64>(a) * resolution);
        record.binY = static_cast<int32_t>(contextStart + static_cast<qint64>(b) * resolution);
        record.counts = static_cast<float>(value);
        output.push_back(record);
    }
    return output;
}

DenseMatrix process(const DenseMatrix& input, const QString& requestedOperation,
                    double parameter, double threshold) {
    if (!input.valid()) return input;
    if (!std::isfinite(parameter) || !std::isfinite(threshold)) {
        DenseMatrix output = input;
        output.error = QStringLiteral("Processing parameters must be finite numbers.");
        return output;
    }
    const QString operation = requestedOperation.trimmed().toLower();
    if (operation == QStringLiteral("identity") || operation == QStringLiteral("input")) return input;
    if (operation == QStringLiteral("gaussian")) return gaussianBlur(input, parameter);
    if (operation == QStringLiteral("dog")) {
        const DenseMatrix a = gaussianBlur(input, std::max(0.25, parameter));
        const DenseMatrix b = gaussianBlur(input, std::max(0.5, parameter * 1.6));
        DenseMatrix output = outputLike(input);
        for (std::size_t i = 0; i < output.values.size(); ++i) output.values[i] = a.values[i] - b.values[i];
        updateRange(output);
        return output;
    }

    DenseMatrix output = outputLike(input);
    if (operation == QStringLiteral("polar")) {
        const double centerX = (input.width - 1) * 0.5;
        const double centerY = (input.height - 1) * 0.5;
        const double maximumRadius = std::max(1.0, std::min(input.width, input.height) * 0.5);
        for (int y = 0; y < input.height; ++y) {
            const double radius = input.height <= 1 ? 0.0
                : static_cast<double>(y) / (input.height - 1) * maximumRadius;
            for (int x = 0; x < input.width; ++x) {
                const double angle = input.width <= 1 ? 0.0
                    : -kPi + static_cast<double>(x) / (input.width - 1) * 2.0 * kPi;
                const int sourceX = static_cast<int>(std::llround(centerX + std::cos(angle) * radius));
                const int sourceY = static_cast<int>(std::llround(centerY + std::sin(angle) * radius));
                output.values[matrixIndex(x, y, input.width)] = sampleClamped(input, sourceX, sourceY);
            }
        }
        updateRange(output);
        return output;
    }
    if (operation == QStringLiteral("matrix-square") || operation == QStringLiteral("graph-diffusion")) {
        if (input.width != input.height) {
            output.error = QStringLiteral("%1 requires a square intrachromosomal region.").arg(requestedOperation);
            return output;
        }
        const int n = input.width;
        const int limit = operation == QStringLiteral("matrix-square") ? 256 : 192;
        if (n > limit) {
            output.error = QStringLiteral("%1 is limited to %2 × %2 bins; zoom in further.")
                               .arg(requestedOperation).arg(limit);
            return output;
        }
        auto multiply = [n](const std::vector<float>& left, const std::vector<float>& right) {
            std::vector<float> product(static_cast<std::size_t>(n * n), 0.0f);
            for (int row = 0; row < n; ++row) {
                for (int inner = 0; inner < n; ++inner) {
                    const float leftValue = left[matrixIndex(inner, row, n)];
                    if (leftValue == 0.0f || !std::isfinite(leftValue)) continue;
                    for (int column = 0; column < n; ++column) {
                        const float rightValue = right[matrixIndex(column, inner, n)];
                        if (rightValue != 0.0f && std::isfinite(rightValue))
                            product[matrixIndex(column, row, n)] += leftValue * rightValue;
                    }
                }
            }
            return product;
        };
        if (operation == QStringLiteral("matrix-square")) {
            output.values = multiply(input.values, input.values);
        } else {
            std::vector<float> transition = input.values;
            for (int row = 0; row < n; ++row) {
                double sum = 0.0;
                for (int column = 0; column < n; ++column)
                    sum += std::max(0.0f, transition[matrixIndex(column, row, n)]);
                if (sum <= 0.0) continue;
                for (int column = 0; column < n; ++column)
                    transition[matrixIndex(column, row, n)] =
                        std::max(0.0f, transition[matrixIndex(column, row, n)]) / static_cast<float>(sum);
            }
            std::vector<float> current = input.values;
            const int iterations = std::clamp(static_cast<int>(std::llround(parameter)), 1, 12);
            const float alpha = static_cast<float>(std::clamp(threshold == 0.0 ? 0.25 : threshold, 0.01, 1.0));
            for (int iteration = 0; iteration < iterations; ++iteration) {
                std::vector<float> propagated = multiply(transition, current);
                for (std::size_t index = 0; index < current.size(); ++index)
                    current[index] = (1.0f - alpha) * current[index] + alpha * propagated[index];
            }
            output.values = std::move(current);
        }
        updateRange(output);
        return output;
    }
    if (operation == QStringLiteral("gradient-x") || operation == QStringLiteral("gradient-y") ||
        operation == QStringLiteral("gradient-magnitude") || operation == QStringLiteral("gradient-orientation") ||
        operation == QStringLiteral("laplacian") || operation == QStringLiteral("log") ||
        operation == QStringLiteral("hessian-determinant") || operation == QStringLiteral("hessian-ridge") ||
        operation == QStringLiteral("structure-anisotropy") || operation == QStringLiteral("structure-orientation") ||
        operation == QStringLiteral("steerable") || operation == QStringLiteral("gabor")) {
        if (operation == QStringLiteral("gabor")) {
            if (parameter > 12.0) {
                output.error = QStringLiteral("Gabor sigma is limited to 12 bins. Choose a smaller parameter.");
                return output;
            }
            return gaborFilter(input, std::max(0.5, parameter), threshold * kPi / 180.0);
        }
        const DenseMatrix source = operation == QStringLiteral("log")
            ? gaussianBlur(input, std::max(0.25, parameter)) : input;
        const double angle = threshold * kPi / 180.0;
        for (int y = 0; y < input.height; ++y) {
            for (int x = 0; x < input.width; ++x) {
                const float gx = 0.5f * (sampleClamped(source, x + 1, y) - sampleClamped(source, x - 1, y));
                const float gy = 0.5f * (sampleClamped(source, x, y + 1) - sampleClamped(source, x, y - 1));
                const float gxx = sampleClamped(source, x + 1, y) - 2.0f * sampleClamped(source, x, y) +
                                  sampleClamped(source, x - 1, y);
                const float gyy = sampleClamped(source, x, y + 1) - 2.0f * sampleClamped(source, x, y) +
                                  sampleClamped(source, x, y - 1);
                const float gxy = 0.25f * (sampleClamped(source, x + 1, y + 1) -
                                           sampleClamped(source, x + 1, y - 1) -
                                           sampleClamped(source, x - 1, y + 1) +
                                           sampleClamped(source, x - 1, y - 1));
                float value = 0.0f;
                if (operation == QStringLiteral("gradient-x")) value = gx;
                else if (operation == QStringLiteral("gradient-y")) value = gy;
                else if (operation == QStringLiteral("gradient-magnitude")) value = std::hypot(gx, gy);
                else if (operation == QStringLiteral("gradient-orientation")) value = std::atan2(gy, gx);
                else if (operation == QStringLiteral("laplacian") || operation == QStringLiteral("log")) value = gxx + gyy;
                else if (operation == QStringLiteral("hessian-determinant")) value = gxx * gyy - gxy * gxy;
                else if (operation == QStringLiteral("hessian-ridge")) {
                    const float trace = gxx + gyy;
                    const float disc = std::sqrt(std::max(0.0f, (gxx - gyy) * (gxx - gyy) + 4.0f * gxy * gxy));
                    value = 0.5f * (trace - disc);
                } else if (operation == QStringLiteral("structure-anisotropy")) {
                    const float a = gx * gx;
                    const float c = gy * gy;
                    const float b = gx * gy;
                    const float disc = std::sqrt(std::max(0.0f, (a - c) * (a - c) + 4.0f * b * b));
                    value = disc / std::max(1e-6f, a + c);
                } else if (operation == QStringLiteral("structure-orientation")) {
                    value = 0.5f * std::atan2(2.0f * gx * gy, gx * gx - gy * gy);
                } else if (operation == QStringLiteral("steerable")) {
                    value = static_cast<float>(gx * std::cos(angle) + gy * std::sin(angle));
                }
                output.values[matrixIndex(x, y, input.width)] = value;
            }
        }
        updateRange(output);
        return output;
    }

    if (operation == QStringLiteral("dilation") || operation == QStringLiteral("erosion"))
        return morphology(input, std::max(1, static_cast<int>(std::round(parameter))),
                          operation == QStringLiteral("dilation"));
    if (operation == QStringLiteral("opening") || operation == QStringLiteral("closing")) {
        const int radius = std::max(1, static_cast<int>(std::round(parameter)));
        const bool opening = operation == QStringLiteral("opening");
        return morphology(morphology(input, radius, !opening), radius, opening);
    }
    if (operation == QStringLiteral("distance-transform")) {
        const float cutoff = static_cast<float>(threshold);
        const int width = input.width;
        const int height = input.height;
        const float inf = static_cast<float>(width + height + 1);
        for (std::size_t i = 0; i < output.values.size(); ++i)
            output.values[i] = input.values[i] > cutoff ? 0.0f : inf;
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            float& value = output.values[matrixIndex(x, y, width)];
            if (x > 0) value = std::min(value, output.values[matrixIndex(x - 1, y, width)] + 1.0f);
            if (y > 0) value = std::min(value, output.values[matrixIndex(x, y - 1, width)] + 1.0f);
        }
        for (int y = height - 1; y >= 0; --y) for (int x = width - 1; x >= 0; --x) {
            float& value = output.values[matrixIndex(x, y, width)];
            if (x + 1 < width) value = std::min(value, output.values[matrixIndex(x + 1, y, width)] + 1.0f);
            if (y + 1 < height) value = std::min(value, output.values[matrixIndex(x, y + 1, width)] + 1.0f);
        }
        updateRange(output);
        return output;
    }
    if (operation == QStringLiteral("lbp")) {
        for (int y = 0; y < input.height; ++y) for (int x = 0; x < input.width; ++x) {
            const float center = sampleClamped(input, x, y);
            unsigned code = 0;
            const int offsets[8][2] = {{-1,-1},{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0}};
            for (int bit = 0; bit < 8; ++bit)
                if (sampleClamped(input, x + offsets[bit][0], y + offsets[bit][1]) >= center) code |= (1U << bit);
            output.values[matrixIndex(x, y, input.width)] = static_cast<float>(code);
        }
        updateRange(output);
        return output;
    }

    output.error = QStringLiteral("Unsupported processing operator: %1").arg(requestedOperation);
    return output;
}

QStringList supportedOperations() {
    return {
        QStringLiteral("Gaussian smoothing"),
        QStringLiteral("Gradient X"),
        QStringLiteral("Gradient Y"),
        QStringLiteral("Gradient magnitude"),
        QStringLiteral("Gradient orientation"),
        QStringLiteral("Laplacian"),
        QStringLiteral("Hessian determinant"),
        QStringLiteral("Hessian ridge"),
        QStringLiteral("Structure anisotropy"),
        QStringLiteral("Structure orientation"),
        QStringLiteral("Difference of Gaussians"),
        QStringLiteral("Laplacian of Gaussian"),
        QStringLiteral("Distance transform"),
        QStringLiteral("Erosion"),
        QStringLiteral("Dilation"),
        QStringLiteral("Opening"),
        QStringLiteral("Closing"),
        QStringLiteral("Steerable filter"),
        QStringLiteral("Gabor filter"),
        QStringLiteral("Local Binary Pattern"),
        QStringLiteral("Polar transform"),
        QStringLiteral("Matrix square"),
        QStringLiteral("Graph diffusion")
    };
}

} // namespace MatrixAnalysis
