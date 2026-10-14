PORTNAME=	parish-compartments
DISTVERSION=	1.0.0
CATEGORIES=	sysutils

MAINTAINER=	maintainer@parishbsd.org
COMMENT=	ParishBSD container manager
WWW=		https://parishbsd.org/

LICENSE=	GPLv3
LICENSE_FILE=	${WRKSRC}/LICENSE

LIB_DEPENDS=	libQt6Core.so:devel/qt6-base \
		libQt6Widgets.so:devel/qt6-base \
		libQt6Network.so:devel/qt6-base

RUN_DEPENDS=	polkit>0:sysutils/polkit \
		lxqt-policykit>0:sysutils/lxqt-policykit

USES=		cmake compiler:c++20-lang pkgconfig qt-dist:6
USE_QT=		base tools

CMAKE_ARGS=	-DCMAKE_BUILD_TYPE=Release

PLIST_FILES=	bin/parish-compartments \
		share/applications/parish-compartments.desktop \
		share/pixmaps/parishbsd.png \
		share/polkit-1/actions/org.parishbsd.parishcompartments.policy

# Install the polkit policy and desktop entry ourselves.
# CMake handles the binary and the icon.
do-install:
	${INSTALL_PROGRAM} ${BUILD_WRKSRC}/parish-compartments \
		${STAGEDIR}${PREFIX}/bin/parish-compartments
	${INSTALL_DATA} ${WRKSRC}/resources/parish-compartments.desktop \
		${STAGEDIR}${PREFIX}/share/applications/parish-compartments.desktop
	${INSTALL_DATA} ${WRKSRC}/resources/parishbsd.png \
		${STAGEDIR}${PREFIX}/share/pixmaps/parishbsd.png
	${INSTALL_DATA} ${WRKSRC}/resources/org.parishbsd.parishcompartments.policy \
		${STAGEDIR}${PREFIX}/share/polkit-1/actions/org.parishbsd.parishcompartments.policy

.include <bsd.port.mk>
