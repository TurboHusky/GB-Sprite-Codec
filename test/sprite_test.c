#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include "cmocka.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "sprite.h"

#define PRIMARY_BUFFER_B 0
#define PRIMARY_BUFFER_C 1
#define TEST_BUFFER_SIZE 392
#define TEST_1X1_02_OFFSET 216

const char *const b1 = "../../test/test_images/test_1x1_02_b1.bin";
const char *const b2 = "../../test/test_images/test_1x1_02_b2.bin";
const char *const b3 = "../../test/test_images/test_1x1_02_b3.bin";
const char *const c1 = "../../test/test_images/test_1x1_02_c1.bin";
const char *const c2 = "../../test/test_images/test_1x1_02_c2.bin";
const char *const c3 = "../../test/test_images/test_1x1_02_c3.bin";

const char *const *const compressed_source_files[6] = {&b1, &b2, &b3, &c1, &c2, &c3};
const size_t compressed_file_sizes[6] = {0x13, 0x13, 0x12, 0x13, 0x13, 0x11};

const uint16_t test_1x1_02_sprite[] = {0x0055, 0x0fa5, 0x3fa9, 0x3c69, 0x96c3, 0x9503, 0xa50f, 0xaaff};
const uint8_t test_1x1_02_ppm[] = {
    0xff, 0xff, 0xff, 0xff, 0xaa, 0xaa, 0xaa, 0xaa,
    0xff, 0xff, 0x33, 0x33, 0x55, 0x55, 0xaa, 0xaa,
    0xff, 0x33, 0x33, 0x33, 0x55, 0x55, 0x55, 0xaa,
    0xff, 0x33, 0x33, 0xff, 0xaa, 0x55, 0x55, 0xaa,
    0x55, 0xaa, 0xaa, 0x55, 0x33, 0xff, 0xff, 0x33,
    0x55, 0xaa, 0xaa, 0xaa, 0xff, 0xff, 0xff, 0x33,
    0x55, 0x55, 0xaa, 0xaa, 0xff, 0xff, 0x33, 0x33,
    0x55, 0x55, 0x55, 0x55, 0x33, 0x33, 0x33, 0x33};

struct sprite_t test_sprite()
{
    struct sprite_t sprite = {
        .width = 1,
        .height = 1,
        .encoding_method = 0,
        .primary_buffer = 0
    };
    sprite.image = calloc(TEST_BUFFER_SIZE, sizeof(uint16_t));
    memcpy(sprite.image + TEST_1X1_02_OFFSET, test_1x1_02_sprite, 16);
    return sprite;
}

size_t get_file_size(const char *filename)
{
    struct stat stbuf;
    if (stat(filename, &stbuf) == -1)
    {
        return 0;
    }
    return stbuf.st_size;
}

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

void check_sprite_data(const struct sprite_t *const sprite, const uint16_t *const ref)
{
    assert_non_null(sprite->image);
    assert_int_equal(sprite->width, 1);
    assert_int_equal(sprite->height, 1);
    assert_memory_equal(sprite->image + TEST_1X1_02_OFFSET, ref, 16);
}

static void decoding(void **state)
{
    (void)state;
    struct sprite_t sprite;
    for (int i = 0; i < 6; i++)
    {
        sprite = load_sprite(*compressed_source_files[i]);
        check_sprite_data(&sprite, test_1x1_02_sprite);
        free_sprite(&sprite);
    }
}

static void encoding(void **state)
{
    (void)state;
    struct
    {
        uint8_t encoding_method;
        uint8_t primary_buffer;
    } test_input[] = {
        {0, PRIMARY_BUFFER_B},
        {1, PRIMARY_BUFFER_B},
        {2, PRIMARY_BUFFER_B},
        {0, PRIMARY_BUFFER_C},
        {1, PRIMARY_BUFFER_C},
        {2, PRIMARY_BUFFER_C}};
    struct sprite_t sprite = test_sprite();
    for (int i = 0; i < 6; i++)
    {
        save_sprite(&sprite, test_input[i].encoding_method, test_input[i].primary_buffer, "encoded.bin");
        size_t filesize = get_file_size("encoded.bin");
        assert_uint_equal(filesize, compressed_file_sizes[i]);
    }
    free_sprite(&sprite);
}

int main()
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(read_test_file),
        cmocka_unit_test(decoding),
        cmocka_unit_test(encoding),
        cmocka_unit_test(free_sprite_resources)};

    return cmocka_run_group_tests(tests, NULL, NULL);
}
