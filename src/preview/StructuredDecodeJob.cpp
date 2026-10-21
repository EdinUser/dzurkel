#include "StructuredDecodeJob.h"
#include "preview/readers/XlsxReader.h"
#include "preview/readers/DocumentReader.h"
#include "preview/document/FormattedDocument.h"
#include "preview/document/DocumentRecords.h"
#include <QColorSpace>
#include <QJsonArray>
#include <cmath>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <algorithm>

namespace dzurkel {
namespace {
QSet<QProcess *> &helpers()
{
    static QSet<QProcess *> processes;
    return processes;
}
void initializeReaper()
{
    static bool initialized = false;
    if (initialized) return;
    initialized = true;
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp, [] {
        for (auto *process : helpers()) {
            if (process->state() == QProcess::NotRunning) continue;
            if (process->processId() > 0) ::kill(-pid_t(process->processId()), SIGKILL);
            process->kill();
            process->setParent(nullptr); // No waiting QProcess destructor at exit.
            process->disconnect();
        }
    });
}
}
int StructuredDecodeJob::runningHelpers() { return helpers().size(); }
StructuredDecodeJob::StructuredDecodeJob(const PreviewData &data, QObject *parent, const QString &program, int timeoutMs)
    : QObject(parent), result_(data)
{
    initializeReaper();
    documentMode_ = !data.readerId.isEmpty() && data.readerId != QStringLiteral("xlsx");
    result_.kind = PreviewKind::Information;
    timeout_.setSingleShot(true);
    timeout_.setInterval(qMax(1, timeoutMs));
    connect(&timeout_, &QTimer::timeout, this, [this] { finish(QStringLiteral("Document/table preview loading timed out.")); });
    if (helpers().size() >= 2) {
        QTimer::singleShot(0, this, [this] { finish(QStringLiteral("Previous Document/table preview is still stopping. Try again shortly.")); });
        return;
    }
    auto *process = new QProcess(qApp);
    process_ = process;
    helpers().insert(process);
    process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    process->setChildProcessModifier([] { if (::setpgid(0, 0) != 0) ::_exit(127); });
    auto group = std::make_shared<qint64>(0);
    connect(process, &QProcess::started, process, [process, group] { *group = process->processId(); });
    connect(process, &QProcess::finished, process, [process, group, store = data.resource.backingStore] {
        Q_UNUSED(store);
        if (*group > 0) ::kill(-pid_t(*group), SIGKILL);
        helpers().remove(process);
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, process, [process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) { helpers().remove(process); process->deleteLater(); }
    });
    connect(process, &QProcess::readyReadStandardOutput, this, &StructuredDecodeJob::read);
    connect(process, &QProcess::finished, this, [this] { finish(QStringLiteral("Document/table preview stopped before loading completed.")); });
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finish(QStringLiteral("Cannot start the Document/table preview helper."));
    });
    const auto executable = program.isEmpty() ? QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("dzurkel")) : program;
    QStringList arguments{documentMode_ ? QStringLiteral("--internal-document") : QStringLiteral("--internal-xlsx"),
        data.resource.backingPath.isEmpty() ? data.resource.path : data.resource.backingPath};
    if (documentMode_) arguments.append(data.readerId);
    process->start(executable, arguments);
    timeout_.start();
}
StructuredDecodeJob::~StructuredDecodeJob() { stop(); }
void StructuredDecodeJob::stop()
{
    stopped_ = true;
    timeout_.stop();
    document_.reset();
    formatted_.reset();
    pendingImage_ = {};
    pendingProfile_.clear();
    line_.clear();
    if (process_) {
        disconnect(process_, nullptr, this, nullptr);
        if (process_->state() != QProcess::NotRunning) {
            if (process_->processId() > 0) ::kill(-pid_t(process_->processId()), SIGKILL);
            process_->kill();
        }
        process_ = nullptr;
    }
}
void StructuredDecodeJob::finish(const QString &error)
{
    if (stopped_) return;
    if (!error.isEmpty()) {
        result_.kind = PreviewKind::Information;
        result_.table.reset();
        result_.document.reset();
        result_.text.clear();
        result_.message = error;
    } else if (process_) {
        disconnect(process_, nullptr, this, nullptr);
        process_->closeWriteChannel();
        QTimer::singleShot(1000, process_, [process = process_] {
            if (process && process->state() != QProcess::NotRunning) {
                if (process->processId() > 0) ::kill(-pid_t(process->processId()), SIGKILL);
                process->kill();
            }
        });
        process_ = nullptr;
    }
    stop();
    emit completed(result_);
}
bool StructuredDecodeJob::record()
{
    const auto object = QJsonDocument::fromJson(line_).object();
    line_.clear();
    if (documentMode_) return documentRecord(object);
    const auto event = object.value("event").toString();
    const auto invalid = [this] { finish(QStringLiteral("Invalid Document/table preview response.")); return false; };
    if (!document_) {
        if (event == QStringLiteral("error")) {
            const auto message = object.value("message").toString();
            finish(message.isEmpty() ? QStringLiteral("Document/table preview loading failed.") : message.left(4096));
            return false;
        }
        if (event != QStringLiteral("begin") || !object.value("partial").isBool()
            || !object.value("notice").isString() || object.value("notice").toString().size() > 4096) return invalid();
        document_ = std::make_shared<TabularDocument>();
        document_->partial = object.value("partial").toBool();
        document_->notice = object.value("notice").toString();
        return true;
    }
    if (event == QStringLiteral("sheet")) {
        const auto name = object.value("name").toString();
        const auto rows = object.value("rows").toInteger(-1), columns = object.value("columns").toInteger(-1);
        if (document_->sheets.size() >= XlsxReader::SheetLimit || name.isEmpty() || name.size() > 255
            || rows < 0 || rows > XlsxReader::RowLimit || columns < 0 || columns > XlsxReader::ColumnLimit) return invalid();
        document_->sheets.append({name, int(rows), int(columns), {}});
        return true;
    }
    if (event == QStringLiteral("cell")) {
        if (document_->sheets.isEmpty() || ++cells_ > XlsxReader::CellLimit) return invalid();
        auto &sheet = document_->sheets.last();
        const auto row = object.value("row").toInteger(-1), column = object.value("column").toInteger(-1);
        const auto value = object.value("value");
        if (row < 0 || row >= sheet.rowCount || column < 0 || column >= sheet.columnCount || !value.isString()) return invalid();
        const auto text = value.toString();
        if (text.size() > XlsxReader::FieldLimit || text.size() > XlsxReader::CharacterLimit - characters_) return invalid();
        characters_ += int(text.size());
        const auto key = TabularSheet::key(int(row), int(column));
        if (sheet.cells.contains(key)) return invalid();
        sheet.cells.insert(key, text);
        return true;
    }
    if (event != QStringLiteral("end") || document_->sheets.isEmpty() || !object.value("detail").isString()
        || object.value("detail").toString().size() > 4096) return invalid();
    result_.kind = PreviewKind::Table;
    result_.table = document_;
    result_.truncated = document_->partial;
    result_.message = document_->notice;
    result_.detail = object.value("detail").toString();
    finish();
    return false;
}
bool StructuredDecodeJob::documentRecord(const QJsonObject &object)
{
    const auto invalid = [this] { finish(QStringLiteral("Invalid document preview response.")); return false; };
    const auto event = object.value("event").toString();
    if (!documentStarted_) {
        if (event == QStringLiteral("error")) {
            const auto library = object.value("missingLibrary").toString();
            if (!library.isEmpty() && (library != QStringLiteral("antiword") || result_.readerId != QStringLiteral("doc"))) return invalid();
            result_.missingLibrary = library;
            const auto message = object.value("message").toString().left(4096);
            finish(message.isEmpty() ? QStringLiteral("Document preview loading failed.") : message);
            return false;
        }
        if (event != QStringLiteral("document") || !object.value("partial").isBool() || !object.value("text").isBool()) return invalid();
        textFallback_ = object.value("text").toBool();
        if (textFallback_ && result_.readerId != QStringLiteral("markdown")) return invalid();
        formatted_ = std::make_shared<FormattedDocument>();
        result_.truncated = formatted_->partial = object.value("partial").toBool();
        documentStarted_ = true;
        return true;
    }
    if (event == QStringLiteral("field")) {
        if (!object.value("value").isString()) return invalid();
        const auto name = object.value("name").toString(), text = object.value("value").toString();
        QString *target = nullptr;
        int limit = 4096;
        if (name == "message") target = &result_.message;
        else if (name == "detail") target = &result_.detail;
        else if (name == "title") target = &formatted_->title;
        else if (name == "author") target = &formatted_->author;
        else if (name == "notice") target = &formatted_->notice;
        else if (name == "html") { target = &formatted_->html; limit = 2 * 1024 * 1024; }
        else if (name == "markdown") { target = &formatted_->markdown; limit = DocumentReader::CharacterLimit; }
        else if (name == "plainText") { target = &formatted_->plainText; limit = DocumentReader::CharacterLimit; }
        else if (name == "text" && textFallback_) { target = &result_.text; limit = 3 * 1024 * 1024; }
        else return invalid();
        if (text.size() > limit - target->size()) return invalid();
        *target += text;
        return true;
    }
    if (event == QStringLiteral("section")) {
        const auto title = object.value("title").toString(), anchor = object.value("anchor").toString();
        if (textFallback_ || ++sections_ > 128 || title.size() > 1024 || anchor.isEmpty() || anchor.size() > 4096) return invalid();
        formatted_->sections.append({title, anchor});
        return true;
    }
    if (event == QStringLiteral("image")) {
        const auto id = object.value("id").toString();
        bool numberValid = false;
        const int number = id.mid(14).toInt(&numberValid);
        const int width = object.value("width").toInt(), height = object.value("height").toInt();
        const int format = object.value("format").toInt();
        const auto bytes = object.value("bytes").toInteger(-1);
        const auto profile = object.value("profileBytes").toInteger(-1);
        if (textFallback_ || !pendingImage_.isNull() || formatted_->images.size() >= 16 || formatted_->images.contains(id)
            || !id.startsWith("dzurkel-image:") || !numberValid || number < 0 || number >= 16
            || width < 1 || height < 1 || qint64(width) * height > 2 * 1024 * 1024
            || qint64(width) * height > 8 * 1024 * 1024 - imagePixels_
            || format <= QImage::Format_Invalid || format >= QImage::NImageFormats || bytes < 1
            || bytes > 64 * 1024 * 1024 - imageBytes_ || profile < 0 || profile > 1024 * 1024) return invalid();
        QImage image(width, height, QImage::Format(format));
        if (image.isNull() || image.sizeInBytes() != bytes) return invalid();
        const auto palette = object.value("palette").toArray();
        if (palette.size() > 256) return invalid();
        QList<QRgb> colors;
        for (const auto &entry : palette) {
            const auto color = entry.toInteger(-1);
            if (color < 0 || color > 0xffffffffLL) return invalid();
            colors.append(QRgb(color));
        }
        if (!colors.isEmpty()) image.setColorTable(colors);
        const auto dpr = object.value("dpr").toDouble(1);
        if (!std::isfinite(dpr) || dpr <= 0 || dpr > 64) return invalid();
        image.setDevicePixelRatio(dpr);
        image.setDotsPerMeterX(object.value("dpmX").toInt());
        image.setDotsPerMeterY(object.value("dpmY").toInt());
        imageBytes_ += bytes;
        imagePixels_ += qint64(width) * height;
        pendingImage_ = std::move(image);
        pendingImageId_ = id;
        imageOffset_ = 0;
        profileSize_ = profile;
        pendingProfile_.clear();
        return true;
    }
    if (event == QStringLiteral("image-data") || event == QStringLiteral("image-profile")) {
        if (pendingImage_.isNull() || !object.value("data").isString()) return invalid();
        const auto decoded = QByteArray::fromBase64Encoding(object.value("data").toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded || decoded.decoded.isEmpty() || decoded.decoded.size() > 32768) return invalid();
        const auto &bytes = decoded.decoded;
        if (event == QStringLiteral("image-data")) {
            if (bytes.size() > pendingImage_.sizeInBytes() - imageOffset_) return invalid();
            std::memcpy(pendingImage_.bits() + imageOffset_, bytes.constData(), size_t(bytes.size()));
            imageOffset_ += bytes.size();
        } else {
            if (bytes.size() > profileSize_ - pendingProfile_.size()) return invalid();
            pendingProfile_ += bytes;
        }
        return true;
    }
    if (event == QStringLiteral("image-end")) {
        if (pendingImage_.isNull() || imageOffset_ != pendingImage_.sizeInBytes() || pendingProfile_.size() != profileSize_) return invalid();
        if (!pendingProfile_.isEmpty()) {
            const auto colorSpace = QColorSpace::fromIccProfile(pendingProfile_);
            if (!colorSpace.isValid()) return invalid();
            pendingImage_.setColorSpace(colorSpace);
        }
        formatted_->images.insert(pendingImageId_, std::move(pendingImage_));
        pendingImage_ = {};
        pendingProfile_.clear();
        return true;
    }
    if (event != QStringLiteral("end") || !pendingImage_.isNull()) return invalid();
    if (textFallback_) {
        const auto lines = object.value("lines").toInteger(-1);
        if (lines < 0 || lines > 20000) return invalid();
        result_.kind = PreviewKind::Text;
        result_.textLineCount = int(lines);
    } else {
        if (formatted_->plainText.trimmed().isEmpty() && formatted_->images.isEmpty()) return invalid();
        result_.kind = PreviewKind::Document;
        result_.document = formatted_;
    }
    finish();
    return false;
}
bool StructuredDecodeJob::decodeDocumentRecords(const QJsonArray &records, PreviewData &data)
{
    StructuredDecodeJob decoder;
    decoder.result_ = data;
    for (const auto &record : records) {
        if (decoder.stopped_ || !record.isObject()) return false;
        decoder.documentRecord(record.toObject());
    }
    if (!decoder.stopped_ || decoder.result_.kind != PreviewKind::Document || !decoder.result_.document) return false;
    data = decoder.result_;
    return true;
}
bool decodeDocumentPortion(const QJsonObject &content, std::shared_ptr<const FormattedDocument> &document)
{
    if (content.value("kind") != QJsonValue(QStringLiteral("document")) || !content.value("records").isArray()) return false;
    const auto records = content.value("records").toArray();
    if (records.size() > 8192) return false;
    PreviewData data;
    data.readerId = QStringLiteral("epub");
    if (!StructuredDecodeJob::decodeDocumentRecords(records, data)) return false;
    auto decoded = std::make_shared<FormattedDocument>(*data.document);
    if (!content.value("navigation").isArray() || !content.value("targetAnchor").isString()) return false;
    const auto navigation = content.value("navigation").toArray();
    if (navigation.size() > 2048) return false;
    for (const auto &entry : navigation) {
        const auto item = entry.toObject();
        const auto title = item.value("title").toString(), anchor = item.value("anchor").toString();
        if (title.isEmpty() || title.size() > 1024 || anchor.isEmpty() || anchor.size() > 4096) return false;
        decoded->navigation.append({title, anchor});
    }
    decoded->targetAnchor = content.value("targetAnchor").toString();
    if (decoded->targetAnchor.size() > 4096) return false;
    document = decoded;
    return true;
}
void StructuredDecodeJob::read()
{
    if (stopped_ || !process_) return;
    qint64 budget = 512 * 1024;
    while (process_ && process_->bytesAvailable() && budget >= 2) {
        const auto bytes = process_->readLine(std::min({qint64(64 * 1024), budget, StructuredRecordLimit + 1 - line_.size()}));
        if (bytes.isEmpty()) return;
        received_ += bytes.size();
        budget -= bytes.size();
        line_ += bytes;
        if (received_ > (documentMode_ ? 2 * StructuredTransferLimit : StructuredTransferLimit) || line_.size() > StructuredRecordLimit
            || (line_.size() == StructuredRecordLimit && !line_.endsWith('\n'))) {
            finish(QStringLiteral("Document/table preview response exceeds its budget.")); return;
        }
        if (line_.endsWith('\n') && !record()) return;
    }
    if (process_ && process_->bytesAvailable()) QTimer::singleShot(0, this, &StructuredDecodeJob::read);
}
}
