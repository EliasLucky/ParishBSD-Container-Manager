PORTNAME=	parishbsd-compartments
DISTVERSION=	1.2.0
CATEGORIES=	sysutils
MASTER_SITES=	https://codeberg.org/EliasLucky/parishbsd-container-manager/archive/
DISTNAME=	parishbsd-container-manager-v${DISTVERSION}

MAINTAINER=	ellucky08@gmail.com
COMMENT=	ParishBSD compartment manager (GUI and CLI)
WWW=		https://eliaslucky.codeberg.page/
LICENSE=	GPLv3
LICENSE_FILE=	${WRKSRC}/LICENSE

LIB_DEPENDS=	libQt6Core.so:devel/qt6-base \
		libQt6Widgets.so:devel/qt6-base \
		libQt6Network.so:devel/qt6-base

RUN_DEPENDS=	polkit>0:sysutils/polkit \
		desktop-file-utils>0:deskutils/desktop-file-utils \
		fusefs-exfat>0:sysutils/fusefs-exfat \
		fusefs-ntfs>0:sysutils/fusefs-ntfs

USES=		cmake compiler:c++20-lang pkgconfig qt:6
USE_QT=		base tools
USE_RC_SUBR=	parish_compartments parishbsd_templates
CMAKE_ARGS=	-DCMAKE_BUILD_TYPE=Release

DATADIR=	${PREFIX}/share/parishbsd

# --------------------------------------------------------------------
# post-install: shell scripts, data directories, polkit policy, .desktop
# (Compiled C++ targets parish-compartments, parish-template-build,
#  parish-template-wizard, parish-compartment-dialog, parish-file-inspect,
#  xpra-sup, and resources/applications.catalog are installed by CMake.)
# --------------------------------------------------------------------

post-install:
	# --- Dispatcher (unprivileged script in bin/) ---
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl ${STAGEDIR}${PREFIX}/bin/

	# --- Per-verb privileged scripts (in sbin/) ---
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-list               ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-show               ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-start              ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-stop               ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-restart            ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-create             ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-destroy            ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-lock               ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-unlock             ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-console            ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-set-risk           ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-audio              ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-exchange           ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-xpra-start         ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-xpra-stop          ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-schedules          ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-schedule-lock      ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-unschedule-lock    ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parish-jail-run              ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parish-app-create-launcher   ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-apps-install       ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-apps-install-file  ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-apps-remove        ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-apps-remove-custom ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-apps-upgrade       ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-apps-list          ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-usb-attach         ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-usb-detach         ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-template-create    ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-template-list      ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-template-show      ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-template-kill      ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-template-destroy   ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-template-scrub     ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-template-hide      ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-template-unhide    ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-template-rename    ${STAGEDIR}${PREFIX}/sbin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parishctl-template-reset     ${STAGEDIR}${PREFIX}/sbin/

	# --- User-facing & backend scripts (in bin/) ---
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parish-apps                  ${STAGEDIR}${PREFIX}/bin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parish-app-launch            ${STAGEDIR}${PREFIX}/bin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parish-audio                 ${STAGEDIR}${PREFIX}/bin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parish-usb                   ${STAGEDIR}${PREFIX}/bin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parish-xpra                  ${STAGEDIR}${PREFIX}/bin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parish-detect-installer      ${STAGEDIR}${PREFIX}/bin/
	${INSTALL_SCRIPT} ${WRKSRC}/cli/parish-exchange              ${STAGEDIR}${PREFIX}/bin/

	# --- Lifecycle scripts (in libexec/parishbsd/) ---
	${MKDIR} ${STAGEDIR}${PREFIX}/libexec/parishbsd
	(cd ${WRKSRC}/scripts && \
		${COPYTREE_BIN} . ${STAGEDIR}${PREFIX}/libexec/parishbsd)

	# --- Shared libraries (in libexec/parishbsd/lib/) ---
	${MKDIR} ${STAGEDIR}${PREFIX}/libexec/parishbsd/lib
	(cd ${WRKSRC}/libexec/parishbsd/lib && \
		${COPYTREE_SHARE} . ${STAGEDIR}${PREFIX}/libexec/parishbsd/lib)

	# --- Templates (in share/parishbsd/templates/) ---
	${MKDIR} ${STAGEDIR}${DATADIR}/templates
	(cd ${WRKSRC}/templates && \
		${COPYTREE_SHARE} . ${STAGEDIR}${DATADIR}/templates)

	# --- Host-side state directories ---
	${MKDIR} ${STAGEDIR}/var/db/parishbsd
	${MKDIR} ${STAGEDIR}/var/db/parishbsd/templates
	${MKDIR} ${STAGEDIR}/var/db/parishbsd/launchers
	${MKDIR} ${STAGEDIR}/var/db/parishbsd/risk
	${MKDIR} ${STAGEDIR}/var/db/parishbsd/audio

	# --- Polkit policy (in share/polkit-1/actions/) ---
	${MKDIR} ${STAGEDIR}${PREFIX}/share/polkit-1/actions
	${INSTALL_DATA} ${FILESDIR}/org.parishbsd.parishcompartments.policy \
		${STAGEDIR}${PREFIX}/share/polkit-1/actions/

	# --- Desktop entry (in share/applications/) ---
	${MKDIR} ${STAGEDIR}${PREFIX}/share/applications
	${INSTALL_DATA} ${FILESDIR}/parish-compartments.desktop \
		${STAGEDIR}${PREFIX}/share/applications/

	# --- Icons and pixmap ---
	${MKDIR} ${STAGEDIR}${PREFIX}/share/pixmaps
	${INSTALL_DATA} ${WRKSRC}/resources/parishbsd.png \
		${STAGEDIR}${PREFIX}/share/pixmaps/

	${MKDIR} ${STAGEDIR}${PREFIX}/share/icons/hicolor/scalable/apps
	${INSTALL_DATA} ${WRKSRC}/resources/icons/parishbsd.svg \
		${STAGEDIR}${PREFIX}/share/icons/hicolor/scalable/apps/

	# --- Template build data ---
	${MKDIR} ${STAGEDIR}${PREFIX}/libexec/parishbsd/templates
	${INSTALL_DATA} ${WRKSRC}/libexec/parishbsd/templates/clusters.tsv \
		${STAGEDIR}${PREFIX}/libexec/parishbsd/templates/

	${MKDIR} ${STAGEDIR}${PREFIX}/libexec/parishbsd/templates/provision.d
	(cd ${WRKSRC}/libexec/parishbsd/templates/provision.d && \
		${COPYTREE_BIN} . ${STAGEDIR}${PREFIX}/libexec/parishbsd/templates/provision.d)

.include <bsd.port.mk>
