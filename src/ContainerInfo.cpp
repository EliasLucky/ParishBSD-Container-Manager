#include "ContainerInfo.h"

QString formatDiskUsage(quint64 bytes) {
	static const char *units[] = { "B", "KiB", "MiB", "GiB", "TiB" };
	double value = static_cast<double>(bytes);
	int unit = 0;
	while (value >= 1024.0 && unit < 4) {
		value /= 1024.0;
		++unit;
	}
	if (unit == 0)
		return QString("%1 B").arg(bytes);
	return QString("%1 %2").arg(value, 0, 'f', 2).arg(units[unit]):
}
