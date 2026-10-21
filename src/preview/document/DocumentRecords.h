#pragma once
#include "preview/PreviewProvider.h"
#include <QJsonObject>
#include <functional>
namespace dzurkel {
bool writeDocumentRecords(const PreviewData &data, const std::function<bool(const QJsonObject &)> &send);
bool decodeDocumentPortion(const QJsonObject &content, std::shared_ptr<const FormattedDocument> &document);
}
