#include <stdio.h>
#include <string.h>

#include "desktop_settings.h"

static int expect_settings(const DesktopSettings *settings, uint16_t level, int health, int ammo,
                           int weapons, int quicksave_load, int always_run, uint8_t volume,
                           uint8_t world_light_tessellation)
{
    return settings->start_level_index == level &&
        (settings->infinite_health != 0u) == health &&
        (settings->infinite_ammo != 0u) == ammo &&
        (settings->all_weapons != 0u) == weapons &&
        (settings->quicksave_load != 0u) == quicksave_load &&
        (settings->always_run != 0u) == always_run && settings->volume == volume &&
        settings->world_light_tessellation == world_light_tessellation;
}

int main(void)
{
    static const char settings_text[] =
        "# desktop session preferences\n"
        "start_level = 16\n"
        "infinite_health = yes\n"
        "infinite_ammo = on\n"
        "all_weapons = true\n"
        "quicksave_load = yes\n"
        "run_default = off\n"
        "volume = 37\n"
        "world_light_tessellation = 8\n"
        "unrelated_source_option = keep\n";
    DesktopSettings settings;
    char error[256] = {0};

    desktop_settings_default(&settings);
    if (!expect_settings(&settings, 0u, 0, 0, 0, 0, 1, 100u, 4u)) {
        fprintf(stderr, "desktop settings defaults differ from the documented template\n");
        return 1;
    }
    if (!desktop_settings_parse(&settings, settings_text, strlen(settings_text), error, sizeof(error)) ||
        !expect_settings(&settings, 15u, 1, 1, 1, 1, 0, 37u, 8u)) {
        fprintf(stderr, "desktop settings parser did not apply valid values: %s\n", error);
        return 1;
    }

    desktop_settings_default(&settings);
    if (desktop_settings_parse(&settings, "quicksave_load=maybe\n", 21u,
                               error, sizeof(error)) ||
        strstr(error, "quicksave_load") == NULL) {
        fprintf(stderr, "invalid quicksave/load option was not reported clearly\n");
        return 1;
    }
    desktop_settings_default(&settings);
    if (desktop_settings_parse(&settings, "start_level=0\n", 14u, error, sizeof(error)) ||
        strstr(error, "start_level") == NULL) {
        fprintf(stderr, "invalid one-indexed start level was not reported clearly\n");
        return 1;
    }
    desktop_settings_default(&settings);
    if (desktop_settings_parse(&settings, "volume=101\n", 11u, error, sizeof(error)) ||
        strstr(error, "volume") == NULL) {
        fprintf(stderr, "invalid volume was not reported clearly\n");
        return 1;
    }
    desktop_settings_default(&settings);
    if (desktop_settings_parse(&settings, "world_light_tessellation=3\n", 27u,
                               error, sizeof(error)) ||
        strstr(error, "world_light_tessellation") == NULL) {
        fprintf(stderr, "invalid world-light tessellation was not reported clearly\n");
        return 1;
    }
    for (uint8_t factor = 1u; factor <= 8u; factor = (uint8_t)(factor * 2u)) {
        char text[64];
        int length = snprintf(text, sizeof(text), "world_light_tessellation=%u\n", factor);

        desktop_settings_default(&settings);
        if (length <= 0 ||
            !desktop_settings_parse(&settings, text, (size_t)length, error, sizeof(error)) ||
            settings.world_light_tessellation != factor) {
            fprintf(stderr, "valid world-light tessellation %u was rejected: %s\n", factor,
                    error);
            return 1;
        }
    }
    return 0;
}
