#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include "cmocka.h"

#include "sprite.h"

static void read_test_file(void **state)
{
    (void)state;
    struct sprite_t result = load_sprite("../../test/test_images/test_1x1_01.bin");
    assert_non_null(result.image);
    assert_int_equal(result.width, 1);
    assert_int_equal(result.height, 1);
    free_sprite(&result);
}

int main()
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(read_test_file)
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}