#include "source_bitmap_sprite.h"
#include "source_vector_model_scene.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_display_color(uint8_t *palette, uint8_t index,
                              uint8_t red, uint8_t green, uint8_t blue)
{
    palette[(size_t)index * 6u + 1u] = red;
    palette[(size_t)index * 6u + 3u] = green;
    palette[(size_t)index * 6u + 5u] = blue;
}

static int check_bitmap_decode(void)
{
    uint8_t wad[9] = {0};
    uint8_t pointers[8] = {0, 0, 0, 1, 0, 0, 0, 5};
    uint8_t palette[64] = {0};
    uint8_t display[256u * 6u] = {0};
    SceneCamera camera = {0};
    SceneSprite sprite = {0};
    SourceBitmapSpriteImage image = {0};
    char error[256] = {0};

    wad[1] = 0u; wad[2] = 1u;
    wad[3] = 0u; wad[4] = 2u;
    wad[5] = 0u; wad[6] = 3u;
    wad[7] = 0u; wad[8] = 0u;
    palette[2u] = 10u;
    palette[4u] = 11u;
    palette[6u] = 12u;
    set_display_color(display, 10u, 10u, 20u, 30u);
    set_display_color(display, 11u, 40u, 50u, 60u);
    set_display_color(display, 12u, 70u, 80u, 90u);
    sprite.source = SCENE_SPRITE_SOURCE_OBJECT_BITMAP;
    sprite.source_width = 1u;
    sprite.source_height = 1u;
    sprite.frame_metrics.strip_count = 1u;
    sprite.frame_metrics.line_count = 1u;
    sprite.source_bytes = wad;
    sprite.source_byte_count = sizeof(wad);
    sprite.source_aux_bytes = pointers;
    sprite.source_aux_byte_count = sizeof(pointers);
    sprite.source_palette_bytes = palette;
    sprite.source_palette_byte_count = sizeof(palette);
    sprite.source_display_palette_bytes = display;
    sprite.source_display_palette_byte_count = sizeof(display);
    if (!source_bitmap_sprite_decode(
            &sprite, &camera, &image, error, sizeof(error))) {
        fprintf(stderr, "bitmap decoder rejected an exact packed fixture: %s\n", error);
        return 0;
    }
    if (image.width != 2u || image.height != 2u || image.additive != 0u ||
        memcmp(image.rgba + 0u, (uint8_t[]){10u,20u,30u,255u}, 4u) != 0 ||
        memcmp(image.rgba + 4u, (uint8_t[]){70u,80u,90u,255u}, 4u) != 0 ||
        memcmp(image.rgba + 8u, (uint8_t[]){40u,50u,60u,255u}, 4u) != 0 ||
        image.rgba[15u] != 0u) {
        fprintf(stderr, "bitmap decoder did not preserve packed columns/palette alpha\n");
        source_bitmap_sprite_image_destroy(&image);
        return 0;
    }
    source_bitmap_sprite_image_destroy(&image);
    return 1;
}

static int check_vector_view_weapon_compile(void)
{
    uint8_t model[140] = {0};
    uint8_t texture_map[2056] = {0};
    uint8_t light_palette[64u * 256u] = {0};
    uint8_t display[256u * 6u] = {0};
    SceneSprite sprite = {0};
    SourceVectorSceneMesh mesh = {0};
    char error[256] = {0};

    model[2] = 0u; model[3] = 3u;
    model[4] = 0u; model[5] = 1u;
    model[6] = 0u; model[7] = 48u;
    model[8] = 0u; model[9] = 98u;
    model[10] = 0u; model[11] = 110u;
    model[14] = 0xffu; model[15] = 0xffu;
    model[53] = 1u;
    model[58] = 0xffu; model[59] = 0xffu;
    model[60] = 0xffu; model[61] = 0xffu;
    model[64] = 0u; model[65] = 1u;
    model[66] = 0xffu; model[67] = 0xffu;
    model[72] = 0u; model[73] = 1u;
    model[112] = 0u; model[113] = 2u;
    model[116] = 0u; model[117] = 0u; model[118] = 0u; model[119] = 0u;
    model[120] = 0u; model[121] = 1u; model[122] = 1u; model[123] = 0u;
    model[124] = 0u; model[125] = 2u; model[126] = 0u; model[127] = 1u;
    model[128] = 0u; model[129] = 0u; model[130] = 0u; model[131] = 0u;
    model[132] = 4u; model[133] = 0u;
    model[134] = 96u;
    model[138] = 0xffu; model[139] = 0xffu;
    texture_map[1024u] = 1u;
    texture_map[1028u] = 2u;
    texture_map[2048u] = 3u;
    light_palette[32u * 256u + 1u] = 20u;
    light_palette[32u * 256u + 2u] = 21u;
    light_palette[32u * 256u + 3u] = 22u;
    set_display_color(display, 20u, 100u, 10u, 20u);
    set_display_color(display, 21u, 30u, 110u, 40u);
    set_display_color(display, 22u, 50u, 60u, 120u);
    sprite.source = SCENE_SPRITE_SOURCE_VECTOR_MODEL;
    sprite.presentation = SCENE_SPRITE_PRESENTATION_PLAYER1_VIEW_WEAPON;
    sprite.source_bytes = model;
    sprite.source_byte_count = sizeof(model);
    sprite.source_palette_bytes = texture_map;
    sprite.source_palette_byte_count = sizeof(texture_map);
    sprite.source_light_palette_bytes = light_palette;
    sprite.source_light_palette_byte_count = sizeof(light_palette);
    sprite.source_display_palette_bytes = display;
    sprite.source_display_palette_byte_count = sizeof(display);
    sprite.view_weapon_projection.sine = 32767;
    sprite.view_weapon_projection.depth_bias = 100;
    sprite.view_weapon_projection.centre_x = 1u;
    sprite.view_weapon_projection.centre_y = 1u;
    sprite.view_weapon_projection.scale_numerator = 1u;
    sprite.view_weapon_projection.scale_denominator = 1u;
    if (!source_vector_scene_compile_view_weapon(
            &sprite, 1.0f, &mesh, error, sizeof(error))) {
        fprintf(stderr, "vector compiler rejected an exact triangle fixture: %s\n", error);
        return 0;
    }
    if (mesh.triangle_count != 1u || mesh.material_count != 1u ||
        mesh.materials[0].width != 2u || mesh.materials[0].height != 2u ||
        mesh.triangles[0].vertices[0].x >= mesh.triangles[0].vertices[1].x ||
        mesh.triangles[0].vertices[2].y >= mesh.triangles[0].vertices[0].y ||
        mesh.triangles[0].vertices[0].source_light < 0.9f ||
        memcmp(mesh.materials[0].rgba,
               (uint8_t[]){100u,10u,20u,255u}, 4u) != 0) {
        fprintf(stderr, "vector compiler lost source winding, projection, light, or map colour\n");
        source_vector_scene_mesh_destroy(&mesh);
        return 0;
    }
    source_vector_scene_mesh_destroy(&mesh);
    return 1;
}

int main(void)
{
    return check_bitmap_decode() && check_vector_view_weapon_compile() ? 0 : 1;
}
