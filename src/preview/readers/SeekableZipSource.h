#pragma once
#include "preview/PreviewProvider.h"
#include <QFile>
#include <QElapsedTimer>
#include <zip.h>
#include <cerrno>

namespace dzurkel {
// Bridge the already validated descriptor to libzip's public seekable-source
// API. libzip owns ZIP parsing; this adapter only bounds filesystem work.
struct SeekableZipSource {
    QFile &file;
    Cancellation cancel;
    const QElapsedTimer &clock;
    const qint64 size;
    static constexpr qint64 MetadataReadLimit = 4 * 1024 * 1024;
    static constexpr qint64 SourceReadLimit = 128 * 1024 * 1024;
    qint64 remaining = MetadataReadLimit;
    bool exceeded = false;
    zip_error_t error;
    SeekableZipSource(QFile &source, const Cancellation &cancellation, const QElapsedTimer &timer)
        : file(source), cancel(cancellation), clock(timer), size(source.size()) { zip_error_init(&error); }
    ~SeekableZipSource() { zip_error_fini(&error); }
    static zip_int64_t callback(void *context, void *data, zip_uint64_t length, zip_source_cmd_t command)
    {
        auto &input = *static_cast<SeekableZipSource *>(context);
        auto fail = [&](int code) -> zip_int64_t { zip_error_set(&input.error, code, EIO); return -1; };
        switch (command) {
        case ZIP_SOURCE_SUPPORTS:
            return ZIP_SOURCE_SUPPORTS_SEEKABLE | ZIP_SOURCE_MAKE_COMMAND_BITMASK(ZIP_SOURCE_ACCEPT_EMPTY);
        case ZIP_SOURCE_ACCEPT_EMPTY: return 0;
        case ZIP_SOURCE_ERROR: return zip_error_to_data(&input.error, data, length);
        case ZIP_SOURCE_CLOSE:
        case ZIP_SOURCE_FREE: return 0; // Caller owns QFile and this adapter.
        case ZIP_SOURCE_STAT: {
            if (length < sizeof(zip_stat_t) || input.size < 0) return fail(ZIP_ER_INVAL);
            auto *stat = static_cast<zip_stat_t *>(data);
            zip_stat_init(stat);
            stat->size = zip_uint64_t(input.size);
            stat->valid = ZIP_STAT_SIZE;
            return sizeof(zip_stat_t);
        }
        default: break;
        }
        if ((input.cancel && input.cancel->load()) || input.clock.elapsed() > 5000) return fail(ZIP_ER_READ);
        switch (command) {
        case ZIP_SOURCE_OPEN: return input.file.seek(0) ? 0 : fail(ZIP_ER_SEEK);
        case ZIP_SOURCE_TELL: return input.file.pos();
        case ZIP_SOURCE_SEEK: {
            const auto offset = zip_source_seek_compute_offset(zip_uint64_t(input.file.pos()),
                zip_uint64_t(input.size), data, length, &input.error);
            if (offset < 0) return -1;
            return input.file.seek(offset) ? 0 : fail(ZIP_ER_SEEK);
        }
        case ZIP_SOURCE_READ: {
            if (!length) return 0;
            if (length > zip_uint64_t(input.remaining)) { input.exceeded = true; return fail(ZIP_ER_READ); }
            const auto count = input.file.read(static_cast<char *>(data), qint64(length));
            if (count < 0 || input.file.error() != QFileDevice::NoError) return fail(ZIP_ER_READ);
            input.remaining -= count;
            return count;
        }
        default: return fail(ZIP_ER_OPNOTSUPP);
        }
    }
};

}
