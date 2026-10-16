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

post-install:
	@${ECHO_MSG} "Updating icon cache..."
	-@${LOCALBASE}/bin/gtk-update-icon-cache -f -t ${STAGEDIR}${PREFIX}/share/icons/hicolor 2>/dev/null || true
.include <bsd.port.mk>
