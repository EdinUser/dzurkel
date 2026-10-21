#include "DocumentReaderInternal.h"
#include <QProcess>
#include "DocumentLines.h"
#include <poll.h>
#include <cerrno>
#include <QStandardPaths>
#include <QFile>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/prctl.h>

namespace dzurkel::document {
namespace {
void startExtractor(Context &c, QFile &file, QProcess &process, const QString &program, int outputFd = -1)
{
    c.check();
    if (outputFd >= 0) process.setStandardOutputFile(QProcess::nullDevice());
    process.setProgram(program);
    process.setArguments({QStringLiteral("-m"), QStringLiteral("UTF-8.txt"), QStringLiteral("-w"), QStringLiteral("0"), QStringLiteral("/proc/self/fd/0")});
    process.setStandardInputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    const auto descriptor = file.handle();
    const auto parent = ::getpid();
    process.setChildProcessModifier([descriptor, parent, outputFd] {
        if (::dup2(descriptor, STDIN_FILENO) < 0 || ::fcntl(STDIN_FILENO, F_SETFD, 0) < 0) ::_exit(127);
        if (outputFd >= 0 && ::dup2(outputFd, STDOUT_FILENO) < 0) ::_exit(127);
        if (::prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 || ::getppid() != parent) ::_exit(127);
    });
    process.start();
    if (!process.waitForStarted(1000)) throw Stop{QStringLiteral("The legacy DOC text extractor could not be started.")};
}
class ExtractorInput final : public QIODevice {
    Context &c;
    QProcess process;
    int reader_ = -1;
public:
    ExtractorInput(Context &context, QFile &file) : c(context) {
        if (!file.peek(8).startsWith(QByteArray::fromHex("d0cf11e0a1b11ae1"))) c.invalid();
        const auto program = QStandardPaths::findExecutable(QStringLiteral("antiword"));
        if (program.isEmpty()) throw Stop{QStringLiteral("Legacy DOC text preview requires the optional antiword extractor.")};
        int descriptors[2];
        if (::pipe2(descriptors, O_CLOEXEC)) c.invalid();
        reader_ = descriptors[0];
        ::fcntl(reader_, F_SETFL, O_NONBLOCK);
        try { startExtractor(c, file, process, program, descriptors[1]); }
        catch (...) { ::close(descriptors[1]); ::close(reader_); reader_ = -1; throw; }
        ::close(descriptors[1]);
        open(QIODevice::ReadOnly);
    }
    ~ExtractorInput() override {
        if (reader_ >= 0) ::close(reader_);
        if (process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(1000); }
    }
    bool isSequential() const override { return true; }
    bool atEnd() const override { return reader_ < 0 && QIODevice::atEnd(); }
    qint64 readData(char *data, qint64 size) override {
        if (reader_ < 0 || size <= 0) return 0;
        for (;;) {
            c.check();
            if (c.sourceRead >= 64 * 1024 * 1024) c.limit();
            const auto count = ::read(reader_, data, size_t(qMin(size, qint64(4096))));
            if (count > 0) { c.sourceRead += count; return count; }
            if (!count) {
                process.waitForFinished(1000);
                if (process.state() != QProcess::NotRunning || process.exitStatus() != QProcess::NormalExit || process.exitCode()) c.invalid();
                ::close(reader_); reader_ = -1; return 0;
            }
            if (errno != EAGAIN && errno != EINTR) c.invalid();
            pollfd descriptor{reader_, POLLIN | POLLHUP, 0};
            ::poll(&descriptor, 1, 20);
        }
    }
    qint64 writeData(const char *, qint64) override { return -1; }
};
class LegacyParser final : public PortionParser {
    Context &c;
    ExtractorInput input;
    DocumentLines lines;
public:
    LegacyParser(Context &context, QFile &file) : c(context), input(context, file), lines(context, input, true) {}
    bool next() override {
        c.notice(QStringLiteral("Text-only preview; document formatting omitted."));
        const auto line = lines.next();
        if (!line) return false;
        c.text(*line); c.markup(QStringLiteral("<br/>")); c.paragraph();
        return true;
    }
};
}
std::unique_ptr<PortionParser> legacyDocParser(Context &c, QFile &file) { return std::make_unique<LegacyParser>(c, file); }
void readLegacyDoc(Context &c, QFile &file)
{
    if (!file.peek(8).startsWith(QByteArray::fromHex("d0cf11e0a1b11ae1"))) c.invalid();
    const auto program = QStandardPaths::findExecutable(QStringLiteral("antiword"));
    if (program.isEmpty()) throw Stop{QStringLiteral("Legacy DOC text preview requires the optional antiword extractor.")};
    QProcess process;
    startExtractor(c, file, process, program);
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
