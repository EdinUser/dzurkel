#include "PdfProtocol.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QPainter>
#include <QPdfDocument>
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
#include <QPdfLinkModel>
#endif
#include <QSocketNotifier>
#include <algorithm>
#include <cmath>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace dzurkel {
namespace {
bool writeBytes(const char *data, qint64 size)
{
    while (size > 0) {
        const auto count = ::write(STDOUT_FILENO, data, size_t(size));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        data += count;
        size -= count;
    }
    return true;
}
bool reply(QJsonObject header, const QImage &image = {})
{
    header["bytes"] = double(image.sizeInBytes());
    const auto json = QJsonDocument(header).toJson(QJsonDocument::Compact) + '\n';
    return writeBytes(json.constData(), json.size())
        && writeBytes(reinterpret_cast<const char *>(image.constBits()), image.sizeInBytes());
}
}
int servePdf(const QString &path)
{
    // All potentially blocking file access and PDFium calls stay in this
    // process. Do not reopen the original URL or implement another VFS here.
    QFile file;
    const auto encoded = QFile::encodeName(path);
    const int fd = ::open(encoded.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    struct stat info{};
    if (fd < 0) return 2;
    if (::fstat(fd, &info) || !S_ISREG(info.st_mode)) {
        ::close(fd);
        return 2;
    }
    if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        ::close(fd);
        return 2;
    }
    QPdfDocument document;
    bool opened = false;
    bool password = false;
    QObject::connect(&document, &QPdfDocument::passwordRequired, &document, [&] { password = true; });
    QByteArray input;
    QSocketNotifier notifier(STDIN_FILENO, QSocketNotifier::Read);
    QObject::connect(&notifier, &QSocketNotifier::activated, &document, [&] {
        char buffer[4096];
        const auto count = ::read(STDIN_FILENO, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) return;
        if (count <= 0) { QCoreApplication::quit(); return; }
        input.append(buffer, count);
        if (input.size() > 4096) { QCoreApplication::exit(2); return; }
        while (input.contains('\n')) {
            const auto end = input.indexOf('\n');
            const auto request = QJsonDocument::fromJson(input.left(end)).object();
            input.remove(0, end + 1);
            const auto id = request.value("id").toInteger();
            const int page = request.value("page").toInt(-1);
            auto error = [&](const QString &message) {
                if (!reply({{"id", id}, {"error", message}})) QCoreApplication::exit(2);
            };
            if (!opened) {
                document.load(&file);
                opened = document.status() == QPdfDocument::Status::Ready;
            }
            if (!opened) {
                error(password ? QStringLiteral("Password-protected PDF cannot be previewed.")
                               : QStringLiteral("PDF could not be opened."));
                continue;
            }
            if (document.pageCount() < 1) {
                error(QStringLiteral("PDF has no pages."));
                continue;
            }
            if (page < 0 || page >= document.pageCount()) { error(QStringLiteral("Invalid PDF page.")); continue; }
            const auto points = document.pagePointSize(page);
            if (!std::isfinite(points.width()) || !std::isfinite(points.height()) || points.isEmpty()
                || points.width() > 10000000 || points.height() > 10000000) {
                error(QStringLiteral("Invalid PDF page dimensions."));
                continue;
            }
            const auto requested = request.value("scale").toDouble();
            const auto width = request.value("width").toInt(800);
            if (!std::isfinite(requested) || requested < 0 || requested > 128 || width < 1 || width > PdfMaxEdge) {
                error(QStringLiteral("Invalid PDF render request."));
                continue;
            }
            const auto factor = std::min({requested > 0 ? requested : width / points.width(),
                PdfMaxEdge / points.width(), PdfMaxEdge / points.height(),
                std::sqrt(double(PdfMaxPixels) / (points.width() * points.height()))});
            const QSize pixels(std::max(1, int(std::floor(points.width() * factor))),
                               std::max(1, int(std::floor(points.height() * factor))));
            const auto rendered = document.render(page, pixels);
            if (rendered.isNull()) { error(QStringLiteral("PDF page could not be rendered.")); continue; }
            QImage image(pixels, QImage::Format_RGBA8888);
            if (image.isNull()) { error(QStringLiteral("PDF page allocation failed.")); continue; }
            image.fill(Qt::white);
            { QPainter painter(&image); painter.drawImage(0, 0, rendered); }
            QJsonArray links;
            // Qt 6.4/6.5 can render pages but have no public link model.
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
            QPdfLinkModel model;
            model.setDocument(&document);
            model.setPage(page);
            int linkBytes = 0;
            for (int row = 0; row < std::min(256, model.rowCount({})); ++row) {
                const auto link = model.data(model.index(row), int(QPdfLinkModel::Role::Link)).value<QPdfLink>();
                if (!link.isValid() || link.url().toString().size() > 2048) continue;
                for (const auto &rectangle : link.rectangles()) {
                    if (links.size() >= 256) break;
                    const auto location = link.location();
                    QJsonObject entry{{"x", rectangle.x()}, {"y", rectangle.y()},
                        {"w", rectangle.width()}, {"h", rectangle.height()},
                        {"url", link.url().toString()}, {"page", link.page()},
                        {"toX", location.x()}, {"toY", location.y()}};
                    const int bytes = QJsonDocument(entry).toJson(QJsonDocument::Compact).size() + 1;
                    if (linkBytes + bytes > PdfMaxHeader - 1024) break;
                    linkBytes += bytes;
                    links.append(entry);
                }
            }
#endif
            if (!reply({{"id", id}, {"page", page}, {"pages", document.pageCount()}, {"links", links},
                        {"width", pixels.width()}, {"height", pixels.height()},
                        {"pointWidth", points.width()}, {"pointHeight", points.height()}}, image)) {
                QCoreApplication::exit(2);
                return;
            }
        }
    });
    return QCoreApplication::exec();
}
}
