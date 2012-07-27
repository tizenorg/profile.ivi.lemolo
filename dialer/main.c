#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Elementary.h>
#ifndef ELM_LIB_QUICKLAUNCH

#include "log.h"
#include "ofono.h"
#include "gui.h"
#include "rc.h"

#include <Ecore_Getopt.h>

static const char def_modem_api[] =
	"SimManager,"
	"VoiceCallManager,"
	"MessageManager,"
	"SimToolkit,"
	"CallForwarding";

static const Ecore_Getopt options = {
	PACKAGE_NAME,
	"%prog [options]",
	PACKAGE_VERSION,
	"(C) 2012 Intel Corporation",
	"GPL-2" /* TODO: check license with Intel */,
	"Phone Dialer using oFono and EFL.",
	EINA_FALSE,
	{ECORE_GETOPT_STORE_STR('m', "modem", "Modem object path in oFono."),
	 ECORE_GETOPT_STORE_DEF_STR('a', "api", "oFono modem APIs to use.",
					def_modem_api),
	 ECORE_GETOPT_VERSION('V', "version"),
	 ECORE_GETOPT_COPYRIGHT('C', "copyright"),
	 ECORE_GETOPT_LICENSE('L', "license"),
	 ECORE_GETOPT_HELP('h', "help"),
	 ECORE_GETOPT_SENTINEL
	}
};

int _log_domain = -1;
int _app_exit_code = EXIT_SUCCESS;

EAPI int elm_main(int argc, char **argv)
{
	int args;
	char *modem_path = NULL;
	char *modem_api = NULL;
	Eina_Bool quit_option = EINA_FALSE;
	Ecore_Getopt_Value values[] = {
		ECORE_GETOPT_VALUE_STR(modem_path),
		ECORE_GETOPT_VALUE_STR(modem_api),
		ECORE_GETOPT_VALUE_BOOL(quit_option),
		ECORE_GETOPT_VALUE_BOOL(quit_option),
		ECORE_GETOPT_VALUE_BOOL(quit_option),
		ECORE_GETOPT_VALUE_BOOL(quit_option),
		ECORE_GETOPT_VALUE_NONE
	};

	_log_domain = eina_log_domain_register("dialer", NULL);
	if (_log_domain < 0)
	{
		EINA_LOG_CRIT("Could not create log domain 'dialer'.");
		elm_shutdown();
		return EXIT_FAILURE;
	}

	args = ecore_getopt_parse(&options, values, argc, argv);
	if (args < 0)
	{
		ERR("Could not parse command line options.");
		_app_exit_code = EXIT_FAILURE;
		goto end;
	}
	if (quit_option)
		goto end;

	if (!rc_init()) {
		CRITICAL("Could not setup remote control via DBus.");
		_app_exit_code = EXIT_FAILURE;
		goto end;
	}

	if (!ofono_init()) {
		CRITICAL("Could not setup ofono");
		_app_exit_code = EXIT_FAILURE;
		goto end_rc;
	}

	if (modem_path) {
		INF("User-defined modem path: %s", modem_path);
		ofono_modem_path_wanted_set(modem_path);
	}

	if (modem_api) {
		INF("User-defined modem API: %s", modem_api);
		ofono_modem_api_require(modem_api);
	} else {
		INF("Using default modem API: %s", def_modem_api);
		ofono_modem_api_require(def_modem_api);
	}

	if (!gui_init()) {
		CRITICAL("Could not setup graphical user interface");
		_app_exit_code = EXIT_FAILURE;
		goto end_ofono;
	}

	INF("Entering main loop");
	elm_run();
	INF("Quit main loop");

	gui_shutdown();

end_ofono:
	ofono_shutdown();
end_rc:
	rc_shutdown();
end:
	elm_shutdown();

	return _app_exit_code;
}

#endif
ELM_MAIN()
