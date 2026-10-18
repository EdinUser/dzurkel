#include "resources/media/MediaSourceHelper.h"
#include <QCoreApplication>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const auto arguments = application.arguments();
    if (arguments.size() != 3) return 2;
    return dzurkel::serveMediaSource(arguments.at(1), arguments.at(2).toInt());
}
