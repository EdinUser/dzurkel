#include "DocumentReaderInternal.h"
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>

namespace dzurkel::document {
void readLegacyDoc(Context &c, const QByteArray &bytes)
{
    if (!bytes.startsWith(QByteArray::fromHex("d0cf11e0a1b11ae1"))) c.invalid();
    const auto program = QStandardPaths::findExecutable(QStringLiteral("antiword"));
    if (program.isEmpty()) throw Stop{QStringLiteral("Legacy DOC text preview requires the optional antiword extractor.")};
    QTemporaryFile snapshot;
    if (!snapshot.open() || snapshot.write(bytes) != bytes.size() || !snapshot.flush())
        throw Stop{QStringLiteral("Cannot prepare the document for text extraction.")};
    QProcess process;
    process.setProgram(program);
    process.setArguments({QStringLiteral("-m"), QStringLiteral("UTF-8.txt"), QStringLiteral("-w"), QStringLiteral("0"), snapshot.fileName()});
    process.setStandardInputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    process.start();
    if (!process.waitForStarted(1000)) throw Stop{QStringLiteral("The legacy DOC text extractor could not be started.")};
    QByteArray output;
    bool partial = false;
    try {
        while (process.state() != QProcess::NotRunning || process.bytesAvailable()) {
            c.check();
            process.waitForReadyRead(20);
            const auto data = process.read(1024 * 1024 + 1 - output.size());
            output += data;
            if (output.size() > 1024 * 1024) { partial = true; process.kill(); break; }
        }
    } catch (...) {
        process.kill(); process.waitForFinished(1000); throw;
    }
    if (!process.waitForFinished(1000) && process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(1000); }
    if (!partial && (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)) c.invalid();
    c.notice(QStringLiteral("Text-only preview; document formatting omitted."));
    QString text = QString::fromUtf8(output);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QChar('\r'), QChar('\n'));
    for (const auto &line : text.split(QChar('\n'))) {
        c.text(line); c.markup(QStringLiteral("<br/>")); c.paragraph();
    }
    if (partial) throw Stop{{}, false, true};
}
}
