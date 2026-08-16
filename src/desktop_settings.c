#include "desktop_settings.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    DESKTOP_SETTINGS_LEVEL_COUNT = 16u,
    DESKTOP_SETTINGS_LINE_CAPACITY = 512u
};

static void desktop_settings_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static char *desktop_settings_trim(char *text)
{
    char *end;

    while (*text != '\0' && isspace((unsigned char)*text)) {
        ++text;
    }
    if (*text == '\0') {
        return text;
    }
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        --end;
    }
    *end = '\0';
    return text;
}

static int desktop_settings_equals_ci(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right)) {
            return 0;
        }
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

static int desktop_settings_parse_bool(const char *text, uint8_t *out_value)
{
    if (!text || !out_value) {
        return 0;
    }
    if (desktop_settings_equals_ci(text, "1") || desktop_settings_equals_ci(text, "true") ||
        desktop_settings_equals_ci(text, "yes") || desktop_settings_equals_ci(text, "on")) {
        *out_value = UINT8_MAX;
        return 1;
    }
    if (desktop_settings_equals_ci(text, "0") || desktop_settings_equals_ci(text, "false") ||
        desktop_settings_equals_ci(text, "no") || desktop_settings_equals_ci(text, "off")) {
        *out_value = 0u;
        return 1;
    }
    return 0;
}

static int desktop_settings_parse_unsigned(const char *text, unsigned long maximum,
                                           unsigned long *out_value)
{
    char *end;
    unsigned long value;

    if (!text || !*text || !out_value || text[0] == '-') {
        return 0;
    }
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *desktop_settings_trim(end) != '\0' || value > maximum) {
        return 0;
    }
    *out_value = value;
    return 1;
}

static int desktop_settings_apply_line(DesktopSettings *settings, char *line,
                                       size_t line_number, char *error, size_t error_size)
{
    char *equals = strchr(line, '=');
    char *key;
    char *value;
    unsigned long number;

    if (!equals) {
        return 1;
    }
    *equals = '\0';
    key = desktop_settings_trim(line);
    value = desktop_settings_trim(equals + 1);
    if (*key == '\0') {
        (void)snprintf(error, error_size, "ab3d2.ini line %zu has no key", line_number);
        return 0;
    }
    if (desktop_settings_equals_ci(key, "start_level")) {
        if (!desktop_settings_parse_unsigned(value, DESKTOP_SETTINGS_LEVEL_COUNT, &number) ||
            number == 0u) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: start_level must be 1 through %u", line_number,
                           DESKTOP_SETTINGS_LEVEL_COUNT);
            return 0;
        }
        settings->start_level_index = (uint16_t)(number - 1u);
        return 1;
    }
    if (desktop_settings_equals_ci(key, "infinite_health")) {
        if (!desktop_settings_parse_bool(value, &settings->infinite_health)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: infinite_health must be a boolean", line_number);
            return 0;
        }
        return 1;
    }
    if (desktop_settings_equals_ci(key, "infinite_ammo")) {
        if (!desktop_settings_parse_bool(value, &settings->infinite_ammo)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: infinite_ammo must be a boolean", line_number);
            return 0;
        }
        return 1;
    }
    if (desktop_settings_equals_ci(key, "all_weapons")) {
        if (!desktop_settings_parse_bool(value, &settings->all_weapons)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: all_weapons must be a boolean", line_number);
            return 0;
        }
        return 1;
    }
    if (desktop_settings_equals_ci(key, "all_keys")) {
        if (!desktop_settings_parse_bool(value, &settings->all_keys)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: all_keys must be a boolean", line_number);
            return 0;
        }
        return 1;
    }
    if (desktop_settings_equals_ci(key, "quicksave_load") ||
        desktop_settings_equals_ci(key, "quick_save_load") ||
        desktop_settings_equals_ci(key, "quickload_save")) {
        if (!desktop_settings_parse_bool(value, &settings->quicksave_load)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: quicksave_load must be a boolean", line_number);
            return 0;
        }
        return 1;
    }
    if (desktop_settings_equals_ci(key, "load_autosave")) {
        if (!desktop_settings_parse_bool(value, &settings->load_autosave)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: load_autosave must be a boolean", line_number);
            return 0;
        }
        return 1;
    }
    if (desktop_settings_equals_ci(key, "always_run") ||
        desktop_settings_equals_ci(key, "run_default")) {
        if (!desktop_settings_parse_bool(value, &settings->always_run)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: always_run must be a boolean", line_number);
            return 0;
        }
        return 1;
    }
    if (desktop_settings_equals_ci(key, "volume")) {
        if (!desktop_settings_parse_unsigned(value, 100u, &number)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: volume must be 0 through 100", line_number);
            return 0;
        }
        settings->volume = (uint8_t)number;
        return 1;
    }
    if (desktop_settings_equals_ci(key, "world_light_tessellation")) {
        if (!desktop_settings_parse_unsigned(value, 8u, &number) ||
            (number != 1u && number != 2u && number != 4u && number != 8u)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: world_light_tessellation must be 1, 2, 4, or 8",
                           line_number);
            return 0;
        }
        settings->world_light_tessellation = (uint8_t)number;
        return 1;
    }
    if (desktop_settings_equals_ci(key, "renderer")) {
        if (!renderer_backend_from_string(value, &settings->renderer_backend)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: renderer must be opengl or rtx",
                           line_number);
            return 0;
        }
        return 1;
    }
    if (desktop_settings_equals_ci(key, "rtx_target_fps")) {
        if (!desktop_settings_parse_unsigned(value, 240u, &number) ||
            number < 30u) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: rtx_target_fps must be 30 through 240",
                           line_number);
            return 0;
        }
        settings->rtx_target_fps = (uint16_t)number;
        return 1;
    }
    if (desktop_settings_equals_ci(key, "rtx_dynamic_resolution")) {
        if (!desktop_settings_parse_bool(value, &settings->rtx_dynamic_resolution)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: rtx_dynamic_resolution must be a boolean",
                           line_number);
            return 0;
        }
        return 1;
    }
    if (desktop_settings_equals_ci(key, "rtx_resolution_scale")) {
        if (!desktop_settings_parse_unsigned(value, 100u, &number) ||
            number < 50u) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: rtx_resolution_scale must be 50 through 100",
                           line_number);
            return 0;
        }
        settings->rtx_resolution_scale = (uint8_t)number;
        return 1;
    }
    if (desktop_settings_equals_ci(key, "rtx_denoiser_iterations")) {
        if (!desktop_settings_parse_unsigned(value, 4u, &number) ||
            (number != 2u && number != 4u)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: rtx_denoiser_iterations must be 2 or 4",
                           line_number);
            return 0;
        }
        settings->rtx_denoiser_iterations = (uint8_t)number;
        return 1;
    }
    if (desktop_settings_equals_ci(key, "rtx_bloom")) {
        if (!desktop_settings_parse_bool(value, &settings->rtx_bloom)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: rtx_bloom must be a boolean",
                           line_number);
            return 0;
        }
        return 1;
    }
    if (desktop_settings_equals_ci(key, "rtx_debug_view")) {
        if (!renderer_rtx_debug_view_from_string(
                value, &settings->rtx_debug_view)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: rtx_debug_view must be final, albedo, normal, roughness, metalness, emissive, direct, indirect, specular, variance, history, or gradients",
                           line_number);
            return 0;
        }
        return 1;
    }
    return 1;
}

void desktop_settings_default(DesktopSettings *settings)
{
    if (!settings) {
        return;
    }
    memset(settings, 0, sizeof(*settings));
    settings->always_run = UINT8_MAX;
    settings->volume = 100u;
    settings->world_light_tessellation = 4u;
    settings->renderer_backend = RENDERER_BACKEND_OPENGL;
    settings->rtx_dynamic_resolution = 0u;
    settings->rtx_resolution_scale = 100u;
    settings->rtx_denoiser_iterations = 4u;
    settings->rtx_bloom = UINT8_MAX;
    settings->rtx_target_fps = 60u;
    settings->rtx_debug_view = RENDERER_RTX_DEBUG_FINAL;
}

int desktop_settings_parse(DesktopSettings *settings, const char *text, size_t text_size,
                           char *error, size_t error_size)
{
    size_t offset = 0u;
    size_t line_number = 0u;

    if (!settings || (!text && text_size != 0u)) {
        desktop_settings_set_error(error, error_size, "desktop settings parser received null input");
        return 0;
    }
    while (offset < text_size) {
        char line[DESKTOP_SETTINGS_LINE_CAPACITY];
        size_t line_length = 0u;
        char *trimmed;

        ++line_number;
        while (offset < text_size && text[offset] != '\n') {
            if (line_length + 1u >= sizeof(line)) {
                (void)snprintf(error, error_size,
                               "ab3d2.ini line %zu exceeds %u bytes", line_number,
                               DESKTOP_SETTINGS_LINE_CAPACITY - 1u);
                return 0;
            }
            line[line_length++] = text[offset++];
        }
        if (offset < text_size && text[offset] == '\n') {
            ++offset;
        }
        line[line_length] = '\0';
        trimmed = desktop_settings_trim(line);
        if (*trimmed == '\0' || *trimmed == '#' || *trimmed == ';') {
            continue;
        }
        if (!desktop_settings_apply_line(settings, trimmed, line_number, error, error_size)) {
            return 0;
        }
    }
    return 1;
}

DesktopSettingsLoadResult desktop_settings_load_file(DesktopSettings *settings,
                                                     const char *path,
                                                     char *error, size_t error_size)
{
    FILE *file;
    long file_size;
    char *text;
    size_t bytes_read;
    int parsed;

    if (!settings || !path || !*path) {
        desktop_settings_set_error(error, error_size, "desktop settings path is empty");
        return DESKTOP_SETTINGS_LOAD_ERROR;
    }
    file = fopen(path, "rb");
    if (!file) {
        if (errno == ENOENT) {
            return DESKTOP_SETTINGS_LOAD_NOT_FOUND;
        }
        (void)snprintf(error, error_size, "could not open %s", path);
        return DESKTOP_SETTINGS_LOAD_ERROR;
    }
    if (fseek(file, 0L, SEEK_END) != 0 || (file_size = ftell(file)) < 0L ||
        fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        (void)snprintf(error, error_size, "could not measure %s", path);
        return DESKTOP_SETTINGS_LOAD_ERROR;
    }
    if ((unsigned long)file_size > SIZE_MAX - 1u) {
        fclose(file);
        (void)snprintf(error, error_size, "%s is too large", path);
        return DESKTOP_SETTINGS_LOAD_ERROR;
    }
    text = malloc((size_t)file_size + 1u);
    if (!text) {
        fclose(file);
        desktop_settings_set_error(error, error_size, "out of memory loading desktop settings");
        return DESKTOP_SETTINGS_LOAD_ERROR;
    }
    bytes_read = fread(text, 1u, (size_t)file_size, file);
    if (fclose(file) != 0 || bytes_read != (size_t)file_size) {
        free(text);
        (void)snprintf(error, error_size, "could not read %s", path);
        return DESKTOP_SETTINGS_LOAD_ERROR;
    }
    text[bytes_read] = '\0';
    parsed = desktop_settings_parse(settings, text, bytes_read, error, error_size);
    free(text);
    return parsed ? DESKTOP_SETTINGS_LOAD_OK : DESKTOP_SETTINGS_LOAD_ERROR;
}
