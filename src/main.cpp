#include <QApplication>
#include <QMessageBox>

#include "MainWindow.h"

int main(int argc, char *argv[])
{
	QApplication app(argc, argv);
	QApplication::setApplicationName(QStringLiteral("parish-manager"));
	QApplication::setApplicationDisplayName(QStringLiteral("ParishBSD Container Manager"));
	QApplication::setOrganizationName(QStringLiteral("ParishBSD"));
	QApplication::setOrganizationDOmain(QStringLiteral("parishbsd.org"));
	QApplication::setApplicationVersion(QStringLiteral("1.0.0"));

	// The container manager runs parishctl which requires root.
	if (::geteuid() != 0) {
		QMessageBox::critical(nullptr, QObject::tr("Root privileges required"), QObject::tr("ParishBSD Container manager be run as root.\n\n"
		"Run it from the application, or use:\n"
		"    sudo parish-manager"));
		return 1;
	}

	MainWindow w;
	w.resize(900,600);
	w.show();

	return app.exec();
}
