#include "controller/PreviewController.h"
#include "preview/pdf/PdfSession.h"
#include "preview/pdf/PdfView.h"
#include "preview/PreviewRegistry.h"
#include "resources/ResourcePreparation.h"
#include "../support/OffscreenWarnings.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QPainter>
#include <QPdfWriter>
#include <QPointingDevice>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>

using namespace dzurkel;

class PdfTest : public QObject {
    Q_OBJECT
private:
    std::unique_ptr<OffscreenWarnings> warnings_;
    QTemporaryDir directory_;
    QString pdf_;
    QString script(const QByteArray &body)
    {
        const auto path = directory_.filePath(QStringLiteral("helper"));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return {};
        file.write("#!/usr/bin/python3\nimport sys, time, json\n" + body);
        file.close();
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return path;
    }
    static void wheel(QWidget *target, int angle, int pixel = 0, Qt::ScrollPhase phase = Qt::NoScrollPhase,
                      Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        // Native Wayland's primary device can be a touchpad. Specify a mouse
        // for discrete notch tests rather than inheriting the host hardware.
        static const QPointingDevice mouse(QStringLiteral("PDF test mouse"), 42,
            QInputDevice::DeviceType::Mouse, QPointingDevice::PointerType::Generic,
            QInputDevice::Capability::Position, 1, 3);
        QWheelEvent event(QPointF(20, 20), target->mapToGlobal(QPoint(20, 20)), QPoint(0, pixel), QPoint(0, angle),
                          Qt::NoButton, modifiers, phase, false, Qt::MouseEventNotSynthesized, &mouse);
        QApplication::sendEvent(target, &event);
    }
private slots:
    void initTestCase()
    {
        warnings_ = std::make_unique<OffscreenWarnings>();
        QVERIFY(directory_.isValid());
        qputenv("XDG_CONFIG_HOME", directory_.filePath(QStringLiteral("config")).toUtf8());
        pdf_ = directory_.filePath(QStringLiteral("pages.pdf"));
        QPdfWriter writer(pdf_);
        writer.setResolution(72);
        QPainter painter(&writer);
        for (int page = 0; page < 3; ++page) {
            if (page == 2) writer.setPageSize(QPageSize(QSizeF(800, 400), QPageSize::Point));
            if (page) writer.newPage();
            painter.fillRect(0, 0, 400, 500, page == 0 ? Qt::blue : page == 1 ? Qt::red : Qt::green);
        }
    }
    void cleanup()
    {
        QTRY_COMPARE(PdfSession::runningHelpers(), 0);
    }
    void opensLargeManyPageSourceWithoutSnapshot()
    {
        // A valid sparse PDF with an unreferenced binary stream. Its xref is
        // at EOF, so first-page access must seek over 256 MiB of source data.
        const auto path = directory_.filePath(QStringLiteral("large.pdf"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("%PDF-1.4\n");
        constexpr int pages = 10001;
        QList<qint64> offsets;
        auto object = [&](const QByteArray &body) {
            offsets.append(file.pos());
            file.write(QByteArray::number(offsets.size()) + " 0 obj\n" + body + "\nendobj\n");
        };
        object("<< /Type /Catalog /Pages 2 0 R >>");
        QByteArray kids;
        for (int page = 0; page < pages; ++page) kids += QByteArray::number(page + 3) + " 0 R ";
        object("<< /Type /Pages /Count " + QByteArray::number(pages) + " /Kids [" + kids + "] >>");
        for (int page = 0; page < pages; ++page)
            object("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Contents "
                + QByteArray::number(pages + 3) + " 0 R >>");
        const QByteArray content("0 0 1 rg 0 0 600 800 re f\n");
        object("<< /Length " + QByteArray::number(content.size()) + " >>\nstream\n" + content + "endstream");
        offsets.append(file.pos());
        constexpr qint64 padding = 256 * 1024 * 1024;
        file.write(QByteArray::number(offsets.size()) + " 0 obj\n<< /Length " + QByteArray::number(padding) + " >>\nstream\n");
        QVERIFY(file.seek(file.pos() + padding));
        file.write("\nendstream\nendobj\n");
        const auto xref = file.pos();
        file.write("xref\n0 " + QByteArray::number(offsets.size() + 1) + "\n0000000000 65535 f \n");
        for (auto offset : offsets) file.write(QByteArray::number(offset).rightJustified(10, '0') + " 00000 n \n");
        file.write("trailer\n<< /Size " + QByteArray::number(offsets.size() + 1)
            + " /Root 1 0 R >>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n");
        file.close();
        FileResource original;
        QString error;
        QVERIFY(parseFileResource(path, original, error));
        ResourcePreparation preparation;
        bool prepared = false;
        FileResource resource;
        connect(&preparation, &ResourcePreparation::ready, this, [&](const FileResource &result) { resource = result; prepared = true; });
        preparation.start(original);
        QTRY_VERIFY(prepared);
        QVERIFY(!resource.informationOnly);
        QVERIFY(resource.seekableSource);
        QVERIFY(!resource.backingStore);
        QCOMPARE(resource.backingPath, path);
        auto data = PreviewRegistry::load(resource, QSize(400, 400), std::make_shared<std::atomic_bool>(false));
        QCOMPARE(data.kind, PreviewKind::Pdf);
        PdfSession session(data.resource);
        QSignalSpy frames(&session, &PdfSession::frameReady);
        QSignalSpy failures(&session, &PdfSession::failed);
        session.request(0, 400);
        QTRY_VERIFY_WITH_TIMEOUT(!frames.isEmpty() || !failures.isEmpty(), 10000);
        QVERIFY2(failures.isEmpty(), failures.isEmpty() ? "" : qPrintable(failures.first().first().toString()));
        QCOMPARE(session.pageCount(), pages);
        const auto image = qvariant_cast<QImage>(frames.first().at(2));
        QVERIFY(!image.isNull());
        QVERIFY(image.pixelColor(image.width() / 2, image.height() / 2).blue() > 200);
        session.request(pages - 1, 400);
        QTRY_COMPARE_WITH_TIMEOUT(frames.size(), 2, 10000);
        QCOMPARE(frames.last().first().toInt(), pages - 1);
        session.stop();
        QTRY_COMPARE(PdfSession::runningHelpers(), 0);
        // A source can disappear after preparation. The worker must still
        // hand off the candidate without opening it; the PDF helper reports
        // the failure asynchronously.
        QVERIFY(QFile::remove(path));
        data = PreviewRegistry::load(resource, QSize(400, 400), std::make_shared<std::atomic_bool>(false));
        QCOMPARE(data.kind, PreviewKind::Pdf);
        PdfSession missing(data.resource);
        QSignalSpy missingFailed(&missing, &PdfSession::failed);
        missing.request(0, 400);
        QTRY_COMPARE(missingFailed.size(), 1);
    }
    void rendersOnlyRequestedPageAndKeepsWindowSize()
    {
        PreviewController controller;
        QString error;
        QVERIFY(controller.show(pdf_, error));
        QTRY_VERIFY(!controller.loading());
        auto *view = controller.window()->findChild<PdfView *>();
        QVERIFY(view);
        QCOMPARE(view->currentPage(), 0);
        QCOMPARE(view->session()->pageCount(), 3);
        QSignalSpy frames(view->session(), &PdfSession::frameReady);
        QTRY_VERIFY(!view->loading());
        const auto size = controller.window()->size();
        view->goToPage(2);
        QTRY_COMPARE(view->currentPage(), 2);
        QCOMPARE(controller.window()->size(), size);
        QCOMPARE(frames.last().at(0).toInt(), 2);
        const auto image = qvariant_cast<QImage>(frames.last().at(2));
        QVERIFY(image.sizeInBytes() <= 64 * 1024 * 1024);
        QVERIFY(image.width() <= 8192 && image.height() <= 8192);
        QVERIFY(image.width() > image.height());
        controller.hide();
    }
    void edgeScrollNeedsAnotherEventAndPreviousPageEntersAtBottom()
    {
        PreviewController controller;
        QString error;
        QVERIFY(controller.show(pdf_, error));
        QTRY_VERIFY(!controller.loading());
        auto *view = controller.window()->findChild<PdfView *>();
        QVERIFY(view);
        auto *window = const_cast<PreviewWindow *>(controller.window());
        window->resize(500, 420);
        auto *scroll = view->findChild<QScrollArea *>();
        QTRY_VERIFY(!view->loading());
        auto *bar = scroll->verticalScrollBar();
        QTRY_VERIFY(bar->maximum() > 0);
        bar->setValue(bar->maximum() - 1);
        wheel(scroll->viewport(), -120);
        QCOMPARE(view->currentPage(), 0);
        QCOMPARE(bar->value(), bar->maximum());
        wheel(scroll->viewport(), -120);
        // An additional event during rendering must not queue a second hop.
        wheel(scroll->viewport(), -120);
        QTRY_COMPARE(view->currentPage(), 1);
        QTRY_COMPARE(bar->value(), 0);
        QTRY_VERIFY(!view->loading());
        wheel(scroll->viewport(), 120);
        QTRY_COMPARE(view->currentPage(), 0);
        QTRY_COMPARE(bar->value(), bar->maximum());
        bar->setValue(0);
        wheel(scroll->viewport(), 120);
        QCOMPARE(view->currentPage(), 0);
        view->goToPage(2);
        QTRY_COMPARE(view->currentPage(), 2);
        bar->setValue(bar->maximum());
        wheel(scroll->viewport(), -120);
        QCOMPARE(view->currentPage(), 2);
        controller.hide();
    }
    void touchpadMomentumCannotSkipPages()
    {
        PreviewController controller;
        QString error;
        QVERIFY(controller.show(pdf_, error));
        QTRY_VERIFY(!controller.loading());
        auto *view = controller.window()->findChild<PdfView *>();
        QVERIFY(view);
        auto *scroll = view->findChild<QScrollArea *>();
        QTRY_VERIFY(!view->loading());
        auto *bar = scroll->verticalScrollBar();
        bar->setValue(bar->maximum());
        wheel(scroll->viewport(), 0, 0, Qt::ScrollBegin);
        wheel(scroll->viewport(), 0, -20, Qt::ScrollUpdate);
        QCOMPARE(view->currentPage(), 0);
        wheel(scroll->viewport(), 0, -20, Qt::ScrollUpdate);
        QTRY_COMPARE(view->currentPage(), 1);
        QTRY_VERIFY(!view->loading());
        bar->setValue(bar->maximum());
        for (int i = 0; i < 5; ++i) wheel(scroll->viewport(), 0, -100, Qt::ScrollMomentum);
        wheel(scroll->viewport(), 0, -100, Qt::ScrollUpdate);
        QCOMPARE(view->currentPage(), 1);
        wheel(scroll->viewport(), 0, 0, Qt::ScrollEnd);
        wheel(scroll->viewport(), 0, 0, Qt::ScrollBegin);
        wheel(scroll->viewport(), 0, -40, Qt::ScrollUpdate);
        QTRY_COMPARE(view->currentPage(), 2);
        controller.hide();
    }
    void fittedPagesAndKeyboardNavigationKeepZoomControls()
    {
        PreviewController controller;
        QString error;
        QVERIFY(controller.show(pdf_, error));
        QTRY_VERIFY(!controller.loading());
        auto *view = controller.window()->findChild<PdfView *>();
        QVERIFY(view);
        auto *scroll = view->findChild<QScrollArea *>();
        QTRY_VERIFY(!view->loading());
        view->zoomBy(-100);
        QTRY_VERIFY(!view->loading());
        QCOMPARE(scroll->verticalScrollBar()->maximum(), 0);
        wheel(scroll->viewport(), -120);
        QTRY_COMPARE(view->currentPage(), 1);
        QTest::keyClick(scroll, Qt::Key_PageUp);
        QTRY_COMPARE(view->currentPage(), 0);
        const auto oldZoom = view->zoomFactor();
        wheel(scroll->viewport(), 120, 0, Qt::NoScrollPhase, Qt::ControlModifier);
        QVERIFY(view->zoomFactor() > oldZoom);
        QCOMPARE(view->currentPage(), 0);
        controller.hide();
    }
    void phaseLessFineWheelUsesIdleGestureBoundary()
    {
        PreviewController controller;
        QString error;
        QVERIFY(controller.show(pdf_, error));
        QTRY_VERIFY(!controller.loading());
        auto *view = controller.window()->findChild<PdfView *>();
        QVERIFY(view);
        auto *scroll = view->findChild<QScrollArea *>();
        QTRY_VERIFY(!view->loading());
        auto *bar = scroll->verticalScrollBar();
        bar->setValue(bar->maximum());
        for (int i = 0; i < 8; ++i) wheel(scroll->viewport(), -15);
        QTRY_COMPARE(view->currentPage(), 1);
        QTRY_VERIFY(!view->loading());
        QCoreApplication::processEvents();
        bar->setValue(bar->maximum());
        for (int i = 0; i < 12; ++i) wheel(scroll->viewport(), -15);
        QCOMPARE(view->currentPage(), 1);
        QTest::qWait(270);
        for (int i = 0; i < 8; ++i) wheel(scroll->viewport(), -15);
        QTRY_COMPARE(view->currentPage(), 2);
        controller.hide();
    }
    void followsInternalLinksWithoutAHostPdfDocument()
    {
        const QList<QByteArray> objects = {
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Contents 4 0 R /Annots [7 0 R] >>",
            "<< /Length 0 >>\nstream\n\nendstream",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Contents 6 0 R >>",
            "<< /Length 0 >>\nstream\n\nendstream",
            "<< /Type /Annot /Subtype /Link /Rect [20 700 200 760] /Dest [5 0 R /XYZ 0 800 0] >>"
        };
        QByteArray bytes("%PDF-1.4\n");
        QList<qint64> offsets;
        for (int i = 0; i < objects.size(); ++i) {
            offsets.append(bytes.size());
            bytes += QByteArray::number(i + 1) + " 0 obj\n" + objects.at(i) + "\nendobj\n";
        }
        const auto xref = bytes.size();
        bytes += "xref\n0 8\n0000000000 65535 f \n";
        for (const auto offset : offsets) bytes += QByteArray::number(offset).rightJustified(10, '0') + " 00000 n \n";
        bytes += "trailer\n<< /Size 8 /Root 1 0 R >>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
        const auto path = directory_.filePath(QStringLiteral("links.pdf"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size());
        file.close();
        PreviewController controller;
        QString error;
        QVERIFY(controller.show(path, error));
        QTRY_VERIFY(!controller.loading());
        auto *view = controller.window()->findChild<PdfView *>();
        QVERIFY(view);
        QTRY_VERIFY(!view->loading());
        QVERIFY(!view->session()->links().isEmpty());
        const auto link = view->session()->links().first();
        QCOMPARE(link.page, 1);
        auto *canvas = view->findChild<QWidget *>(QStringLiteral("pdfPage"));
        const auto center = link.rectangle.center();
        const QPoint hit(qRound(center.x() * canvas->width() / 600), qRound(center.y() * canvas->height() / 800));
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, hit);
        QTRY_COMPARE(view->currentPage(), 1);
        controller.hide();
    }
    void rejectsMalformedFrames_data()
    {
        QTest::addColumn<QByteArray>("output");
        QTest::newRow("oversized-header") << QByteArray(65537, 'x');
        QTest::newRow("oversized-image") << QByteArray("{\"id\":1,\"page\":0,\"pages\":1,\"width\":8192,\"height\":8192,\"bytes\":268435456,\"pointWidth\":600,\"pointHeight\":800}\n");
        QTest::newRow("wrong-id") << QByteArray("{\"id\":7,\"error\":\"bad\"}\n");
        QTest::newRow("truncated-frame") << QByteArray("{\"id\":1,\"page\":0,\"pages\":1,\"width\":100,\"height\":100,\"bytes\":40000,\"pointWidth\":600,\"pointHeight\":800}\nshort");
    }
    void rejectsMalformedFrames()
    {
        QFETCH(QByteArray, output);
        const auto program = script("sys.stdin.readline()\nsys.stdout.buffer.write(bytes.fromhex('" + output.toHex() + "'))\nsys.stdout.flush()\n");
        FileResource resource;
        resource.path = pdf_;
        PdfSession session(resource, program);
        QSignalSpy frames(&session, &PdfSession::frameReady);
        QSignalSpy failures(&session, &PdfSession::failed);
        session.request(0, 400);
        QTRY_COMPARE(failures.size(), 1);
        QCOMPARE(frames.size(), 0);
    }
    void stalledHelperDoesNotBlockCancellationAndRetainsStaging()
    {
        const auto program = script("sys.stdin.readline()\ntime.sleep(10)\n");
        FileResource resource;
        resource.path = pdf_;
        resource.backingStore = std::make_shared<QTemporaryDir>();
        const auto staged = resource.backingStore->path();
        auto session = std::make_unique<PdfSession>(resource, program);
        session->request(0, 400);
        resource.backingStore.reset();
        QTest::qWait(100);
        QElapsedTimer elapsed;
        elapsed.start();
        session.reset();
        QVERIFY(elapsed.elapsed() < 100);
        QVERIFY(QFile::exists(staged));
        QTRY_COMPARE(PdfSession::runningHelpers(), 0);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QTRY_VERIFY(!QFile::exists(staged));
    }
    void timesOutAndBoundsConcurrentHelpers()
    {
        const auto program = script("sys.stdin.readline()\ntime.sleep(10)\n");
        FileResource resource;
        resource.path = pdf_;
        PdfSession first(resource, program, 150);
        PdfSession second(resource, program, 150);
        PdfSession third(resource, program, 150);
        QSignalSpy firstFailed(&first, &PdfSession::failed);
        QSignalSpy thirdFailed(&third, &PdfSession::failed);
        first.request(0, 400);
        second.request(0, 400);
        third.request(0, 400);
        QCOMPARE(PdfSession::runningHelpers(), 2);
        QTRY_COMPARE(thirdFailed.size(), 1);
        QTRY_COMPARE(firstFailed.size(), 1);
        QVERIFY(firstFailed.first().first().toString().contains(QStringLiteral("timed out")));
    }
    void coalescesRequestsAndDropsObsoleteFrames()
    {
        const auto program = script(
            "for line in sys.stdin:\n"
            "    request = json.loads(line)\n"
            "    time.sleep(0.1)\n"
            "    frame = dict(id=request['id'], page=request['page'], pages=100, width=1, height=1, bytes=4, pointWidth=600, pointHeight=800)\n"
            "    sys.stdout.buffer.write(json.dumps(frame).encode() + b'\\n' + bytes([0,0,255,255]))\n"
            "    sys.stdout.flush()\n");
        FileResource resource;
        resource.path = pdf_;
        PdfSession session(resource, program);
        QSignalSpy frames(&session, &PdfSession::frameReady);
        session.request(0, 400);
        QTRY_VERIFY(session.busy());
        for (int page = 1; page < 50; ++page) session.request(page, 400);
        QTRY_COMPARE(frames.size(), 1);
        QCOMPARE(frames.first().first().toInt(), 49);
        QVERIFY(!session.busy());
        session.stop();
    }
};
QTEST_MAIN(PdfTest)
#include "PdfTest.moc"
