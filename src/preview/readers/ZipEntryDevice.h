#pragma once
#include <QIODevice>
#include <functional>
#include <zip.h>

namespace dzurkel {
class ZipEntryDevice final : public QIODevice {
public:
    ZipEntryDevice(zip_t *archive, zip_uint64_t index, qint64 &expanded, qint64 limit, std::function<void(const QString &)> fail) : expanded_(expanded), limit_(limit), fail_(std::move(fail)) {
        entry_ = zip_fopen_index(archive, index, 0);
        if (!entry_) fail_(QStringLiteral("Cannot open ZIP entry."));
        open(QIODevice::ReadOnly);
    }
    ~ZipEntryDevice() override { if (entry_) zip_fclose(entry_); }
    bool isSequential() const override { return true; }
    bool atEnd() const override { return !entry_ && QIODevice::atEnd(); }
protected:
    qint64 readData(char *data, qint64 size) override {
        if (!entry_ || size <= 0) return 0;
        if (expanded_ >= limit_) fail_(QStringLiteral("ZIP entry replay exceeds the per-request work budget."));
        const auto count = zip_fread(entry_, data, zip_uint64_t(qMin(size, limit_ - expanded_)));
        if (count < 0) fail_(QStringLiteral("Cannot read damaged ZIP entry."));
        expanded_ += count;
        if (!count) {
            const auto result = zip_fclose(entry_);
            entry_ = nullptr;
            if (result) fail_(QStringLiteral("Cannot read damaged ZIP entry."));
        }
        return count;
    }
    qint64 writeData(const char *, qint64) override { return -1; }
private:
    zip_file_t *entry_ = nullptr;
    qint64 &expanded_;
    qint64 limit_;
    std::function<void(const QString &)> fail_;
};
}
