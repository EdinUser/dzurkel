#pragma once
#include <QtGlobal>
#include <QString>

namespace dzurkel {
// Private pipe protocol. Raster bytes are tightly packed RGBA8888, never an
// image format that the GUI process must decode.
inline constexpr qint64 PdfMaxPixels = 16 * 1024 * 1024;
inline constexpr qint64 PdfMaxBody = PdfMaxPixels * 4;
inline constexpr int PdfMaxEdge = 8192;
inline constexpr int PdfMaxHeader = 64 * 1024;
int servePdf(const QString &path);
}
