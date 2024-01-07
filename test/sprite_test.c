#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include "cmocka.h"

#include "sprite.h"

static void read_test_file(void **state)
{
    (void)state;
    struct sprite_t sprite = load_sprite("../../test/test_images/test_1x1_01.bin");
    assert_non_null(sprite.image);
    assert_int_equal(sprite.width, 1);
    assert_int_equal(sprite.height, 1);
    free_sprite(&sprite);

}

static void free_sprite_resources(void **state)
{
    (void)state;
    struct sprite_t sprite = load_sprite("../../test/test_images/test_1x1_01.bin");
    free_sprite(&sprite);
    assert_null(sprite.image);
    assert_uint_equal(sprite.width, 0);
    assert_uint_equal(sprite.height, 0);    
}

int main()
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(read_test_file),
        cmocka_unit_test(free_sprite_resources)
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
