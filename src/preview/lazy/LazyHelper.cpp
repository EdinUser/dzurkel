#include "LazyCoordinator.h"
#include "TextPortionReader.h"
#include "CsvPortionReader.h"
#include "preview/readers/XlsxReader.h"
#include "preview/readers/DocumentReader.h"
#include <QImageReader>
#include "preview/PreviewRecordWriter.h"
#include <QFile>
#include <QJsonDocument>
#include <cstdio>

namespace dzurkel {
int serveLazy(const QString &path, const QString &reader)
{
    const bool csv = reader == QStringLiteral("csv") || reader == QStringLiteral("csv-tsv");
    const bool xlsx = reader == QStringLiteral("xlsx");
    const bool document = DocumentReader::supportsPortions(reader);
    QImageReader::setAllocationLimit(64);
    if (reader != QStringLiteral("text") && !csv && !xlsx && !document) return 2;
    std::unique_ptr<RichDocumentPortionReader> book;
    std::unique_ptr<XlsxPortionReader> workbook;
    std::unique_ptr<TextPortionReader> text;
    std::unique_ptr<CsvPortionReader> table;
    if (document) book = std::make_unique<RichDocumentPortionReader>(path, reader);
    else if (xlsx) workbook = std::make_unique<XlsxPortionReader>(path);
    else if (csv) table = std::make_unique<CsvPortionReader>(path, reader == QStringLiteral("csv-tsv"));
    else text = std::make_unique<TextPortionReader>(path);
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly, QFileDevice::DontCloseHandle)) return 2;
    while (true) {
        const auto line = input.readLine(32770);
        if (line.isEmpty()) return 0;
        if (line.size() > 32768 || !line.endsWith('\n')) return 2;
        const auto request = QJsonDocument::fromJson(line).object();
        if (!request.value("id").isDouble() || !request.value("token").isString() || request.value("token").toString().size() > 4096) return 2;
        auto response = document ? book->read(request.value("token").toString()) : xlsx ? workbook->read(request.value("token").toString()) : csv ? table->read(request.value("token").toString()) : text->read(request.value("token").toString());
        response.insert(QStringLiteral("id"), request.value("id"));
        response.insert(QStringLiteral("token"), request.value("token"));
        // Each request has its own transfer budget; there is no lifetime cutoff.
        PreviewRecordWriter writer(2 * LazyCoordinator::TablePayloadLimit);
        if (!writer.sendChunked(response, (csv || xlsx || document) ? LazyCoordinator::TablePayloadLimit : LazyCoordinator::RecordLimit)) return 2;
    }
}
}
