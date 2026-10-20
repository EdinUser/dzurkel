#include <QApplication>
#include <QDialog>
#include <QLineEdit>
#include <QTest>
#include <QTimer>
#include <QVBoxLayout>
int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    QDialog dialog;
    auto *layout = new QVBoxLayout(&dialog);
    auto *editor = new QLineEdit(&dialog);
    layout->addWidget(editor);
    dialog.show();
    int result = 1;
    QTimer::singleShot(100, &dialog, [&] {
        QTest::keyClick(editor, Qt::Key_Space);
        result = editor->text() == QLatin1String(" ") ? 0 : 2;
        dialog.reject();
    });
    application.exec();
    return result;
}
