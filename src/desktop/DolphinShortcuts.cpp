#include "DolphinShortcuts.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLockFile>
#include <QMap>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <optional>
#include <stdexcept>

namespace dzurkel {
namespace {
const QString target = QStringLiteral("servicemenu_dzurkel.desktop::dzurkel");
const QString selection = QStringLiteral("toggle_selection_mode");
using Changes = QMap<QString, std::optional<QString>>;
void check(bool valid, const QString &message)
{
    if (!valid) throw std::runtime_error(message.toStdString());
}
QByteArray read(const QString &path)
{
    const QFileInfo info(path);
    check(!info.isSymLink(), QStringLiteral("The settings file is a symbolic link; configure shortcuts in Dolphin instead."));
    if (!info.exists()) return {};
    check(info.isFile(), QStringLiteral("The settings path is not a regular file."));
    QFile file(path);
    check(file.open(QIODevice::ReadOnly) && file.size() <= 2 * 1024 * 1024,
          QStringLiteral("Cannot read the settings file, or it exceeds 2 MiB."));
    const auto bytes = file.readAll();
    check(file.error() == QFileDevice::NoError, QStringLiteral("Cannot read the settings file."));
    return bytes;
}
void write(const QString &path, const QByteArray &bytes)
{
    check(QDir().mkpath(QFileInfo(path).absolutePath()), QStringLiteral("Cannot create the settings directory."));
    QSaveFile file(path);
    check(file.open(QIODevice::WriteOnly), QStringLiteral("Cannot open the settings file for writing."));
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    check(file.write(bytes) == bytes.size() && file.commit(), QStringLiteral("Cannot save the settings file."));
}
// Preserve menus, toolbars, comments, namespaces and attributes. Only shortcut
// attributes inside the root ActionProperties section are changed.
QByteArray transform(QByteArray bytes, const Changes &changes, Changes *current = nullptr)
{
    if (bytes.isEmpty()) bytes = "<gui name=\"dolphin\" version=\"1\"><ActionProperties/></gui>";
    QXmlStreamReader reader(bytes);
    QByteArray result;
    QXmlStreamWriter writer(&result);
    int depth = 0;
    bool properties = false;
    bool sawProperties = false;
    QSet<QString> seen;
    auto appendMissing = [&] {
        for (auto it = changes.begin(); it != changes.end(); ++it) {
            if (seen.contains(it.key()) || !it.value()) continue;
            writer.writeStartElement(QStringLiteral("Action"));
            writer.writeAttribute(QStringLiteral("name"), it.key());
            writer.writeAttribute(QStringLiteral("shortcut"), *it.value());
            writer.writeEndElement();
        }
    };
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            ++depth;
            if (depth == 1) check(reader.name() == QStringLiteral("gui")
                && reader.attributes().value(QStringLiteral("name")).compare(QStringLiteral("dolphin"), Qt::CaseInsensitive) == 0,
                QStringLiteral("Unrecognized Dolphin settings format."));
            if (depth == 2 && reader.name() == QStringLiteral("ActionProperties")) {
                check(!reader.attributes().hasAttribute(QStringLiteral("scheme"))
                    || reader.attributes().value(QStringLiteral("scheme")) == QStringLiteral("Default"),
                    QStringLiteral("Custom shortcut schemes should be configured in Dolphin."));
                check(!sawProperties, QStringLiteral("Duplicate shortcut sections; configure shortcuts in Dolphin instead."));
                properties = sawProperties = true;
            }
            if (properties && depth == 3 && reader.name() == QStringLiteral("Action")) {
                const QString name = reader.attributes().value(QStringLiteral("name")).toString();
                check(!seen.contains(name), QStringLiteral("Duplicate shortcut entries; configure shortcuts in Dolphin instead."));
                seen.insert(name);
                if (current) current->insert(name, reader.attributes().hasAttribute(QStringLiteral("shortcut"))
                    ? std::optional<QString>(reader.attributes().value(QStringLiteral("shortcut")).toString()) : std::nullopt);
                if (changes.contains(name)) {
                    writer.writeStartElement(reader.qualifiedName().toString());
                    for (const auto &ns : reader.namespaceDeclarations()) writer.writeNamespace(ns.namespaceUri().toString(), ns.prefix().toString());
                    for (const auto &attribute : reader.attributes())
                        if (attribute.name() != QStringLiteral("shortcut")) writer.writeAttribute(attribute.qualifiedName().toString(), attribute.value().toString());
                    if (changes.value(name)) writer.writeAttribute(QStringLiteral("shortcut"), *changes.value(name));
                    continue;
                }
            }
        } else if (reader.isEndElement()) {
            if (properties && depth == 2) { appendMissing(); properties = false; }
            if (depth == 1 && !sawProperties) {
                writer.writeStartElement(QStringLiteral("ActionProperties"));
                appendMissing();
                writer.writeEndElement();
            }
            --depth;
        }
        if (reader.tokenType() != QXmlStreamReader::Invalid) writer.writeCurrentToken(reader);
    }
    check(!reader.hasError(), QStringLiteral("Dolphin settings contain invalid XML; nothing was changed."));
    return result;
}
QList<QKeySequence> shortcuts(const std::optional<QString> &value)
{
    if (!value || value->isEmpty() || *value == QStringLiteral("none")) return {};
    return QKeySequence::listFromString(*value, QKeySequence::PortableText);
}
bool startsWithSpace(const QKeySequence &sequence)
{
    return !sequence.isEmpty() && sequence[0] == QKeyCombination(Qt::Key_Space);
}
} // namespace
DolphinShortcuts::DolphinShortcuts()
    : DolphinShortcuts(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
          + QStringLiteral("/kxmlgui5/dolphin/dolphinui.rc"),
          QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
          + QStringLiteral("/dzurkel/dolphin-space-backup.json")) { checkScheme_ = true; }
DolphinShortcuts::DolphinShortcuts(QString configuration, QString backup)
    : configuration_(std::move(configuration)), backup_(std::move(backup)) {}
bool DolphinShortcuts::hasBackup() const { return QFileInfo::exists(backup_); }
bool DolphinShortcuts::supportsAutomaticSetup() const
{
    if (!checkScheme_) return true;
    const QString path = QStandardPaths::locate(QStandardPaths::GenericConfigLocation, QStringLiteral("dolphinrc"));
    if (path.isEmpty()) return true;
    QSettings settings(path, QSettings::IniFormat);
    return settings.value(QStringLiteral("Shortcut Schemes/Current Scheme"), QStringLiteral("Default")).toString() == QStringLiteral("Default");
}
bool DolphinShortcuts::usesSpace() const
{
    try {
        if (!supportsAutomaticSetup()) return false;
        Changes current;
        transform(read(configuration_), {}, &current);
        return shortcuts(current.value(target)).contains(QKeySequence(Qt::Key_Space));
    } catch (...) { return false; }
}
bool DolphinShortcuts::useSpace(QString &error)
{
    try {
        check(supportsAutomaticSetup(), QStringLiteral("Custom shortcut schemes should be configured in Dolphin."));
        check(QDir().mkpath(QFileInfo(backup_).absolutePath()), QStringLiteral("Cannot create the backup directory."));
        QLockFile lock(backup_ + QStringLiteral(".lock"));
        check(lock.tryLock(), QStringLiteral("Another shortcut setup is already running."));
        check(!hasBackup(), QStringLiteral("A previous setup backup exists. Restore it before applying a new setup."));
        const QByteArray before = read(configuration_);
        Changes current;
        transform(before, {}, &current);
        Changes changes;
        for (auto it = current.begin(); it != current.end(); ++it) {
            const auto old = shortcuts(it.value());
            auto kept = old;
            kept.removeIf(startsWithSpace);
            if (kept.size() != old.size()) changes[it.key()] = kept.isEmpty() ? QString()
                : QKeySequence::listToString(kept, QKeySequence::PortableText);
        }
        // Missing overrides use Dolphin's default Space selection binding.
        if (!current.value(selection)) changes[selection] = QString();
        changes[target] = QStringLiteral("Space");
        QJsonArray entries;
        for (auto it = changes.begin(); it != changes.end(); ++it) {
            const auto original = current.value(it.key());
            entries.append(QJsonObject{{"action", it.key()}, {"hadShortcut", bool(original)},
                {"before", original.value_or(QString())}, {"applied", *it.value()}});
        }
        const auto after = transform(before, changes);
        write(backup_, QJsonDocument(QJsonObject{{"version", 1}, {"entries", entries}}).toJson());
        try { write(configuration_, after); }
        catch (...) { QFile::remove(backup_); throw; }
        error.clear();
        return true;
    } catch (const std::exception &failure) { error = QString::fromUtf8(failure.what()); return false; }
}
bool DolphinShortcuts::restore(QString &error)
{
    try {
        QLockFile lock(backup_ + QStringLiteral(".lock"));
        check(lock.tryLock(), QStringLiteral("Another shortcut setup is already running."));
        const auto backup = QJsonDocument::fromJson(read(backup_)).object();
        check(backup.value(QStringLiteral("version")).toInt() == 1
            && !backup.value(QStringLiteral("entries")).toArray().isEmpty(), QStringLiteral("No valid shortcut backup is available."));
        const auto bytes = read(configuration_);
        Changes current, changes;
        transform(bytes, {}, &current);
        for (const auto &value : backup.value(QStringLiteral("entries")).toArray()) {
            const auto entry = value.toObject();
            const QString name = entry.value(QStringLiteral("action")).toString();
            check(!name.isEmpty() && !changes.contains(name)
                && entry.value(QStringLiteral("hadShortcut")).isBool()
                && entry.value(QStringLiteral("before")).isString()
                && entry.value(QStringLiteral("applied")).isString(), QStringLiteral("The shortcut backup is invalid."));
            check(shortcuts(current.value(name)) == shortcuts(entry.value(QStringLiteral("applied")).toString()),
                QStringLiteral("A managed shortcut has changed since setup. Restore it in Dolphin to avoid replacing newer choices."));
            changes[name] = entry.value(QStringLiteral("hadShortcut")).toBool()
                ? std::optional<QString>(entry.value(QStringLiteral("before")).toString()) : std::nullopt;
        }
        write(configuration_, transform(bytes, changes));
        check(QFile::remove(backup_), QStringLiteral("Shortcuts were restored, but the backup could not be removed."));
        error.clear();
        return true;
    } catch (const std::exception &failure) { error = QString::fromUtf8(failure.what()); return false; }
}
} // namespace dzurkel
