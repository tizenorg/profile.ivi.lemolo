#ifndef _I18N_H__
#define _I18N_H__

#ifdef HAVE_TIZEN
#include <appcore-efl.h>
#else
#include <libintl.h>
#define _(String) gettext (String)
#define gettext_noop(String) String
#define N_(String) gettext_noop (String)
#endif

#endif
