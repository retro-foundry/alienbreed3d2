#ifndef AB3D2_DESKTOP_SETTINGS_H
#define AB3D2_DESKTOP_SETTINGS_H

#include <stddef.h>
#include <stdint.h>

#include "renderer_backend.h"
#include "renderer_ray_tracing_options.h"

/*
 * Optional PC presentation/session settings.  These do not replace any
 * source preference bytes or campaign data; GameBootstrap applies them at
 * the corresponding desktop boundary.
 */
typedef struct {
    /* ab3d2.ini is deliberately one-indexed for the A--P campaign. */
    uint16_t start_level_index;
    uint8_t infinite_health;
    uint8_t infinite_ammo;
    uint8_t all_weapons;
    uint8_t all_keys;
    /* First-port-compatible F5/F9 savegame.bin shortcuts. */
    uint8_t quicksave_load;
    /* Restore savegame.bin at startup and bypass the initial story screen. */
    uint8_t load_autosave;
    uint8_t always_run;
    /* Master mixer gain as a percentage, 0 through 100. */
    uint8_t volume;
    /* Presentation-only world-light subdivisions per source mesh edge. */
    uint8_t world_light_tessellation;
    /* Desktop graphics backend; OpenGL remains the documented default. */
    RendererBackend renderer_backend;
    /* Ray-traced backend quality and output settings. Absent keys retain the
     * renderer defaults; output mode zero is automatic monitor detection. */
    RendererRayTracingOptions ray_tracing;
} DesktopSettings;

typedef enum {
    DESKTOP_SETTINGS_LOAD_ERROR = 0,
    DESKTOP_SETTINGS_LOAD_OK = 1,
    DESKTOP_SETTINGS_LOAD_NOT_FOUND = 2
} DesktopSettingsLoadResult;

void desktop_settings_default(DesktopSettings *settings);

/* Parse text supplied by an INI file. Recognised malformed values fail clearly. */
int desktop_settings_parse(DesktopSettings *settings, const char *text, size_t text_size,
                           char *error, size_t error_size);

/* Load one INI path; a missing path is distinct from malformed content. */
DesktopSettingsLoadResult desktop_settings_load_file(DesktopSettings *settings,
                                                     const char *path,
                                                     char *error, size_t error_size);

#endif
