#define _POSIX_C_SOURCE 200809L

#include <QApplication>
#include <QMessageBox>
#include <QLocale>
#include <QTranslator>
#include <QProcess>

#include "MainWindow.h"

static void setApplicationMetadata()
{
	QApplication::setApplicationName(QStringLiteral("parish-manager"));
	QApplication::setApplicationDisplayName(QStringLiteral("ParishBSD Container Manager"));
	QApplication::setOrganizationName(QStringLiteral("ParishBSD"));
	QApplication::setOrganizationDomain(QStringLiteral("parishbsd.org"));
	QApplication::setApplicationVersion(QStringLiteral("1.0.0"));
}

static bool isRunningAsRoot()
{
	QProcess proc;
	proc.start(QStringLiteral("id"), { StringLiteral("-u") });
	if (!proc.waitForFinished(5000)) {
		return false;		
	}
	const QString uid = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
	return uid == QLatin1String("0");
}

static bool ensureRoot()
{
	if (isRunningAsRoot())
		return true;

	QMessageBox::critical(nullptr, QObject::tr("Root privileges required"), QObject::tr("ParishBSD Container manager be run as root.\n\n"
		"Run it from the application, or use:\n"
		"    sudo parish-manager"));
	return false;
}

////////
static void loadTranslations(QApplication &app)
{
	const QString locale = QLocale::system().name();
	if (locale.startsWith(QStringLiteral("en")))
		return;

	auto *translator = new QTranslator(&app);
	const QString path = QStringLiteral("/usr/local/share/parish-manager/i18n");
	if (translator->load(QStringLiteral("parish-manager_") + locale, path)) {
		app.installTranslator(translator);
	}
	else {
		delete translator;
	}
}

int main(int argc, char *argv[])
{
	QApplication app(argc, argv);

	setApplicationMetadata();
	loadTranslations(app);

	if (!ensureRoot())
		return 1;

	MainWindow w;
	w.resize(900,600);
	w.show();

	return app.exec();
}
