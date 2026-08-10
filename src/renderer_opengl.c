#include "renderer_opengl.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#if defined(__EMSCRIPTEN__)
#include <SDL_opengles2.h>
#else
#include <SDL_opengl.h>
#endif

/* The shader subset is shared by desktop OpenGL 2.1 and WebGL 1 / GLES 2. */
enum {
    RENDERER_OPENGL_POSITION_ATTRIBUTE = 0,
    RENDERER_OPENGL_TEXTURE_COORDINATE_ATTRIBUTE = 1,
    RENDERER_OPENGL_SOURCE_LIGHT_ATTRIBUTE = 2,
    RENDERER_OPENGL_SOURCE_COLOR_ATTRIBUTE = 3,
    RENDERER_OPENGL_TEXTURE_CACHE_INITIAL_CAPACITY = 64,
    RENDERER_OPENGL_WINDOW_MINIMUM_SIZE = 1
};

/* Source Y coordinates are 8.8 fixed point; X/Z coordinates are integer words. */
static const float renderer_opengl_source_y_unit = 1.0f / 256.0f;
static const float renderer_opengl_pi = 3.14159265358979323846f;
static const float renderer_opengl_near_plane = 0.05f;
static const float renderer_opengl_far_plane = 8192.0f;
static const float renderer_opengl_source_angle_full_turn = 8192.0f;
static const float renderer_opengl_source_angle_quarter_turn = 2048.0f;
/* The authored shade tables are fitted before upload.  Eight comfortably
 * covers every source response while retaining useful 8-bit parameter precision. */
static const float renderer_opengl_light_response_exponent_maximum = 8.0f;

typedef struct {
    PFNGLCREATESHADERPROC create_shader;
    PFNGLSHADERSOURCEPROC shader_source;
    PFNGLCOMPILESHADERPROC compile_shader;
    PFNGLGETSHADERIVPROC get_shader_iv;
    PFNGLGETSHADERINFOLOGPROC get_shader_info_log;
    PFNGLDELETESHADERPROC delete_shader;
    PFNGLCREATEPROGRAMPROC create_program;
    PFNGLATTACHSHADERPROC attach_shader;
    PFNGLBINDATTRIBLOCATIONPROC bind_attrib_location;
    PFNGLLINKPROGRAMPROC link_program;
    PFNGLGETPROGRAMIVPROC get_program_iv;
    PFNGLGETPROGRAMINFOLOGPROC get_program_info_log;
    PFNGLDELETEPROGRAMPROC delete_program;
    PFNGLUSEPROGRAMPROC use_program;
    PFNGLGETUNIFORMLOCATIONPROC get_uniform_location;
    PFNGLUNIFORMMATRIX4FVPROC uniform_matrix_4fv;
    PFNGLUNIFORM1FPROC uniform_1f;
    PFNGLUNIFORM3FPROC uniform_3f;
    PFNGLUNIFORM1IPROC uniform_1i;
    PFNGLACTIVETEXTUREPROC active_texture;
    PFNGLGENBUFFERSPROC gen_buffers;
    PFNGLDELETEBUFFERSPROC delete_buffers;
    PFNGLBINDBUFFERPROC bind_buffer;
    PFNGLBUFFERDATAPROC buffer_data;
    PFNGLENABLEVERTEXATTRIBARRAYPROC enable_vertex_attrib_array;
    PFNGLDISABLEVERTEXATTRIBARRAYPROC disable_vertex_attrib_array;
    PFNGLVERTEXATTRIBPOINTERPROC vertex_attrib_pointer;
} RendererOpenGLFunctions;

typedef struct {
    float x;
    float y;
    float z;
    float u;
    float v;
    float source_light;
    float source_red;
    float source_green;
    float source_blue;
} RendererOpenGLVertex;

typedef struct {
    GLuint texture;
    const uint8_t *source_bytes;
    size_t source_byte_count;
    const uint8_t *source_palette_bytes;
    size_t source_palette_byte_count;
    const uint8_t *source_display_palette_bytes;
    size_t source_display_palette_byte_count;
    uint32_t source_asset_id;
    SceneTextureWindow texture_window;
    SceneSpriteFrameMetrics frame_metrics;
    uint16_t width;
    uint16_t height;
    uint8_t kind;
    uint8_t source_effect;
    GLuint light_response_exponent_texture;
    GLuint light_response_floor_texture;
} RendererOpenGLTexture;

typedef enum {
    RENDERER_OPENGL_TEXTURE_WALL,
    RENDERER_OPENGL_TEXTURE_FLAT,
    RENDERER_OPENGL_TEXTURE_SPRITE,
    RENDERER_OPENGL_TEXTURE_BACKDROP,
    /* objdrawhires.s:doapoly source map converted once to a neutral RGBA texture. */
    RENDERER_OPENGL_TEXTURE_VECTOR
} RendererOpenGLTextureKind;

struct RendererOpenGL {
    SDL_Window *window;
    SDL_GLContext context;
    RendererOpenGLFunctions gl;
    GLuint program;
    GLuint vertex_buffer;
    GLuint white_texture;
    GLint view_projection_uniform;
    GLint point_size_uniform;
    GLint texture_uniform;
    GLint opacity_uniform;
    GLint material_light_response_enabled_uniform;
    GLint material_light_response_exponent_texture_uniform;
    GLint material_light_response_floor_texture_uniform;
    RendererOpenGLTexture *textures;
    size_t texture_count;
    size_t texture_capacity;
    size_t last_view_weapon_coverage;
    uint64_t last_frame_rgb_checksum;
    uint8_t measure_view_weapon_coverage;
};

static void renderer_opengl_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static void renderer_opengl_set_sdl_error(char *error, size_t error_size, const char *operation)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s: %s", operation, SDL_GetError());
    }
}

/*
 * modules/transform.s:RotateLevelPts subtracts and rotates the low X/Z words
 * directly.  In contrast, player height is 12*1024 and floor/roof records
 * use the same 8.8 Y domain (for example, Level A is y=0..-32768).  Preserve
 * that mixed source representation here: integer X/Z units, 8.8 down-positive
 * Y converted to native up-positive units.  The low-word conversion also
 * matches the source's `move.w Plr_XOff_l` and `move.w Plr_ZOff_l` reads.
 */
static void renderer_opengl_world_point(const SceneWorldPoint *point, float *out_x,
                                        float *out_y, float *out_z)
{
    *out_x = (float)(int16_t)(uint16_t)point->x;
    *out_y = -(float)point->y * renderer_opengl_source_y_unit;
    *out_z = (float)(int16_t)(uint16_t)point->z;
}

/*
 * World lighting is not an RGB multiplier.  The original paths first turn
 * their 300-centred CurrentPointBrights entry into a palette-row coordinate.
 * Keep the two source equations separate:
 *
 * - hiresgourwall.s doubles a wall's point delta before the final ASR #1,
 *   yielding point_delta + view_depth / 256.
 * - hires.s:goursides/dofloorGOUR uses point_delta + view_depth / 512.
 *
 * The source row zero is brightest.  Material conversion resolves that
 * neutral row once; the continuous inverse row coordinate retains the live
 * source light range in the filtered GPU presentation.
 */
static float renderer_opengl_world_palette_light(const SceneVertex *source_vertex,
                                                 const SceneCamera *camera,
                                                 SceneGeometryPrimitive primitive)
{
    float point_x;
    float point_y;
    float point_z;
    float camera_x;
    float camera_y;
    float camera_z;
    float yaw;
    float forward_depth;
    float shade;
    float row_count;

    renderer_opengl_world_point(&source_vertex->position, &point_x, &point_y, &point_z);
    renderer_opengl_world_point(&camera->position, &camera_x, &camera_y, &camera_z);
    (void)point_y;
    (void)camera_y;
    yaw = (float)camera->yaw * (2.0f * renderer_opengl_pi / 8192.0f);
    forward_depth = (point_x - camera_x) * sinf(yaw) + (point_z - camera_z) * cosf(yaw);
    if (forward_depth < 0.0f) {
        forward_depth = 0.0f;
    }

    if (primitive == SCENE_GEOMETRY_PRIMITIVE_WALL) {
        /* hiresgourwall.s: (2 * point_delta + (depth >> 7)) >> 1. */
        shade = (float)source_vertex->source_light_level - 300.0f +
            forward_depth / 256.0f;
        row_count = 32.0f;
    } else {
        /* hires.s:goursides/dofloorGOUR: point delta plus the /512 depth term. */
        shade = (float)source_vertex->source_light_level - 300.0f +
            forward_depth / 512.0f;
        row_count = 31.0f;
    }

    if (shade < 0.0f) {
        shade = 0.0f;
    }
    if (shade > row_count - 1.0f) {
        shade = row_count - 1.0f;
    }
    return 1.0f - shade / (row_count - 1.0f);
}

static float renderer_opengl_sprite_light(int16_t source_light)
{
    float result = 0.45f + ((float)source_light - 300.0f) / 96.0f;

    if (result < 0.05f) {
        return 0.05f;
    }
    if (result > 1.25f) {
        return 1.25f;
    }
    return result;
}

static void renderer_opengl_make_vertex(RendererOpenGLVertex *out_vertex,
                                        const SceneVertex *source_vertex,
                                        float texture_u, float texture_v,
                                        const SceneCamera *camera,
                                        SceneGeometryPrimitive primitive)
{
    renderer_opengl_world_point(&source_vertex->position, &out_vertex->x, &out_vertex->y,
                                &out_vertex->z);
    out_vertex->u = texture_u;
    out_vertex->v = texture_v;
    out_vertex->source_light = renderer_opengl_world_palette_light(source_vertex, camera, primitive);
    out_vertex->source_red = 1.0f;
    out_vertex->source_green = 1.0f;
    out_vertex->source_blue = 1.0f;
}

static double renderer_opengl_cross_xz(const SceneVertex *first, const SceneVertex *second,
                                       const SceneVertex *third)
{
    double ab_x = (double)second->position.x - first->position.x;
    double ab_z = (double)second->position.z - first->position.z;
    double ac_x = (double)third->position.x - first->position.x;
    double ac_z = (double)third->position.z - first->position.z;

    return ab_x * ac_z - ab_z * ac_x;
}

static int renderer_opengl_point_in_triangle_xz(const SceneVertex *point,
                                                const SceneVertex *first,
                                                const SceneVertex *second,
                                                const SceneVertex *third,
                                                int winding)
{
    double first_cross = renderer_opengl_cross_xz(first, second, point) * winding;
    double second_cross = renderer_opengl_cross_xz(second, third, point) * winding;
    double third_cross = renderer_opengl_cross_xz(third, first, point) * winding;

    return first_cross >= 0.0 && second_cross >= 0.0 && third_cross >= 0.0;
}

static int renderer_opengl_triangulate_polygon(const SceneGeometry *geometry,
                                               float texture_u_scale,
                                               float texture_v_scale,
                                               float texture_v_offset,
                                               const SceneCamera *camera,
                                               RendererOpenGLVertex **out_vertices,
                                               uint32_t *out_vertex_count,
                                               char *error, size_t error_size)
{
    uint32_t *indices;
    RendererOpenGLVertex *vertices;
    uint32_t active_count;
    uint32_t output_index = 0u;
    uint32_t guard;
    double signed_area = 0.0;
    int winding;

    if (geometry->vertex_count < 3u) {
        renderer_opengl_set_error(error, error_size,
                                  "source polygon has fewer than three boundary vertices");
        return 0;
    }
    if (geometry->vertex_count > UINT32_MAX / 3u + 2u ||
        (size_t)(geometry->vertex_count - 2u) > SIZE_MAX / (3u * sizeof(*vertices))) {
        renderer_opengl_set_error(error, error_size, "source polygon is too large to triangulate");
        return 0;
    }
    indices = malloc((size_t)geometry->vertex_count * sizeof(*indices));
    vertices = malloc((size_t)(geometry->vertex_count - 2u) * 3u * sizeof(*vertices));
    if (!indices || !vertices) {
        free(indices);
        free(vertices);
        renderer_opengl_set_error(error, error_size, "polygon triangulation allocation failed");
        return 0;
    }
    for (uint32_t index = 0u; index < geometry->vertex_count; ++index) {
        const SceneVertex *first = &geometry->vertices[index];
        const SceneVertex *second = &geometry->vertices[(index + 1u) % geometry->vertex_count];

        signed_area += (double)first->position.x * second->position.z -
            (double)second->position.x * first->position.z;
        indices[index] = index;
    }
    if (signed_area == 0.0) {
        free(indices);
        free(vertices);
        renderer_opengl_set_error(error, error_size, "source polygon has zero X/Z area");
        return 0;
    }
    winding = signed_area > 0.0 ? 1 : -1;
    active_count = geometry->vertex_count;
    /* Ear clipping preserves concave authored boundary polygons without a fan shortcut. */
    for (guard = 0u; active_count > 3u; ++guard) {
        uint32_t ear_index;
        int clipped = 0;

        if (guard > geometry->vertex_count) {
            free(indices);
            free(vertices);
            renderer_opengl_set_error(error, error_size,
                                      "source polygon triangulation did not converge");
            return 0;
        }
        for (ear_index = 0u; ear_index < active_count; ++ear_index) {
            uint32_t previous = (ear_index + active_count - 1u) % active_count;
            uint32_t next = (ear_index + 1u) % active_count;
            const SceneVertex *first = &geometry->vertices[indices[previous]];
            const SceneVertex *second = &geometry->vertices[indices[ear_index]];
            const SceneVertex *third = &geometry->vertices[indices[next]];
            uint32_t point_index;
            int contains_point = 0;

            if (renderer_opengl_cross_xz(first, second, third) * winding <= 0.0) {
                continue;
            }
            for (point_index = 0u; point_index < active_count; ++point_index) {
                if (point_index == previous || point_index == ear_index || point_index == next) {
                    continue;
                }
                if (renderer_opengl_point_in_triangle_xz(
                        &geometry->vertices[indices[point_index]], first, second, third,
                        winding)) {
                    contains_point = 1;
                    break;
                }
            }
            if (contains_point) {
                continue;
            }
            renderer_opengl_make_vertex(&vertices[output_index++], first,
                                        (float)first->texture_u * texture_u_scale,
                                        ((float)first->texture_v + texture_v_offset) *
                                            texture_v_scale, camera, geometry->primitive);
            renderer_opengl_make_vertex(&vertices[output_index++], second,
                                        (float)second->texture_u * texture_u_scale,
                                        ((float)second->texture_v + texture_v_offset) *
                                            texture_v_scale, camera, geometry->primitive);
            renderer_opengl_make_vertex(&vertices[output_index++], third,
                                        (float)third->texture_u * texture_u_scale,
                                        ((float)third->texture_v + texture_v_offset) *
                                            texture_v_scale, camera, geometry->primitive);
            memmove(&indices[ear_index], &indices[ear_index + 1u],
                    (size_t)(active_count - ear_index - 1u) * sizeof(*indices));
            --active_count;
            clipped = 1;
            break;
        }
        if (!clipped) {
            free(indices);
            free(vertices);
            renderer_opengl_set_error(error, error_size,
                                      "source polygon is not a simple X/Z boundary");
            return 0;
        }
    }
    renderer_opengl_make_vertex(&vertices[output_index++], &geometry->vertices[indices[0u]],
                                (float)geometry->vertices[indices[0u]].texture_u * texture_u_scale,
                                ((float)geometry->vertices[indices[0u]].texture_v +
                                 texture_v_offset) * texture_v_scale, camera, geometry->primitive);
    renderer_opengl_make_vertex(&vertices[output_index++], &geometry->vertices[indices[1u]],
                                (float)geometry->vertices[indices[1u]].texture_u * texture_u_scale,
                                ((float)geometry->vertices[indices[1u]].texture_v +
                                 texture_v_offset) * texture_v_scale, camera, geometry->primitive);
    renderer_opengl_make_vertex(&vertices[output_index++], &geometry->vertices[indices[2u]],
                                (float)geometry->vertices[indices[2u]].texture_u * texture_u_scale,
                                ((float)geometry->vertices[indices[2u]].texture_v +
                                 texture_v_offset) * texture_v_scale, camera, geometry->primitive);
    free(indices);
    *out_vertices = vertices;
    *out_vertex_count = output_index;
    return 1;
}

static uint16_t renderer_opengl_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static uint32_t renderer_opengl_read_be32(const uint8_t *source)
{
    return ((uint32_t)source[0] << 24) | ((uint32_t)source[1] << 16) |
           ((uint32_t)source[2] << 8) | source[3];
}

static int renderer_opengl_display_color(const uint8_t *palette, size_t palette_size,
                                         uint8_t color_index, uint8_t out_color[4])
{
    size_t offset = (size_t)color_index * 6u;

    if (!palette || offset > palette_size || 6u > palette_size - offset) {
        return 0;
    }
    /* draw_Palette_vw stores 16-bit big-endian RGB components; the low bytes are RGB8. */
    out_color[0] = palette[offset + 1u];
    out_color[1] = palette[offset + 3u];
    out_color[2] = palette[offset + 5u];
    out_color[3] = UINT8_MAX;
    return 1;
}

static int renderer_opengl_write_palette_texel(uint8_t *pixels, size_t pixel_offset,
                                               const uint8_t *display_palette,
                                               size_t display_palette_size,
                                               uint8_t color_index, int transparent)
{
    if (!renderer_opengl_display_color(display_palette, display_palette_size, color_index,
                                       pixels + pixel_offset)) {
        return 0;
    }
    if (transparent) {
        pixels[pixel_offset + 3u] = 0u;
    }
    return 1;
}

/* The source palette is display-referred RGB.  Keep that encoding on the
 * GLES2/OpenGL 2.1 texture, then explicitly convert around GPU lighting. */
static float renderer_opengl_srgb_to_linear(float component)
{
    if (component <= 0.04045f) {
        return component / 12.92f;
    }
    return powf((component + 0.055f) / 1.055f, 2.4f);
}

static float renderer_opengl_linear_to_srgb(float component)
{
    if (component <= 0.0031308f) {
        return component * 12.92f;
    }
    return 1.055f * powf(component, 1.0f / 2.4f) - 0.055f;
}

static uint8_t renderer_opengl_linear_to_srgb_byte(float component)
{
    float encoded = renderer_opengl_linear_to_srgb(component);

    if (encoded <= 0.0f) {
        return 0u;
    }
    if (encoded >= 1.0f) {
        return UINT8_MAX;
    }
    return (uint8_t)(encoded * 255.0f + 0.5f);
}

static uint8_t renderer_opengl_unit_float_to_byte(float value)
{
    if (value <= 0.0f) {
        return 0u;
    }
    if (value >= 1.0f) {
        return UINT8_MAX;
    }
    return (uint8_t)(value * (float)UINT8_MAX + 0.5f);
}

/*
 * Fit the source palette response for one source texel colour.  The output is
 * ordinary linear-light material data (an exponent and retained black point),
 * not an indexed palette lookup.  It can consequently be sampled and filtered
 * alongside a converted RGBA albedo texture by the modern renderer.
 */
static int renderer_opengl_palette_index_light_response(
    const uint8_t *source_palette, size_t source_palette_size,
    const uint8_t *display_palette, size_t display_palette_size,
    size_t palette_width, size_t palette_row_stride, size_t palette_entry_stride,
    size_t first_shade_row, size_t shade_row_count, uint8_t source_index,
    float out_exponent[3], float out_floor[3], char *error, size_t error_size)
{
    if (!source_palette || !display_palette || !out_exponent || !out_floor ||
        source_index >= palette_width || shade_row_count < 2u ||
        first_shade_row > SIZE_MAX - shade_row_count ||
        palette_row_stride > SIZE_MAX / (first_shade_row + shade_row_count) ||
        source_palette_size < (first_shade_row + shade_row_count) * palette_row_stride) {
        renderer_opengl_set_error(error, error_size,
                                  "source palette light-response descriptor is invalid");
        return 0;
    }
    for (uint32_t component = 0u; component < 3u; ++component) {
        uint8_t base_color[4];
        uint8_t floor_color[4];
        float base_linear;
        float floor_linear;
        double floor;
        double numerator = 0.0;
        double denominator = 0.0;

        if (!renderer_opengl_display_color(
                display_palette, display_palette_size,
                source_palette[first_shade_row * palette_row_stride +
                               (size_t)source_index * palette_entry_stride],
                base_color) ||
            !renderer_opengl_display_color(
                display_palette, display_palette_size,
                source_palette[(first_shade_row + shade_row_count - 1u) * palette_row_stride +
                               (size_t)source_index * palette_entry_stride],
                floor_color)) {
            renderer_opengl_set_error(error, error_size,
                                      "source palette light response has an invalid display colour");
            return 0;
        }
        base_linear = renderer_opengl_srgb_to_linear(
            (float)base_color[component] / (float)UINT8_MAX);
        if (base_linear == 0.0f) {
            out_exponent[component] = 1.0f;
            out_floor[component] = 0.0f;
            continue;
        }
        floor_linear = renderer_opengl_srgb_to_linear(
            (float)floor_color[component] / (float)UINT8_MAX);
        floor = (double)floor_linear / (double)base_linear;
        if (floor < 0.0) {
            floor = 0.0;
        } else if (floor > 1.0) {
            floor = 1.0;
        }
        out_floor[component] = (float)floor;
        if (floor == 1.0) {
            out_exponent[component] = 1.0f;
            continue;
        }
        for (size_t shade_row = 1u; shade_row + 1u < shade_row_count; ++shade_row) {
            uint8_t shaded_color[4];
            float shaded_linear;
            double source_light = 1.0 - (double)shade_row /
                (double)(shade_row_count - 1u);
            double ratio;
            double normalized_ratio;
            double log_light = log(source_light);

            if (!renderer_opengl_display_color(
                    display_palette, display_palette_size,
                    source_palette[(first_shade_row + shade_row) * palette_row_stride +
                                   (size_t)source_index * palette_entry_stride],
                    shaded_color)) {
                renderer_opengl_set_error(error, error_size,
                                          "source palette shade observation has an invalid display colour");
                return 0;
            }
            shaded_linear = renderer_opengl_srgb_to_linear(
                (float)shaded_color[component] / (float)UINT8_MAX);
            ratio = (double)shaded_linear / (double)base_linear;
            if (ratio < 0.0) {
                ratio = 0.0;
            } else if (ratio > 1.0) {
                ratio = 1.0;
            }
            normalized_ratio = (ratio - floor) / (1.0 - floor);
            if (normalized_ratio <= 0.0) {
                continue;
            }
            if (normalized_ratio > 1.0) {
                normalized_ratio = 1.0;
            }
            numerator += log_light * log(normalized_ratio);
            denominator += log_light * log_light;
        }
        if (denominator == 0.0) {
            out_exponent[component] = 1.0f;
        } else if (!isfinite(numerator / denominator)) {
            renderer_opengl_set_error(error, error_size,
                                      "source palette has an invalid linear light response");
            return 0;
        } else {
            float exponent = (float)(numerator / denominator);

            /* Some valid source table entries intentionally retain one colour
             * across every shade row. A zero exponent is the constrained
             * continuous fit for that emissive response; it is not malformed
             * input or a generic lighting fallback. */
            if (exponent < 0.0f) {
                exponent = 0.0f;
            }
            out_exponent[component] = exponent > renderer_opengl_light_response_exponent_maximum ?
                renderer_opengl_light_response_exponent_maximum : exponent;
        }
    }
    return 1;
}

/*
 * Convert the source's discrete shade tables into a continuous linear-light
 * response once while building a true-colour material. Wall rows 0..31 and
 * flat rows 32..62 are the colour observations produced respectively by
 * hiresgourwall.s:drawwallPACK*G and hires.s:draw_GoraudFloor. Fit a
 * per-channel power curve through those observations; the GPU later
 * interpolates its source brightness normally and never selects a palette
 * row per texel.
 */
/*
 * Build the modern material's response maps before resolving its source texels
 * to RGBA.  A material can contain several palette entries with distinct
 * authored hue shifts in darkness, so a material-wide average is not enough
 * to reproduce the source art.  These maps hold linear-light parameters, not
 * palette indices; the forward shader still receives continuous Gouraud
 * brightness and applies one filtered true-colour material response.
 */
static int renderer_opengl_build_material_light_response_maps(
    const uint8_t *pixels, uint16_t width, uint16_t height, const SceneMaterial *material,
    RendererOpenGLTextureKind kind, uint8_t **out_exponent_pixels,
    uint8_t **out_floor_pixels, char *error, size_t error_size)
{
    enum {
        WALL_PALETTE_WIDTH = 32u,
        WALL_PALETTE_ROW_STRIDE = 64u,
        WALL_PALETTE_ROW_COUNT = 32u,
        FLAT_PALETTE_WIDTH = 256u,
        FLAT_FIRST_SHADE_ROW = 32u,
        FLAT_PALETTE_ROW_COUNT = 31u
    };
    size_t palette_width;
    size_t palette_row_stride;
    size_t palette_entry_stride;
    size_t first_shade_row;
    size_t shade_row_count;
    uint8_t *exponent_pixels;
    uint8_t *floor_pixels;
    uint8_t response_valid[FLAT_PALETTE_WIDTH] = {0u};
    float exponents[FLAT_PALETTE_WIDTH][3];
    float floors[FLAT_PALETTE_WIDTH][3];

    if (!pixels || width == 0u || height == 0u || !material || !out_exponent_pixels ||
        !out_floor_pixels || !material->source_palette_bytes ||
        !material->source_display_palette_bytes ||
        (size_t)width > SIZE_MAX / (size_t)height / 4u) {
        renderer_opengl_set_error(error, error_size,
                                  "source material response-map descriptor is invalid");
        return 0;
    }
    if (kind == RENDERER_OPENGL_TEXTURE_WALL) {
        palette_width = WALL_PALETTE_WIDTH;
        palette_row_stride = WALL_PALETTE_ROW_STRIDE;
        palette_entry_stride = 2u;
        first_shade_row = 0u;
        shade_row_count = WALL_PALETTE_ROW_COUNT;
    } else if (kind == RENDERER_OPENGL_TEXTURE_FLAT) {
        palette_width = FLAT_PALETTE_WIDTH;
        palette_row_stride = FLAT_PALETTE_WIDTH;
        palette_entry_stride = 1u;
        first_shade_row = FLAT_FIRST_SHADE_ROW;
        shade_row_count = FLAT_PALETTE_ROW_COUNT;
    } else {
        renderer_opengl_set_error(error, error_size, "source material response-map kind is invalid");
        return 0;
    }
    exponent_pixels = malloc((size_t)width * height * 4u);
    floor_pixels = malloc((size_t)width * height * 4u);
    if (!exponent_pixels || !floor_pixels) {
        free(exponent_pixels);
        free(floor_pixels);
        renderer_opengl_set_error(error, error_size,
                                  "source material response-map allocation failed");
        return 0;
    }
    for (size_t pixel_index = 0u; pixel_index < (size_t)width * height; ++pixel_index) {
        size_t pixel_offset = pixel_index * 4u;
        uint8_t source_index = pixels[pixel_offset];

        if (source_index >= palette_width ||
            (!response_valid[source_index] &&
             !renderer_opengl_palette_index_light_response(
                 material->source_palette_bytes, material->source_palette_byte_count,
                 material->source_display_palette_bytes,
                 material->source_display_palette_byte_count, palette_width, palette_row_stride,
                 palette_entry_stride, first_shade_row, shade_row_count, source_index,
                 exponents[source_index], floors[source_index], error, error_size))) {
            free(exponent_pixels);
            free(floor_pixels);
            return 0;
        }
        response_valid[source_index] = 1u;
        for (uint32_t component = 0u; component < 3u; ++component) {
            exponent_pixels[pixel_offset + component] = renderer_opengl_unit_float_to_byte(
                exponents[source_index][component] /
                renderer_opengl_light_response_exponent_maximum);
            floor_pixels[pixel_offset + component] =
                renderer_opengl_unit_float_to_byte(floors[source_index][component]);
        }
        exponent_pixels[pixel_offset + 3u] = UINT8_MAX;
        floor_pixels[pixel_offset + 3u] = UINT8_MAX;
    }
    *out_exponent_pixels = exponent_pixels;
    *out_floor_pixels = floor_pixels;
    return 1;
}

static int renderer_opengl_decode_wall_texture(const SceneMaterial *material,
                                                const SceneTextureWindow *window,
                                                uint8_t **out_pixels, uint16_t *out_width,
                                                uint16_t *out_height, char *error,
                                                size_t error_size)
{
    uint8_t *pixels;
    uint16_t x;
    uint16_t y;
    size_t pixel_count;

    if (!material || !window || !out_pixels || !out_width || !out_height ||
        !material->source_bytes || material->source_byte_count < 2048u ||
        window->u_period == 0u ||
        window->v_period == 0u ||
        (size_t)window->u_period > SIZE_MAX / (size_t)window->v_period ||
        (size_t)window->u_period * (size_t)window->v_period > SIZE_MAX / 4u) {
        renderer_opengl_set_error(error, error_size, "source wall texture descriptor is invalid");
        return 0;
    }
    pixel_count = (size_t)window->u_period * (size_t)window->v_period;
    pixels = calloc(pixel_count, 4u);
    if (!pixels) {
        renderer_opengl_set_error(error, error_size, "source wall texture conversion allocation failed");
        return 0;
    }
    for (y = 0u; y < window->v_period; ++y) {
        for (x = 0u; x < window->u_period; ++x) {
            uint16_t source_u = (uint16_t)(window->u_offset + x);
            size_t strip_offset = 2048u + (size_t)(source_u / 3u) *
                (size_t)window->v_period * 2u + (size_t)y * 2u;
            uint8_t packed_texel;

            if (strip_offset > material->source_byte_count ||
                2u > material->source_byte_count - strip_offset) {
                free(pixels);
                renderer_opengl_set_error(error, error_size,
                                          "source wall texture strip is outside its WAD asset");
                return 0;
            }
            switch (source_u % 3u) {
            case 0u:
                packed_texel = (uint8_t)(material->source_bytes[strip_offset + 1u] & 31u);
                break;
            case 1u:
                packed_texel = (uint8_t)((renderer_opengl_read_be16(
                    material->source_bytes + strip_offset) >> 5u) & 31u);
                break;
            default:
                packed_texel = (uint8_t)((material->source_bytes[strip_offset] >> 2u) & 31u);
                break;
            }
            /* The source strip is indexed through its 32 x 32 shade table in the shader. */
            pixels[((size_t)y * window->u_period + x) * 4u] = packed_texel;
            /* draw_ScreenWallStripGouraud writes palette entry zero; it is not a cutout key. */
            pixels[((size_t)y * window->u_period + x) * 4u + 3u] = UINT8_MAX;
        }
    }
    *out_pixels = pixels;
    *out_width = window->u_period;
    *out_height = window->v_period;
    return 1;
}

static int renderer_opengl_decode_flat_texture(const SceneMaterial *material,
                                                uint8_t **out_pixels, uint16_t *out_width,
                                                uint16_t *out_height, char *error,
                                                size_t error_size)
{
    enum { LOGICAL_TILE_SIZE = 64u, TILE_ROW_STRIDE = 1024u };
    uint8_t *pixels;
    uint16_t x;
    uint16_t y;
    size_t tile_offset;

    if (!material || !out_pixels || !out_width || !out_height || !material->source_bytes ||
        material->source_byte_count < LOGICAL_TILE_SIZE * TILE_ROW_STRIDE) {
        renderer_opengl_set_error(error, error_size, "source flat texture descriptor is invalid");
        return 0;
    }
    pixels = malloc(LOGICAL_TILE_SIZE * LOGICAL_TILE_SIZE * 4u);
    if (!pixels) {
        renderer_opengl_set_error(error, error_size, "source flat texture conversion allocation failed");
        return 0;
    }
    /*
     * draw_FloorLine indexes the 64 KiB floortile lookup with its packed
     * coordinate mask. A graph tile origin can cross the end of that source
     * lookup, so preserve the source logical-address wrap instead of reading
     * into an adjacent host allocation.
     */
    tile_offset = (size_t)material->source_asset_id % material->source_byte_count;
    for (y = 0u; y < LOGICAL_TILE_SIZE; ++y) {
        for (x = 0u; x < LOGICAL_TILE_SIZE; ++x) {
            size_t source_offset = (tile_offset + (size_t)y * TILE_ROW_STRIDE +
                                    (size_t)x * 4u) % material->source_byte_count;
            pixels[((size_t)y * LOGICAL_TILE_SIZE + x) * 4u] =
                material->source_bytes[source_offset];
            pixels[((size_t)y * LOGICAL_TILE_SIZE + x) * 4u + 3u] = UINT8_MAX;
        }
    }
    *out_pixels = pixels;
    *out_width = LOGICAL_TILE_SIZE;
    *out_height = LOGICAL_TILE_SIZE;
    return 1;
}

/*
 * `hiresgourwall.s:drawwallPACK*G` and `draw_floor.s:draw_GoraudFloor`
 * first map a packed source texel through the brightest source palette row.
 * Resolve that mapping here, before the texture reaches the GPU. The native
 * renderer can then linearly filter and mipmap ordinary RGBA texels without
 * ever blending unrelated palette indices into the coloured seams visible in
 * the indexed lookup path.
 */
static int renderer_opengl_resolve_material_texture(uint8_t *pixels, uint16_t width,
                                                    uint16_t height,
                                                    const SceneMaterial *material,
                                                    RendererOpenGLTextureKind kind,
                                                    char *error, size_t error_size)
{
    enum {
        WALL_PALETTE_WIDTH = 32u,
        FLAT_FIRST_SHADE_ROW = 32u,
        FLAT_PALETTE_WIDTH = 256u
    };

    if (!pixels || width == 0u || height == 0u || !material ||
        !material->source_palette_bytes || !material->source_display_palette_bytes) {
        renderer_opengl_set_error(error, error_size, "source material palette descriptor is invalid");
        return 0;
    }
    if (kind == RENDERER_OPENGL_TEXTURE_WALL) {
        if (material->source_palette_byte_count < (size_t)WALL_PALETTE_WIDTH * 2u) {
            renderer_opengl_set_error(error, error_size,
                                      "source wall palette has no bright source row");
            return 0;
        }
    } else if (kind == RENDERER_OPENGL_TEXTURE_FLAT) {
        if (material->source_palette_byte_count <
            (size_t)(FLAT_FIRST_SHADE_ROW + 1u) * FLAT_PALETTE_WIDTH) {
            renderer_opengl_set_error(error, error_size,
                                      "source flat palette has no bright Gouraud row");
            return 0;
        }
    } else {
        renderer_opengl_set_error(error, error_size, "source material palette kind is invalid");
        return 0;
    }
    for (size_t pixel_index = 0u; pixel_index < (size_t)width * height; ++pixel_index) {
        size_t pixel_offset = pixel_index * 4u;
        uint8_t source_index = pixels[pixel_offset];
        size_t palette_offset = kind == RENDERER_OPENGL_TEXTURE_WALL ?
            (size_t)source_index * 2u :
            (size_t)FLAT_FIRST_SHADE_ROW * FLAT_PALETTE_WIDTH + source_index;

        if ((kind == RENDERER_OPENGL_TEXTURE_WALL && source_index >= WALL_PALETTE_WIDTH) ||
            palette_offset >= material->source_palette_byte_count ||
            !renderer_opengl_write_palette_texel(
                pixels, pixel_offset, material->source_display_palette_bytes,
                material->source_display_palette_byte_count,
                material->source_palette_bytes[palette_offset], pixels[pixel_offset + 3u] == 0u)) {
            renderer_opengl_set_error(error, error_size,
                                      "source material palette references invalid source colour");
            return 0;
        }
    }
    return 1;
}

static int renderer_opengl_decode_sprite_texture(const SceneSprite *sprite,
                                                  uint8_t **out_pixels, uint16_t *out_width,
                                                  uint16_t *out_height, char *error,
                                                  size_t error_size)
{
    uint8_t *pixels;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    size_t table_offset;
    size_t palette_offset = 0u;

    if (!sprite || !out_pixels || !out_width || !out_height || !sprite->source_bytes ||
        !sprite->source_aux_bytes || !sprite->source_palette_bytes ||
        !sprite->source_display_palette_bytes ||
        (sprite->source != SCENE_SPRITE_SOURCE_OBJECT_BITMAP &&
         sprite->source != SCENE_SPRITE_SOURCE_GLARE_BITMAP) ||
        sprite->frame_metrics.strip_count == 0u || sprite->frame_metrics.line_count == 0u ||
        (size_t)sprite->frame_metrics.strip_count >
            SIZE_MAX / (size_t)sprite->frame_metrics.line_count ||
        (size_t)sprite->frame_metrics.strip_count * (size_t)sprite->frame_metrics.line_count >
            SIZE_MAX / 4u) {
        renderer_opengl_set_error(error, error_size, "source bitmap sprite descriptor is invalid");
        return 0;
    }
    /* objdrawhires.s:draw_Bitmap indexes one of four 256-byte light palettes. */
    if ((sprite->flags & SCENE_SPRITE_FLAG_LIGHT_PALETTE) != 0u) {
        uint8_t light_palette = (uint8_t)(sprite->source_effect & 0x7fu);

        if (light_palette < 2u || light_palette >= 6u) {
            renderer_opengl_set_error(error, error_size,
                                      "source bitmap light-palette selector is invalid");
            return 0;
        }
        palette_offset = (size_t)(light_palette - 2u) * 256u;
        if (palette_offset > sprite->source_palette_byte_count ||
            256u > sprite->source_palette_byte_count - palette_offset) {
            renderer_opengl_set_error(error, error_size,
                                      "source bitmap light palette is outside its asset");
            return 0;
        }
    }
    width = sprite->frame_metrics.strip_count;
    height = sprite->frame_metrics.line_count;
    table_offset = (size_t)sprite->frame_metrics.pointer_table_index * 4u;
    if (table_offset > sprite->source_aux_byte_count ||
        (size_t)width > (sprite->source_aux_byte_count - table_offset) / 4u) {
        renderer_opengl_set_error(error, error_size, "source bitmap sprite PTR table is invalid");
        return 0;
    }
    pixels = calloc((size_t)width * height, 4u);
    if (!pixels) {
        renderer_opengl_set_error(error, error_size, "source bitmap sprite conversion allocation failed");
        return 0;
    }
    for (x = 0u; x < width; ++x) {
        uint32_t source_pointer = renderer_opengl_read_be32(sprite->source_aux_bytes +
                                                             table_offset + (size_t)x * 4u);
        uint8_t pack = (uint8_t)(source_pointer >> 24u);
        size_t column_offset = source_pointer & UINT32_C(0x00ffffff);

        if (source_pointer == 0u) {
            continue;
        }
        if (pack > 2u || column_offset > sprite->source_byte_count ||
            sprite->frame_metrics.down_strip > UINT16_MAX - height ||
            (size_t)sprite->frame_metrics.down_strip + height >
                (sprite->source_byte_count - column_offset) / 2u) {
            free(pixels);
            renderer_opengl_set_error(error, error_size,
                                      "source bitmap sprite WAD column is invalid");
            return 0;
        }
        for (y = 0u; y < height; ++y) {
            size_t word_offset = column_offset +
                ((size_t)sprite->frame_metrics.down_strip + y) * 2u;
            uint16_t packed_word = renderer_opengl_read_be16(sprite->source_bytes + word_offset);
            uint8_t packed_texel;
            uint8_t color_index;

            switch (pack) {
            case 0u:
                packed_texel = (uint8_t)(packed_word & 31u);
                break;
            case 1u:
                packed_texel = (uint8_t)((packed_word >> 5u) & 31u);
                break;
            default:
                packed_texel = (uint8_t)((packed_word >> 2u) & 31u);
                break;
            }
            if (palette_offset > sprite->source_palette_byte_count ||
                (size_t)packed_texel * 2u + 2u >
                    sprite->source_palette_byte_count - palette_offset) {
                free(pixels);
                renderer_opengl_set_error(error, error_size,
                                          "source bitmap sprite palette is invalid");
                return 0;
            }
            color_index = sprite->source_palette_bytes[
                palette_offset + (size_t)packed_texel * 2u];
            if (!renderer_opengl_write_palette_texel(
                    pixels, ((size_t)y * width + x) * 4u,
                    sprite->source_display_palette_bytes,
                    sprite->source_display_palette_byte_count, color_index,
                    packed_texel == 0u)) {
                free(pixels);
                renderer_opengl_set_error(error, error_size,
                                          "source bitmap palette references invalid display colour");
                return 0;
            }
        }
    }
    *out_pixels = pixels;
    *out_width = width;
    *out_height = height;
    return 1;
}

static int renderer_opengl_is_power_of_two(uint16_t value)
{
    return value != 0u && (value & (uint16_t)(value - 1u)) == 0u;
}

static int renderer_opengl_create_texture(const uint8_t *pixels, uint16_t width, uint16_t height,
                                          int repeat, int filtered, int linear_data,
                                          GLuint *out_texture, char *error,
                                          size_t error_size)
{
    GLuint texture = 0u;
    int mipmapped = filtered != 0 && renderer_opengl_is_power_of_two(width) &&
        renderer_opengl_is_power_of_two(height);

    if (!pixels || !out_texture || width == 0u || height == 0u) {
        renderer_opengl_set_error(error, error_size, "OpenGL texture conversion has invalid pixels");
        return 0;
    }
    glGenTextures(1, &texture);
    if (texture == 0u) {
        renderer_opengl_set_error(error, error_size, "OpenGL texture allocation failed");
        return 0;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    mipmapped != 0 ? GL_LINEAR_MIPMAP_LINEAR :
                    filtered != 0 ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filtered ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    if (mipmapped != 0) {
        const uint8_t *level_pixels = pixels;
        uint16_t level_width = width;
        uint16_t level_height = height;
        int level = 1;

        while (level_width > 1u || level_height > 1u) {
            uint16_t next_width = level_width > 1u ? level_width / 2u : 1u;
            uint16_t next_height = level_height > 1u ? level_height / 2u : 1u;
            uint8_t *next_pixels = malloc((size_t)next_width * next_height * 4u);

            if (!next_pixels) {
                if (level_pixels != pixels) {
                    free((void *)level_pixels);
                }
                glDeleteTextures(1, &texture);
                renderer_opengl_set_error(error, error_size,
                                          "OpenGL mipmap conversion allocation failed");
                return 0;
            }
            for (uint16_t y = 0u; y < next_height; ++y) {
                for (uint16_t x = 0u; x < next_width; ++x) {
                    size_t destination = ((size_t)y * next_width + x) * 4u;
                    size_t source_x = (size_t)x * 2u;
                    size_t source_y = (size_t)y * 2u;

                    for (uint32_t component = 0u; component < 4u; ++component) {
                        float linear_total = 0.0f;
                        uint32_t total = 0u;
                        uint32_t samples = 0u;

                        for (uint32_t sample_y = 0u; sample_y < 2u; ++sample_y) {
                            for (uint32_t sample_x = 0u; sample_x < 2u; ++sample_x) {
                                size_t actual_x = source_x + sample_x;
                                size_t actual_y = source_y + sample_y;

                                if (actual_x < level_width && actual_y < level_height) {
                                    uint8_t sample = level_pixels[(actual_y * level_width + actual_x) *
                                                                  4u + component];

                                    if (component < 3u && linear_data == 0) {
                                        linear_total += renderer_opengl_srgb_to_linear(
                                            (float)sample / 255.0f);
                                    } else {
                                        total += sample;
                                    }
                                    ++samples;
                                }
                            }
                        }
                        next_pixels[destination + component] = component < 3u && linear_data == 0 ?
                            renderer_opengl_linear_to_srgb_byte(linear_total / (float)samples) :
                            (uint8_t)(total / samples);
                    }
                }
            }
            glTexImage2D(GL_TEXTURE_2D, level++, GL_RGBA, next_width, next_height, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, next_pixels);
            if (level_pixels != pixels) {
                free((void *)level_pixels);
            }
            level_pixels = next_pixels;
            level_width = next_width;
            level_height = next_height;
        }
        if (level_pixels != pixels) {
            free((void *)level_pixels);
        }
    }
    if (glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &texture);
        renderer_opengl_set_error(error, error_size, "OpenGL texture upload failed");
        return 0;
    }
    *out_texture = texture;
    return 1;
}

static int renderer_opengl_texture_cache_reserve(RendererOpenGL *renderer, size_t capacity,
                                                 char *error, size_t error_size)
{
    RendererOpenGLTexture *textures;

    if (capacity <= renderer->texture_capacity) {
        return 1;
    }
    if (capacity > SIZE_MAX / sizeof(*textures)) {
        renderer_opengl_set_error(error, error_size, "OpenGL texture cache is too large");
        return 0;
    }
    textures = realloc(renderer->textures, capacity * sizeof(*textures));
    if (!textures) {
        renderer_opengl_set_error(error, error_size, "OpenGL texture cache allocation failed");
        return 0;
    }
    renderer->textures = textures;
    renderer->texture_capacity = capacity;
    return 1;
}

static int renderer_opengl_find_material_texture(RendererOpenGL *renderer,
                                                 RendererOpenGLTextureKind kind,
                                                 const SceneMaterial *material,
                                                 const SceneTextureWindow *window,
                                                 const RendererOpenGLTexture **out_texture, char *error,
                                                 size_t error_size)
{
    uint8_t *pixels = NULL;
    uint8_t *exponent_pixels = NULL;
    uint8_t *floor_pixels = NULL;
    uint16_t width = 0u;
    uint16_t height = 0u;
    GLuint texture = 0u;
    GLuint exponent_texture = 0u;
    GLuint floor_texture = 0u;
    SceneTextureWindow key_window = {0};

    if (!renderer || !material || !out_texture) {
        renderer_opengl_set_error(error, error_size, "scene material texture request is invalid");
        return 0;
    }
    if (window) {
        key_window = *window;
    }
    for (size_t index = 0u; index < renderer->texture_count; ++index) {
        const RendererOpenGLTexture *cached = &renderer->textures[index];

        if (cached->kind == (uint8_t)kind && cached->source_bytes == material->source_bytes &&
            cached->source_byte_count == material->source_byte_count &&
            cached->source_palette_bytes == material->source_palette_bytes &&
            cached->source_palette_byte_count == material->source_palette_byte_count &&
            cached->source_display_palette_bytes == material->source_display_palette_bytes &&
            cached->source_display_palette_byte_count ==
                material->source_display_palette_byte_count &&
            cached->source_asset_id == material->source_asset_id &&
            memcmp(&cached->texture_window, &key_window, sizeof(key_window)) == 0) {
            *out_texture = cached;
            return 1;
        }
    }
    if ((kind == RENDERER_OPENGL_TEXTURE_WALL &&
         !renderer_opengl_decode_wall_texture(material, window, &pixels, &width, &height,
                                              error, error_size)) ||
        (kind == RENDERER_OPENGL_TEXTURE_FLAT &&
         !renderer_opengl_decode_flat_texture(material, &pixels, &width, &height, error,
                                              error_size)) ||
        !renderer_opengl_build_material_light_response_maps(
            pixels, width, height, material, kind, &exponent_pixels, &floor_pixels,
            error, error_size) ||
        !renderer_opengl_resolve_material_texture(pixels, width, height, material, kind, error,
                                                  error_size) ||
        !renderer_opengl_create_texture(pixels, width, height, 1, 1, 0, &texture, error, error_size) ||
        !renderer_opengl_create_texture(exponent_pixels, width, height, 1, 1, 1,
                                        &exponent_texture, error, error_size) ||
        !renderer_opengl_create_texture(floor_pixels, width, height, 1, 1, 1,
                                        &floor_texture, error, error_size)) {
        free(pixels);
        free(exponent_pixels);
        free(floor_pixels);
        if (texture != 0u) {
            glDeleteTextures(1, &texture);
        }
        if (exponent_texture != 0u) {
            glDeleteTextures(1, &exponent_texture);
        }
        if (floor_texture != 0u) {
            glDeleteTextures(1, &floor_texture);
        }
        return 0;
    }
    free(pixels);
    free(exponent_pixels);
    free(floor_pixels);
    if (renderer->texture_count == renderer->texture_capacity &&
        !renderer_opengl_texture_cache_reserve(
            renderer, renderer->texture_capacity == 0u ?
                RENDERER_OPENGL_TEXTURE_CACHE_INITIAL_CAPACITY : renderer->texture_capacity * 2u,
            error, error_size)) {
        glDeleteTextures(1, &texture);
        glDeleteTextures(1, &exponent_texture);
        glDeleteTextures(1, &floor_texture);
        return 0;
    }
    renderer->textures[renderer->texture_count++] = (RendererOpenGLTexture){
        texture, material->source_bytes, material->source_byte_count, material->source_palette_bytes,
        material->source_palette_byte_count, material->source_display_palette_bytes,
        material->source_display_palette_byte_count, material->source_asset_id, key_window, {0},
        width, height, (uint8_t)kind, 0u, exponent_texture, floor_texture
    };
    *out_texture = &renderer->textures[renderer->texture_count - 1u];
    return 1;
}

static int renderer_opengl_find_sprite_texture(RendererOpenGL *renderer, const SceneSprite *sprite,
                                               GLuint *out_texture, char *error,
                                               size_t error_size)
{
    uint8_t *pixels = NULL;
    uint16_t width = 0u;
    uint16_t height = 0u;
    GLuint texture = 0u;

    if (!renderer || !sprite || !out_texture) {
        renderer_opengl_set_error(error, error_size, "scene bitmap sprite texture request is invalid");
        return 0;
    }
    for (size_t index = 0u; index < renderer->texture_count; ++index) {
        const RendererOpenGLTexture *cached = &renderer->textures[index];

        if (cached->kind == RENDERER_OPENGL_TEXTURE_SPRITE &&
            cached->source_bytes == sprite->source_bytes &&
            cached->source_byte_count == sprite->source_byte_count &&
            cached->source_palette_bytes == sprite->source_palette_bytes &&
            cached->source_palette_byte_count == sprite->source_palette_byte_count &&
            cached->source_display_palette_bytes == sprite->source_display_palette_bytes &&
            cached->source_display_palette_byte_count == sprite->source_display_palette_byte_count &&
            cached->source_asset_id == sprite->source_asset_id &&
            cached->source_effect == sprite->source_effect &&
            memcmp(&cached->frame_metrics, &sprite->frame_metrics,
                   sizeof(sprite->frame_metrics)) == 0) {
            *out_texture = cached->texture;
            return 1;
        }
    }
    if (!renderer_opengl_decode_sprite_texture(sprite, &pixels, &width, &height, error, error_size) ||
        !renderer_opengl_create_texture(pixels, width, height, 0, 0, 0, &texture, error, error_size)) {
        free(pixels);
        return 0;
    }
    free(pixels);
    if (renderer->texture_count == renderer->texture_capacity &&
        !renderer_opengl_texture_cache_reserve(
            renderer, renderer->texture_capacity == 0u ?
                RENDERER_OPENGL_TEXTURE_CACHE_INITIAL_CAPACITY : renderer->texture_capacity * 2u,
            error, error_size)) {
        glDeleteTextures(1, &texture);
        return 0;
    }
    renderer->textures[renderer->texture_count++] = (RendererOpenGLTexture){
        texture, sprite->source_bytes, sprite->source_byte_count, sprite->source_palette_bytes,
        sprite->source_palette_byte_count, sprite->source_display_palette_bytes,
        sprite->source_display_palette_byte_count, sprite->source_asset_id, {0},
        sprite->frame_metrics, width, height, RENDERER_OPENGL_TEXTURE_SPRITE,
        sprite->source_effect
    };
    *out_texture = texture;
    return 1;
}

static int renderer_opengl_find_backdrop_texture(RendererOpenGL *renderer,
                                                 const SceneEnvironment *environment,
                                                 GLuint *out_texture, char *error,
                                                 size_t error_size)
{
    enum { BACKDROP_WIDTH = 648u, BACKDROP_HEIGHT = 240u };
    uint8_t *pixels;
    GLuint texture = 0u;

    if (!renderer || !environment || !out_texture || !environment->backdrop_bytes ||
        environment->backdrop_byte_count != BACKDROP_WIDTH * BACKDROP_HEIGHT ||
        !environment->source_display_palette_bytes) {
        renderer_opengl_set_error(error, error_size, "source sky backdrop descriptor is invalid");
        return 0;
    }
    for (size_t index = 0u; index < renderer->texture_count; ++index) {
        const RendererOpenGLTexture *cached = &renderer->textures[index];

        if (cached->kind == RENDERER_OPENGL_TEXTURE_BACKDROP &&
            cached->source_bytes == environment->backdrop_bytes &&
            cached->source_byte_count == environment->backdrop_byte_count &&
            cached->source_display_palette_bytes == environment->source_display_palette_bytes &&
            cached->source_display_palette_byte_count ==
                environment->source_display_palette_byte_count) {
            *out_texture = cached->texture;
            return 1;
        }
    }
    pixels = malloc((size_t)BACKDROP_WIDTH * BACKDROP_HEIGHT * 4u);
    if (!pixels) {
        renderer_opengl_set_error(error, error_size, "source sky backdrop conversion allocation failed");
        return 0;
    }
    /* Draw_SkyBackdrop walks a 240-byte source column before moving to the next X. */
    for (uint16_t y = 0u; y < BACKDROP_HEIGHT; ++y) {
        for (uint16_t x = 0u; x < BACKDROP_WIDTH; ++x) {
            if (!renderer_opengl_write_palette_texel(
                    pixels, ((size_t)y * BACKDROP_WIDTH + x) * 4u,
                    environment->source_display_palette_bytes,
                    environment->source_display_palette_byte_count,
                    environment->backdrop_bytes[(size_t)x * BACKDROP_HEIGHT + y], 0)) {
                free(pixels);
                renderer_opengl_set_error(error, error_size,
                                          "source sky backdrop references invalid display colour");
                return 0;
            }
        }
    }
    if (!renderer_opengl_create_texture(pixels, BACKDROP_WIDTH, BACKDROP_HEIGHT, 1, 1, 0,
                                        &texture, error, error_size)) {
        free(pixels);
        return 0;
    }
    free(pixels);
    if (renderer->texture_count == renderer->texture_capacity &&
        !renderer_opengl_texture_cache_reserve(
            renderer, renderer->texture_capacity == 0u ?
                RENDERER_OPENGL_TEXTURE_CACHE_INITIAL_CAPACITY : renderer->texture_capacity * 2u,
            error, error_size)) {
        glDeleteTextures(1, &texture);
        return 0;
    }
    renderer->textures[renderer->texture_count++] = (RendererOpenGLTexture){
        texture, environment->backdrop_bytes, environment->backdrop_byte_count, NULL, 0u,
        environment->source_display_palette_bytes,
        environment->source_display_palette_byte_count, 0u, {0}, {0},
        BACKDROP_WIDTH, BACKDROP_HEIGHT, RENDERER_OPENGL_TEXTURE_BACKDROP, 0u
    };
    *out_texture = texture;
    return 1;
}

static void renderer_opengl_identity(float matrix[16])
{
    memset(matrix, 0, 16u * sizeof(*matrix));
    matrix[0] = 1.0f;
    matrix[5] = 1.0f;
    matrix[10] = 1.0f;
    matrix[15] = 1.0f;
}

static void renderer_opengl_multiply(float out_matrix[16], const float left[16],
                                     const float right[16])
{
    float result[16];

    for (uint32_t column = 0u; column < 4u; ++column) {
        for (uint32_t row = 0u; row < 4u; ++row) {
            result[column * 4u + row] =
                left[0u * 4u + row] * right[column * 4u + 0u] +
                left[1u * 4u + row] * right[column * 4u + 1u] +
                left[2u * 4u + row] * right[column * 4u + 2u] +
                left[3u * 4u + row] * right[column * 4u + 3u];
        }
    }
    memcpy(out_matrix, result, sizeof(result));
}

static void renderer_opengl_view_projection(float out_matrix[16], const SceneCamera *camera,
                                            const RenderView *view, float aspect)
{
    float projection[16];
    float camera_view[16];
    float eye_x;
    float eye_y;
    float eye_z;
    /*
     * hires.s:DrawDisplay indexes SinCosTable_vw with Vis_AngPos_w, a byte
     * address.  The source comments document 4,096 words (8,192 bytes) for
     * one 2pi cycle.  modules/transform.s:RotateLevelPts then forms the
     * camera-space axes as x' = cos(x) - sin(z), z' = sin(x) + cos(z).
     */
    float yaw = (float)camera->yaw * (2.0f * renderer_opengl_pi / 8192.0f);
    float pitch = view->pitch_degrees * (renderer_opengl_pi / 180.0f);
    float forward_x = sinf(yaw) * cosf(pitch);
    float forward_y = sinf(pitch);
    float forward_z = cosf(yaw) * cosf(pitch);
    float right_x = cosf(yaw);
    float right_z = -sinf(yaw);
    float up_x = right_z * forward_y;
    float up_y = forward_z * right_x - forward_x * right_z;
    float up_z = -right_x * forward_y;
    float field_of_view = 70.0f * (renderer_opengl_pi / 180.0f);
    float focal_length = 1.0f / tanf(field_of_view * 0.5f);

    renderer_opengl_world_point(&camera->position, &eye_x, &eye_y, &eye_z);
    renderer_opengl_identity(projection);
    projection[0] = focal_length / aspect;
    projection[5] = focal_length;
    projection[10] = (renderer_opengl_far_plane + renderer_opengl_near_plane) /
        (renderer_opengl_near_plane - renderer_opengl_far_plane);
    projection[11] = -1.0f;
    projection[14] = (2.0f * renderer_opengl_far_plane * renderer_opengl_near_plane) /
        (renderer_opengl_near_plane - renderer_opengl_far_plane);
    projection[15] = 0.0f;

    renderer_opengl_identity(camera_view);
    camera_view[0] = right_x;
    camera_view[4] = 0.0f;
    camera_view[8] = right_z;
    camera_view[12] = -(right_x * eye_x + right_z * eye_z);
    camera_view[1] = up_x;
    camera_view[5] = up_y;
    camera_view[9] = up_z;
    camera_view[13] = -(up_x * eye_x + up_y * eye_y + up_z * eye_z);
    camera_view[2] = -forward_x;
    camera_view[6] = -forward_y;
    camera_view[10] = -forward_z;
    camera_view[14] = forward_x * eye_x + forward_y * eye_y + forward_z * eye_z;
    renderer_opengl_multiply(out_matrix, projection, camera_view);
}

static int renderer_opengl_load_functions(RendererOpenGL *renderer, char *error,
                                          size_t error_size)
{
#if defined(__EMSCRIPTEN__)
    /* Emscripten links this GLES 2 entry-point set directly into the Wasm module. */
    (void)error;
    (void)error_size;
    renderer->gl.create_shader = glCreateShader;
    renderer->gl.shader_source = glShaderSource;
    renderer->gl.compile_shader = glCompileShader;
    renderer->gl.get_shader_iv = glGetShaderiv;
    renderer->gl.get_shader_info_log = glGetShaderInfoLog;
    renderer->gl.delete_shader = glDeleteShader;
    renderer->gl.create_program = glCreateProgram;
    renderer->gl.attach_shader = glAttachShader;
    renderer->gl.bind_attrib_location = glBindAttribLocation;
    renderer->gl.link_program = glLinkProgram;
    renderer->gl.get_program_iv = glGetProgramiv;
    renderer->gl.get_program_info_log = glGetProgramInfoLog;
    renderer->gl.delete_program = glDeleteProgram;
    renderer->gl.use_program = glUseProgram;
    renderer->gl.get_uniform_location = glGetUniformLocation;
    renderer->gl.uniform_matrix_4fv = glUniformMatrix4fv;
    renderer->gl.uniform_1f = glUniform1f;
    renderer->gl.uniform_3f = glUniform3f;
    renderer->gl.uniform_1i = glUniform1i;
    renderer->gl.active_texture = glActiveTexture;
    renderer->gl.gen_buffers = glGenBuffers;
    renderer->gl.delete_buffers = glDeleteBuffers;
    renderer->gl.bind_buffer = glBindBuffer;
    renderer->gl.buffer_data = glBufferData;
    renderer->gl.enable_vertex_attrib_array = glEnableVertexAttribArray;
    renderer->gl.disable_vertex_attrib_array = glDisableVertexAttribArray;
    renderer->gl.vertex_attrib_pointer = glVertexAttribPointer;
    return 1;
#else
#define RENDERER_OPENGL_LOAD(member, symbol) \
    do { \
        *(void **)(&renderer->gl.member) = SDL_GL_GetProcAddress(symbol); \
        if (!renderer->gl.member) { \
            renderer_opengl_set_error(error, error_size, "OpenGL 2.1 / GLES 2 function unavailable: " symbol); \
            return 0; \
        } \
    } while (0)
    RENDERER_OPENGL_LOAD(create_shader, "glCreateShader");
    RENDERER_OPENGL_LOAD(shader_source, "glShaderSource");
    RENDERER_OPENGL_LOAD(compile_shader, "glCompileShader");
    RENDERER_OPENGL_LOAD(get_shader_iv, "glGetShaderiv");
    RENDERER_OPENGL_LOAD(get_shader_info_log, "glGetShaderInfoLog");
    RENDERER_OPENGL_LOAD(delete_shader, "glDeleteShader");
    RENDERER_OPENGL_LOAD(create_program, "glCreateProgram");
    RENDERER_OPENGL_LOAD(attach_shader, "glAttachShader");
    RENDERER_OPENGL_LOAD(bind_attrib_location, "glBindAttribLocation");
    RENDERER_OPENGL_LOAD(link_program, "glLinkProgram");
    RENDERER_OPENGL_LOAD(get_program_iv, "glGetProgramiv");
    RENDERER_OPENGL_LOAD(get_program_info_log, "glGetProgramInfoLog");
    RENDERER_OPENGL_LOAD(delete_program, "glDeleteProgram");
    RENDERER_OPENGL_LOAD(use_program, "glUseProgram");
    RENDERER_OPENGL_LOAD(get_uniform_location, "glGetUniformLocation");
    RENDERER_OPENGL_LOAD(uniform_matrix_4fv, "glUniformMatrix4fv");
    RENDERER_OPENGL_LOAD(uniform_1f, "glUniform1f");
    RENDERER_OPENGL_LOAD(uniform_3f, "glUniform3f");
    RENDERER_OPENGL_LOAD(uniform_1i, "glUniform1i");
    RENDERER_OPENGL_LOAD(active_texture, "glActiveTexture");
    RENDERER_OPENGL_LOAD(gen_buffers, "glGenBuffers");
    RENDERER_OPENGL_LOAD(delete_buffers, "glDeleteBuffers");
    RENDERER_OPENGL_LOAD(bind_buffer, "glBindBuffer");
    RENDERER_OPENGL_LOAD(buffer_data, "glBufferData");
    RENDERER_OPENGL_LOAD(enable_vertex_attrib_array, "glEnableVertexAttribArray");
    RENDERER_OPENGL_LOAD(disable_vertex_attrib_array, "glDisableVertexAttribArray");
    RENDERER_OPENGL_LOAD(vertex_attrib_pointer, "glVertexAttribPointer");
#undef RENDERER_OPENGL_LOAD
    return 1;
#endif
}

static int renderer_opengl_compile_shader(RendererOpenGL *renderer, GLenum type,
                                          const char *source, GLuint *out_shader,
                                          char *error, size_t error_size)
{
    GLuint shader;
    GLint compiled = GL_FALSE;
    GLchar log[512];
    GLsizei log_length = 0;

    shader = renderer->gl.create_shader(type);
    if (shader == 0u) {
        renderer_opengl_set_error(error, error_size, "OpenGL shader allocation failed");
        return 0;
    }
    renderer->gl.shader_source(shader, 1, (const GLchar *const *)&source, NULL);
    renderer->gl.compile_shader(shader);
    renderer->gl.get_shader_iv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
        log[0] = '\0';
        renderer->gl.get_shader_info_log(shader, (GLsizei)(sizeof(log) - 1u), &log_length, log);
        log[log_length < (GLsizei)sizeof(log) ? log_length : (GLsizei)(sizeof(log) - 1u)] = '\0';
        if (error && error_size > 0u) {
            (void)snprintf(error, error_size, "OpenGL shader compilation failed: %s", log);
        }
        renderer->gl.delete_shader(shader);
        return 0;
    }
    *out_shader = shader;
    return 1;
}

static int renderer_opengl_create_program(RendererOpenGL *renderer, char *error,
                                          size_t error_size)
{
    static const char vertex_source[] =
        "attribute vec3 a_position;\n"
        "attribute vec2 a_texture_coordinate;\n"
        "attribute float a_source_light;\n"
        "attribute vec3 a_source_color;\n"
        "uniform mat4 u_view_projection;\n"
        "uniform float u_point_size;\n"
        "varying vec2 v_texture_coordinate;\n"
        "varying float v_source_light;\n"
        "varying vec3 v_source_color;\n"
        "void main() {\n"
        "  gl_Position = u_view_projection * vec4(a_position, 1.0);\n"
        "  gl_PointSize = u_point_size;\n"
        "  v_texture_coordinate = a_texture_coordinate;\n"
        "  v_source_light = a_source_light;\n"
        "  v_source_color = a_source_color;\n"
        "}\n";
#if defined(__EMSCRIPTEN__)
    static const char fragment_source[] =
        "precision mediump float;\n"
        "varying vec2 v_texture_coordinate;\n"
        "varying float v_source_light;\n"
        "varying vec3 v_source_color;\n"
        "uniform sampler2D u_texture;\n"
        "uniform float u_opacity;\n"
        "uniform float u_material_light_response_enabled;\n"
        "uniform sampler2D u_material_light_response_exponent_texture;\n"
        "uniform sampler2D u_material_light_response_floor_texture;\n"
        "vec3 srgb_to_linear(vec3 color) {\n"
        "  vec3 low = color / 12.92;\n"
        "  vec3 high = pow((color + 0.055) / 1.055, vec3(2.4));\n"
        "  return mix(high, low, step(color, vec3(0.04045)));\n"
        "}\n"
        "vec3 linear_to_srgb(vec3 color) {\n"
        "  vec3 low = color * 12.92;\n"
        "  vec3 high = 1.055 * pow(max(color, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;\n"
        "  return mix(high, low, step(color, vec3(0.0031308)));\n"
        "}\n"
        "void main() {\n"
        "  vec4 color = texture2D(u_texture, v_texture_coordinate);\n"
        "  vec3 source_light;\n"
        "  vec3 response_exponent;\n"
        "  vec3 response_floor;\n"
        "  if (color.a < 0.5) discard;\n"
        "  source_light = max(vec3(v_source_light) * v_source_color, vec3(0.0));\n"
        "  response_exponent = texture2D(u_material_light_response_exponent_texture,\n"
        "      v_texture_coordinate).rgb * 8.0;\n"
        "  response_floor = texture2D(u_material_light_response_floor_texture,\n"
        "      v_texture_coordinate).rgb;\n"
        "  source_light = mix(srgb_to_linear(source_light),\n"
        "                     response_floor +\n"
        "                     (vec3(1.0) - response_floor) *\n"
        "                     pow(max(source_light, vec3(0.0039215686)), response_exponent),\n"
        "                     u_material_light_response_enabled);\n"
        "  gl_FragColor = vec4(linear_to_srgb(srgb_to_linear(color.rgb) * source_light),\n"
        "                      color.a * u_opacity);\n"
        "}\n";
#else
    static const char fragment_source[] =
        "varying vec2 v_texture_coordinate;\n"
        "varying float v_source_light;\n"
        "varying vec3 v_source_color;\n"
        "uniform sampler2D u_texture;\n"
        "uniform float u_opacity;\n"
        "uniform float u_material_light_response_enabled;\n"
        "uniform sampler2D u_material_light_response_exponent_texture;\n"
        "uniform sampler2D u_material_light_response_floor_texture;\n"
        "vec3 srgb_to_linear(vec3 color) {\n"
        "  vec3 low = color / 12.92;\n"
        "  vec3 high = pow((color + 0.055) / 1.055, vec3(2.4));\n"
        "  return mix(high, low, step(color, vec3(0.04045)));\n"
        "}\n"
        "vec3 linear_to_srgb(vec3 color) {\n"
        "  vec3 low = color * 12.92;\n"
        "  vec3 high = 1.055 * pow(max(color, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;\n"
        "  return mix(high, low, step(color, vec3(0.0031308)));\n"
        "}\n"
        "void main() {\n"
        "  vec4 color = texture2D(u_texture, v_texture_coordinate);\n"
        "  vec3 source_light;\n"
        "  vec3 response_exponent;\n"
        "  vec3 response_floor;\n"
        "  if (color.a < 0.5) discard;\n"
        "  source_light = max(vec3(v_source_light) * v_source_color, vec3(0.0));\n"
        "  response_exponent = texture2D(u_material_light_response_exponent_texture,\n"
        "      v_texture_coordinate).rgb * 8.0;\n"
        "  response_floor = texture2D(u_material_light_response_floor_texture,\n"
        "      v_texture_coordinate).rgb;\n"
        "  source_light = mix(srgb_to_linear(source_light),\n"
        "                     response_floor +\n"
        "                     (vec3(1.0) - response_floor) *\n"
        "                     pow(max(source_light, vec3(0.0039215686)), response_exponent),\n"
        "                     u_material_light_response_enabled);\n"
        "  gl_FragColor = vec4(linear_to_srgb(srgb_to_linear(color.rgb) * source_light),\n"
        "                      color.a * u_opacity);\n"
        "}\n";
#endif
    GLuint vertex_shader = 0u;
    GLuint fragment_shader = 0u;
    GLint linked = GL_FALSE;
    GLchar log[512];
    GLsizei log_length = 0;

    if (!renderer_opengl_compile_shader(renderer, GL_VERTEX_SHADER, vertex_source, &vertex_shader,
                                        error, error_size) ||
        !renderer_opengl_compile_shader(renderer, GL_FRAGMENT_SHADER, fragment_source,
                                        &fragment_shader, error, error_size)) {
        if (vertex_shader != 0u) {
            renderer->gl.delete_shader(vertex_shader);
        }
        if (fragment_shader != 0u) {
            renderer->gl.delete_shader(fragment_shader);
        }
        return 0;
    }
    renderer->program = renderer->gl.create_program();
    if (renderer->program == 0u) {
        renderer->gl.delete_shader(fragment_shader);
        renderer->gl.delete_shader(vertex_shader);
        renderer_opengl_set_error(error, error_size, "OpenGL program allocation failed");
        return 0;
    }
    renderer->gl.attach_shader(renderer->program, vertex_shader);
    renderer->gl.attach_shader(renderer->program, fragment_shader);
    renderer->gl.bind_attrib_location(renderer->program, RENDERER_OPENGL_POSITION_ATTRIBUTE,
                                      "a_position");
    renderer->gl.bind_attrib_location(renderer->program,
                                      RENDERER_OPENGL_TEXTURE_COORDINATE_ATTRIBUTE,
                                      "a_texture_coordinate");
    renderer->gl.bind_attrib_location(renderer->program, RENDERER_OPENGL_SOURCE_LIGHT_ATTRIBUTE,
                                      "a_source_light");
    renderer->gl.bind_attrib_location(renderer->program, RENDERER_OPENGL_SOURCE_COLOR_ATTRIBUTE,
                                      "a_source_color");
    renderer->gl.link_program(renderer->program);
    renderer->gl.delete_shader(fragment_shader);
    renderer->gl.delete_shader(vertex_shader);
    renderer->gl.get_program_iv(renderer->program, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
        log[0] = '\0';
        renderer->gl.get_program_info_log(renderer->program, (GLsizei)(sizeof(log) - 1u),
                                          &log_length, log);
        log[log_length < (GLsizei)sizeof(log) ? log_length : (GLsizei)(sizeof(log) - 1u)] = '\0';
        if (error && error_size > 0u) {
            (void)snprintf(error, error_size, "OpenGL shader link failed: %s", log);
        }
        renderer->gl.delete_program(renderer->program);
        renderer->program = 0u;
        return 0;
    }
    renderer->view_projection_uniform =
        renderer->gl.get_uniform_location(renderer->program, "u_view_projection");
    renderer->point_size_uniform = renderer->gl.get_uniform_location(renderer->program,
                                                                       "u_point_size");
    renderer->texture_uniform = renderer->gl.get_uniform_location(renderer->program, "u_texture");
    renderer->opacity_uniform = renderer->gl.get_uniform_location(renderer->program, "u_opacity");
    renderer->material_light_response_enabled_uniform = renderer->gl.get_uniform_location(
        renderer->program, "u_material_light_response_enabled");
    renderer->material_light_response_exponent_texture_uniform = renderer->gl.get_uniform_location(
        renderer->program, "u_material_light_response_exponent_texture");
    renderer->material_light_response_floor_texture_uniform = renderer->gl.get_uniform_location(
        renderer->program, "u_material_light_response_floor_texture");
    if (renderer->view_projection_uniform < 0 || renderer->point_size_uniform < 0 ||
        renderer->texture_uniform < 0 || renderer->opacity_uniform < 0 ||
        renderer->material_light_response_enabled_uniform < 0 ||
        renderer->material_light_response_exponent_texture_uniform < 0 ||
        renderer->material_light_response_floor_texture_uniform < 0) {
        renderer->gl.delete_program(renderer->program);
        renderer->program = 0u;
        renderer_opengl_set_error(error, error_size, "OpenGL shader uniforms are unavailable");
        return 0;
    }
    return 1;
}

static void renderer_opengl_use_default_light_response(RendererOpenGL *renderer)
{
    renderer->gl.uniform_1f(renderer->material_light_response_enabled_uniform, 0.0f);
}

static void renderer_opengl_use_material_light_response(
    RendererOpenGL *renderer, const RendererOpenGLTexture *texture)
{
    renderer->gl.uniform_1f(renderer->material_light_response_enabled_uniform, 1.0f);
    renderer->gl.active_texture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, texture->light_response_exponent_texture);
    renderer->gl.active_texture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, texture->light_response_floor_texture);
    renderer->gl.active_texture(GL_TEXTURE0);
}

static void renderer_opengl_use_texture_light_response(
    RendererOpenGL *renderer, const RendererOpenGLTexture *texture)
{
    renderer->gl.uniform_1f(renderer->material_light_response_enabled_uniform, 1.0f);
    renderer->gl.active_texture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, texture->light_response_exponent_texture);
    renderer->gl.active_texture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, texture->light_response_floor_texture);
    renderer->gl.active_texture(GL_TEXTURE0);
}

static int renderer_opengl_draw_vertices(RendererOpenGL *renderer,
                                         const RendererOpenGLVertex *vertices,
                                         uint32_t vertex_count, GLenum mode,
                                         char *error, size_t error_size)
{
    size_t byte_count;

    if (vertex_count == 0u) {
        return 1;
    }
    if (vertex_count > (uint32_t)INT_MAX ||
        (size_t)vertex_count > SIZE_MAX / sizeof(*vertices)) {
        renderer_opengl_set_error(error, error_size, "OpenGL draw vertex count is too large");
        return 0;
    }
    byte_count = (size_t)vertex_count * sizeof(*vertices);
    if (byte_count > (size_t)PTRDIFF_MAX) {
        renderer_opengl_set_error(error, error_size, "OpenGL draw buffer is too large");
        return 0;
    }
    renderer->gl.bind_buffer(GL_ARRAY_BUFFER, renderer->vertex_buffer);
    renderer->gl.buffer_data(GL_ARRAY_BUFFER, (ptrdiff_t)byte_count, vertices, GL_STREAM_DRAW);
    renderer->gl.vertex_attrib_pointer(RENDERER_OPENGL_POSITION_ATTRIBUTE, 3, GL_FLOAT, GL_FALSE,
                                       (GLsizei)sizeof(*vertices), (const void *)0);
    renderer->gl.vertex_attrib_pointer(RENDERER_OPENGL_TEXTURE_COORDINATE_ATTRIBUTE, 2, GL_FLOAT,
                                       GL_FALSE,
                                       (GLsizei)sizeof(*vertices),
                                       (const void *)offsetof(RendererOpenGLVertex, u));
    renderer->gl.vertex_attrib_pointer(RENDERER_OPENGL_SOURCE_LIGHT_ATTRIBUTE, 1, GL_FLOAT,
                                       GL_FALSE, (GLsizei)sizeof(*vertices),
                                       (const void *)offsetof(RendererOpenGLVertex, source_light));
    renderer->gl.vertex_attrib_pointer(RENDERER_OPENGL_SOURCE_COLOR_ATTRIBUTE, 3, GL_FLOAT,
                                       GL_FALSE, (GLsizei)sizeof(*vertices),
                                       (const void *)offsetof(RendererOpenGLVertex, source_red));
    glDrawArrays(mode, 0, (GLsizei)vertex_count);
    if (glGetError() != GL_NO_ERROR) {
        renderer_opengl_set_error(error, error_size, "OpenGL draw command failed");
        return 0;
    }
    return 1;
}

static int renderer_opengl_draw_sky(RendererOpenGL *renderer,
                                    const SceneEnvironment *environment,
                                    const SceneCamera *camera, char *error,
                                    size_t error_size)
{
    RendererOpenGLVertex vertices[6];
    float identity[16];
    float scroll;
    GLuint texture;
    int result;

    if (!environment || environment->sky_enabled == 0u) {
        return 1;
    }
    if (!camera || !renderer_opengl_find_backdrop_texture(renderer, environment, &texture,
                                                          error, error_size)) {
        return 0;
    }
    renderer_opengl_use_default_light_response(renderer);
    /* newanims.s:Draw_SkyBackdrop selects yaw*648/4096 source columns. */
    scroll = (float)(camera->yaw & 4095u) / 4096.0f;
    vertices[0] = (RendererOpenGLVertex){-1.0f, -1.0f, 0.0f, scroll, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    vertices[1] = (RendererOpenGLVertex){ 1.0f, -1.0f, 0.0f, scroll + 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    vertices[2] = (RendererOpenGLVertex){ 1.0f,  1.0f, 0.0f, scroll + 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    vertices[3] = vertices[0];
    vertices[4] = vertices[2];
    vertices[5] = (RendererOpenGLVertex){-1.0f,  1.0f, 0.0f, scroll, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    renderer_opengl_identity(identity);
    renderer->gl.uniform_matrix_4fv(renderer->view_projection_uniform, 1, GL_FALSE, identity);
    renderer->gl.uniform_1f(renderer->opacity_uniform, 1.0f);
    renderer->gl.active_texture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glDisable(GL_DEPTH_TEST);
    result = renderer_opengl_draw_vertices(renderer, vertices, 6u, GL_TRIANGLES, error, error_size);
    glEnable(GL_DEPTH_TEST);
    return result;
}

static int renderer_opengl_draw_geometry(RendererOpenGL *renderer,
                                         const SceneMaterial *material,
                                         const SceneGeometry *geometry,
                                         const SceneEnvironment *environment,
                                         const SceneCamera *camera,
                                         char *error, size_t error_size)
{
    RendererOpenGLVertex *vertices = NULL;
    uint32_t vertex_count;
    RendererOpenGLTextureKind texture_kind;
    const RendererOpenGLTexture *texture;
    float texture_u_scale;
    float texture_v_scale;
    float texture_v_offset = 0.0f;
    int water_blend = 0;
    int cull_backfaces;
    int result;

    if (!material || !geometry || !geometry->vertices || !camera) {
        renderer_opengl_set_error(error, error_size, "scene geometry has no material or vertices");
        return 0;
    }
    if (geometry->primitive == SCENE_GEOMETRY_PRIMITIVE_WALL) {
        if (geometry->texture_window.u_period == 0u || geometry->texture_window.v_period == 0u) {
            renderer_opengl_set_error(error, error_size, "scene wall geometry has no source texture window");
            return 0;
        }
        texture_kind = RENDERER_OPENGL_TEXTURE_WALL;
        texture_u_scale = 1.0f / (float)geometry->texture_window.u_period;
        texture_v_scale = 1.0f / (float)geometry->texture_window.v_period;
        /*
         * hires.s:DrawDisplay:draw_WallYOffset_w offsets source wall samples
         * for its screen-column projection.  Scene vertices already retain
         * their authored world-space V coordinates, so applying the viewer Y
         * again would make a fixed wall texture slide during camera bob.
         */
    } else {
        texture_kind = RENDERER_OPENGL_TEXTURE_FLAT;
        texture_u_scale = 1.0f / 64.0f;
        texture_v_scale = 1.0f / 64.0f;
        if (geometry->primitive == SCENE_GEOMETRY_PRIMITIVE_WATER) {
            uint32_t source_offset;

            if (!environment || !environment->water_bytes ||
                environment->water_byte_count != 256u * 256u) {
                renderer_opengl_set_error(error, error_size,
                                          "water geometry has no source water-frame table");
                return 0;
            }
            /* hires.s uses one of eight table origins plus wateroff every VBlank. */
            source_offset = ((uint32_t)environment->water_frame * 128u +
                             (environment->water_scroll & 0xffu)) & 0xffffu;
            texture_v_offset = (float)((environment->water_scroll +
                environment->water_bytes[source_offset]) & 63u);
            water_blend = 1;
        }
    }
    if (!renderer_opengl_find_material_texture(renderer, texture_kind, material,
                                               texture_kind == RENDERER_OPENGL_TEXTURE_WALL ?
                                                   &geometry->texture_window : NULL,
                                               &texture, error, error_size)) {
        return 0;
    }
    renderer->gl.active_texture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture->texture);
    renderer_opengl_use_material_light_response(renderer, texture);
    renderer->gl.uniform_1f(renderer->opacity_uniform, water_blend != 0 ? 0.68f : 1.0f);
    if (water_blend != 0) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
    }
    if (geometry->topology == SCENE_GEOMETRY_TOPOLOGY_TRIANGLE_LIST) {
        if (geometry->vertex_count == 0u || geometry->vertex_count % 3u != 0u ||
            (size_t)geometry->vertex_count > SIZE_MAX / sizeof(*vertices)) {
            renderer_opengl_set_error(error, error_size, "scene triangle list is malformed");
            return 0;
        }
        vertices = malloc((size_t)geometry->vertex_count * sizeof(*vertices));
        if (!vertices) {
            renderer_opengl_set_error(error, error_size, "triangle-list conversion allocation failed");
            return 0;
        }
        for (uint32_t index = 0u; index < geometry->vertex_count; ++index) {
            renderer_opengl_make_vertex(&vertices[index], &geometry->vertices[index],
                                        (float)geometry->vertices[index].texture_u *
                                            texture_u_scale,
                                        ((float)geometry->vertices[index].texture_v +
                                         texture_v_offset) * texture_v_scale, camera,
                                        geometry->primitive);
        }
        vertex_count = geometry->vertex_count;
    } else if (geometry->topology == SCENE_GEOMETRY_TOPOLOGY_POLYGON_BOUNDARY) {
        if (!renderer_opengl_triangulate_polygon(geometry, texture_u_scale, texture_v_scale,
                                                 texture_v_offset, camera, &vertices, &vertex_count,
                                                 error, error_size)) {
            return 0;
        }
    } else {
        renderer_opengl_set_error(error, error_size, "scene geometry topology is unsupported");
        return 0;
    }
    /* hireswall.s:Draw_Wall returns through wallfacingaway for a rear-facing
     * record.  The native world transform reflects source Y, so the retained
     * source-visible winding becomes OpenGL front-facing winding.  With every
     * source graph submitted, reject that inverse record in the GPU depth pass
     * rather than allowing it to overwrite the wall's light and material. */
    cull_backfaces = geometry->primitive == SCENE_GEOMETRY_PRIMITIVE_WALL;
    if (cull_backfaces != 0) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
    }
    result = renderer_opengl_draw_vertices(renderer, vertices, vertex_count, GL_TRIANGLES,
                                           error, error_size);
    free(vertices);
    if (water_blend != 0) {
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
    if (cull_backfaces != 0) {
        glDisable(GL_CULL_FACE);
    }
    renderer->gl.uniform_1f(renderer->opacity_uniform, 1.0f);
    return result;
}

static int renderer_opengl_draw_sprite(RendererOpenGL *renderer, const SceneSprite *sprite,
                                       const SceneCamera *camera, char *error,
                                       size_t error_size)
{
    RendererOpenGLVertex vertices[6];
    GLuint texture;
    float center_x;
    float center_y;
    float center_z;
    float yaw;
    float right_x;
    float right_z;
    float half_width;
    float half_height;
    float left_u;
    float right_u;
    float source_light;
    int additive;
    int result;

    if (!sprite || !camera) {
        renderer_opengl_set_error(error, error_size, "scene sprite has no camera or descriptor");
        return 0;
    }
    if (sprite->source != SCENE_SPRITE_SOURCE_OBJECT_BITMAP &&
        sprite->source != SCENE_SPRITE_SOURCE_GLARE_BITMAP) {
        if (error && error_size > 0u) {
            (void)snprintf(error, error_size,
                           "bitmap sprite record %u has unsupported source %u or zero source size",
                           sprite->source_record_id, (unsigned int)sprite->source);
        }
        return 0;
    }
    /* draw_Bitmap's byte-width/height tests branch to object_behind. */
    if (sprite->source_width == 0u || sprite->source_height == 0u) {
        return 1;
    }
    if (!renderer_opengl_find_sprite_texture(renderer, sprite, &texture, error, error_size)) {
        return 0;
    }
    renderer_opengl_use_default_light_response(renderer);
    renderer_opengl_world_point(&sprite->position, &center_x, &center_y, &center_z);
    /* Same Vis_AngPos_w byte-addressed angle convention as the world camera. */
    yaw = (float)camera->yaw * (2.0f * renderer_opengl_pi / 8192.0f);
    right_x = cosf(yaw);
    right_z = -sinf(yaw);
    /* objdrawhires.s applies its auxiliary bitmap offsets in 128 source units. */
    center_x += right_x * (float)sprite->source_aux_offset_x * 0.5f;
    center_z += right_z * (float)sprite->source_aux_offset_x * 0.5f;
    center_y -= (float)sprite->source_aux_offset_y * 0.5f;
    half_width = (float)sprite->source_width * 0.5f;
    half_height = (float)sprite->source_height * 0.5f;
    left_u = (sprite->flags & SCENE_SPRITE_FLAG_FLIP_HORIZONTAL) != 0u ? 1.0f : 0.0f;
    right_u = 1.0f - left_u;
    source_light = renderer_opengl_sprite_light(sprite->source_light_level);
    vertices[0] = (RendererOpenGLVertex){center_x - right_x * half_width, center_y + half_height,
                                          center_z - right_z * half_width, left_u, 0.0f, source_light,
                                          1.0f, 1.0f, 1.0f};
    vertices[1] = (RendererOpenGLVertex){center_x + right_x * half_width, center_y + half_height,
                                          center_z + right_z * half_width, right_u, 0.0f, source_light,
                                          1.0f, 1.0f, 1.0f};
    vertices[2] = (RendererOpenGLVertex){center_x + right_x * half_width, center_y - half_height,
                                          center_z + right_z * half_width, right_u, 1.0f, source_light,
                                          1.0f, 1.0f, 1.0f};
    vertices[3] = vertices[0];
    vertices[4] = vertices[2];
    vertices[5] = (RendererOpenGLVertex){center_x - right_x * half_width, center_y - half_height,
                                          center_z - right_z * half_width, left_u, 1.0f, source_light,
                                          1.0f, 1.0f, 1.0f};
    renderer->gl.active_texture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    additive = (sprite->flags & SCENE_SPRITE_FLAG_ADDITIVE) != 0u ||
        sprite->source == SCENE_SPRITE_SOURCE_GLARE_BITMAP;
    renderer->gl.uniform_1f(renderer->opacity_uniform,
                            sprite->source == SCENE_SPRITE_SOURCE_GLARE_BITMAP ? 0.8f : 1.0f);
    if (additive != 0) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glDepthMask(GL_FALSE);
    }
    result = renderer_opengl_draw_vertices(renderer, vertices, 6u, GL_TRIANGLES, error, error_size);
    if (additive != 0) {
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
    renderer->gl.uniform_1f(renderer->opacity_uniform, 1.0f);
    return result;
}

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} RendererOpenGLVectorPoint;

static int16_t renderer_opengl_read_be16s(const uint8_t *source)
{
    return (int16_t)renderer_opengl_read_be16(source);
}

static int renderer_opengl_vector_append(RendererOpenGLVertex **vertices,
                                         uint32_t *vertex_count, uint32_t *vertex_capacity,
                                         const RendererOpenGLVertex *vertex,
                                         char *error, size_t error_size)
{
    RendererOpenGLVertex *grown;
    uint32_t capacity;

    if (!vertices || !vertex_count || !vertex_capacity || !vertex ||
        *vertex_count == UINT32_MAX) {
        renderer_opengl_set_error(error, error_size, "source vector model has too many vertices");
        return 0;
    }
    if (*vertex_count == *vertex_capacity) {
        capacity = *vertex_capacity == 0u ? 96u : *vertex_capacity * 2u;
        if (capacity < *vertex_capacity || (size_t)capacity > SIZE_MAX / sizeof(*grown)) {
            renderer_opengl_set_error(error, error_size, "source vector model allocation is too large");
            return 0;
        }
        grown = realloc(*vertices, (size_t)capacity * sizeof(*grown));
        if (!grown) {
            renderer_opengl_set_error(error, error_size, "source vector model conversion allocation failed");
            return 0;
        }
        *vertices = grown;
        *vertex_capacity = capacity;
    }
    (*vertices)[(*vertex_count)++] = *vertex;
    return 1;
}

static int renderer_opengl_vector_model_point(const SceneSprite *sprite,
                                               const SceneCamera *camera,
                                               const RenderView *view,
                                               const uint8_t *point_bytes,
                                               int camera_space,
                                               RendererOpenGLVertex *out_vertex)
{
    RendererOpenGLVectorPoint source_point;
    float center_x;
    float center_y;
    float center_z;
    float yaw;
    float local_x;
    float local_y;
    float local_z;

    if (!sprite || !camera || !view || !point_bytes || !out_vertex) {
        return 0;
    }
    source_point.x = renderer_opengl_read_be16s(point_bytes);
    source_point.y = renderer_opengl_read_be16s(point_bytes + 2u);
    source_point.z = renderer_opengl_read_be16s(point_bytes + 4u);
    yaw = (float)sprite->yaw * (2.0f * renderer_opengl_pi /
                                renderer_opengl_source_angle_full_turn);
    renderer_opengl_world_point(&sprite->position, &center_x, &center_y, &center_z);
    if (camera_space != 0) {
        float camera_x;
        float camera_y;
        float camera_z;
        float camera_yaw = (float)camera->yaw * (2.0f * renderer_opengl_pi /
                                                  renderer_opengl_source_angle_full_turn);
        float pitch = view->pitch_degrees * (renderer_opengl_pi / 180.0f);
        float forward_x = sinf(camera_yaw) * cosf(pitch);
        float forward_y = sinf(pitch);
        float forward_z = cosf(camera_yaw) * cosf(pitch);
        float right_x = cosf(camera_yaw);
        float right_z = -sinf(camera_yaw);
        float up_x = right_z * forward_y;
        float up_y = forward_z * right_x - forward_x * right_z;
        float up_z = -right_x * forward_y;
        float source_relative_yaw = ((float)sprite->yaw -
                                     renderer_opengl_source_angle_quarter_turn -
                                     (float)camera->yaw) *
            (2.0f * renderer_opengl_pi / renderer_opengl_source_angle_full_turn);
        float source_view_x;
        float source_view_z;
        float bob;

        renderer_opengl_world_point(&camera->position, &camera_x, &camera_y, &camera_z);
        local_x = (float)source_point.x * 0.0125f;
        local_y = -(float)source_point.y * 0.0125f;
        local_z = (float)source_point.z * 0.0125f;
        /*
         * objdrawhires.s:draw_PolygonModel rotates every vector model by
         * EntT_CurrentAngle_w - 2048 - Vis_AngPos_w.  Perform that complete
         * source-relative rotation in camera space: applying the ordinary
         * world yaw first makes the companion turn the wrong way as the
         * player turns.  Plr1_Use writes its reversed player angle.
         */
        source_view_x = local_x * sinf(source_relative_yaw) -
            local_z * cosf(source_relative_yaw);
        source_view_z = local_z * sinf(source_relative_yaw) +
            local_x * cosf(source_relative_yaw);
        center_x = camera_x + forward_x * 1.3f - right_x * 0.35f;
        center_z = camera_z + forward_z * 1.3f - right_z * 0.35f;
        /*
         * objdrawhires.s:draw_PolygonModel special-cases Plr1_Use's
         * ENT_NEXT_2 companion at depth one and resets its projection centre
         * to the screen centre.  Its ObjT vertical word still carries the
         * live Plr1_Use bob, but is not a world-space origin for that pass.
         * Keep that source bob as a small camera-space displacement instead
         * of placing the whole model at the player's body height.
         */
        bob = (center_y - camera_y) * (1.0f / 64.0f);
        center_x += up_x * (-0.55f + bob);
        center_y = camera_y + up_y * (-0.55f + bob);
        center_z += up_z * (-0.55f + bob);
        out_vertex->x = center_x + right_x * source_view_x + forward_x * source_view_z +
            up_x * local_y;
        out_vertex->y = center_y + forward_y * source_view_z + up_y * local_y;
        out_vertex->z = center_z + right_z * source_view_x + forward_z * source_view_z +
            up_z * local_y;
    } else {
        local_x = (float)source_point.x * 0.5f;
        local_y = -(float)source_point.y * 0.25f;
        local_z = (float)source_point.z * 0.5f;
        out_vertex->x = center_x + cosf(yaw) * local_x - sinf(yaw) * local_z;
        out_vertex->y = center_y + local_y;
        out_vertex->z = center_z + sinf(yaw) * local_x + cosf(yaw) * local_z;
    }
    out_vertex->u = 0.5f;
    out_vertex->v = 0.5f;
    out_vertex->source_light = renderer_opengl_sprite_light(sprite->source_light_level);
    out_vertex->source_red = 1.0f;
    out_vertex->source_green = 1.0f;
    out_vertex->source_blue = 1.0f;
    return 1;
}

static int renderer_opengl_vector_face_texture_info(const SceneSprite *sprite,
                                                     const uint8_t *polygon_angle_bytes,
                                                     size_t polygon_angle_byte_count,
                                                     const uint8_t *face_bytes,
                                                     size_t *out_map_offset,
                                                     float *out_source_light,
                                                     char *error, size_t error_size)
{
    enum {
        VECTOR_LIGHT_PALETTE_ROW_COUNT = 32u
    };
    int16_t source_map_word;
    size_t source_map_offset;
    uint8_t source_polygon_angle;
    uint8_t source_light_index;
    float source_shade;

    if (!sprite || !polygon_angle_bytes || !face_bytes || !out_map_offset ||
        !out_source_light || !sprite->source_palette_bytes ||
        face_bytes[3u] >= polygon_angle_byte_count) {
        renderer_opengl_set_error(error, error_size, "source vector face has no texture map");
        return 0;
    }
    /* objdrawhires.s:doapoly accepts a signed map offset and adds 64 KiB for bit 15. */
    source_map_word = renderer_opengl_read_be16s(face_bytes);
    source_map_offset = source_map_word < 0 ?
        65536u + ((uint16_t)source_map_word & 0x7fffu) : (uint16_t)source_map_word;
    if (source_map_offset >= sprite->source_palette_byte_count) {
        renderer_opengl_set_error(error, error_size,
                                  "source vector face texture map is outside its asset");
        return 0;
    }
    /*
     * objdrawhires.s:doapoly turns this face byte into 31 - ((byte * 32 * 41)
     * >> 12), then adds the live draw_PointAndPolyBrights entry selected by
     * the model's authored polygon-angle table and current object angle.
     * Keep that source state as a continuous GPU light multiplier.  Do not
     * bake its selected Amiga palette-light row into the material texture.
     */
    source_shade = 31.0f - (float)face_bytes[2u] * (32.0f * 41.0f / 4096.0f);
    source_polygon_angle = polygon_angle_bytes[face_bytes[3u]];
    source_light_index = (uint8_t)((source_polygon_angle & 0xf0u) |
        (((uint8_t)(source_polygon_angle + (sprite->yaw >> 9u))) & 0x0fu));
    /* The 68000 MOVE.B into D5 intentionally zero-extends the source byte. */
    source_shade += (float)(uint8_t)sprite->source_point_and_polygon_brightness[
        source_light_index];
    if (source_shade < 0.0f) {
        source_shade = 0.0f;
    } else if (source_shade >= (float)VECTOR_LIGHT_PALETTE_ROW_COUNT) {
        source_shade = (float)VECTOR_LIGHT_PALETTE_ROW_COUNT - 1.0f;
    }
    *out_map_offset = source_map_offset;
    *out_source_light = 1.0f - source_shade /
        ((float)VECTOR_LIGHT_PALETTE_ROW_COUNT - 1.0f);
    return 1;
}

/*
 * objdrawhires.s:draw_PolygonModel calculates `boxbrights_vw` before it
 * starts a model's part list.  A non-zero low byte in a polygon's terminal
 * source word selects `gotlurvelyshading`, which interpolates these per-point
 * shade rows instead of applying doapoly's flat face row.  In particular,
 * `face_bytes[2]` is deliberately not part of this path.
 */
static int renderer_opengl_vector_point_source_light(const SceneSprite *sprite,
                                                      const uint8_t *point_angle_bytes,
                                                      size_t point_angle_byte_count,
                                                      uint16_t point_index,
                                                      float *out_source_light,
                                                      char *error, size_t error_size)
{
    enum {
        VECTOR_LIGHT_PALETTE_ROW_COUNT = 32u
    };
    uint8_t source_point_angle;
    uint8_t source_light_index;
    int16_t source_shade;

    if (!sprite || !point_angle_bytes || !out_source_light ||
        point_index >= point_angle_byte_count) {
        renderer_opengl_set_error(error, error_size,
                                  "source Gouraud vector point has no directional light entry");
        return 0;
    }
    source_point_angle = point_angle_bytes[point_index];
    source_light_index = (uint8_t)((source_point_angle & 0xf0u) |
        (((uint8_t)(source_point_angle + (sprite->yaw >> 9u))) & 0x0fu));
    /* `move.b` followed by BGE/W clamp preserves the signed source byte. */
    source_shade = sprite->source_point_and_polygon_brightness[source_light_index];
    if (source_shade < 0) {
        source_shade = 0;
    } else if (source_shade >= (int16_t)VECTOR_LIGHT_PALETTE_ROW_COUNT) {
        source_shade = (int16_t)VECTOR_LIGHT_PALETTE_ROW_COUNT - 1;
    }
    *out_source_light = 1.0f - (float)source_shade /
        ((float)VECTOR_LIGHT_PALETTE_ROW_COUNT - 1.0f);
    return 1;
}

static int renderer_opengl_decode_vector_face_texture(const SceneSprite *sprite,
                                                       size_t source_map_offset,
                                                       uint8_t maximum_u, uint8_t maximum_v,
                                                       uint8_t **out_pixels,
                                                       uint8_t **out_exponent_pixels,
                                                       uint8_t **out_floor_pixels,
                                                       uint16_t *out_width, uint16_t *out_height,
                                                       char *error, size_t error_size)
{
    enum {
        /* The bright source palette row is the neutral albedo conversion. */
        VECTOR_LIGHT_PALETTE_BASE_ROW = 32u,
        VECTOR_LIGHT_PALETTE_ROW_WIDTH = 256u,
        VECTOR_SOURCE_TEXEL_STRIDE = 4u
    };
    uint16_t width = (uint16_t)maximum_u + 1u;
    uint16_t height = (uint16_t)maximum_v + 1u;
    uint8_t *pixels;
    uint8_t *exponent_pixels;
    uint8_t *floor_pixels;

    if (!sprite || !out_pixels || !out_exponent_pixels || !out_floor_pixels || !out_width ||
        !out_height || !sprite->source_palette_bytes ||
        !sprite->source_light_palette_bytes || !sprite->source_display_palette_bytes ||
        source_map_offset >= sprite->source_palette_byte_count ||
        (size_t)width > SIZE_MAX / (size_t)height / 4u ||
        sprite->source_light_palette_byte_count <
            (size_t)(VECTOR_LIGHT_PALETTE_BASE_ROW + 32u) * VECTOR_LIGHT_PALETTE_ROW_WIDTH) {
        renderer_opengl_set_error(error, error_size, "source vector texture descriptor is invalid");
        return 0;
    }
    pixels = malloc((size_t)width * height * 4u);
    exponent_pixels = malloc((size_t)width * height * 4u);
    floor_pixels = malloc((size_t)width * height * 4u);
    if (!pixels || !exponent_pixels || !floor_pixels) {
        free(pixels);
        free(exponent_pixels);
        free(floor_pixels);
        renderer_opengl_set_error(error, error_size, "source vector texture conversion allocation failed");
        return 0;
    }
    for (uint16_t y = 0u; y < height; ++y) {
        for (uint16_t x = 0u; x < width; ++x) {
            size_t source_coordinate = ((size_t)y << 8u) | x;
            size_t source_texel_offset;
            size_t source_light_palette_offset;
            uint8_t source_texel;
            uint8_t source_colour;
            float exponent[3];
            float floor[3];
            size_t pixel_offset = ((size_t)y * width + x) * 4u;

            if (source_coordinate > (SIZE_MAX - source_map_offset) / VECTOR_SOURCE_TEXEL_STRIDE) {
                free(pixels);
                free(exponent_pixels);
                free(floor_pixels);
                renderer_opengl_set_error(error, error_size,
                                          "source vector texture coordinate is too large");
                return 0;
            }
            source_texel_offset = source_map_offset +
                source_coordinate * VECTOR_SOURCE_TEXEL_STRIDE;
            if (source_texel_offset > sprite->source_palette_byte_count ||
                VECTOR_SOURCE_TEXEL_STRIDE >
                    sprite->source_palette_byte_count - source_texel_offset) {
                free(pixels);
                free(exponent_pixels);
                free(floor_pixels);
                renderer_opengl_set_error(error, error_size,
                                          "source vector texture map is outside its asset");
                return 0;
            }
            source_texel = sprite->source_palette_bytes[source_texel_offset];
            source_light_palette_offset =
                (size_t)VECTOR_LIGHT_PALETTE_BASE_ROW *
                    VECTOR_LIGHT_PALETTE_ROW_WIDTH + source_texel;
            source_colour = sprite->source_light_palette_bytes[source_light_palette_offset];
            if (!renderer_opengl_write_palette_texel(
                    pixels, pixel_offset,
                    sprite->source_display_palette_bytes,
                    sprite->source_display_palette_byte_count, source_colour, 0)) {
                free(pixels);
                free(exponent_pixels);
                free(floor_pixels);
                renderer_opengl_set_error(error, error_size,
                                          "source vector light palette references an invalid display colour");
                return 0;
            }
            if (!renderer_opengl_palette_index_light_response(
                    sprite->source_light_palette_bytes,
                    sprite->source_light_palette_byte_count,
                    sprite->source_display_palette_bytes,
                    sprite->source_display_palette_byte_count,
                    VECTOR_LIGHT_PALETTE_ROW_WIDTH, VECTOR_LIGHT_PALETTE_ROW_WIDTH, 1u,
                    VECTOR_LIGHT_PALETTE_BASE_ROW, 32u, source_texel, exponent, floor,
                    error, error_size)) {
                free(pixels);
                free(exponent_pixels);
                free(floor_pixels);
                return 0;
            }
            for (uint32_t component = 0u; component < 3u; ++component) {
                exponent_pixels[pixel_offset + component] = renderer_opengl_unit_float_to_byte(
                    exponent[component] / renderer_opengl_light_response_exponent_maximum);
                floor_pixels[pixel_offset + component] =
                    renderer_opengl_unit_float_to_byte(floor[component]);
            }
            exponent_pixels[pixel_offset + 3u] = UINT8_MAX;
            floor_pixels[pixel_offset + 3u] = UINT8_MAX;
        }
    }
    *out_pixels = pixels;
    *out_exponent_pixels = exponent_pixels;
    *out_floor_pixels = floor_pixels;
    *out_width = width;
    *out_height = height;
    return 1;
}

static int renderer_opengl_find_vector_face_texture(RendererOpenGL *renderer,
                                                     const SceneSprite *sprite,
                                                     size_t source_map_offset,
                                                     uint8_t maximum_u, uint8_t maximum_v,
                                                     const RendererOpenGLTexture **out_texture,
                                                     char *error, size_t error_size)
{
    SceneTextureWindow key_window = {0};
    uint8_t *pixels = NULL;
    uint8_t *exponent_pixels = NULL;
    uint8_t *floor_pixels = NULL;
    uint16_t width = 0u;
    uint16_t height = 0u;
    GLuint texture = 0u;
    GLuint exponent_texture = 0u;
    GLuint floor_texture = 0u;

    if (!renderer || !sprite || !out_texture || source_map_offset > UINT32_MAX) {
        renderer_opengl_set_error(error, error_size, "source vector texture request is invalid");
        return 0;
    }
    key_window.u_period = (uint16_t)maximum_u + 1u;
    key_window.v_period = (uint16_t)maximum_v + 1u;
    for (size_t index = 0u; index < renderer->texture_count; ++index) {
        const RendererOpenGLTexture *cached = &renderer->textures[index];

        if (cached->kind == RENDERER_OPENGL_TEXTURE_VECTOR &&
            cached->source_bytes == sprite->source_palette_bytes &&
            cached->source_byte_count == sprite->source_palette_byte_count &&
            cached->source_palette_bytes == sprite->source_light_palette_bytes &&
            cached->source_palette_byte_count == sprite->source_light_palette_byte_count &&
            cached->source_display_palette_bytes == sprite->source_display_palette_bytes &&
            cached->source_display_palette_byte_count == sprite->source_display_palette_byte_count &&
            cached->source_asset_id == (uint32_t)source_map_offset &&
            memcmp(&cached->texture_window, &key_window, sizeof(key_window)) == 0) {
            *out_texture = cached;
            return 1;
        }
    }
    if (!renderer_opengl_decode_vector_face_texture(
            sprite, source_map_offset, maximum_u, maximum_v,
            &pixels, &exponent_pixels, &floor_pixels, &width, &height, error, error_size) ||
        !renderer_opengl_create_texture(pixels, width, height, 0, 0, 0, &texture, error, error_size) ||
        !renderer_opengl_create_texture(exponent_pixels, width, height, 0, 0, 1,
                                        &exponent_texture, error, error_size) ||
        !renderer_opengl_create_texture(floor_pixels, width, height, 0, 0, 1,
                                        &floor_texture, error, error_size)) {
        free(pixels);
        free(exponent_pixels);
        free(floor_pixels);
        if (texture != 0u) {
            glDeleteTextures(1, &texture);
        }
        if (exponent_texture != 0u) {
            glDeleteTextures(1, &exponent_texture);
        }
        if (floor_texture != 0u) {
            glDeleteTextures(1, &floor_texture);
        }
        return 0;
    }
    free(pixels);
    free(exponent_pixels);
    free(floor_pixels);
    if (renderer->texture_count == renderer->texture_capacity &&
        !renderer_opengl_texture_cache_reserve(
            renderer, renderer->texture_capacity == 0u ?
                RENDERER_OPENGL_TEXTURE_CACHE_INITIAL_CAPACITY : renderer->texture_capacity * 2u,
            error, error_size)) {
        glDeleteTextures(1, &texture);
        glDeleteTextures(1, &exponent_texture);
        glDeleteTextures(1, &floor_texture);
        return 0;
    }
    renderer->textures[renderer->texture_count++] = (RendererOpenGLTexture){
        texture, sprite->source_palette_bytes, sprite->source_palette_byte_count,
        sprite->source_light_palette_bytes, sprite->source_light_palette_byte_count,
        sprite->source_display_palette_bytes, sprite->source_display_palette_byte_count,
        (uint32_t)source_map_offset, key_window, {0}, width, height,
        RENDERER_OPENGL_TEXTURE_VECTOR, 0u, exponent_texture, floor_texture
    };
    *out_texture = &renderer->textures[renderer->texture_count - 1u];
    return 1;
}

static int renderer_opengl_draw_vector_sprite(RendererOpenGL *renderer,
                                              const SceneSprite *sprite,
                                              const SceneCamera *camera,
                                              const RenderView *view,
                                              char *error, size_t error_size)
{
    const uint8_t *bytes;
    size_t size;
    uint16_t point_count;
    uint16_t frame_count;
    uint16_t frame_index;
    size_t start_offset = 2u;
    size_t pointer_table_offset = 6u;
    size_t frame_pointer_offset;
    size_t lines_offset;
    size_t frame_offset;
    size_t polygon_angle_offset;
    size_t point_data_offset;
    uint32_t on_off;
    RendererOpenGLVertex *vertices = NULL;
    uint32_t vertex_count = 0u;
    uint32_t vertex_capacity = 0u;
    int result = 0;

    if (!renderer || !sprite || !camera || !view ||
        sprite->source != SCENE_SPRITE_SOURCE_VECTOR_MODEL ||
        !sprite->source_bytes || sprite->source_byte_count < 6u) {
        renderer_opengl_set_error(error, error_size, "source vector sprite descriptor is invalid");
        return 0;
    }
    renderer_opengl_use_default_light_response(renderer);
    bytes = sprite->source_bytes;
    size = sprite->source_byte_count;
    point_count = renderer_opengl_read_be16(bytes + 2u);
    frame_count = renderer_opengl_read_be16(bytes + 4u);
    frame_index = sprite->frame_index;
    if (point_count == 0u || frame_count == 0u || frame_index >= frame_count ||
        (size_t)frame_count > (size - pointer_table_offset) / 4u) {
        renderer_opengl_set_error(error, error_size, "source vector model header or frame is invalid");
        return 0;
    }
    lines_offset = pointer_table_offset + (size_t)frame_count * 4u;
    frame_pointer_offset = pointer_table_offset + (size_t)frame_index * 4u;
    frame_offset = start_offset + renderer_opengl_read_be16(bytes + frame_pointer_offset);
    polygon_angle_offset = start_offset + renderer_opengl_read_be16(
        bytes + frame_pointer_offset + 2u);
    if (frame_offset > size || 4u > size - frame_offset ||
        polygon_angle_offset >= size) {
        renderer_opengl_set_error(error, error_size,
                                  "source vector model frame points or polygon angles are outside the asset");
        return 0;
    }
    point_data_offset = frame_offset + 4u + (size_t)point_count + (point_count & 1u);
    if (point_data_offset > size || (size_t)point_count > (size - point_data_offset) / 6u) {
        renderer_opengl_set_error(error, error_size, "source vector model point table is malformed");
        return 0;
    }
    on_off = renderer_opengl_read_be32(bytes + frame_offset);
    for (uint32_t part_index = 0u; ; ++part_index) {
        size_t list_offset = lines_offset + (size_t)part_index * 4u;
        int16_t part_relative;
        size_t part_offset;

        if (list_offset > size || 4u > size - list_offset) {
            renderer_opengl_set_error(error, error_size, "source vector model has no part-list terminator");
            goto done;
        }
        part_relative = renderer_opengl_read_be16s(bytes + list_offset);
        if (part_relative < 0) {
            break;
        }
        if (part_index >= 32u || (on_off & (UINT32_C(1) << part_index)) == 0u) {
            continue;
        }
        part_offset = start_offset + (uint16_t)part_relative;
        if (part_offset > size || 2u > size - part_offset) {
            renderer_opengl_set_error(error, error_size, "source vector model part is outside the asset");
            goto done;
        }
        for (;;) {
            uint16_t line_count_minus_one = renderer_opengl_read_be16(bytes + part_offset);
            uint32_t polygon_point_count;
            size_t polygon_byte_count;

            if ((int16_t)line_count_minus_one < 0) {
                break;
            }
            polygon_point_count = (uint32_t)line_count_minus_one + 1u;
            if (polygon_point_count < 3u || line_count_minus_one >
                    (size - part_offset < 18u ? 0u : (size - part_offset - 18u) / 4u)) {
                renderer_opengl_set_error(error, error_size, "source vector model polygon is malformed");
                goto done;
            }
            polygon_byte_count = 18u + (size_t)line_count_minus_one * 4u;
            {
                const uint8_t *polygon_point_bytes = bytes + part_offset + 4u;
                const uint8_t *face_bytes =
                    polygon_point_bytes + (size_t)polygon_point_count * 4u;
                size_t source_map_offset;
                float source_light;
                /*
                 * doapoly reads the terminal word at face + 8 into the
                 * adjacent `draw_PreGouraud_b`/`draw_Gouraud_b` bytes.  The
                 * high byte selects gotlurvelyshading; the low byte belongs
                 * to the next source flag and must not select Gouraud here.
                 */
                int source_gouraud = face_bytes[8u] != 0u;
                uint8_t maximum_u = 0u;
                uint8_t maximum_v = 0u;
                const RendererOpenGLTexture *texture;

                /* Each four-byte source polygon entry is point index, U, V. */
                for (uint32_t corner = 0u; corner < polygon_point_count; ++corner) {
                    const uint8_t *source_corner = polygon_point_bytes + (size_t)corner * 4u;

                    if (renderer_opengl_read_be16(source_corner) >= point_count) {
                        renderer_opengl_set_error(error, error_size,
                                                  "source vector polygon references an invalid point");
                        goto done;
                    }
                    if (source_corner[2u] > maximum_u) {
                        maximum_u = source_corner[2u];
                    }
                    if (source_corner[3u] > maximum_v) {
                        maximum_v = source_corner[3u];
                    }
                }
                if (!renderer_opengl_vector_face_texture_info(
                        sprite, bytes + polygon_angle_offset, size - polygon_angle_offset,
                        face_bytes, &source_map_offset, &source_light,
                        error, error_size) ||
                    !renderer_opengl_find_vector_face_texture(
                        renderer, sprite, source_map_offset,
                        maximum_u, maximum_v, &texture, error, error_size)) {
                    goto done;
                }

                for (uint32_t triangle = 1u; triangle + 1u < polygon_point_count; ++triangle) {
                    const uint32_t corners[3] = {0u, triangle, triangle + 1u};

                    for (uint32_t corner = 0u; corner < 3u; ++corner) {
                        const uint8_t *source_corner =
                            polygon_point_bytes + (size_t)corners[corner] * 4u;
                        uint16_t point_index = renderer_opengl_read_be16(source_corner);
                        RendererOpenGLVertex vertex;

                        if (!renderer_opengl_vector_model_point(
                                sprite, camera, view,
                                bytes + point_data_offset + (size_t)point_index * 6u,
                                sprite->presentation == SCENE_SPRITE_PRESENTATION_PLAYER1_VIEW_WEAPON,
                                &vertex)) {
                            renderer_opengl_set_error(error, error_size,
                                                      "source vector model point is invalid");
                            goto done;
                        }
                        if (source_gouraud &&
                            !renderer_opengl_vector_point_source_light(
                                sprite, bytes + frame_offset + 4u, point_count, point_index,
                                &vertex.source_light, error, error_size)) {
                            goto done;
                        }
                        vertex.u = ((float)source_corner[2u] + 0.5f) /
                            ((float)maximum_u + 1.0f);
                        vertex.v = ((float)source_corner[3u] + 0.5f) /
                            ((float)maximum_v + 1.0f);
                        if (!source_gouraud) {
                            /* The source flat palette row is continuous GPU lighting. */
                            vertex.source_light = source_light;
                        }
                        if (!renderer_opengl_vector_append(
                                &vertices, &vertex_count, &vertex_capacity, &vertex,
                                error, error_size)) {
                            goto done;
                        }
                    }
                }
                renderer->gl.active_texture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, texture->texture);
                renderer_opengl_use_texture_light_response(renderer, texture);
                renderer->gl.uniform_1f(renderer->opacity_uniform, 1.0f);
                if (!renderer_opengl_draw_vertices(renderer, vertices, vertex_count,
                                                   GL_TRIANGLES, error, error_size)) {
                    goto done;
                }
                free(vertices);
                vertices = NULL;
                vertex_count = 0u;
                vertex_capacity = 0u;
            }
            part_offset += polygon_byte_count;
            if (part_offset > size || 2u > size - part_offset) {
                renderer_opengl_set_error(error, error_size,
                                          "source vector polygon extends outside the asset");
                goto done;
            }
        }
    }
    result = 1;
done:
    free(vertices);
    return result;
}

RendererOpenGL *renderer_opengl_create(int window_width, int window_height,
                                       const char *window_title,
                                       int hidden_window,
                                       char *error, size_t error_size)
{
    RendererOpenGL *renderer;

    if (!window_title || window_width < RENDERER_OPENGL_WINDOW_MINIMUM_SIZE ||
        window_height < RENDERER_OPENGL_WINDOW_MINIMUM_SIZE) {
        renderer_opengl_set_error(error, error_size, "OpenGL window configuration is invalid");
        return NULL;
    }
    if (SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1) != 0 ||
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24) != 0) {
        renderer_opengl_set_sdl_error(error, error_size, "SDL OpenGL attribute setup failed");
        return NULL;
    }
#if defined(__EMSCRIPTEN__)
    if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES) != 0 ||
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2) != 0 ||
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0) != 0) {
        renderer_opengl_set_sdl_error(error, error_size, "SDL WebGL context setup failed");
        return NULL;
    }
#else
    if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2) != 0 ||
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1) != 0) {
        renderer_opengl_set_sdl_error(error, error_size, "SDL OpenGL 2.1 context setup failed");
        return NULL;
    }
#endif
    renderer = calloc(1u, sizeof(*renderer));
    if (!renderer) {
        renderer_opengl_set_error(error, error_size, "OpenGL renderer allocation failed");
        return NULL;
    }
    /* The opt-in hidden window is the GPU smoke path, not the game loop. */
    renderer->measure_view_weapon_coverage = hidden_window != 0 ? 1u : 0u;
    renderer->window = SDL_CreateWindow(window_title, SDL_WINDOWPOS_CENTERED,
                                        SDL_WINDOWPOS_CENTERED, window_width, window_height,
                                        SDL_WINDOW_OPENGL |
                                        (hidden_window != 0 ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN) |
                                        SDL_WINDOW_RESIZABLE);
    if (!renderer->window) {
        renderer_opengl_set_sdl_error(error, error_size, "SDL OpenGL window creation failed");
        free(renderer);
        return NULL;
    }
    renderer->context = SDL_GL_CreateContext(renderer->window);
    if (!renderer->context) {
        renderer_opengl_set_sdl_error(error, error_size, "SDL OpenGL context creation failed");
        SDL_DestroyWindow(renderer->window);
        free(renderer);
        return NULL;
    }
    if (!renderer_opengl_load_functions(renderer, error, error_size) ||
        !renderer_opengl_create_program(renderer, error, error_size)) {
        renderer_opengl_destroy(renderer);
        return NULL;
    }
    renderer->gl.gen_buffers(1, &renderer->vertex_buffer);
    if (renderer->vertex_buffer == 0u) {
        renderer_opengl_set_error(error, error_size, "OpenGL vertex-buffer allocation failed");
        renderer_opengl_destroy(renderer);
        return NULL;
    }
    {
        static const uint8_t white_pixel[4] = {255u, 255u, 255u, 255u};

        if (!renderer_opengl_create_texture(white_pixel, 1u, 1u, 0, 0, 0,
                                            &renderer->white_texture, error, error_size)) {
            renderer_opengl_destroy(renderer);
            return NULL;
        }
    }
    renderer->gl.use_program(renderer->program);
    renderer->gl.enable_vertex_attrib_array(RENDERER_OPENGL_POSITION_ATTRIBUTE);
    renderer->gl.enable_vertex_attrib_array(RENDERER_OPENGL_TEXTURE_COORDINATE_ATTRIBUTE);
    renderer->gl.enable_vertex_attrib_array(RENDERER_OPENGL_SOURCE_LIGHT_ATTRIBUTE);
    renderer->gl.enable_vertex_attrib_array(RENDERER_OPENGL_SOURCE_COLOR_ATTRIBUTE);
    renderer->gl.uniform_1i(renderer->texture_uniform, 0);
    renderer->gl.uniform_1i(renderer->material_light_response_exponent_texture_uniform, 1);
    renderer->gl.uniform_1i(renderer->material_light_response_floor_texture_uniform, 2);
    renderer->gl.active_texture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, renderer->white_texture);
    renderer->gl.active_texture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, renderer->white_texture);
    renderer->gl.active_texture(GL_TEXTURE0);
    renderer->gl.uniform_1f(renderer->opacity_uniform, 1.0f);
    renderer_opengl_use_default_light_response(renderer);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glClearColor(0.025f, 0.035f, 0.060f, 1.0f);
    if (SDL_GL_SetSwapInterval(1) != 0) {
        fprintf(stderr, "[RENDER] OpenGL swap interval unavailable: %s\n", SDL_GetError());
    }
    return renderer;
}

void renderer_opengl_destroy(RendererOpenGL *renderer)
{
    if (!renderer) {
        return;
    }
    if (renderer->context) {
        for (size_t index = 0u; index < renderer->texture_count; ++index) {
            if (renderer->textures[index].texture != 0u) {
                glDeleteTextures(1, &renderer->textures[index].texture);
            }
            if (renderer->textures[index].light_response_exponent_texture != 0u) {
                glDeleteTextures(1, &renderer->textures[index].light_response_exponent_texture);
            }
            if (renderer->textures[index].light_response_floor_texture != 0u) {
                glDeleteTextures(1, &renderer->textures[index].light_response_floor_texture);
            }
        }
        if (renderer->white_texture != 0u) {
            glDeleteTextures(1, &renderer->white_texture);
        }
        if (renderer->vertex_buffer != 0u && renderer->gl.delete_buffers) {
            renderer->gl.delete_buffers(1, &renderer->vertex_buffer);
        }
        if (renderer->program != 0u && renderer->gl.delete_program) {
            renderer->gl.delete_program(renderer->program);
        }
        SDL_GL_DeleteContext(renderer->context);
    }
    free(renderer->textures);
    SDL_DestroyWindow(renderer->window);
    free(renderer);
}

size_t renderer_opengl_last_view_weapon_coverage(const RendererOpenGL *renderer)
{
    return renderer ? renderer->last_view_weapon_coverage : 0u;
}

uint64_t renderer_opengl_last_frame_rgb_checksum(const RendererOpenGL *renderer)
{
    return renderer ? renderer->last_frame_rgb_checksum : UINT64_C(0);
}

typedef struct {
    const SceneSprite *sprite;
    float depth;
} RendererOpenGLSpriteOrder;

static int renderer_opengl_compare_sprite_order(const void *left, const void *right)
{
    const RendererOpenGLSpriteOrder *left_sprite = left;
    const RendererOpenGLSpriteOrder *right_sprite = right;

    /* Far-to-near additive blending, matching the source's visible effect layering. */
    return left_sprite->depth < right_sprite->depth ? 1 :
           left_sprite->depth > right_sprite->depth ? -1 : 0;
}

static int renderer_opengl_sprite_is_additive_effect(const SceneSprite *sprite)
{
    return sprite->source == SCENE_SPRITE_SOURCE_GLARE_BITMAP ||
           (sprite->flags & SCENE_SPRITE_FLAG_ADDITIVE) != 0u;
}

int renderer_opengl_present(RendererOpenGL *renderer, const SceneFrame *frame,
                            const RenderView *view, char *error, size_t error_size)
{
    const SceneCamera *camera = NULL;
    const SceneEnvironment *environment = NULL;
    const SceneMaterial *active_material = NULL;
    RendererOpenGLSpriteOrder *additive_sprites = NULL;
    size_t additive_count = 0u;
    float view_projection[16];
    int drawable_width;
    int drawable_height;

    if (!renderer || !renderer->window || !renderer->context || !frame || !view) {
        renderer_opengl_set_error(error, error_size, "OpenGL presenter received invalid state");
        return 0;
    }
    renderer->last_view_weapon_coverage = 0u;
    renderer->last_frame_rgb_checksum = UINT64_C(0);
    for (size_t index = 0u; index < frame->count; ++index) {
        if (frame->commands[index].type == SCENE_COMMAND_CAMERA) {
            camera = &frame->commands[index].data.camera;
        } else if (frame->commands[index].type == SCENE_COMMAND_ENVIRONMENT) {
            environment = &frame->commands[index].data.environment;
        }
    }
    if (!camera) {
        renderer_opengl_set_error(error, error_size, "scene frame has no camera command");
        return 0;
    }
    SDL_GL_GetDrawableSize(renderer->window, &drawable_width, &drawable_height);
    if (drawable_width <= 0 || drawable_height <= 0) {
        return 1;
    }
    renderer_opengl_view_projection(view_projection, camera, view,
                                    (float)drawable_width / (float)drawable_height);
    glViewport(0, 0, drawable_width, drawable_height);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    renderer->gl.use_program(renderer->program);
    renderer->gl.uniform_matrix_4fv(renderer->view_projection_uniform, 1, GL_FALSE,
                                    view_projection);
    renderer->gl.uniform_1f(renderer->point_size_uniform, 10.0f);
    renderer->gl.uniform_1i(renderer->texture_uniform, 0);
    renderer->gl.uniform_1f(renderer->opacity_uniform, 1.0f);
    renderer->gl.active_texture(GL_TEXTURE0);
    if (!renderer_opengl_draw_sky(renderer, environment, camera, error, error_size)) {
        return 0;
    }
    renderer->gl.uniform_matrix_4fv(renderer->view_projection_uniform, 1, GL_FALSE,
                                    view_projection);
    for (size_t index = 0u; index < frame->count; ++index) {
        const SceneCommand *command = &frame->commands[index];

        if (command->type == SCENE_COMMAND_MATERIAL) {
            active_material = &command->data.material;
        } else if (command->type == SCENE_COMMAND_GEOMETRY) {
            if (!renderer_opengl_draw_geometry(renderer, active_material, &command->data.geometry,
                                               environment, camera, error, error_size)) {
                return 0;
            }
        }
    }
    additive_sprites = calloc(frame->count, sizeof(*additive_sprites));
    if (!additive_sprites && frame->count != 0u) {
        renderer_opengl_set_error(error, error_size, "additive sprite order allocation failed");
        return 0;
    }
    for (size_t index = 0u; index < frame->count; ++index) {
        const SceneCommand *command = &frame->commands[index];

        if (command->type == SCENE_COMMAND_SPRITE) {
            const SceneSprite *sprite = &command->data.sprite;

            if (sprite->presentation == SCENE_SPRITE_PRESENTATION_PLAYER1_VIEW_WEAPON) {
                continue;
            }
            if (renderer_opengl_sprite_is_additive_effect(sprite)) {
                float sprite_x;
                float sprite_y;
                float sprite_z;
                float camera_x;
                float camera_y;
                float camera_z;
                float yaw = (float)camera->yaw *
                    (2.0f * renderer_opengl_pi / 8192.0f);

                renderer_opengl_world_point(&sprite->position, &sprite_x, &sprite_y, &sprite_z);
                renderer_opengl_world_point(&camera->position, &camera_x, &camera_y, &camera_z);
                additive_sprites[additive_count].sprite = sprite;
                additive_sprites[additive_count].depth =
                    (sprite_x - camera_x) * sinf(yaw) + (sprite_z - camera_z) * cosf(yaw);
                ++additive_count;
            } else if ((sprite->source == SCENE_SPRITE_SOURCE_VECTOR_MODEL &&
                        !renderer_opengl_draw_vector_sprite(renderer, sprite, camera, view, error,
                                                           error_size)) ||
                       (sprite->source != SCENE_SPRITE_SOURCE_VECTOR_MODEL &&
                        !renderer_opengl_draw_sprite(renderer, sprite, camera, error,
                                                    error_size))) {
                free(additive_sprites);
                return 0;
            }
        }
    }
    qsort(additive_sprites, additive_count, sizeof(*additive_sprites),
          renderer_opengl_compare_sprite_order);
    for (size_t index = 0u; index < additive_count; ++index) {
        const SceneSprite *sprite = additive_sprites[index].sprite;

        if ((sprite->source == SCENE_SPRITE_SOURCE_VECTOR_MODEL &&
             !renderer_opengl_draw_vector_sprite(renderer, sprite, camera, view, error, error_size)) ||
            (sprite->source != SCENE_SPRITE_SOURCE_VECTOR_MODEL &&
             !renderer_opengl_draw_sprite(renderer, sprite, camera, error, error_size))) {
            free(additive_sprites);
            return 0;
        }
    }
    free(additive_sprites);
    for (size_t index = 0u; index < frame->count; ++index) {
        const SceneCommand *command = &frame->commands[index];

        if (command->type == SCENE_COMMAND_SPRITE &&
            command->data.sprite.presentation == SCENE_SPRITE_PRESENTATION_PLAYER1_VIEW_WEAPON) {
            uint8_t *before_pixels = NULL;
            size_t pixel_byte_count = 0u;

            if (renderer->measure_view_weapon_coverage != 0u) {
                if ((size_t)drawable_width > SIZE_MAX / (size_t)drawable_height / 4u) {
                    renderer_opengl_set_error(error, error_size,
                                              "weapon GPU coverage buffer is too large");
                    return 0;
                }
                pixel_byte_count = (size_t)drawable_width * (size_t)drawable_height * 4u;
                before_pixels = malloc(pixel_byte_count);
                if (!before_pixels) {
                    renderer_opengl_set_error(error, error_size,
                                              "weapon GPU coverage buffer allocation failed");
                    return 0;
                }
                glReadPixels(0, 0, drawable_width, drawable_height, GL_RGBA, GL_UNSIGNED_BYTE,
                             before_pixels);
                if (glGetError() != GL_NO_ERROR) {
                    free(before_pixels);
                    renderer_opengl_set_error(error, error_size,
                                              "weapon GPU coverage readback before draw failed");
                    return 0;
                }
            }
            /*
             * The companion is a camera-space model, so it must not be
             * rejected by world depth.  Do not disable depth testing though:
             * that lets its back and internal textured polygons paint over
             * the visible faces in source part order.  A fresh depth buffer
             * keeps the weapon in front of the world while preserving normal
             * per-fragment self-occlusion for the modern 3D presentation.
             */
            glClear(GL_DEPTH_BUFFER_BIT);
            if (command->data.sprite.source != SCENE_SPRITE_SOURCE_VECTOR_MODEL ||
                !renderer_opengl_draw_vector_sprite(renderer, &command->data.sprite, camera, view,
                                                   error, error_size)) {
                free(before_pixels);
                return 0;
            }
            if (before_pixels) {
                uint8_t *after_pixels = malloc(pixel_byte_count);

                if (!after_pixels) {
                    free(before_pixels);
                    renderer_opengl_set_error(error, error_size,
                                              "weapon GPU coverage buffer allocation failed");
                    return 0;
                }
                glReadPixels(0, 0, drawable_width, drawable_height, GL_RGBA, GL_UNSIGNED_BYTE,
                             after_pixels);
                if (glGetError() != GL_NO_ERROR) {
                    free(after_pixels);
                    free(before_pixels);
                    renderer_opengl_set_error(error, error_size,
                                              "weapon GPU coverage readback after draw failed");
                    return 0;
                }
                for (size_t pixel_offset = 0u; pixel_offset < pixel_byte_count;
                     pixel_offset += 4u) {
                    if (memcmp(before_pixels + pixel_offset, after_pixels + pixel_offset,
                               4u) != 0) {
                        ++renderer->last_view_weapon_coverage;
                    }
                }
                free(after_pixels);
                free(before_pixels);
            }
        }
    }
    if (renderer->measure_view_weapon_coverage != 0u) {
        size_t pixel_byte_count;
        uint8_t *pixels;

        if ((size_t)drawable_width > SIZE_MAX / (size_t)drawable_height / 4u) {
            renderer_opengl_set_error(error, error_size,
                                      "GPU smoke framebuffer checksum buffer is too large");
            return 0;
        }
        pixel_byte_count = (size_t)drawable_width * (size_t)drawable_height * 4u;
        pixels = malloc(pixel_byte_count);
        if (!pixels) {
            renderer_opengl_set_error(error, error_size,
                                      "GPU smoke framebuffer checksum allocation failed");
            return 0;
        }
        glReadPixels(0, 0, drawable_width, drawable_height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        if (glGetError() != GL_NO_ERROR) {
            free(pixels);
            renderer_opengl_set_error(error, error_size,
                                      "GPU smoke framebuffer checksum readback failed");
            return 0;
        }
        for (size_t pixel_offset = 0u; pixel_offset < pixel_byte_count; pixel_offset += 4u) {
            uint64_t rgb = (uint64_t)pixels[pixel_offset] * UINT64_C(3) +
                (uint64_t)pixels[pixel_offset + 1u] * UINT64_C(5) +
                (uint64_t)pixels[pixel_offset + 2u] * UINT64_C(7);

            renderer->last_frame_rgb_checksum +=
                rgb * (uint64_t)(pixel_offset / 4u + 1u);
        }
        free(pixels);
    }
    SDL_GL_SwapWindow(renderer->window);
    return 1;
}
