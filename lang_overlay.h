#ifndef LANG_OVERLAY_H
#define LANG_OVERLAY_H

#include <stddef.h>

/* Serves <game dir>\lang\<language>\<path> instead of <game dir>\<path>
   whenever that file exists. Returns the number of overlay files. */
int lang_overlay_install(const char *language);

/* The overlay file for a game path, as an ANSI path; 0 when there is none. */
int lang_overlay_resolve_a(const char *path, char *out, size_t capacity);

#endif
