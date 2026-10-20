#include "ChooserIntegration.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGenericPlugin>
#include <QJsonArray>
#include <QJsonObject>
#include <QPluginLoader>
#include <QSaveFile>
#include <QStandardPaths>

namespace dzurkel {
namespace {
QString quote(QString value) { return QChar('\'') + value.replace(QChar('\''), QStringLiteral("'\\''")) + QChar('\''); }
}
ChooserIntegration::ChooserIntegration()
    : configuration_(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/plasma-workspace/env/dzurkel-chooser.sh")),
      pluginRoot_(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral(DZURKEL_CHOOSER_PLUGIN_ROOT))),
      executable_(QCoreApplication::applicationFilePath())
{
    const auto buildRoot = QCoreApplication::applicationDirPath() + QStringLiteral("/dzurkel-qt-plugins");
    if (QFileInfo::exists(buildRoot + QStringLiteral("/generic/libdzurkel_chooser.so"))) pluginRoot_ = buildRoot;
    pluginRoot_ = QDir::cleanPath(pluginRoot_);
}
ChooserIntegration::ChooserIntegration(QString configuration, QString pluginRoot, QString executable)
    : configuration_(std::move(configuration)), pluginRoot_(std::move(pluginRoot)), executable_(std::move(executable)) {}
bool ChooserIntegration::available(QString &reason) const
{
    if (pluginRoot_.contains(QChar(':')) || pluginRoot_.contains(QChar('\n')) || pluginRoot_.contains(QChar(','))
        || executable_.contains(QChar('\n')) || executable_.contains(QChar(','))) {
        reason = QStringLiteral("Installation paths cannot contain Qt plugin-list separators.");
        return false;
    }
    QPluginLoader loader(pluginRoot_ + QStringLiteral("/generic/libdzurkel_chooser.so"));
    const auto metadata = loader.metaData();
    if (metadata.value(QStringLiteral("IID")).toString() != QLatin1String(QGenericPluginFactoryInterface_iid)
        || !metadata.value(QStringLiteral("MetaData")).toObject().value(QStringLiteral("Keys")).toArray().contains(QStringLiteral("dzurkelchooser"))) {
        reason = QStringLiteral("The optional KDE file-dialog adapter is missing or incompatible."); return false;
    }
    if (!loader.load()) {
        reason = QStringLiteral("The optional KDE file-dialog adapter or its matching Qt/KIO libraries are unavailable. %1").arg(loader.errorString());
        return false;
    }
    loader.unload();
    reason.clear();
    return true;
}
QByteArray ChooserIntegration::script() const
{
    // Plasma sources this file before session applications are launched. No
    // existing environment/plugin entries are overwritten or executed here.
    return QStringLiteral("# Dzurkel managed KDE file-dialog opt-in, version 1\n"
        "dzurkel_chooser_path=%1\n"
        "dzurkel_chooser_key=%2\n"
        "if [ -f \"$dzurkel_chooser_path/generic/libdzurkel_chooser.so\" ]; then\n"
        "  case \":${QT_PLUGIN_PATH-}:\" in\n"
        "    *\":$dzurkel_chooser_path:\"*) ;;\n"
        "    *) export QT_PLUGIN_PATH=\"$dzurkel_chooser_path${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}\" ;;\n"
        "  esac\n"
        "  case \",${QT_QPA_GENERIC_PLUGINS-},\" in\n"
        "    *,dzurkelchooser,*|*,dzurkelchooser:*) ;;\n"
        "    *) export QT_QPA_GENERIC_PLUGINS=\"${QT_QPA_GENERIC_PLUGINS:+$QT_QPA_GENERIC_PLUGINS,}$dzurkel_chooser_key\" ;;\n"
        "  esac\n"
        "fi\n"
        "unset dzurkel_chooser_path dzurkel_chooser_key\n")
        .arg(quote(pluginRoot_), quote(QStringLiteral("dzurkelchooser:") + executable_)).toUtf8();
}
bool ChooserIntegration::enabled() const
{
    if (QFileInfo(configuration_).isSymLink()) return false;
    QFile file(configuration_);
    return file.open(QIODevice::ReadOnly) && file.size() <= 16384 && file.readAll() == script();
}
QByteArray ChooserIntegration::disabledScript() const
{
    // User managers may survive logout. Strip only our entries from inherited
    // environments on the next login, retaining all unrelated plugin settings.
    return QStringLiteral("# Dzurkel managed KDE file-dialog opt-out, version 1\n"
        "dzurkel_chooser_path=%1\n"
        "dzurkel_chooser_rest=${QT_PLUGIN_PATH-}:\n"
        "dzurkel_chooser_new=\n"
        "while [ -n \"$dzurkel_chooser_rest\" ]; do\n"
        "  dzurkel_chooser_entry=${dzurkel_chooser_rest%%:*}\n"
        "  dzurkel_chooser_rest=${dzurkel_chooser_rest#*:}\n"
        "  if [ -n \"$dzurkel_chooser_entry\" ] && [ \"$dzurkel_chooser_entry\" != \"$dzurkel_chooser_path\" ]; then\n"
        "    dzurkel_chooser_new=\"${dzurkel_chooser_new:+$dzurkel_chooser_new:}$dzurkel_chooser_entry\"\n"
        "  fi\n"
        "done\n"
        "if [ -n \"$dzurkel_chooser_new\" ]; then export QT_PLUGIN_PATH=$dzurkel_chooser_new; else unset QT_PLUGIN_PATH; fi\n"
        "dzurkel_chooser_rest=${QT_QPA_GENERIC_PLUGINS-},\n"
        "dzurkel_chooser_new=\n"
        "while [ -n \"$dzurkel_chooser_rest\" ]; do\n"
        "  dzurkel_chooser_entry=${dzurkel_chooser_rest%%,*}\n"
        "  dzurkel_chooser_rest=${dzurkel_chooser_rest#*,}\n"
        "  case $dzurkel_chooser_entry in ''|dzurkelchooser|dzurkelchooser:*) ;;\n"
        "    *) dzurkel_chooser_new=\"${dzurkel_chooser_new:+$dzurkel_chooser_new,}$dzurkel_chooser_entry\" ;;\n"
        "  esac\n"
        "done\n"
        "if [ -n \"$dzurkel_chooser_new\" ]; then export QT_QPA_GENERIC_PLUGINS=$dzurkel_chooser_new; else unset QT_QPA_GENERIC_PLUGINS; fi\n"
        "unset dzurkel_chooser_path dzurkel_chooser_rest dzurkel_chooser_entry dzurkel_chooser_new\n")
        .arg(quote(pluginRoot_)).toUtf8();
}
bool ChooserIntegration::setEnabled(bool enable, QString &error)
{
    error.clear();
    if (QFileInfo(configuration_).isSymLink()) {
        error = QStringLiteral("The chooser session file is a symbolic link; it was not changed."); return false;
    }
    if (QFileInfo::exists(configuration_)) {
        QFile file(configuration_);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 16384) { error = QStringLiteral("Cannot read the chooser session setup."); return false; }
        const auto existing = file.readAll();
        if (existing != script() && existing != disabledScript()) {
            error = QStringLiteral("The chooser session file has other content; it was not changed."); return false;
        }
    }
    if (enable && !available(error)) return false;
    if (!QDir().mkpath(QFileInfo(configuration_).absolutePath())) {
        error = QStringLiteral("Cannot create the Plasma session configuration directory."); return false;
    }
    QSaveFile output(configuration_);
    if (!output.open(QIODevice::WriteOnly)) { error = output.errorString(); return false; }
    output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const auto bytes = enable ? script() : disabledScript();
    if (output.write(bytes) != bytes.size() || !output.commit()) { error = output.errorString(); return false; }
    return true;
}
}
