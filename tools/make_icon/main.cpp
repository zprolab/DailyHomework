// Builds the multi-resolution Windows application icon from the shared
// dhw::icons vector glyphs. The ICO container is written by hand: Windows
// Vista and later accept PNG-compressed entries, which keeps the file small.

#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QString>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

#include "dhw/draw_cmd.h"
#include "dhw/icons.h"
#include "dhw/qt/renderer.h"

namespace {

constexpr dhw::Color kBrandBlue = dhw::make_color(0x2E, 0x5F, 0xA3);
constexpr dhw::Color kWhite = dhw::make_color(0xFF, 0xFF, 0xFF);
// Largest frame first: this is the order Windows and common icon inspectors
// expect to see the primary 256 px artwork in.
constexpr std::array<int, 7> kSizes = {256, 128, 64, 48, 32, 24, 16};

struct Options {
    QString out_file;
    QString png_file;
};

void usage() {
    std::printf("dhw_make_icon --out <file.ico> [--png <file.png>]\n");
}

bool parse_options(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const QString flag = QString::fromLocal8Bit(argv[i]);
        const auto value = [&](QString& target) {
            if (i + 1 >= argc) {
                return false;
            }
            target = QString::fromLocal8Bit(argv[++i]);
            return true;
        };
        if (flag == "--out") {
            if (!value(options.out_file)) {
                return false;
            }
        } else if (flag == "--png") {
            if (!value(options.png_file)) {
                return false;
            }
        } else if (flag == "--help" || flag == "-h") {
            usage();
            std::exit(0);
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return false;
        }
    }
    return !options.out_file.isEmpty();
}

// One frame of the icon: a brand blue rounded square with the white book glyph
// from the shared icon set. Drawn at the target size so small sizes stay crisp.
QImage render_icon(int size, const dhw::qt::QtTextMetrics& metrics) {
    const double edge = static_cast<double>(size);
    const double radius = std::max(2.0, edge * 0.22);
    std::vector<dhw::DrawCmd> commands;
    commands.push_back(dhw::DrawCmd::rect(0.0, 0.0, edge, edge, radius, kBrandBlue));

    const double glyph = edge * 0.62;
    const double offset = (edge - glyph) / 2.0;
    const std::vector<dhw::DrawCmd> book =
        dhw::icons::draw(dhw::icons::Icon::Book, offset, offset, glyph, kWhite);
    commands.insert(commands.end(), book.begin(), book.end());

    return dhw::qt::render_commands(commands, size, size, {}, metrics, 1.0);
}

bool encode_png(const QImage& image, std::vector<unsigned char>& out) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly)) {
        return false;
    }
    const bool ok = image.save(&buffer, "PNG");
    buffer.close();
    if (!ok) {
        return false;
    }
    const auto* begin = reinterpret_cast<const unsigned char*>(bytes.constData());
    out.assign(begin, begin + bytes.size());
    return true;
}

void put_u16(std::vector<unsigned char>& out, std::uint16_t value) {
    out.push_back(static_cast<unsigned char>(value & 0xFFu));
    out.push_back(static_cast<unsigned char>((value >> 8) & 0xFFu));
}

void put_u32(std::vector<unsigned char>& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<unsigned char>((value >> shift) & 0xFFu));
    }
}

struct Frame {
    int size = 0;
    std::vector<unsigned char> png;
};

// ICONDIR + one ICONDIRENTRY per frame + the PNG payloads. All integers are
// little endian; a width or height byte of 0 encodes 256.
std::vector<unsigned char> build_ico(const std::vector<Frame>& frames) {
    std::vector<unsigned char> out;
    out.reserve(6 + frames.size() * 16 + 512 * 1024);

    put_u16(out, 0);  // reserved
    put_u16(out, 1);  // type: icon
    put_u16(out, static_cast<std::uint16_t>(frames.size()));

    std::uint32_t offset = static_cast<std::uint32_t>(6 + frames.size() * 16);
    for (const Frame& frame : frames) {
        const unsigned char dimension =
            frame.size >= 256 ? 0 : static_cast<unsigned char>(frame.size);
        out.push_back(dimension);
        out.push_back(dimension);
        out.push_back(0);  // palette colour count
        out.push_back(0);  // reserved
        put_u16(out, 1);   // colour planes
        put_u16(out, 32);  // bits per pixel
        put_u32(out, static_cast<std::uint32_t>(frame.png.size()));
        put_u32(out, offset);
        offset += static_cast<std::uint32_t>(frame.png.size());
    }

    for (const Frame& frame : frames) {
        out.insert(out.end(), frame.png.begin(), frame.png.end());
    }
    return out;
}

bool write_bytes(const QString& path, const std::vector<unsigned char>& bytes) {
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    const QByteArray data(reinterpret_cast<const char*>(bytes.data()),
                          static_cast<qsizetype>(bytes.size()));
    return file.write(data) == data.size();
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse_options(argc, argv, options)) {
        usage();
        return 2;
    }

    // This tool never shows a window; default to the offscreen platform so it
    // also works over SSH and in CI without a display.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    }
    QGuiApplication app(argc, argv);
    Q_UNUSED(app);

    const dhw::qt::QtTextMetrics metrics;
    std::vector<Frame> frames;
    frames.reserve(kSizes.size());
    for (const int size : kSizes) {
        Frame frame;
        frame.size = size;
        if (!encode_png(render_icon(size, metrics), frame.png)) {
            std::fprintf(stderr, "cannot encode %dx%d PNG\n", size, size);
            return 1;
        }
        frames.push_back(std::move(frame));
    }

    if (!options.png_file.isEmpty()) {
        const QImage large = render_icon(256, metrics);
        const QFileInfo info(options.png_file);
        if (!QDir().mkpath(info.absolutePath()) ||
            !large.save(options.png_file, "PNG")) {
            std::fprintf(stderr, "cannot write %s\n",
                         options.png_file.toLocal8Bit().constData());
            return 1;
        }
        std::printf("png=%s (%dx%d)\n", options.png_file.toLocal8Bit().constData(),
                    large.width(), large.height());
    }

    const std::vector<unsigned char> ico = build_ico(frames);
    if (!write_bytes(options.out_file, ico)) {
        std::fprintf(stderr, "cannot write %s\n",
                     options.out_file.toLocal8Bit().constData());
        return 1;
    }

    std::printf("ico=%s entries=%zu bytes=%zu\n",
                options.out_file.toLocal8Bit().constData(), frames.size(), ico.size());
    return 0;
}
