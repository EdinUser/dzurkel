#include "ThunarIntegration.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <stdexcept>
#include <unistd.h>
#include "DesktopIntegration.h"

namespace dzurkel {
namespace {
const QString actionId = QStringLiteral("io.github.EdinUser.Dzurkel.preview");
const QString accelPath = QStringLiteral("<Actions>/ThunarActions/uca-action-") + actionId;
bool thunarWindowActive() { return desktopThunarWindowActive(); }

void check(bool condition, const QString &message)
{
    if (!condition) throw std::runtime_error(message.toStdString());
}
QByteArray read(const QString &path, qint64 limit = 2 * 1024 * 1024)
{
    const QFileInfo info(path);
    check(!info.isSymLink(), QStringLiteral("Thunar setup refuses symbolic links: %1").arg(path));
    if (!info.exists()) return {};
    check(info.isFile(), QStringLiteral("Not a regular configuration file: %1").arg(path));
    QFile file(path);
    check(file.open(QIODevice::ReadOnly) && file.size() <= limit,
        QStringLiteral("Cannot read configuration, or it exceeds the size limit: %1").arg(path));
    const auto bytes = file.readAll();
    check(file.error() == QFileDevice::NoError, QStringLiteral("Cannot read %1").arg(path));
    return bytes;
}
void write(const QString &path, const QByteArray &bytes, QFile::Permissions permissions)
{
    check(!QFileInfo(path).isSymLink(), QStringLiteral("Configuration became a symbolic link: %1").arg(path));
    check(QDir().mkpath(QFileInfo(path).absolutePath()), QStringLiteral("Cannot create configuration directory."));
    QSaveFile file(path);
    check(file.open(QIODevice::WriteOnly), QStringLiteral("Cannot write %1").arg(path));
    check(file.setPermissions(permissions), QStringLiteral("Cannot preserve configuration permissions."));
    check(file.write(bytes) == bytes.size() && file.commit(), QStringLiteral("Cannot save %1").arg(path));
}
QString quote(QString value)
{
    // Thunar expands desktop-entry field codes before shell-style argument
    // parsing. Escape percent signs as well as quoting the executable.
    value.replace(QLatin1Char('%'), QStringLiteral("%%"));
    value.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + value + QLatin1Char('\'');
}
QByteArray action(const QString &executable)
{
    QByteArray bytes;
    QXmlStreamWriter xml(&bytes);
    xml.writeStartElement(QStringLiteral("action"));
    const auto element = [&xml](const char *name, const QString &text) { xml.writeTextElement(QString::fromLatin1(name), text); };
    element("icon", QStringLiteral("dzurkel"));
    element("name", QStringLiteral("Preview with D_zurkel"));
    element("unique-id", actionId);
    element("command", quote(executable) + QStringLiteral(" --show -- %f"));
    element("description", QStringLiteral("Preview the selected file"));
    element("patterns", QStringLiteral("*"));
    element("range", QStringLiteral("1-1"));
    for (const auto *name : {"startup-notify", "text-files", "image-files", "audio-files", "video-files", "other-files"})
        xml.writeEmptyElement(QString::fromLatin1(name));
    xml.writeEndElement();
    return bytes;
}
struct XmlResult { QByteArray document, managed; int count = 0; };
QByteArray canonicalAction(const QByteArray &block)
{
    QXmlStreamReader reader(block);
    check(reader.readNextStartElement() && reader.name() == QLatin1String("action") && reader.attributes().isEmpty(),
        QStringLiteral("Unrecognized managed Thunar action."));
    QJsonObject fields;
    while (reader.readNextStartElement()) {
        const QString name = reader.name().toString();
        check(!fields.contains(name) && reader.attributes().isEmpty(), QStringLiteral("Duplicate or edited Thunar action fields."));
        fields.insert(name, reader.readElementText());
    }
    check(!reader.hasError(), QStringLiteral("Invalid Thunar action."));
    // Thunar's editor inserts this empty optional field when saving all actions.
    if (fields.value("submenu").toString().isEmpty()) fields.remove("submenu");
    return QJsonDocument(fields).toJson(QJsonDocument::Compact);
}
XmlResult transform(QByteArray input, const QByteArray &insert = {}, bool remove = false)
{
    if (input.isEmpty()) input = "<actions/>";
    QXmlStreamReader reader(input);
    XmlResult result;
    QXmlStreamWriter output(&result.document);
    int depth = 0;
    bool root = false;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            ++depth;
            if (depth == 1) {
                check(reader.name() == QLatin1String("actions") && !root, QStringLiteral("Unrecognized Thunar actions format."));
                root = true;
            }
            if (depth == 2 && reader.name() == QLatin1String("action")) {
                QByteArray block;
                QXmlStreamWriter item(&block);
                QString id;
                int nested = 1;
                item.writeCurrentToken(reader);
                while (nested && !reader.atEnd()) {
                    reader.readNext();
                    if (reader.isStartElement()) ++nested;
                    if (reader.isEndElement()) --nested;
                    if (reader.isStartElement() && nested == 2 && reader.name() == QLatin1String("unique-id")) {
                        item.writeStartElement(QStringLiteral("unique-id"));
                        id = reader.readElementText();
                        item.writeCharacters(id);
                        item.writeEndElement();
                        --nested;
                    } else item.writeCurrentToken(reader);
                }
                if (id == actionId) { result.managed = canonicalAction(block); ++result.count; }
                if (id != actionId || !remove) {
                    QXmlStreamReader copy(block);
                    while (!copy.atEnd()) { copy.readNext(); if (!copy.isStartDocument() && !copy.isEndDocument()) output.writeCurrentToken(copy); }
                }
                --depth;
                continue;
            }
        } else if (reader.isEndElement()) {
            if (depth == 1 && !insert.isEmpty()) {
                QXmlStreamReader extra(insert);
                while (!extra.atEnd()) { extra.readNext(); if (!extra.isStartDocument() && !extra.isEndDocument()) output.writeCurrentToken(extra); }
            }
            --depth;
        }
        if (reader.tokenType() != QXmlStreamReader::Invalid) output.writeCurrentToken(reader);
    }
    check(root && !reader.hasError() && result.count <= 1, QStringLiteral("Invalid or duplicate Thunar actions; nothing was changed."));
    return result;
}
struct Binding { QString path, key; qsizetype start, size, lineStart, lineSize; bool disabled; };
QList<Binding> bindings(const QString &text, bool includeDisabled = false)
{
    static const QRegularExpression expression(QStringLiteral(
        R"re(^[ \t]*(;[ \t]*)?\(gtk_accel_path[ \t]+"([^"\\]*)"[ \t]+"([^"\\]*)"\)[ \t]*\r?$)re"), QRegularExpression::MultilineOption);
    QList<Binding> result;
    QSet<QString> paths;
    auto matches = expression.globalMatch(text);
    while (matches.hasNext()) {
        const auto match = matches.next();
        const bool disabled = !match.captured(1).isEmpty();
        if (disabled && !includeDisabled) continue;
        check(!paths.contains(match.captured(2)), QStringLiteral("Duplicate Thunar shortcut entries; review them before setup."));
        paths.insert(match.captured(2));
        result.append({match.captured(2), match.captured(3), match.capturedStart(3), match.capturedLength(3), match.capturedStart(), match.capturedLength(), disabled});
    }
    // Refuse unfamiliar live entries rather than silently overlooking a conflict.
    for (const auto &line : text.split('\n'))
        if (line.trimmed().startsWith(QLatin1Char('(')))
            check(expression.match(line).hasMatch(), QStringLiteral("Unrecognized Thunar shortcut syntax; review it before setup."));
    return result;
}
QJsonObject snapshot(const QString &path)
{
    return {{"exists", QFileInfo::exists(path)}, {"bytes", QString::fromLatin1(read(path).toBase64())},
        {"permissions", int(QFileInfo::exists(path) ? QFileInfo(path).permissions() : QFile::ReadOwner | QFile::WriteOwner)}};
}
QByteArray original(const QJsonObject &saved)
{
    const auto decoded = QByteArray::fromBase64Encoding(saved.value("bytes").toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    check(bool(decoded), QStringLiteral("Corrupt setup backup; nothing was changed."));
    return decoded.decoded;
}
void putOriginal(const QString &path, const QJsonObject &saved)
{
    if (saved.value("exists").toBool()) write(path, original(saved), QFile::Permissions(saved.value("permissions").toInt()));
    else check(!QFileInfo::exists(path) || QFile::remove(path), QStringLiteral("Cannot remove %1").arg(path));
}
QJsonObject manifest(const QString &path)
{
    const auto document = QJsonDocument::fromJson(read(path, 16 * 1024 * 1024));
    check(document.isObject() && document.object().value("version").toInt() == 1,
        QStringLiteral("Unrecognized Thunar setup backup; nothing was changed."));
    const auto object = document.object();
    check(object.value("actions").isObject() && object.value("shortcuts").isObject()
        && object.value("changes").isArray() && object.value("managed").isString(), QStringLiteral("Incomplete Thunar setup backup."));
    for (const auto *key : {"actions", "shortcuts"}) {
        const auto file = object.value(QLatin1String(key)).toObject();
        check(file.value("exists").isBool() && file.value("bytes").isString() && file.value("permissions").isDouble(),
            QStringLiteral("Incomplete Thunar file backup."));
        original(file);
    }
    check(object.value("installedActions").isString() && object.value("installedKeys").isString()
        && !QJsonDocument::fromJson(QByteArray::fromBase64(object.value("managed").toString().toLatin1())).object().isEmpty(),
        QStringLiteral("Incomplete Thunar setup backup."));
    return object;
}
}
ThunarIntegration::ThunarIntegration()
    : ThunarIntegration(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/Thunar"),
        qEnvironmentVariable("XDG_STATE_HOME", QDir::homePath() + QStringLiteral("/.local/state")) + QStringLiteral("/dzurkel"),
        QCoreApplication::applicationFilePath())
{
    defaultActions_ = QStandardPaths::locate(QStandardPaths::GenericConfigLocation, QStringLiteral("Thunar/uca.xml"));
}
ThunarIntegration::ThunarIntegration(QString configDirectory, QString stateDirectory, QString executable,
                                   std::function<bool()> running, QString defaultActions, std::function<bool()> activeWindow)
    : config_(std::move(configDirectory)), state_(std::move(stateDirectory)), executable_(std::move(executable)),
      defaultActions_(std::move(defaultActions)),
      running_(running ? std::move(running) : isRunning),
      activeWindow_(activeWindow ? std::move(activeWindow) : thunarWindowActive) {}
bool ThunarIntegration::running() const { return running_(); }
bool ThunarIntegration::activeWindow() const { return activeWindow_(); }
QString ThunarIntegration::actionsFile() const { return config_ + QStringLiteral("/uca.xml"); }
QString ThunarIntegration::shortcutsFile() const { return config_ + QStringLiteral("/accels.scm"); }
QString ThunarIntegration::backupFile() const { return state_ + QStringLiteral("/thunar-preview-backup.json"); }
bool ThunarIntegration::hasBackup() const { return QFileInfo::exists(backupFile()) || QFileInfo(backupFile()).isSymLink(); }
bool ThunarIntegration::isRunning()
{
    for (const auto &pid : QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool numeric; pid.toLongLong(&numeric);
        const QString path = QStringLiteral("/proc/") + pid;
        if (!numeric || QFileInfo(path).ownerId() != getuid()) continue;
        QFile name(path + QStringLiteral("/comm"));
        if (name.open(QIODevice::ReadOnly) && name.readAll().trimmed().compare("thunar", Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}
bool ThunarIntegration::enabled() const
{
    try {
        const auto saved = manifest(backupFile());
        const auto xml = transform(read(actionsFile()));
        if (xml.count != 1 || xml.managed != QByteArray::fromBase64(saved.value("managed").toString().toLatin1())) return false;
        bool space = false;
        for (const auto &binding : bindings(QString::fromUtf8(read(shortcutsFile())))) {
            if (binding.path == accelPath) space = binding.key == QLatin1String("space");
            else if (binding.key.compare(QStringLiteral("space"), Qt::CaseInsensitive) == 0) return false;
        }
        return space;
    } catch (const std::exception &) { return false; }
}
bool ThunarIntegration::enable(QString &error) { return change(false, error); }
bool ThunarIntegration::restore(QString &error) { return change(true, error); }
bool ThunarIntegration::change(bool restore, QString &error)
{
    error.clear();
    try {
        check(!running(), QStringLiteral("Close Thunar before changing its preview setup."));
        check(QDir().mkpath(state_), QStringLiteral("Cannot create the setup backup directory."));
        QLockFile lock(state_ + QStringLiteral("/thunar-preview.lock"));
        check(lock.tryLock(0), QStringLiteral("Another Thunar setup is in progress."));
        if (!restore && hasBackup()) {
            check(enabled(), QStringLiteral("Thunar preview settings changed after setup. Review them before enabling again."));
            return true;
        }
        const auto beforeActions = snapshot(actionsFile()), beforeKeys = snapshot(shortcutsFile());
        QByteArray nextActions, nextKeys;
        QJsonObject saved;
        if (!restore) {
            const auto base = !beforeActions.value("exists").toBool() && !defaultActions_.isEmpty()
                ? read(defaultActions_) : original(beforeActions);
            const auto xml = transform(base);
            check(xml.count == 0, QStringLiteral("A Dzurkel action already exists without its backup. Review Thunar's custom actions first."));
            nextActions = transform(base, action(executable_)).document;
            QString keys = QString::fromUtf8(original(beforeKeys));
            check(keys.toUtf8() == original(beforeKeys), QStringLiteral("Thunar shortcuts must be UTF-8."));
            const auto entries = bindings(keys);
            QJsonArray changes;
            for (auto it = entries.crbegin(); it != entries.crend(); ++it) {
                check(it->path != accelPath, QStringLiteral("A Dzurkel shortcut already exists without its backup."));
                if (it->key.compare(QStringLiteral("space"), Qt::CaseInsensitive) != 0) continue;
                changes.append(QJsonObject{{"path", it->path}, {"before", it->key}});
                keys.replace(it->start, it->size, QString());
            }
            keys += QStringLiteral("\n(gtk_accel_path \"%1\" \"space\")\n").arg(accelPath);
            nextKeys = keys.toUtf8();
            saved = {{"version", 1}, {"actions", beforeActions}, {"shortcuts", beforeKeys}, {"changes", changes},
                {"installedActions", QString::fromLatin1(nextActions.toBase64())}, {"installedKeys", QString::fromLatin1(nextKeys.toBase64())},
                {"managed", QString::fromLatin1(transform(nextActions).managed.toBase64())}};
        } else {
            saved = manifest(backupFile());
            const auto xml = transform(original(beforeActions));
            check(xml.count == 1 && xml.managed == QByteArray::fromBase64(saved.value("managed").toString().toLatin1()),
                QStringLiteral("Dzurkel's Thunar action was edited or removed; nothing was restored."));
            nextActions = original(beforeActions) == QByteArray::fromBase64(saved.value("installedActions").toString().toLatin1())
                ? original(saved.value("actions").toObject()) : transform(original(beforeActions), {}, true).document;
            QString keys = QString::fromUtf8(original(beforeKeys));
            const auto entries = bindings(keys, true);
            QMap<QString, QString> replacements;
            replacements.insert(accelPath, QStringLiteral("space"));
            for (const auto &change : saved.value("changes").toArray()) replacements.insert(change.toObject().value("path").toString(), QString());
            for (const auto &entry : entries) if (replacements.contains(entry.path)) {
                check(entry.key == replacements.value(entry.path) && (entry.path != accelPath || !entry.disabled), QStringLiteral("A shortcut changed after setup; nothing was restored."));
                replacements.remove(entry.path);
            }
            check(replacements.isEmpty(), QStringLiteral("A managed shortcut is missing; nothing was restored."));
            if (original(beforeKeys) == QByteArray::fromBase64(saved.value("installedKeys").toString().toLatin1())) nextKeys = original(saved.value("shortcuts").toObject());
            else {
                for (auto it = entries.crbegin(); it != entries.crend(); ++it) {
                    if (it->path == accelPath) keys.remove(it->lineStart, it->lineSize);
                    for (const auto &change : saved.value("changes").toArray())
                        if (it->path == change.toObject().value("path").toString()) keys.replace(it->lineStart, it->lineSize,
                            QStringLiteral("(gtk_accel_path \"%1\" \"%2\")").arg(it->path, change.toObject().value("before").toString()));
                }
                nextKeys = keys.toUtf8();
            }
        }
        check(nextActions.size() <= 2 * 1024 * 1024 && nextKeys.size() <= 2 * 1024 * 1024,
            QStringLiteral("Resulting Thunar configuration exceeds 2 MiB; nothing was changed."));
        check(!running(), QStringLiteral("Thunar started again; configuration was not changed."));
        if (!restore) write(backupFile(), QJsonDocument(saved).toJson(), QFile::ReadOwner | QFile::WriteOwner);
        try {
            const auto actionPermissions = restore ? saved.value("actions").toObject() : beforeActions;
            const auto keyPermissions = restore ? saved.value("shortcuts").toObject() : beforeKeys;
            write(actionsFile(), nextActions, QFile::Permissions(actionPermissions.value("permissions").toInt()));
            write(shortcutsFile(), nextKeys, QFile::Permissions(keyPermissions.value("permissions").toInt()));
            if (restore) {
                if (!saved.value("actions").toObject().value("exists").toBool() && nextActions.isEmpty()) check(QFile::remove(actionsFile()), QStringLiteral("Cannot remove generated actions file."));
                if (!saved.value("shortcuts").toObject().value("exists").toBool() && nextKeys.isEmpty()) check(QFile::remove(shortcutsFile()), QStringLiteral("Cannot remove generated shortcuts file."));
            }
        } catch (...) {
            putOriginal(actionsFile(), beforeActions);
            putOriginal(shortcutsFile(), beforeKeys);
            if (!restore) QFile::remove(backupFile());
            throw;
        }
        if (restore) check(QFile::remove(backupFile()), QStringLiteral("Restored settings, but could not remove the setup backup."));
        return true;
    } catch (const std::exception &exception) { error = QString::fromUtf8(exception.what()); return false; }
}
}
