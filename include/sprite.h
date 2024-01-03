#ifndef SPRITE_H_INCLUDED
#define SPRITE_H_INCLUDED

#include <stdint.h>

struct sprite_t
{
    uint8_t width;
    uint8_t height;
    uint8_t primary_buffer;
    uint8_t encoding_method;
    uint16_t *image;
};

struct sprite_t load_sprite(const char *const filename);
void save_sprite(const struct sprite_t *const v_sprite, const uint8_t encoding_method, const uint8_t primary_buffer, const char *const filename);
void free_sprite(struct sprite_t *const sprite);

void export_sprite_to_ppm(const uint16_t *const data, const char *const filename);

#endif // SPRITE_H_INCLUDED
