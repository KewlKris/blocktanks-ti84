#ifndef wall_include_file
#define wall_include_file

#ifdef __cplusplus
extern "C" {
#endif

#define wall_width 32
#define wall_height 32
#define wall_size 1026
#define wall ((gfx_sprite_t*)wall_data)
extern unsigned char wall_data[1026];

#ifdef __cplusplus
}
#endif

#endif
