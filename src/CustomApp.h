#pragmaa once
#include <QString>

struct CustomApp {
	QString name; // display name
	QString version; // user-visible version
	QString installPath; // where it lives inside the jail (e.g. /opt/MyApp)
	QString launcherCommand; // how to run it
	QString sourceFile;
	QString installMethod;
	QString installDate; // ISO 8601
	QString iconName; // optional; falls back to parish-container-app
	
	QString metadataFile;

	bool isValid() const { return !name.isEmpty(); }
}
