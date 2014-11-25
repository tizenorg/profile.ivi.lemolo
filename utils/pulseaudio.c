#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Elementary.h>
#include <pulse/context.h>
#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>
#include <pulse/simple.h>

#include "fcntl.h"
#include "pulseaudio.h"
#include "log.h"

#define BUFFERSIZE 1024

static pa_glib_mainloop *mainloop = NULL;
static pa_context *pa_ctx = NULL;
static pa_simple *connection = NULL;
static Ecore_Thread *ringtone_thread = NULL;
static pa_proplist *proplist = NULL;
static Eina_Bool stopped = EINA_TRUE;

/* The Sample format to use */
static const pa_sample_spec ss = {
	.format = PA_SAMPLE_S16LE,
	.rate = 44100,
	.channels = 2
};

static void _play(void *data __UNUSED__, Ecore_Thread *thread)
{
	int error, fd;
	char file_path[PATH_MAX];

	DBG("Ringtone playing");
	stopped = EINA_FALSE;

	snprintf(file_path, sizeof(file_path), "%s/ringtones/default.wav", elm_app_data_dir_get());

	if ((fd = open(file_path, O_RDONLY)) < 0) {
		DBG("open() failed: %s", strerror(errno));
		return;
	}

	if (dup2(fd, STDIN_FILENO) < 0 ) {
		DBG("dup2() failed: %s", strerror(errno));
		return;
	}

	close(fd);

	/* loops until stopped thread is canceled */
	for (;;) {
		uint8_t buf[BUFFERSIZE];
		ssize_t r;

		/* check if playing is stopped */
		if (ecore_thread_check(thread)) {
			DBG("Ringtone stopping");
			if (pa_simple_flush(connection, &error) < 0) {
				ERR("pa_simple_flush() failed: %s\n", pa_strerror(error));
				return;
			}

			break;
		}

		/* Read the ringtone file */
		if ((r = read(STDIN_FILENO, buf, sizeof(buf))) <= 0) {
			if (r == 0) {
				/* EOF, repeat */
				DBG("Ringtone repeating");
				lseek(STDIN_FILENO, 0, SEEK_SET);
				continue;
			}

			ERR("reading from ringtone wav file failed");
			return;
		}

		/* play iit */
		if (pa_simple_write(connection, buf, (size_t) r, &error) < 0) {
			ERR("pa_simple_write() failed: %s", pa_strerror(error));
			return;
		}
	}

	if (pa_simple_drain(connection, &error) < 0) {
		ERR("pa_simple_drain() failed: %s\n", pa_strerror(error));
	}
}

static void _end(void *data __UNUSED__, Ecore_Thread *thread __UNUSED__)
{
	DBG("Ringtone ended");
	if (ringtone_thread)
		ringtone_thread = NULL;
}


static void _cancel(void *data __UNUSED__, Ecore_Thread *thread __UNUSED__)
{
	DBG("Ringtone stopped");
	if (ringtone_thread)
		ringtone_thread = NULL;
}

Eina_Bool pa_init(void)
{
	int error;

	if (!mainloop)
		mainloop = pa_glib_mainloop_new(NULL);

	pa_ctx = pa_context_new(pa_glib_mainloop_get_api(mainloop),
				 "lemolo");

	// connects to the pulse server
	error = pa_context_connect(pa_ctx, NULL, PA_CONTEXT_NOFAIL, NULL);
	if (error < 0)
	{
		ERR("Failed to connect to pulseaudio daemon: %s", pa_strerror(error));
		goto error;
	}

	// Creates a connection for ringtone
#ifdef HAVE_TIZEN
	proplist = pa_proplist_new();
	error = pa_proplist_sets(proplist, PA_PROP_MEDIA_ROLE, "phone");
	if (error < 0) {
		DBG("pa_proplist_sets() failed: %s", pa_strerror(error));
		goto error;
	}

	if (!(connection = pa_simple_new_proplist(NULL, NULL, PA_STREAM_PLAYBACK, NULL,
		"lemolo", &ss, NULL, NULL, proplist, &error))) {
		DBG("pa_simple_new() failed: %s", pa_strerror(error));
		goto error;
	}
#else
        if (!(connection = pa_simple_new(NULL, NULL, PA_STREAM_PLAYBACK, NULL,
                "lemolo", &ss, NULL, NULL, &error))) {
                DBG("pa_simple_new() failed: %s", pa_strerror(error));
                goto error;
        }
#endif

	return EINA_TRUE;

error:
	if (connection)
		pa_simple_free(connection);

	if (pa_ctx) {
		pa_context_disconnect(pa_ctx);
		pa_context_unref(pa_ctx);
	}

	if (mainloop) {
		pa_glib_mainloop_free(mainloop);
		mainloop = NULL;
	}

	if (proplist)
		pa_proplist_free(proplist);

	return EINA_FALSE;
}

Eina_Bool pa_play_ringtone(void)
{
	if (!ringtone_thread)
		ringtone_thread = ecore_thread_run(_play, _end, _cancel, NULL);

	return EINA_TRUE;
}

void pa_stop_ringtone(void)
{
	if (ringtone_thread)
		ecore_thread_cancel(ringtone_thread);
}

void pa_shutdown(void)
{
	pa_stop_ringtone();

	if (connection)
		pa_simple_free(connection);

	if (pa_ctx) {
		pa_context_disconnect(pa_ctx);
		pa_context_unref(pa_ctx);
	}

	if (mainloop) {
		pa_glib_mainloop_free(mainloop);
		mainloop = NULL;
	}

	if (proplist)
		pa_proplist_free(proplist);
}
