#ifndef QUAKE_VR_WEAPON_CATALOG_H
#define QUAKE_VR_WEAPON_CATALOG_H

#include <ctype.h>
#include <string.h>

/*
 * Small, engine-independent policy state for the VR weapon wheel.  The wheel
 * learns viewmodels at runtime because QuakeC is free to reuse stock weapon
 * selectors.  Keep that ownership boundary here so it can be exercised
 * without a renderer, server, or game data.
 */

#define VR_WEAPON_CATALOG_MAX_OBSERVATIONS 128

typedef enum {
  VR_WEAPON_CATALOG_SOURCE_STOCK,
  VR_WEAPON_CATALOG_SOURCE_PROFILE,
  VR_WEAPON_CATALOG_SOURCE_SCHEMA,
  VR_WEAPON_CATALOG_SOURCE_DISCOVERED
} vr_weapon_catalog_source_t;

typedef struct {
  int selector;
  int model_index;
} vr_weapon_catalog_observation_t;

typedef struct {
  vr_weapon_catalog_observation_t observations
      [VR_WEAPON_CATALOG_MAX_OBSERVATIONS];
  int num_observations;
} vr_weapon_catalog_t;

static inline void VR_WeaponCatalog_Reset(vr_weapon_catalog_t *catalog) {
  if (catalog)
    memset(catalog, 0, sizeof(*catalog));
}

/* Returns non-zero when this observation was newly learned. */
static inline int VR_WeaponCatalog_Observe(vr_weapon_catalog_t *catalog,
                                           int selector, int model_index) {
  int i;

  if (!catalog || selector == 0 || model_index <= 0)
    return 0;
  for (i = 0; i < catalog->num_observations; ++i) {
    if (catalog->observations[i].selector == selector &&
        catalog->observations[i].model_index == model_index)
      return 0;
  }
  if (catalog->num_observations >= VR_WEAPON_CATALOG_MAX_OBSERVATIONS)
    return 0;
  catalog->observations[catalog->num_observations].selector = selector;
  catalog->observations[catalog->num_observations].model_index = model_index;
  ++catalog->num_observations;
  return 1;
}

/* The wheel renders an absent pickup model by swapping its g_ basename for
 * the corresponding v_ viewmodel. Treat that same pair as one catalogue
 * identity so runtime viewmodel discovery does not add a second wheel slot. */
static inline int VR_WeaponCatalog_ModelPathsMatch(const char *a,
                                                   const char *b) {
  const char *a_marker;
  const char *b_marker;
  size_t prefix_len;
  size_t i;

  if (!a || !a[0] || !b || !b[0])
    return 0;
  for (i = 0; a[i] && b[i]; ++i) {
    if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
      break;
  }
  if (!a[i] && !b[i])
    return 1;

  a_marker = strrchr(a, '/');
  b_marker = strrchr(b, '/');
  if (!a_marker || !b_marker || a_marker[1] == '\0' || b_marker[1] == '\0' ||
      a_marker[2] != '_' || b_marker[2] != '_')
    return 0;
  if (!((tolower((unsigned char)a_marker[1]) == 'g' &&
         tolower((unsigned char)b_marker[1]) == 'v') ||
        (tolower((unsigned char)a_marker[1]) == 'v' &&
         tolower((unsigned char)b_marker[1]) == 'g')))
    return 0;

  prefix_len = (size_t)(a_marker - a) + 1;
  if ((size_t)(b_marker - b) + 1 != prefix_len)
    return 0;
  for (i = 0; i < prefix_len; ++i) {
    if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
      return 0;
  }
  for (i = 2; a_marker[i] && b_marker[i]; ++i) {
    if (tolower((unsigned char)a_marker[i]) !=
        tolower((unsigned char)b_marker[i]))
      return 0;
  }
  return !a_marker[i] && !b_marker[i];
}

/*
 * Apply the wheel's established precedence policy consistently. An active
 * weapon stays visible while client inventory stats lag. A supplied
 * wwheel.txt is authoritative, schema entries supersede profile/stock entries,
 * and profile entries supersede stock entries. Runtime-only observations do
 * not inherit arbitrary QuakeC impulses or suppress a selectable fallback.
 */
static inline int VR_WeaponCatalog_ShouldExpose(
    vr_weapon_catalog_source_t source, int authoritative_schema,
    int has_schema_peer, int has_profile_peer, int owned, int active) {
  if (!owned && !active)
    return 0;
  /* An actual extra weapon observed in play may extend an incomplete file
   * roster. Still exclude undeclared stock guesses and duplicate selectors;
   * observing a model does not itself authorize a guessed switch impulse. */
  if (authoritative_schema && source == VR_WEAPON_CATALOG_SOURCE_STOCK)
    return 0;
  if (source != VR_WEAPON_CATALOG_SOURCE_SCHEMA && has_schema_peer)
    return 0;
  if (source != VR_WEAPON_CATALOG_SOURCE_SCHEMA &&
      source != VR_WEAPON_CATALOG_SOURCE_PROFILE && has_profile_peer)
    return 0;
  return 1;
}

/* Runtime maxima (for example MG3 capacity upgrades) win over fallbacks. */
static inline int VR_WeaponCatalog_ResolveAmmoMax(int fallback_max,
                                                  int dynamic_max) {
  return dynamic_max > 0 ? dynamic_max : fallback_max;
}

#endif
