#include <QBuffer>
#include <QCoreApplication>
#include <QDataStream>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

#include <array>

namespace {

QImage renderSvg(QSvgRenderer& renderer, int size) {
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    // Native macOS icon resources use 72 DPI (2,835 dots per meter).
    image.setDotsPerMeterX(2835);
    image.setDotsPerMeterY(2835);

    QPainter painter(&image);
    renderer.render(&painter, image.rect());
    painter.end();
    return image;
}

QByteArray renderPng(QSvgRenderer& renderer, int size) {
    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly) || !renderSvg(renderer, size).save(&buffer, "PNG")) {
        return {};
    }
    return png;
}

bool writeIcns(QSvgRenderer& renderer, const QString& path) {
    struct MacIcon {
        const char* type;
        int size;
    };
    // Include both the base and Retina chunk identifiers understood by Finder.
    constexpr std::array<MacIcon, 11> icons{{
        {"icp4", 16}, {"icp5", 32}, {"icp6", 64}, {"ic07", 128},
        {"ic08", 256}, {"ic09", 512}, {"ic10", 1024}, {"ic11", 32},
        {"ic12", 64}, {"ic13", 256}, {"ic14", 512},
    }};
    std::array<QByteArray, icons.size()> pngImages;
    quint32 totalSize = 8;
    for (qsizetype index = 0; index < static_cast<qsizetype>(icons.size()); ++index) {
        const auto& icon = icons[static_cast<std::size_t>(index)];
        auto& png = pngImages[static_cast<std::size_t>(index)];
        png = renderPng(renderer, icon.size);
        if (png.isEmpty()) {
            qCritical("Unable to render the %dx%d macOS icon", icon.size, icon.size);
            return false;
        }
        totalSize += 8 + static_cast<quint32>(png.size());
    }

    QFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        qCritical("Unable to write ICNS: %s", qPrintable(path));
        return false;
    }
    QDataStream stream(&output);
    stream.setByteOrder(QDataStream::BigEndian);
    output.write("icns", 4);
    stream << totalSize;
    for (qsizetype index = 0; index < static_cast<qsizetype>(icons.size()); ++index) {
        const auto& icon = icons[static_cast<std::size_t>(index)];
        const auto& png = pngImages[static_cast<std::size_t>(index)];
        output.write(icon.type, 4);
        stream << quint32(8 + png.size());
        if (output.write(png) != png.size()) {
            qCritical("Unable to finish writing ICNS: %s", qPrintable(path));
            return false;
        }
    }
    return true;
}

bool writeIco(QSvgRenderer& renderer, const QString& path) {
    constexpr std::array<int, 7> sizes{{16, 24, 32, 48, 64, 128, 256}};
    std::array<QByteArray, sizes.size()> pngImages;

    for (qsizetype index = 0; index < static_cast<qsizetype>(sizes.size()); ++index) {
        pngImages[static_cast<std::size_t>(index)] =
            renderPng(renderer, sizes[static_cast<std::size_t>(index)]);
        if (pngImages[static_cast<std::size_t>(index)].isEmpty()) {
            qCritical("Unable to render the %dx%d Windows icon", sizes[static_cast<std::size_t>(index)],
                      sizes[static_cast<std::size_t>(index)]);
            return false;
        }
    }

    QFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        qCritical("Unable to write ICO: %s", qPrintable(path));
        return false;
    }

    QDataStream stream(&output);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint16(0) << quint16(1) << quint16(sizes.size());

    constexpr quint32 directorySize = 6 + 16 * sizes.size();
    quint32 imageOffset = directorySize;
    for (qsizetype index = 0; index < static_cast<qsizetype>(sizes.size()); ++index) {
        const auto size = sizes[static_cast<std::size_t>(index)];
        const auto& png = pngImages[static_cast<std::size_t>(index)];
        stream << quint8(size == 256 ? 0 : size) << quint8(size == 256 ? 0 : size)
               << quint8(0) << quint8(0) << quint16(1) << quint16(32)
               << quint32(png.size()) << imageOffset;
        imageOffset += static_cast<quint32>(png.size());
    }
    for (const auto& png : pngImages) {
        if (output.write(png) != png.size()) {
            qCritical("Unable to finish writing ICO: %s", qPrintable(path));
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    const auto arguments = QCoreApplication::arguments();
    if (arguments.size() != 4 || arguments.at(1) != QStringLiteral("--icns")
        && arguments.at(1) != QStringLiteral("--ico")) {
        qCritical("Usage: carton_icon_renderer (--icns <file> | --ico <file>) <logo.svg>");
        return 2;
    }

    QSvgRenderer renderer(arguments.at(3));
    if (!renderer.isValid()) {
        qCritical("Unable to load SVG: %s", qPrintable(arguments.at(3)));
        return 1;
    }

    const bool success = arguments.at(1) == QStringLiteral("--icns")
        ? writeIcns(renderer, arguments.at(2))
        : writeIco(renderer, arguments.at(2));
    return success ? 0 : 1;
}
