#ifndef AB3D2_LEVEL_MECHANISMS_H
#define AB3D2_LEVEL_MECHANISMS_H

#include <stddef.h>
#include <stdint.h>

#include "level_bootstrap.h"

/* defs.i:LVL_MAX_DOOR_ZONES and LVL_MAX_LIFT_ZONES. */
enum {
    LEVEL_MECHANISMS_MAX_DOORS = 16,
    LEVEL_MECHANISMS_MAX_LIFTS = 16,
    /* newanims.s:SwitchRoutine starts with d0 = 7 and uses DBRA. */
    LEVEL_MECHANISMS_SWITCH_COUNT = 8
};

/*
 * defs.i:ZLiftableT native view. The adjacent door/lift-wall list remains
 * separate because newanims.s walks it to its negative source marker.
 */
typedef struct {
    int16_t bottom;
    int16_t top;
    int16_t opening_speed;
    int16_t closing_speed;
    int16_t open_duration;
    int16_t opening_sound_fx;
    int16_t closing_sound_fx;
    int16_t opened_sound_fx;
    int16_t closed_sound_fx;
    int16_t word9;
    int16_t word10;
    int16_t word11;
    int16_t word12;
    uint32_t graphics_offset;
    int16_t zone_id;
    int16_t word16;
    uint8_t raise_condition;
    uint8_t lower_condition;
    uint32_t wall_data_offset;
    uint16_t wall_count;
} LevelLiftable;

/* c/zone_liftable.h:ZDoorWall, used by both DoorRoutine and LiftRoutine. */
typedef struct {
    int16_t edge_index;
    uint32_t graphics_offset;
    uint32_t unknown_long;
} LevelLiftableWall;

/*
 * newanims.s:SwitchRoutine's fixed 14-byte record. Names only describe bytes
 * whose use is directly established by that routine; remaining bytes are raw.
 */
typedef struct {
    int16_t word0;
    uint8_t byte2;
    uint8_t byte3;
    uint16_t point_index;
    uint32_t graphics_offset;
    uint8_t byte10;
    uint8_t bytes11_to_13[3];
} LevelSwitch;

typedef struct {
    const uint8_t *graphics_bytes;
    size_t graphics_size;
    LevelLiftable doors[LEVEL_MECHANISMS_MAX_DOORS];
    uint16_t door_count;
    LevelLiftable lifts[LEVEL_MECHANISMS_MAX_LIFTS];
    uint16_t lift_count;
    LevelSwitch switches[LEVEL_MECHANISMS_SWITCH_COUNT];
} LevelMechanisms;

int level_mechanisms_init(const AssetBlob *graphics_data,
                          const LevelGraphicsBootstrap *graphics_header,
                          LevelMechanisms *out_mechanisms,
                          char *error, size_t error_size);
int level_mechanisms_get_door(const LevelMechanisms *mechanisms, uint16_t door_index,
                              LevelLiftable *out_door,
                              char *error, size_t error_size);
int level_mechanisms_get_lift(const LevelMechanisms *mechanisms, uint16_t lift_index,
                              LevelLiftable *out_lift,
                              char *error, size_t error_size);
int level_mechanisms_get_door_wall(const LevelMechanisms *mechanisms,
                                   uint16_t door_index, uint16_t wall_index,
                                   LevelLiftableWall *out_wall,
                                   char *error, size_t error_size);
int level_mechanisms_get_lift_wall(const LevelMechanisms *mechanisms,
                                   uint16_t lift_index, uint16_t wall_index,
                                   LevelLiftableWall *out_wall,
                                   char *error, size_t error_size);
int level_mechanisms_get_switch(const LevelMechanisms *mechanisms,
                                uint16_t switch_index, LevelSwitch *out_switch,
                                char *error, size_t error_size);

#endif
