ACLOCAL_AMFLAGS = -I m4
AM_MAKEFLAGS = --no-print-directory
AM_CFLAGS = \
	-DPACKAGE_DATA_DIR=\"$(pkgdatadir)/\" \
	-DPACKAGE_BIN_DIR=\"$(bindir)\" \
	-DPACKAGE_LIB_DIR=\"$(libdir)\" \
	-I$(top_srcdir)/utils \
	@EFL_CFLAGS@ \
	@TIZEN_CFLAGS@

MAINTAINERCLEANFILES = \
	aclocal.m4 \
	compile \
	config.guess \
	config.h.in \
	config.sub \
	configure \
	depcomp \
	install-sh \
	ltmain.sh \
	Makefile.in \
	missing \
	mkinstalldirs
EXTRA_DIST =

noinst_LTLIBRARIES = utils/libofono-efl-utils.la

utils_libofono_efl_utils_la_SOURCES = \
	utils/contacts-ofono-efl.h \
	utils/log.h \
	utils/ofono.c \
	utils/ofono.h \
	utils/simple-popup.c \
	utils/simple-popup.h \
	utils/util.c \
	utils/util.h
utils_libofono_efl_utils_la_LIBADD = \
	@EFL_LIBS@ \
	@TIZEN_LIBS@

if HAVE_TIZEN
utils_libofono_efl_utils_la_SOURCES += utils/contacts-tizen.c
else
utils_libofono_efl_utils_la_SOURCES += utils/contacts.c
endif

bin_PROGRAMS = \
	dialer/dialer \
	messages/messages \
	tizen/answer_daemon

dialer_dialer_LDADD = \
	@EFL_LIBS@ \
	@TIZEN_LIBS@ \
	utils/libofono-efl-utils.la

dialer_dialer_SOURCES = \
	dialer/main.c \
	dialer/rc.c \
	dialer/rc.h \
	dialer/gui.c \
	dialer/gui.h \
	dialer/keypad.c \
	dialer/keypad.h \
	dialer/history.c \
	dialer/history.h \
	dialer/callscreen.c \
	dialer/callscreen.h \
	dialer/ussd.c \
	dialer/ussd.h

messages_messages_LDADD = \
	@EFL_LIBS@ \
	@TIZEN_LIBS@ \
	utils/libofono-efl-utils.la

messages_messages_SOURCES = \
	messages/main.c \
	messages/rc.c \
	messages/rc.h \
	messages/overview.c \
	messages/overview.h \
	messages/gui.c \
	messages/gui.h \
	messages/compose.c \
	messages/compose.h

AM_V_SED = $(am__v_SED_$(V))
am__v_SED_ = $(am__v_SED_$(AM_DEFAULT_VERBOSITY))
am__v_SED_0 = @echo "  SED   " $@;

tizen_answer_daemon_SOURCES = tizen/answer_daemon.c
tizen_answer_daemon_LDADD = @EFL_LIBS@ @TIZEN_LIBS@

if HAVE_TIZEN
bin_PROGRAMS += \
	tizen/message_daemon \
	tizen/dialer_open \
	tizen/dialer_daemon

tizen_dialer_open_SOURCES = tizen/dialer_open.c
tizen_dialer_open_LDADD = @EFL_LIBS@ @TIZEN_LIBS@

tizen_dialer_daemon_SOURCES = tizen/dialer_daemon.c
tizen_dialer_daemon_LDADD = @EFL_LIBS@ @TIZEN_LIBS@

tizen_message_daemon_SOURCES = tizen/message_daemon.c
tizen_message_daemon_LDADD = @EFL_LIBS@ @TIZEN_LIBS@ utils/libofono-efl-utils.la

%.service: %.service.in Makefile
	sed 's:@bindir[@]:$(bindir):g' $< > $@

dbusservicesdir = @with_dbusservicesdir@
dbusservices_DATA = \
tizen/org.tizen.dialer.service

EXTRA_DIST += \
tizen/org.tizen.dialer.service.in

%.desktop: %.desktop.in Makefile
	$(AM_V_SED)sed 's:@bindir[@]:$(bindir):g' $< > $@

desktopdir = @with_desktopdir@
desktop_DATA = \
tizen/answer_daemon.desktop \
tizen/dialer.desktop \
tizen/org.tizen.call.desktop \
tizen/org.tizen.phone.desktop

EXTRA_DIST += \
tizen/answer_daemon.desktop.in \
tizen/dialer.desktop.in \
tizen/org.tizen.call.desktop.in \
tizen/org.tizen.phone.desktop.in


tizen/order: tizen/order.in Makefile
	$(AM_V_SED)sed 's:@desktopdir[@]:$(with_desktopdir):g' $< > $@

tizenexamplesdir = $(pkgdatadir)/tizen-examples
tizenexamples_DATA = \
tizen/order

EXTRA_DIST += \
tizen/order.in

endif

# Themes are compiled with edje_cc given by user (cross-compile)
EDJE_CC = @edje_cc@
EDJE_FLAGS_VERBOSE_ =
EDJE_FLAGS_VERBOSE_0 =
EDJE_FLAGS_VERBOSE_1 = -v
EDJE_FLAGS = $(EDJE_FLAGS_VERBOSE_$(V)) -id $(top_srcdir)/data/themes/images -fd $(top_srcdir)/data/fonts

if HAVE_TIZEN
EDJE_FLAGS += -DHAVE_TIZEN=1
endif

filesdir = $(pkgdatadir)/themes
files_DATA = \
data/themes/default.edj \
data/themes/default-sd.edj

AM_V_EDJ = $(am__v_EDJ_$(V))
am__v_EDJ_ = $(am__v_EDJ_$(AM_DEFAULT_VERBOSITY))
am__v_EDJ_0 = @echo "  EDJ   " $@;

THEME_IMAGES = \
data/themes/images/arrow_left.png \
data/themes/images/arrow_right.png \
data/themes/images/bg_call.jpg \
data/themes/images/bg_chat_incoming.png \
data/themes/images/bg_chat_outgoing.png \
data/themes/images/bg_keypad.jpg \
data/themes/images/ico_add_call.png \
data/themes/images/ico_add_contact.png \
data/themes/images/ico_arrow_right.png \
data/themes/images/ico_backspace.png \
data/themes/images/ico_ball.png \
data/themes/images/ico_call.png \
data/themes/images/ico_contacts.png \
data/themes/images/ico_del.png \
data/themes/images/ico_edit.png \
data/themes/images/ico_history.png \
data/themes/images/ico_keypad.png \
data/themes/images/ico_merge.png \
data/themes/images/ico_msg_delivered.png \
data/themes/images/ico_multiparty.png \
data/themes/images/ico_multiparty_hangup.png \
data/themes/images/ico_multiparty_private.png \
data/themes/images/ico_mute.png \
data/themes/images/ico_person.png \
data/themes/images/ico_speaker.png \
data/themes/images/ico_swap.png \
data/themes/images/ico_voicemail.png \
data/themes/images/loading_0.png \
data/themes/images/loading_1.png \
data/themes/images/loading_2.png \
data/themes/images/loading_3.png \
data/themes/images/loading_4.png \
data/themes/images/loading_5.png \
data/themes/images/loading_6.png \
data/themes/images/loading_7.png

THEME_INCLUDES = \
data/themes/includes/action_answer.edc \
data/themes/includes/answer_screen.edc \
data/themes/includes/buttons.edc \
data/themes/includes/call.edc \
data/themes/includes/colors.edc \
data/themes/includes/compose.edc \
data/themes/includes/contacts-bg.edc \
data/themes/includes/contacts.edc \
data/themes/includes/dialer.edc \
data/themes/includes/history-bg.edc \
data/themes/includes/history.edc \
data/themes/includes/keypad.edc \
data/themes/includes/messages-overview-list.edc \
data/themes/includes/messages-overview.edc \
data/themes/includes/messages.edc \
data/themes/includes/multiparty-list.edc \
data/themes/includes/popup.edc \
data/themes/includes/scroller.edc


EXTRA_DIST += \
data/themes/default-hd.edc \
data/themes/default-sd.edc \
data/themes/default.edc \
$(THEME_INCLUDES) $(THEME_IMAGES)


data/themes/default.edj: $(top_builddir)/Makefile $(top_srcdir)/data/themes/default.edc $(top_srcdir)/data/themes/default-hd.edc $(THEME_INCLUDES) $(THEME_IMAGES)
	$(MKDIR_P) $(top_builddir)/data/themes
	$(AM_V_EDJ)$(EDJE_CC) $(EDJE_FLAGS) \
	$(top_srcdir)/data/themes/default-hd.edc \
	$(top_builddir)/data/themes/default.edj

data/themes/default-sd.edj: $(top_builddir)/Makefile $(top_srcdir)/data/themes/default.edc $(top_srcdir)/data/themes/default-sd.edc $(THEME_INCLUDES) $(THEME_IMAGES)
	$(MKDIR_P) $(top_builddir)/data/themes
	$(AM_V_EDJ)$(EDJE_CC) $(EDJE_FLAGS) \
	$(top_srcdir)/data/themes/default-sd.edc \
	$(top_builddir)/data/themes/default-sd.edj

clean-local:
	rm -f $(top_builddir)/data/themes/default.edj
	rm -f $(top_builddir)/data/themes/default-sd.edj

examplesdir = $(pkgdatadir)/examples
examples_DATA = \
data/examples/contacts.csv

scriptsdir = $(pkgdatadir)/scripts
scripts_SCRIPTS = \
data/scripts/ofono-efl-contacts-db-create.py

EXTRA_DIST += $(examples_DATA) $(scripts_SCRIPTS)
