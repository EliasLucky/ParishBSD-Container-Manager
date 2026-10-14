#include <QApplication>
#include <QMessageBox>
#include <QLocale>
#include <QTranslator>

#include "MainWindow.h"

static void setApplicationMetadata()
{
	QApplication::setApplicationName(QStringLiteral("parish-compartments"));
	QApplication::setApplicationDisplayName(QStringLiteral("ParishBSD Compartment Manager"));
	QApplication::setOrganizationName(QStringLiteral("ParishBSD"));
	QApplication::setOrganizationDomain(QStringLiteral("parishbsd.org"));
	QApplication::setApplicationVersion(QStringLiteral("1.0.0"));
}

static void loadTranslations(QApplication &app)
{
	const QString locale = QLocale::system().name();
	if (locale.startsWith(QStringLiteral("en")))
		return;

	auto *translator = new QTranslator(&app);
	const QString path = QStringLiteral("/usr/local/share/parish-compartments/i18n");
	if (translator->load(QStringLiteral("parish-compartments_") + locale, path)) {
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

	MainWindow w;
	//w.resize(900,600);
	w.show();

	return app.exec();
}
