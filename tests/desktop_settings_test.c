#include <stdio.h>
#include <string.h>

#include "desktop_settings.h"

static int expect_settings(const DesktopSettings *settings, uint16_t level, int health,
                           int weapons, int always_run, uint8_t volume)
{
    return settings->start_level_index == level &&
        (settings->infinite_health != 0u) == health &&
        (settings->all_weapons != 0u) == weapons &&
        (settings->always_run != 0u) == always_run && settings->volume == volume;
}

int main(void)
{
    static const char settings_text[] =
        "# desktop session preferences\n"
        "start_level = 16\n"
        "infinite_health = yes\n"
        "all_weapons = true\n"
        "run_default = off\n"
        "volume = 37\n"
        "unrelated_source_option = keep\n";
    DesktopSettings settings;
    char error[256] = {0};

    desktop_settings_default(&settings);
    if (!expect_settings(&settings, 0u, 0, 0, 1, 100u)) {
        fprintf(stderr, "desktop settings defaults differ from the documented template\n");
        return 1;
    }
    if (!desktop_settings_parse(&settings, settings_text, strlen(settings_text), error, sizeof(error)) ||
        !expect_settings(&settings, 15u, 1, 1, 0, 37u)) {
        fprintf(stderr, "desktop settings parser did not apply valid values: %s\n", error);
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
    return 0;
}
