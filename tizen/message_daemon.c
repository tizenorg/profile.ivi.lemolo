#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Elementary.h>
#include <appcore-efl.h>
#include <Eina.h>
#include <Eldbus.h>
#include <notification.h>
#include <vconf.h>
#include <vconf-keys.h>
#include <power.h>
#include "ofono.h"

#define APP_NAME "org.tizen.message_daemon"

static OFono_Callback_List_Incoming_SMS_Node *inc_sms = NULL;

static char _img_path[PATH_MAX];

int _log_domain = -1;

#define ERR(...)      EINA_LOG_DOM_ERR(_log_domain, __VA_ARGS__)
#define INF(...)      EINA_LOG_DOM_INFO(_log_domain, __VA_ARGS__)
#define DBG(...)      EINA_LOG_DOM_DBG(_log_domain, __VA_ARGS__)

static void _notification_create(const char *sender, const char *message)
{
	int id;
	notification_h noti;
	notification_error_e err;

	noti = notification_new(NOTIFICATION_TYPE_NOTI,
				NOTIFICATION_GROUP_ID_NONE,
				NOTIFICATION_PRIV_ID_NONE);
	EINA_SAFETY_ON_NULL_RETURN(noti);

	err = notification_set_text(noti, NOTIFICATION_TEXT_TYPE_TITLE,
					sender, NULL,
					NOTIFICATION_VARIABLE_TYPE_NONE);

	if (err != NOTIFICATION_ERROR_NONE)
		ERR("Could not set the title");

	err = notification_set_text(noti, NOTIFICATION_TEXT_TYPE_CONTENT,
					message, NULL,
					NOTIFICATION_VARIABLE_TYPE_NONE);

	if (err != NOTIFICATION_ERROR_NONE)
		ERR("Could not set the body");

	err = notification_set_image(noti, NOTIFICATION_IMAGE_TYPE_ICON, _img_path);

	if (err != NOTIFICATION_ERROR_NONE)
		ERR("Could not set the image");

	err = notification_insert(noti, &id);

	if (err != NOTIFICATION_ERROR_NONE)
		ERR("Could not show the notification");

	notification_free(noti);
}

static Eina_Bool _phone_locked(void)
{
	int lock;
	if (vconf_get_int(VCONFKEY_IDLE_LOCK_STATE, &lock) == -1)
		return EINA_FALSE;

	if (lock == VCONFKEY_IDLE_LOCK)
		return EINA_TRUE;

	return EINA_FALSE;
}

static void _inc_sms_cb(void *data __UNUSED__, unsigned int sms_class,
			time_t timestamp __UNUSED__, const char *sender,
			const char *message)
{
	if (sms_class != 1)
		return;

	if (_phone_locked())
		power_wakeup(EINA_TRUE);

	_notification_create(sender, message);
}

static int _create(void *data __UNUSED__)
{
	if (!ofono_init())
		return -1;

	inc_sms = ofono_incoming_sms_cb_add(_inc_sms_cb, NULL);

	elm_app_compile_data_dir_set(PACKAGE_DATA_DIR);
	/* TODO SET IMAGE */
	snprintf(_img_path, sizeof(_img_path), "%s/img",
			elm_app_data_dir_get());
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
	ofono_incoming_sms_cb_del(inc_sms);
	ofono_shutdown();
	return 0;
}

int main(int argc, char **argv)
{
	int r = EXIT_FAILURE;
	struct appcore_ops ops = {
		.create = _create,
		.resume = _resume,
		.reset = _reset,
		.pause = _pause,
		.terminate = _terminate,
	};
	ops.data = NULL;

	eina_init();
	eldbus_init();

	_log_domain = eina_log_domain_register("answer_daemon", NULL);

	r = appcore_efl_main(APP_NAME, &argc, &argv, &ops);

	eina_shutdown();
	eldbus_shutdown();

	return r;
}
