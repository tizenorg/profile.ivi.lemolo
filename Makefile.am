ACLOCAL_AMFLAGS = -I m4
AM_MAKEFLAGS = --no-print-directory
AM_CFLAGS = \
	-DPACKAGE_DATA_DIR=\"$(pkgdatadir)/\" \
	-DPACKAGE_BIN_DIR=\"$(bindir)\" \
	-DPACKAGE_LIB_DIR=\"$(libdir)\" \
	@EFL_CFLAGS@

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

bin_PROGRAMS = dialer/dialer

dialer_dialer_LDADD = @EFL_LIBS@
dialer_dialer_SOURCES = \
	dialer/main.c \
	dialer/log.h \
	dialer/ofono.c \
	dialer/ofono.h \
	dialer/rc.c \
	dialer/rc.h \
	dialer/gui.c \
	dialer/gui.h \
	dialer/keypad.c \
	dialer/keypad.h \
	dialer/callscreen.c \
	dialer/callscreen.h

# Themes are compiled with edje_cc given by user (cross-compile)
EDJE_CC = @edje_cc@
EDJE_FLAGS_VERBOSE_ =
EDJE_FLAGS_VERBOSE_0 =
EDJE_FLAGS_VERBOSE_1 = -v
EDJE_FLAGS = $(EDJE_FLAGS_VERBOSE_$(V)) -id $(top_srcdir)/data/themes/images -fd $(top_srcdir)/data/fonts

filesdir = $(pkgdatadir)/themes
files_DATA = data/themes/default.edj

AM_V_EDJ = $(am__v_EDJ_$(V))
am__v_EDJ_ = $(am__v_EDJ_$(AM_DEFAULT_VERBOSITY))
am__v_EDJ_0 = @echo "  EDJ   " $@;

THEME_IMAGES = \
data/themes/images/bg_call.jpg \
data/themes/images/bg_keypad.jpg \
data/themes/images/bt_add_contact_pressed.png \
data/themes/images/bt_add_contact_released.png \
data/themes/images/bt_backspace_pressed.png \
data/themes/images/bt_backspace_released.png \
data/themes/images/bt_call_pressed.png \
data/themes/images/bt_call_released.png \
data/themes/images/bt_contacts_pressed.png \
data/themes/images/bt_contacts_released.png \
data/themes/images/bt_keypad_pressed.png \
data/themes/images/bt_keypad_released.png \
data/themes/images/bt_merge_pressed.png \
data/themes/images/bt_merge_released.png \
data/themes/images/bt_mute_pressed.png \
data/themes/images/bt_mute_released.png \
data/themes/images/bt_speaker_pressed.png \
data/themes/images/bt_speaker_released.png \
data/themes/images/bt_swap_pressed.png \
data/themes/images/bt_swap_released.png

EXTRA_DIST = data/themes/default.edc $(THEME_IMAGES)


data/themes/default.edj: $(top_builddir)/Makefile $(top_srcdir)/data/themes/default.edc $(THEME_IMAGES)
	$(MKDIR_P) $(top_builddir)/data/themes
	$(AM_V_EDJ)$(EDJE_CC) $(EDJE_FLAGS) \
	$(top_srcdir)/data/themes/default.edc \
	$(top_builddir)/data/themes/default.edj

clean-local:
	rm -f $(top_builddir)/data/themes/default.edj
