/* Test-only native owner; the included production file remains unchanged. */
#include "../Quake/pr_ext.c"

qboolean FixtureSurfaceCacheValid (void) { return nearsurface_cache_valid; }
int FixtureSurfaceCacheCount (void) { return nearsurface_cache_entries; }
