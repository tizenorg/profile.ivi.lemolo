#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Elementary.h>
#include <pulse/context.h>
#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>

#include "pulseaudio.h"
#include "log.h"

static pa_glib_mainloop *mainloop = NULL;
static pa_context *pa_ctx = NULL;

Eina_Bool pa_init(void)
{
	if (!mainloop)
		mainloop = pa_glib_mainloop_new(NULL);

	pa_ctx = pa_context_new(pa_glib_mainloop_get_api(mainloop),
				 "lemolo");

	// connects to the pulse server
	if (pa_context_connect(pa_ctx,
				NULL,
				PA_CONTEXT_NOFAIL, NULL) < 0)
	{
		ERR("Failed to connect to pulseaudio daemon");
                pa_glib_mainloop_free(mainloop);
                mainloop = NULL;
		return EINA_FALSE;
	}

	return EINA_TRUE;
}

void pa_shutdown(void)
{
	if (pa_ctx) {
		pa_context_disconnect(pa_ctx);
		pa_context_unref(pa_ctx);
	}

	if (mainloop) {
		pa_glib_mainloop_free(mainloop);
		mainloop = NULL;
	}
}
