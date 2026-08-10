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
    RENDERER_OPENGL_TEXTURE_CACHE_INITIAL_CAPACITY = 64,
    RENDERER_OPENGL_WINDOW_MINIMUM_SIZE = 1
};

/* Source Y coordinates are 8.8 fixed point; X/Z coordinates are integer words. */
static const float renderer_opengl_source_y_unit = 1.0f / 256.0f;
static const float renderer_opengl_pi = 3.14159265358979323846f;
static const float renderer_opengl_near_plane = 0.05f;
static const float renderer_opengl_far_plane = 8192.0f;

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
    uint8_t kind;
} RendererOpenGLTexture;

typedef enum {
    RENDERER_OPENGL_TEXTURE_WALL,
    RENDERER_OPENGL_TEXTURE_FLAT,
    RENDERER_OPENGL_TEXTURE_SPRITE
} RendererOpenGLTextureKind;

struct RendererOpenGL {
    SDL_Window *window;
    SDL_GLContext context;
    RendererOpenGLFunctions gl;
    GLuint program;
    GLuint vertex_buffer;
    GLint view_projection_uniform;
    GLint point_size_uniform;
    GLint texture_uniform;
    RendererOpenGLTexture *textures;
    size_t texture_count;
    size_t texture_capacity;
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

static void renderer_opengl_make_vertex(RendererOpenGLVertex *out_vertex,
                                        const SceneVertex *source_vertex,
                                        float texture_u, float texture_v)
{
    renderer_opengl_world_point(&source_vertex->position, &out_vertex->x, &out_vertex->y,
                                &out_vertex->z);
    out_vertex->u = texture_u;
    out_vertex->v = texture_v;
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
                                            texture_v_scale);
            renderer_opengl_make_vertex(&vertices[output_index++], second,
                                        (float)second->texture_u * texture_u_scale,
                                        ((float)second->texture_v + texture_v_offset) *
                                            texture_v_scale);
            renderer_opengl_make_vertex(&vertices[output_index++], third,
                                        (float)third->texture_u * texture_u_scale,
                                        ((float)third->texture_v + texture_v_offset) *
                                            texture_v_scale);
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
                                 texture_v_offset) * texture_v_scale);
    renderer_opengl_make_vertex(&vertices[output_index++], &geometry->vertices[indices[1u]],
                                (float)geometry->vertices[indices[1u]].texture_u * texture_u_scale,
                                ((float)geometry->vertices[indices[1u]].texture_v +
                                 texture_v_offset) * texture_v_scale);
    renderer_opengl_make_vertex(&vertices[output_index++], &geometry->vertices[indices[2u]],
                                (float)geometry->vertices[indices[2u]].texture_u * texture_u_scale,
                                ((float)geometry->vertices[indices[2u]].texture_v +
                                 texture_v_offset) * texture_v_scale);
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
        !material->source_palette_bytes || material->source_palette_byte_count < 2048u ||
        !material->source_display_palette_bytes || window->u_period == 0u ||
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
            uint8_t color_index;

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
            color_index = material->source_palette_bytes[(size_t)packed_texel * 2u];
            if (!renderer_opengl_write_palette_texel(
                    pixels, ((size_t)y * window->u_period + x) * 4u,
                    material->source_display_palette_bytes,
                    material->source_display_palette_byte_count, color_index,
                    packed_texel == 0u)) {
                free(pixels);
                renderer_opengl_set_error(error, error_size,
                                          "source wall palette references invalid display colour");
                return 0;
            }
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
    enum { LOGICAL_TILE_SIZE = 64u, TILE_ROW_STRIDE = 1024u, BASE_SHADE_ROW = 32u };
    uint8_t *pixels;
    uint16_t x;
    uint16_t y;
    size_t tile_offset;

    if (!material || !out_pixels || !out_width || !out_height || !material->source_bytes ||
        !material->source_palette_bytes || !material->source_display_palette_bytes ||
        material->source_byte_count < LOGICAL_TILE_SIZE * TILE_ROW_STRIDE ||
        material->source_palette_byte_count < (BASE_SHADE_ROW + 1u) * 256u) {
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
            uint8_t packed_texel = material->source_bytes[source_offset];
            uint8_t color_index = material->source_palette_bytes[
                BASE_SHADE_ROW * 256u + packed_texel];

            if (!renderer_opengl_write_palette_texel(
                    pixels, ((size_t)y * LOGICAL_TILE_SIZE + x) * 4u,
                    material->source_display_palette_bytes,
                    material->source_display_palette_byte_count, color_index, 0)) {
                free(pixels);
                renderer_opengl_set_error(error, error_size,
                                          "source flat palette references invalid display colour");
                return 0;
            }
        }
    }
    *out_pixels = pixels;
    *out_width = LOGICAL_TILE_SIZE;
    *out_height = LOGICAL_TILE_SIZE;
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

    if (!sprite || !out_pixels || !out_width || !out_height || !sprite->source_bytes ||
        !sprite->source_aux_bytes || !sprite->source_palette_bytes ||
        !sprite->source_display_palette_bytes ||
        sprite->source != SCENE_SPRITE_SOURCE_OBJECT_BITMAP ||
        sprite->frame_metrics.strip_count == 0u || sprite->frame_metrics.line_count == 0u ||
        (size_t)sprite->frame_metrics.strip_count >
            SIZE_MAX / (size_t)sprite->frame_metrics.line_count ||
        (size_t)sprite->frame_metrics.strip_count * (size_t)sprite->frame_metrics.line_count >
            SIZE_MAX / 4u) {
        renderer_opengl_set_error(error, error_size, "source bitmap sprite descriptor is invalid");
        return 0;
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
            if ((size_t)packed_texel * 2u + 2u > sprite->source_palette_byte_count) {
                free(pixels);
                renderer_opengl_set_error(error, error_size,
                                          "source bitmap sprite palette is invalid");
                return 0;
            }
            color_index = sprite->source_palette_bytes[(size_t)packed_texel * 2u];
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

static int renderer_opengl_create_texture(const uint8_t *pixels, uint16_t width, uint16_t height,
                                          int repeat, GLuint *out_texture, char *error,
                                          size_t error_size)
{
    GLuint texture = 0u;

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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
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
                                                 GLuint *out_texture, char *error,
                                                 size_t error_size)
{
    uint8_t *pixels = NULL;
    uint16_t width = 0u;
    uint16_t height = 0u;
    GLuint texture = 0u;
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
            *out_texture = cached->texture;
            return 1;
        }
    }
    if ((kind == RENDERER_OPENGL_TEXTURE_WALL &&
         !renderer_opengl_decode_wall_texture(material, window, &pixels, &width, &height,
                                              error, error_size)) ||
        (kind == RENDERER_OPENGL_TEXTURE_FLAT &&
         !renderer_opengl_decode_flat_texture(material, &pixels, &width, &height, error,
                                              error_size)) ||
        !renderer_opengl_create_texture(pixels, width, height, 1, &texture, error, error_size)) {
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
        texture, material->source_bytes, material->source_byte_count, material->source_palette_bytes,
        material->source_palette_byte_count, material->source_display_palette_bytes,
        material->source_display_palette_byte_count, material->source_asset_id, key_window, {0},
        (uint8_t)kind
    };
    *out_texture = texture;
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
            memcmp(&cached->frame_metrics, &sprite->frame_metrics,
                   sizeof(sprite->frame_metrics)) == 0) {
            *out_texture = cached->texture;
            return 1;
        }
    }
    if (!renderer_opengl_decode_sprite_texture(sprite, &pixels, &width, &height, error, error_size) ||
        !renderer_opengl_create_texture(pixels, width, height, 0, &texture, error, error_size)) {
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
        sprite->frame_metrics, RENDERER_OPENGL_TEXTURE_SPRITE
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
        "uniform mat4 u_view_projection;\n"
        "uniform float u_point_size;\n"
        "varying vec2 v_texture_coordinate;\n"
        "void main() {\n"
        "  gl_Position = u_view_projection * vec4(a_position, 1.0);\n"
        "  gl_PointSize = u_point_size;\n"
        "  v_texture_coordinate = a_texture_coordinate;\n"
        "}\n";
#if defined(__EMSCRIPTEN__)
    static const char fragment_source[] =
        "precision mediump float;\n"
        "varying vec2 v_texture_coordinate;\n"
        "uniform sampler2D u_texture;\n"
        "void main() {\n"
        "  vec4 color = texture2D(u_texture, v_texture_coordinate);\n"
        "  if (color.a < 0.5) discard;\n"
        "  gl_FragColor = color;\n"
        "}\n";
#else
    static const char fragment_source[] =
        "varying vec2 v_texture_coordinate;\n"
        "uniform sampler2D u_texture;\n"
        "void main() {\n"
        "  vec4 color = texture2D(u_texture, v_texture_coordinate);\n"
        "  if (color.a < 0.5) discard;\n"
        "  gl_FragColor = color;\n"
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
    if (renderer->view_projection_uniform < 0 || renderer->point_size_uniform < 0 ||
        renderer->texture_uniform < 0) {
        renderer->gl.delete_program(renderer->program);
        renderer->program = 0u;
        renderer_opengl_set_error(error, error_size, "OpenGL shader uniforms are unavailable");
        return 0;
    }
    return 1;
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
    glDrawArrays(mode, 0, (GLsizei)vertex_count);
    if (glGetError() != GL_NO_ERROR) {
        renderer_opengl_set_error(error, error_size, "OpenGL draw command failed");
        return 0;
    }
    return 1;
}

static int32_t renderer_opengl_asr32_8(int32_t value)
{
    if (value >= 0) {
        return value >> 8u;
    }
    return -((-(int64_t)value + 255) >> 8u);
}

static int renderer_opengl_draw_geometry(RendererOpenGL *renderer,
                                         const SceneMaterial *material,
                                         const SceneGeometry *geometry,
                                         const SceneCamera *camera,
                                         char *error, size_t error_size)
{
    RendererOpenGLVertex *vertices = NULL;
    uint32_t vertex_count;
    RendererOpenGLTextureKind texture_kind;
    GLuint texture;
    float texture_u_scale;
    float texture_v_scale;
    float texture_v_offset = 0.0f;
    int result;

    if (!material || !geometry || !camera || !geometry->vertices) {
        renderer_opengl_set_error(error, error_size, "scene geometry has no material, camera, or vertices");
        return 0;
    }
    if (geometry->primitive == SCENE_GEOMETRY_PRIMITIVE_WALL) {
        uint16_t wall_height_mask;

        if (geometry->texture_window.u_period == 0u || geometry->texture_window.v_period == 0u) {
            renderer_opengl_set_error(error, error_size, "scene wall geometry has no source texture window");
            return 0;
        }
        texture_kind = RENDERER_OPENGL_TEXTURE_WALL;
        texture_u_scale = 1.0f / (float)geometry->texture_window.u_period;
        texture_v_scale = 1.0f / (float)geometry->texture_window.v_period;
        wall_height_mask = (uint16_t)(geometry->texture_window.v_period - 1u);
        texture_v_offset = (float)((renderer_opengl_asr32_8(camera->position.y) + 224) &
                                   wall_height_mask);
    } else {
        texture_kind = RENDERER_OPENGL_TEXTURE_FLAT;
        texture_u_scale = 1.0f / 64.0f;
        texture_v_scale = 1.0f / 64.0f;
    }
    if (!renderer_opengl_find_material_texture(renderer, texture_kind, material,
                                               texture_kind == RENDERER_OPENGL_TEXTURE_WALL ?
                                                   &geometry->texture_window : NULL,
                                               &texture, error, error_size)) {
        return 0;
    }
    renderer->gl.active_texture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
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
                                         texture_v_offset) * texture_v_scale);
        }
        vertex_count = geometry->vertex_count;
    } else if (geometry->topology == SCENE_GEOMETRY_TOPOLOGY_POLYGON_BOUNDARY) {
        if (!renderer_opengl_triangulate_polygon(geometry, texture_u_scale, texture_v_scale,
                                                 texture_v_offset, &vertices, &vertex_count,
                                                 error, error_size)) {
            return 0;
        }
    } else {
        renderer_opengl_set_error(error, error_size, "scene geometry topology is unsupported");
        return 0;
    }
    result = renderer_opengl_draw_vertices(renderer, vertices, vertex_count, GL_TRIANGLES,
                                           error, error_size);
    free(vertices);
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
    int result;

    if (!sprite || !camera) {
        renderer_opengl_set_error(error, error_size, "scene sprite has no camera or descriptor");
        return 0;
    }
    /* Vector and glare draw paths need their own source routines; do not substitute markers. */
    if (sprite->source != SCENE_SPRITE_SOURCE_OBJECT_BITMAP || sprite->source_width == 0u ||
        sprite->source_height == 0u) {
        return 1;
    }
    if (!renderer_opengl_find_sprite_texture(renderer, sprite, &texture, error, error_size)) {
        return 0;
    }
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
    vertices[0] = (RendererOpenGLVertex){center_x - right_x * half_width, center_y + half_height,
                                          center_z - right_z * half_width, left_u, 0.0f};
    vertices[1] = (RendererOpenGLVertex){center_x + right_x * half_width, center_y + half_height,
                                          center_z + right_z * half_width, right_u, 0.0f};
    vertices[2] = (RendererOpenGLVertex){center_x + right_x * half_width, center_y - half_height,
                                          center_z + right_z * half_width, right_u, 1.0f};
    vertices[3] = vertices[0];
    vertices[4] = vertices[2];
    vertices[5] = (RendererOpenGLVertex){center_x - right_x * half_width, center_y - half_height,
                                          center_z - right_z * half_width, left_u, 1.0f};
    renderer->gl.active_texture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    if ((sprite->flags & SCENE_SPRITE_FLAG_ADDITIVE) != 0u) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
    }
    result = renderer_opengl_draw_vertices(renderer, vertices, 6u, GL_TRIANGLES, error, error_size);
    if ((sprite->flags & SCENE_SPRITE_FLAG_ADDITIVE) != 0u) {
        glDisable(GL_BLEND);
    }
    return result;
}

RendererOpenGL *renderer_opengl_create(int window_width, int window_height,
                                       const char *window_title,
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
    renderer->window = SDL_CreateWindow(window_title, SDL_WINDOWPOS_CENTERED,
                                        SDL_WINDOWPOS_CENTERED, window_width, window_height,
                                        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
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
    renderer->gl.use_program(renderer->program);
    renderer->gl.enable_vertex_attrib_array(RENDERER_OPENGL_POSITION_ATTRIBUTE);
    renderer->gl.enable_vertex_attrib_array(RENDERER_OPENGL_TEXTURE_COORDINATE_ATTRIBUTE);
    renderer->gl.uniform_1i(renderer->texture_uniform, 0);
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

int renderer_opengl_present(RendererOpenGL *renderer, const SceneFrame *frame,
                            const RenderView *view, char *error, size_t error_size)
{
    const SceneCamera *camera = NULL;
    const SceneMaterial *active_material = NULL;
    float view_projection[16];
    int drawable_width;
    int drawable_height;

    if (!renderer || !renderer->window || !renderer->context || !frame || !view) {
        renderer_opengl_set_error(error, error_size, "OpenGL presenter received invalid state");
        return 0;
    }
    for (size_t index = 0u; index < frame->count; ++index) {
        if (frame->commands[index].type == SCENE_COMMAND_CAMERA) {
            camera = &frame->commands[index].data.camera;
            break;
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
    renderer->gl.active_texture(GL_TEXTURE0);
    for (size_t index = 0u; index < frame->count; ++index) {
        const SceneCommand *command = &frame->commands[index];

        if (command->type == SCENE_COMMAND_MATERIAL) {
            active_material = &command->data.material;
        } else if (command->type == SCENE_COMMAND_GEOMETRY) {
            if (!renderer_opengl_draw_geometry(renderer, active_material, &command->data.geometry,
                                               camera, error, error_size)) {
                return 0;
            }
        }
    }
    for (size_t index = 0u; index < frame->count; ++index) {
        const SceneCommand *command = &frame->commands[index];

        if (command->type == SCENE_COMMAND_SPRITE &&
            !renderer_opengl_draw_sprite(renderer, &command->data.sprite, camera, error,
                                         error_size)) {
            return 0;
        }
    }
    SDL_GL_SwapWindow(renderer->window);
    return 1;
}
