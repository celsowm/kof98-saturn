#ifndef MOCK_VDP1_H
#define MOCK_VDP1_H
#include <stdint.h>
typedef enum {SAT_OK=0,SAT_ERR_INVALID_ARG=-1,SAT_ERR_CAPACITY=-3,SAT_ERR_UNSUPPORTED=-4,SAT_ERR_IO=-5,SAT_ERR_NOT_FOUND=-6,SAT_ERR_BUSY=-7,SAT_ERR_ALREADY_EXISTS=-12} sat_result_t;
#define SAT_TRY(x) do {sat_result_t st_=(x);if(st_!=SAT_OK)return st_;}while(0)
#define SAT_FX16_ONE 65536
#define SAT_VDP1_TEXTURE_LUT4 1u
#define SAT_SPRITE_FLAG_FLIP_H 0x20u
#define SAT_SPRITE_FLAG_FLIP_V 0x40u
typedef struct {uint16_t srca,width,height,palette,valid,format;} sat_vdp1_texture_t;
typedef struct {int32_t x,y;uint16_t width,height;const sat_vdp1_texture_t *texture;uint16_t palette_override,flags;} sat_sprite_cmd_t;
typedef struct {uint32_t frame_serial;uint16_t used,gouraud_tables,overlay_reserved;uint8_t overlay_pass,reserved;} sat_vdp1_command_checkpoint_t;
typedef struct {uint16_t used,capacity,overlay_reserved;uint8_t overlay_pass,reserved;} sat_vdp1_command_stats_t;
sat_result_t sat_vdp1_vram_reserve(uint32_t,uint32_t*);sat_result_t sat_vdp1_vram_write(uint32_t,const void*,uint32_t);sat_result_t sat_vdp1_command_checkpoint(sat_vdp1_command_checkpoint_t*);sat_result_t sat_vdp1_command_rollback(const sat_vdp1_command_checkpoint_t*);sat_result_t sat_vdp1_command_stats(sat_vdp1_command_stats_t*);sat_result_t sat_draw_sprite(const sat_sprite_cmd_t*);
#endif
