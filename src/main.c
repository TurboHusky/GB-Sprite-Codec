#include "sprite.h"

int main(int argc, char **argv)
{
    (void) argc;
    struct sprite_t sprite = load_sprite(argv[1]);
    if (sprite.image)
    {
        export_sprite_to_ppm(sprite.image, "sprite.ppm");
        save_sprite(&sprite, sprite.encoding_method, sprite.primary_buffer, "resave.bin");
    }
    free_sprite(&sprite);
}
