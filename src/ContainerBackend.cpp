#include "ContainerBackend.h"

#include <QProcess>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QStandardPats>

#include <memory>

///////////////////////////////////////////////
/// Constructor
///////////////////////////////////////////////

ContainerBackend::ContainerBacked(QObject *parent) : QObject(parent)
{
	m_root = qEnvironmentVariable("PARISHBSD_ROOT", QStringLiteral("/usr/local/parishbsd"));
	m_parishctlPath = m_root + QStringLiteral("/parishctl/parishctl");
	m_jailsDir = QStringLiteral("/usr/local/jails");
	m_valtDataDir = QStringLiteral("/zrot/vault-data");
}

/////////////////////////////////////////////

QString ContainerBacked:runSync(const QString &program, const
