#pragma once
#include <QWidget>
#include <algorithm>
#include <cmath>

namespace dzurkel {
// Provider widgets expose zoom without coupling the window to their renderer.
class ZoomPreview : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;
    virtual void zoomBy(int steps) = 0;
    virtual void fit() = 0;
    virtual qreal zoomFactor() const = 0;
    virtual bool isFit() const = 0;
    virtual qreal maximumZoom() const { return 8.0; }
    virtual qreal minimumZoom() const { return .1; }
    virtual QString zoomHint() const { return {}; }
    // Optional controls owned by the preview. The window hosts them in its
    // toolbar and returns them to the preview before replacing its content.
    virtual QWidget *toolbarWidget() const { return nullptr; }
    // Natural content size at 100%, when a provider supports window fitting.
    virtual QSize naturalSize() const { return {}; }
    virtual void scrollVertically(int direction) { Q_UNUSED(direction); }
protected:
    qreal steppedZoom(int steps) const
    {
        const auto current = zoomFactor();
        const auto maximum = maximumZoom();
        const auto minimum = std::min(minimumZoom(), maximum);
        if (!steps || (steps > 0 && current >= maximum) || (steps < 0 && current <= minimum)) return current;
        // Snap in the requested direction, then advance on a 10 percentage-point
        // grid. The tolerance avoids jumping over exact steps due to rounding.
        const auto grid = current * 10;
        const auto next = steps > 0 ? std::floor(grid + 1e-8) + steps : std::ceil(grid - 1e-8) + steps;
        return std::clamp(next / 10.0, minimum, maximum);
    }
signals:
    void zoomChanged();
};
}
