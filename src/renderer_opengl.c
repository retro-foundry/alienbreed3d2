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
    RENDERER_OPENGL_COLOR_ATTRIBUTE = 1,
    RENDERER_OPENGL_WINDOW_MINIMUM_SIZE = 1
};

static const float renderer_opengl_source_unit = 1.0f / 256.0f;
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
    float r;
    float g;
    float b;
} RendererOpenGLVertex;

typedef struct {
    float r;
    float g;
    float b;
} RendererOpenGLColor;

struct RendererOpenGL {
    SDL_Window *window;
    SDL_GLContext context;
    RendererOpenGLFunctions gl;
    GLuint program;
    GLuint vertex_buffer;
    GLint view_projection_uniform;
    GLint point_size_uniform;
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
 * The source scene coordinates share the gameplay fixed-point world.  The
 * renderer only converts units and the source's down-positive Y axis; it
 * leaves topology, positions, and source yaw untouched.
 */
static void renderer_opengl_world_point(const SceneWorldPoint *point, float *out_x,
                                        float *out_y, float *out_z)
{
    *out_x = (float)point->x * renderer_opengl_source_unit;
    *out_y = -(float)point->y * renderer_opengl_source_unit;
    *out_z = (float)point->z * renderer_opengl_source_unit;
}

static RendererOpenGLColor renderer_opengl_geometry_color(const SceneGeometry *geometry)
{
    RendererOpenGLColor color;
    float material_variation = (float)(geometry->material_id % 7u) * 0.025f;

    /*
     * TODO(port): `hireswall.s:Draw_Wall` and `hires.s:Draw_Flats` must first
     * establish source-to-GPU texture coordinates. The user requested basic
     * visible output now, so these source primitive/material-ID diagnostic
     * colours intentionally do not pretend to be source textures.
     */
    switch (geometry->primitive) {
    case SCENE_GEOMETRY_PRIMITIVE_WALL:
        color.r = 0.38f + material_variation;
        color.g = 0.50f + material_variation;
        color.b = 0.62f + material_variation;
        break;
    case SCENE_GEOMETRY_PRIMITIVE_FLOOR:
        color.r = 0.20f + material_variation;
        color.g = 0.32f + material_variation;
        color.b = 0.22f + material_variation;
        break;
    case SCENE_GEOMETRY_PRIMITIVE_CEILING:
        color.r = 0.24f + material_variation;
        color.g = 0.25f + material_variation;
        color.b = 0.30f + material_variation;
        break;
    case SCENE_GEOMETRY_PRIMITIVE_WATER:
        color.r = 0.08f + material_variation;
        color.g = 0.38f + material_variation;
        color.b = 0.62f + material_variation;
        break;
    default:
        color.r = 1.0f;
        color.g = 0.0f;
        color.b = 1.0f;
        break;
    }
    return color;
}

static RendererOpenGLColor renderer_opengl_sprite_color(const SceneSprite *sprite)
{
    RendererOpenGLColor color;

    switch (sprite->source) {
    case SCENE_SPRITE_SOURCE_OBJECT_BITMAP:
        color.r = 0.96f;
        color.g = 0.80f;
        color.b = 0.20f;
        break;
    case SCENE_SPRITE_SOURCE_VECTOR_MODEL:
        color.r = 0.94f;
        color.g = 0.30f;
        color.b = 0.24f;
        break;
    case SCENE_SPRITE_SOURCE_GLARE_BITMAP:
        color.r = 0.35f;
        color.g = 0.90f;
        color.b = 1.0f;
        break;
    default:
        color.r = 1.0f;
        color.g = 0.0f;
        color.b = 1.0f;
        break;
    }
    return color;
}

static void renderer_opengl_make_vertex(RendererOpenGLVertex *out_vertex,
                                        const SceneVertex *source_vertex,
                                        RendererOpenGLColor color)
{
    renderer_opengl_world_point(&source_vertex->position, &out_vertex->x, &out_vertex->y,
                                &out_vertex->z);
    out_vertex->r = color.r;
    out_vertex->g = color.g;
    out_vertex->b = color.b;
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
                                               RendererOpenGLColor color,
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
            renderer_opengl_make_vertex(&vertices[output_index++], first, color);
            renderer_opengl_make_vertex(&vertices[output_index++], second, color);
            renderer_opengl_make_vertex(&vertices[output_index++], third, color);
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
                                color);
    renderer_opengl_make_vertex(&vertices[output_index++], &geometry->vertices[indices[1u]],
                                color);
    renderer_opengl_make_vertex(&vertices[output_index++], &geometry->vertices[indices[2u]],
                                color);
    free(indices);
    *out_vertices = vertices;
    *out_vertex_count = output_index;
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
    float yaw = (float)camera->yaw * (2.0f * renderer_opengl_pi / 16384.0f);
    float pitch = view->pitch_degrees * (renderer_opengl_pi / 180.0f);
    float forward_x = -sinf(yaw) * cosf(pitch);
    float forward_y = sinf(pitch);
    float forward_z = -cosf(yaw) * cosf(pitch);
    float right_x = cosf(yaw);
    float right_z = -sinf(yaw);
    float up_x = -right_z * forward_y;
    float up_y = right_z * forward_x - right_x * forward_z;
    float up_z = right_x * forward_y;
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
        "attribute vec3 a_color;\n"
        "uniform mat4 u_view_projection;\n"
        "uniform float u_point_size;\n"
        "varying vec3 v_color;\n"
        "void main() {\n"
        "  gl_Position = u_view_projection * vec4(a_position, 1.0);\n"
        "  gl_PointSize = u_point_size;\n"
        "  v_color = a_color;\n"
        "}\n";
#if defined(__EMSCRIPTEN__)
    static const char fragment_source[] =
        "precision mediump float;\n"
        "varying vec3 v_color;\n"
        "void main() { gl_FragColor = vec4(v_color, 1.0); }\n";
#else
    static const char fragment_source[] =
        "varying vec3 v_color;\n"
        "void main() { gl_FragColor = vec4(v_color, 1.0); }\n";
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
    renderer->gl.bind_attrib_location(renderer->program, RENDERER_OPENGL_COLOR_ATTRIBUTE,
                                      "a_color");
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
    if (renderer->view_projection_uniform < 0 || renderer->point_size_uniform < 0) {
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
    renderer->gl.vertex_attrib_pointer(RENDERER_OPENGL_COLOR_ATTRIBUTE, 3, GL_FLOAT, GL_FALSE,
                                       (GLsizei)sizeof(*vertices),
                                       (const void *)offsetof(RendererOpenGLVertex, r));
    glDrawArrays(mode, 0, (GLsizei)vertex_count);
    if (glGetError() != GL_NO_ERROR) {
        renderer_opengl_set_error(error, error_size, "OpenGL draw command failed");
        return 0;
    }
    return 1;
}

static int renderer_opengl_draw_geometry(RendererOpenGL *renderer,
                                         const SceneGeometry *geometry,
                                         char *error, size_t error_size)
{
    RendererOpenGLColor color;
    RendererOpenGLVertex *vertices = NULL;
    uint32_t vertex_count;
    int result;

    if (!geometry->vertices) {
        renderer_opengl_set_error(error, error_size, "scene geometry has no vertices");
        return 0;
    }
    color = renderer_opengl_geometry_color(geometry);
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
            renderer_opengl_make_vertex(&vertices[index], &geometry->vertices[index], color);
        }
        vertex_count = geometry->vertex_count;
    } else if (geometry->topology == SCENE_GEOMETRY_TOPOLOGY_POLYGON_BOUNDARY) {
        if (!renderer_opengl_triangulate_polygon(geometry, color, &vertices, &vertex_count,
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

static int renderer_opengl_draw_sprite_marker(RendererOpenGL *renderer,
                                              const SceneSprite *sprite,
                                              char *error, size_t error_size)
{
    RendererOpenGLVertex vertex;
    RendererOpenGLColor color = renderer_opengl_sprite_color(sprite);

    renderer_opengl_world_point(&sprite->position, &vertex.x, &vertex.y, &vertex.z);
    vertex.r = color.r;
    vertex.g = color.g;
    vertex.b = color.b;
    return renderer_opengl_draw_vertices(renderer, &vertex, 1u, GL_POINTS, error, error_size);
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
    renderer->gl.enable_vertex_attrib_array(RENDERER_OPENGL_COLOR_ATTRIBUTE);
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
        if (renderer->vertex_buffer != 0u && renderer->gl.delete_buffers) {
            renderer->gl.delete_buffers(1, &renderer->vertex_buffer);
        }
        if (renderer->program != 0u && renderer->gl.delete_program) {
            renderer->gl.delete_program(renderer->program);
        }
        SDL_GL_DeleteContext(renderer->context);
    }
    SDL_DestroyWindow(renderer->window);
    free(renderer);
}

int renderer_opengl_present(RendererOpenGL *renderer, const SceneFrame *frame,
                            const RenderView *view, char *error, size_t error_size)
{
    const SceneCamera *camera = NULL;
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
    for (size_t index = 0u; index < frame->count; ++index) {
        const SceneCommand *command = &frame->commands[index];

        if (command->type == SCENE_COMMAND_GEOMETRY &&
            !renderer_opengl_draw_geometry(renderer, &command->data.geometry, error, error_size)) {
            return 0;
        }
    }
    for (size_t index = 0u; index < frame->count; ++index) {
        const SceneCommand *command = &frame->commands[index];

        if (command->type == SCENE_COMMAND_SPRITE &&
            !renderer_opengl_draw_sprite_marker(renderer, &command->data.sprite, error,
                                                error_size)) {
            return 0;
        }
    }
    SDL_GL_SwapWindow(renderer->window);
    return 1;
}
