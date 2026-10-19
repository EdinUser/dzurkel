#include "RuntimeCompatibility.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace dzurkel {
namespace {
QString requirements(const QJsonObject &values)
{
    return QStringLiteral("Qt %1 or newer, Qt PDF %2 or newer, libzip %3 or newer")
        .arg(values.value(QStringLiteral("qt_base")).toString(),
             values.value(QStringLiteral("qt_pdf")).toString(),
             values.value(QStringLiteral("libzip")).toString());
}
bool completeRequirements(const QJsonObject &values)
{
    for (const auto &key : {"qt_base", "qt_pdf", "libzip"})
        if (values.value(QLatin1String(key)).toString().isEmpty()) return false;
    return true;
}
QString distributionName(const QString &id)
{
    if (id == QLatin1String("debian")) return QStringLiteral("Debian");
    if (id == QLatin1String("fedora")) return QStringLiteral("Fedora");
    if (id == QLatin1String("arch")) return QStringLiteral("Arch Linux");
    if (id == QLatin1String("rhel")) return QStringLiteral("Red Hat Enterprise Linux");
    return id;
}
}
RuntimeCompatibility RuntimeCompatibility::fromReport(const QByteArray &json)
{
    RuntimeCompatibility result;
    result.summary = QStringLiteral("OS compatibility report unavailable for this build.");
    result.details = QStringLiteral("The compatibility report is missing, unreadable or does not match this Dzurkel version. "
        "Reinstall the matching application package to restore it.\n\n"
        "The source baseline is Qt %1, Qt PDF %1 and libzip %2 or newer. "
        "A downloaded package can require newer versions; use a package built for your distribution and release.")
        .arg(QStringLiteral(DZURKEL_MIN_QT), QStringLiteral(DZURKEL_MIN_LIBZIP));
    const auto report = QJsonDocument::fromJson(json).object();
    const auto baseline = report.value(QStringLiteral("baseline_requirements")).toObject();
    const auto artifact = report.value(QStringLiteral("artifact_requirements")).toObject();
    const auto architecture = report.value(QStringLiteral("artifact")).toObject().value(QStringLiteral("architecture")).toString();
    if (report.value(QStringLiteral("schema_version")).toInt() != 1
        || report.value(QStringLiteral("dzurkel_version")).toString() != QLatin1String(DZURKEL_VERSION)
        || !completeRequirements(baseline) || !completeRequirements(artifact) || architecture.isEmpty()
        || !report.value(QStringLiteral("artifact_os_candidates")).isArray()) return result;
    result.reportAvailable = true;
    result.summary = QStringLiteral("This build (%1): %2.").arg(architecture, requirements(artifact));
    QStringList details{
        QStringLiteral("Dzurkel %1 — %2").arg(QStringLiteral(DZURKEL_VERSION), architecture),
        result.summary,
        QStringLiteral("OS compatibility for this build")
    };
    QStringList minimums, candidates;
    for (const auto &entry : report.value(QStringLiteral("artifact_os_candidates")).toArray()) {
        const auto distribution = entry.toObject();
        const auto name = distributionName(distribution.value(QStringLiteral("distribution")).toString());
        const auto supported = distribution.value(QStringLiteral("supported_minimum")).toString();
        const auto candidate = distribution.value(QStringLiteral("earliest_reviewed_candidate")).toString();
        QString status;
        if (!supported.isEmpty()) {
            status = QStringLiteral("validated minimum: %1").arg(supported);
            minimums.append(name + QChar(' ') + supported);
        } else if (!candidate.isEmpty()) {
            status = QStringLiteral("earliest reviewed dependency candidate: %1; package and desktop validation pending").arg(candidate);
            candidates.append(name + QChar(' ') + candidate);
        }
        else status = QStringLiteral("no minimum established for this build");
        QString text = name + QStringLiteral(": ") + status + QStringLiteral(".");
        const auto coverage = distribution.value(QStringLiteral("coverage")).toString();
        if (!coverage.isEmpty()) text += QChar('\n') + coverage;
        if (candidate.isEmpty()) {
            for (const auto &release : distribution.value(QStringLiteral("releases")).toArray()) {
                const auto item = release.toObject();
                QStringList reasons;
                for (const auto &reason : item.value(QStringLiteral("reasons")).toArray()) {
                    auto text = reason.toString();
                    text.replace(QStringLiteral("qt_base"), QStringLiteral("Qt"));
                    text.replace(QStringLiteral("qt_pdf"), QStringLiteral("Qt PDF"));
                    text.replace(QStringLiteral("coinstallable package set not established"),
                        QStringLiteral("a compatible package set has not been verified"));
                    reasons.append(text);
                }
                if (!reasons.isEmpty()) text += QStringLiteral("\n%1: %2.")
                    .arg(item.value(QStringLiteral("release")).toString(), reasons.join(QStringLiteral("; ")));
            }
        }
        details.append(text);
    }
    if (!minimums.isEmpty()) result.summary += QStringLiteral("\nValidated OS minimums: %1.").arg(minimums.join(QStringLiteral(", ")));
    if (!candidates.isEmpty()) result.summary += QStringLiteral("\nReviewed OS candidates: %1 (validation pending).")
        .arg(candidates.join(QStringLiteral(", ")));
    if (minimums.isEmpty() && candidates.isEmpty()) result.summary += QStringLiteral("\nOS minimum not established.");
    details.append(QStringLiteral("Dependency candidates apply only to the reviewed releases, repositories and architecture. "
        "They do not establish support for untested releases or desktops. Install the package intended for your system; "
        "your package manager checks its runtime dependencies."));
    details.append(QStringLiteral("Minimum for building from source\n%1.\n"
        "This source baseline does not mean this binary runs on every system meeting it.").arg(requirements(baseline)));
    result.details = details.join(QStringLiteral("\n\n"));
    return result;
}
RuntimeCompatibility RuntimeCompatibility::load()
{
    const QDir application(QCoreApplication::applicationDirPath());
    const QStringList paths{
        application.filePath(QStringLiteral("build-compatibility.json")),
        application.filePath(QStringLiteral(DZURKEL_COMPATIBILITY_INSTALL_PATH))
    };
    for (const auto &path : paths) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) continue;
        auto result = fromReport(file.read(1024 * 1024));
        if (result.reportAvailable) return result;
    }
    return fromReport({});
}
}
