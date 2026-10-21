#pragma once
#include "preview/StructuredDecodeJob.h"
namespace dzurkel {
using XlsxDecodeJob = StructuredDecodeJob;
inline constexpr qint64 XlsxRecordLimit = StructuredRecordLimit;
inline constexpr qint64 XlsxTransferLimit = StructuredTransferLimit;
int serveXlsx(const QString &path);
}
