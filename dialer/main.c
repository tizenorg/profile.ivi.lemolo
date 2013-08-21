#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#ifdef HAVE_TIZEN
#include <appcore-efl.h>
#endif
#include <Elementary.h>
#include <stdio.h>
#include <stdlib.h>
#include <Evas.h>

#ifndef ELM_LIB_QUICKLAUNCH

#include "amb.h"
#include "gui.h"
#include "log.h"
#include "ofono.h"
#include "rc.h"
#include "util.h"

#include <Ecore_Getopt.h>

static const char def_modem_hardware_api[] =
	"SimManager,"
	"VoiceCallManager,"
	"MessageManager,"
	"SimToolkit,"
	"CallForwarding";

static const char def_modem_hfp_api[] =
	"VoiceCallManager";

static const char def_modem_type[] =
	"hfp";

static const char def_rc_service[] = "org.tizen.dialer";


static const Ecore_Getopt options = {
	PACKAGE_NAME,
	"%prog [options]",
	PACKAGE_VERSION,
	"(C) 2012 Intel Corporation",
	"GPL-2" /* TODO: check license with Intel */,
	"Phone Dialer using oFono and EFL.",
	EINA_FALSE,
	{ECORE_GETOPT_STORE_STR('H', "theme", "path to theme EDJ file."),
	 ECORE_GETOPT_STORE_STR('m', "modem", "Modem object path in oFono."),
	 ECORE_GETOPT_STORE_STR('a', "api", "oFono modem APIs to use, comma "
				"separated. Example: "
				"SimManager,VoiceCallManager. See --list-api"),
	 ECORE_GETOPT_STORE_TRUE('A', "list-api", "list all oFono modem API."),
	 ECORE_GETOPT_STORE_DEF_STR('t', "type", "oFono modem type to use.",
					def_modem_type),
	 ECORE_GETOPT_STORE_TRUE('T', "list-types",
					"list all oFono modem types."),
	 ECORE_GETOPT_STORE_STR('R', "rc-dbus-name",
				"The DBus name to use for the remote control."),
	 ECORE_GETOPT_VERSION('V', "version"),
	 ECORE_GETOPT_COPYRIGHT('C', "copyright"),
	 ECORE_GETOPT_LICENSE('L', "license"),
	 ECORE_GETOPT_HELP('h', "help"),
	 ECORE_GETOPT_SENTINEL
	}
};

int _log_domain = -1;
int _app_exit_code = EXIT_SUCCESS;

#ifdef HAVE_TIZEN
static int _create(void *data __UNUSED__)
{
	return 0;
}

static int _reset(bundle *b __UNUSED__, void *data __UNUSED__)
{
	return 0;
}

static int _resume(void *data __UNUSED__)
{
	return 0;
}

static int _pause(void *data __UNUSED__)
{
	return 0;
}

static int _terminate(void *data __UNUSED__)
{
	return 0;
}
#endif

EAPI int elm_main(int argc, char **argv)
{
	int args;
	char *modem_path = NULL;
	char *modem_api = NULL;
	char *modem_type = NULL;
	char *theme = NULL;
	char *rc_service = NULL;
	Eina_Bool list_api = EINA_FALSE;
	Eina_Bool list_type = EINA_FALSE;
	Eina_Bool quit_option = EINA_FALSE;
	Ecore_Getopt_Value values[] = {
		ECORE_GETOPT_VALUE_STR(theme),
		ECORE_GETOPT_VALUE_STR(modem_path),
		ECORE_GETOPT_VALUE_STR(modem_api),
		ECORE_GETOPT_VALUE_BOOL(list_api),
		ECORE_GETOPT_VALUE_STR(modem_type),
		ECORE_GETOPT_VALUE_BOOL(list_type),
		ECORE_GETOPT_VALUE_STR(rc_service),
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
	if (list_api) {
		puts("Supported oFono API:");
		ofono_modem_api_list(stdout, "\t", "\n");
		goto end;
	}
	if (list_type) {
		puts("Supported oFono type:");
		ofono_modem_type_list(stdout, "\t", "\n");
		goto end;
	}

	if (quit_option)
		goto end;

	if (rc_service) {
		INF("User-defined DBus remote control service name: %s",
			rc_service);
	} else {
		INF("Using default DBus remote control service name: %s",
			def_rc_service);
	}

	if (!rc_init(rc_service ? rc_service : def_rc_service)) {
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
		const char *api;
		if (!modem_type)
			api = def_modem_hfp_api;
		else if (strstr(modem_type, "hfp"))
			api = def_modem_hfp_api;
		else if (strcmp(modem_type, "hardware"))
			api = def_modem_hardware_api;
		else {
			WRN("modem type not handled: %s", modem_type);
			api = "VoiceCallManager";
		}

		INF("Using default modem API: %s", api);
		ofono_modem_api_require(api);
	}

	if (modem_type) {
		INF("User-defined modem type: %s", modem_type);
		ofono_modem_type_require(modem_type);
	} else {
		INF("Using default modem type: %s", def_modem_type);
		ofono_modem_type_require(def_modem_type);
	}

	if (!util_init(theme)) {
		CRITICAL("Could not setup graphical user interface");
		_app_exit_code = EXIT_FAILURE;
		goto end_ofono;
	}

#ifdef HAVE_TIZEN
	if (!amb_init()) {
		CRITICAL("Could not setup automotive-message-broker");
		_app_exit_code = EXIT_FAILURE;
		goto end_amb;
	}
#endif

	if (!gui_init()) {
		CRITICAL("Could not setup graphical user interface");
		_app_exit_code = EXIT_FAILURE;
		goto end_util;
	}

	INF("Entering main loop");

#ifdef HAVE_TIZEN
	int iReturn = 0;
	struct appcore_ops ops = {
		.create = _create,
		.resume = _resume,
		.reset = _reset,
		.pause = _pause,
		.terminate = _terminate,
	};
	ops.data = NULL;
	iReturn = appcore_efl_main("org.tizen.dialer", &argc, &argv, &ops);
#else
	elm_run();
#endif
	INF("Quit main loop");

	gui_shutdown();

end_util:
	util_shutdown();
#ifdef HAVE_TIZEN
end_amb:
	amb_shutdown();
#endif
end_ofono:
	ofono_shutdown();
end_rc:
	rc_shutdown();
end:
	elm_shutdown();

#ifdef HAVE_TIZEN
	return iReturn;
#else
	return _app_exit_code;
#endif
}

#endif
ELM_MAIN()
