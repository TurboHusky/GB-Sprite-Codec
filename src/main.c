#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

enum rle_data_t
{
    RUN = 0,
    DATA = 1
};

enum decode_status_t
{
    OK = 0,
    ERROR
};

struct bit_buffer_t
{
    uint8_t *data;
    size_t size;
    size_t byte_index;
    int8_t bit_index;
};

struct sprite_t
{
    uint8_t width;
    uint8_t height;
    uint8_t *BP0;
    uint8_t *BP1;
};

// Buffer for bitplane using 8x8px tiles @ 1bpp
#define PX_PER_BYTE 8
#define BUFFER_WIDTH_IN_TILES 7
#define BUFFER_HEIGHT_IN_TILES 7
#define TILE_WIDTH 1
#define TILE_HEIGHT 8
#define BUFFER_SIZE (BUFFER_WIDTH_IN_TILES * TILE_WIDTH * BUFFER_HEIGHT_IN_TILES * TILE_HEIGHT)
#define RLE_MASK 0x0000000000000001

void draw_bitplane(const uint8_t *const data, const uint8_t width, const uint8_t height, const char *const filename)
{
    FILE *fp = fopen(filename, "wb");
    fprintf(fp, "P6\n%d %d\n255\n", width * TILE_WIDTH * 8, height * TILE_HEIGHT);
    uint8_t bw[] = {0x00, 0x00, 0x00, 0xff, 0xff, 0xff};

    for (int y = 0; y < height * TILE_HEIGHT; y++)
    {
        for (int x = 0; x < width * TILE_WIDTH; x++)
        {
            uint8_t byte = data[y + x * height * TILE_HEIGHT];
            for (int shift = 7; shift >= 0; shift--)
            {
                if ((byte >> shift) & 0x01)
                {
                    fwrite(bw, sizeof(uint8_t), 3, fp);
                }
                else
                {
                    fwrite(bw + 3, sizeof(uint8_t), 3, fp);
                }
            }
        }
    }

    fclose(fp);
}

enum decode_status_t decrement_bit_index(struct bit_buffer_t *buffer, int8_t offset)
{
    buffer->bit_index -= offset;
    if (buffer->bit_index < 0)
    {
        buffer->byte_index++;
        if (buffer->byte_index >= buffer->size)
        {
            printf("ERROR: RLE read out of bounds");
            return ERROR;
        }
        buffer->bit_index += 8;
    }
    return OK;
}

void diff_buffer(const uint8_t width, const uint8_t height, uint8_t *buffer)
{
    for (uint8_t y = 0; y < height * TILE_HEIGHT; y++)
    {
        uint8_t last_bit = 0;
        for (uint8_t x = 0; x < width * TILE_WIDTH; x++)
        {
            uint8_t temp = buffer[x * height * TILE_HEIGHT + y];
            for (int8_t i = 7; i >= 0; i--)
            {
                temp ^= last_bit << i;
                last_bit = (temp >> i) & 0x01;
            }
            buffer[x * height * TILE_HEIGHT + y] = temp;
        }
    }
}

void decode(struct bit_buffer_t *inputstream, const uint8_t width, const uint8_t height, uint8_t *output_buffer)
{
    uint16_t bitplane_size = width * TILE_WIDTH * height * TILE_HEIGHT * PX_PER_BYTE;
    uint16_t bitplane_count = 0;

    enum rle_data_t packet_type = (inputstream->data[inputstream->byte_index] >> inputstream->bit_index) & 0x01;
    inputstream->bit_index--;

    uint8_t x = 0;
    uint8_t y = 0;
    int8_t shift = 6;

    memset(output_buffer, 0, BUFFER_SIZE);

    while (bitplane_count < bitplane_size)
    {
        if (packet_type == RUN)
        {
            uint64_t run_length = 0;
            uint64_t value = 0;
            uint8_t bit_count = 0;

            do
            {
                run_length <<= 1;
                run_length |= (inputstream->data[inputstream->byte_index] >> inputstream->bit_index) & 0x01;
                if (decrement_bit_index(inputstream, 1) == ERROR)
                {
                    return;
                }
                bit_count++;
            } while (run_length & RLE_MASK);

            for (size_t i = 0; i < bit_count; i++)
            {

                value <<= 1;
                value |= (inputstream->data[inputstream->byte_index] >> inputstream->bit_index) & 0x01;

                if (decrement_bit_index(inputstream, 1) == ERROR)
                {
                    return;
                }
            }

            uint64_t N = run_length + value + 1;
            bitplane_count += N << 1;

            uint64_t delta_x = (y + N) / (height * TILE_HEIGHT);
            y = (y + N) % (height * TILE_HEIGHT);
            x += (delta_x - (shift >> 1) + 3) >> 2;
            shift = (shift - (delta_x << 1)) % 8;

            packet_type = DATA;
        }
        else
        {
            if (inputstream->bit_index < 1)
            {
                inputstream->byte_index++;
                if (inputstream->byte_index >= inputstream->size)
                {
                    printf("ERROR: RLE read out of bounds");
                    return;
                }
                inputstream->bit_index += 8;
            }

            uint8_t bit_pair = (inputstream->bit_index == 8) ? ((inputstream->data[inputstream->byte_index - 1] << 1) & 0x02) | (inputstream->data[inputstream->byte_index] >> 7) : (inputstream->data[inputstream->byte_index] >> (inputstream->bit_index - 1)) & 0x03;

            if (bit_pair)
            {
                output_buffer[x * height * TILE_HEIGHT + y] |= (bit_pair << shift);
                y++;
                if (y >= height * TILE_HEIGHT)
                {
                    y = 0;
                    shift -= 2;
                    if (shift < 0)
                    {
                        shift += 8;
                        x++;
                    }
                }

                bitplane_count += 2;
            }
            else
            {
                packet_type = RUN;
            }

            if (decrement_bit_index(inputstream, 2) == ERROR)
            {
                return;
            }
        }
    }

    if (decrement_bit_index(inputstream, 0) == ERROR)
    {
        return;
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    // uint8_t input[] = {0x55, 0x3B, 0x13, 0xC4, 0x5D, 0x74, 0xEA, 0x54, 0x1F, 0xB0, 0x71, 0xB3, 0xA2, 0xEB, 0x9C, 0x30, 0xC1, 0x06, 0x4E, 0x74, 0x22, 0x28, 0xAE, 0x08, 0xE7, 0x5C, 0x86, 0x31, 0xBF, 0xC1, 0x38, 0x1E, 0x86, 0x8D, 0x94, 0x24, 0xDB, 0xAF, 0x29, 0xF1, 0x49, 0x47, 0xA0, 0x4D, 0x20, 0x90, 0x51, 0x7B, 0x07, 0xEA, 0xD1, 0x76, 0x34, 0x66, 0x0A, 0x41, 0x0A, 0x41, 0x25, 0xA0, 0x89, 0x74, 0xC5, 0x50, 0x99, 0x91, 0x5A, 0xBA, 0x06, 0x25, 0xC1, 0x4C, 0x29, 0x09, 0x0B, 0xF9, 0x92, 0x6E, 0x0E, 0x50, 0xFE, 0x30, 0x59, 0x90, 0x57, 0xB6, 0x16, 0x82, 0x07, 0xA7, 0x63, 0x07, 0x81, 0xDE, 0xFF, 0xEA, 0xDA, 0x82, 0x16, 0x1B, 0xA4, 0xA8, 0x38, 0x6F, 0xFD, 0x64, 0x8F, 0x98, 0x20, 0x95, 0x81, 0xA3, 0x0A, 0xAC, 0x1C, 0x1A, 0x07, 0xA1, 0x74, 0x25, 0x58, 0x54, 0xD6, 0xFE, 0xAE, 0x7F, 0x86, 0xF9, 0x57, 0xE3, 0x82, 0x95, 0xF4, 0x23, 0x0A, 0xA8, 0xF9, 0x53, 0xB1, 0x3C, 0x45, 0xD7, 0x4E, 0xA5, 0x41, 0xFB, 0xA8, 0x68, 0xE8, 0xAA, 0xE7, 0xF1, 0x88, 0xA3, 0x9D, 0x52, 0x2D, 0x88, 0xE6, 0x39, 0xD7, 0x4D, 0x64, 0x3F, 0x4E, 0x06, 0x29, 0x8D, 0x28, 0x68, 0xD5, 0xEC, 0xA6, 0x69, 0x21, 0x26, 0x86, 0x8D, 0xB9, 0x07, 0x58, 0x25, 0x9A, 0xB4, 0x89, 0x8D, 0x2A, 0x1A, 0x1C, 0x89, 0x4B, 0x58, 0xA9, 0x30, 0x56, 0x21, 0x63, 0x19, 0x16, 0x31, 0x4C, 0x78, 0x53, 0x7A, 0x2F, 0x58, 0xC1, 0x48, 0x4B, 0x4D, 0x56, 0x42, 0x30, 0x79, 0x68, 0x15, 0x68, 0x23, 0x2A, 0x26, 0x09, 0x2A, 0xEF, 0xFC, 0xC1, 0x32, 0x8C, 0xA2, 0xEA, 0xA6, 0xA5, 0xA2, 0x18, 0x54, 0x65, 0x58, 0x84, 0xD6, 0x45, 0x55, 0x99, 0x86, 0x52, 0x96, 0x38, 0x2B, 0xFF, 0xD8, 0xC2, 0xAA, 0x3E, 0x54};
    // uint8_t input[] = {0x77, 0x3E, 0x59, 0x4C, 0x14, 0xED, 0x69, 0x53, 0x27, 0xA8, 0x87, 0x0A, 0x0D, 0x05, 0x4E, 0x54, 0x54, 0x63, 0x05, 0xD7, 0x98, 0x11, 0x55, 0xBF, 0xA5, 0x38, 0x11, 0xCB, 0x49, 0x56, 0x90, 0x6B, 0x6D, 0x06, 0x0C, 0xBB, 0x4D, 0x55, 0x0A, 0x1E, 0x09, 0x0F, 0x22, 0x08, 0x50, 0x92, 0x22, 0x18, 0x89, 0x4D, 0x62, 0x24, 0x52, 0x94, 0x22, 0x11, 0x88, 0xA8, 0x95, 0xFD, 0x94, 0xE4, 0x42, 0x4C, 0x70, 0x68, 0x53, 0x36, 0x21, 0x53, 0x26, 0x29, 0x29, 0x2D, 0x60, 0xC7, 0x8A, 0xC2, 0x89, 0x41, 0x4C, 0x92, 0x12, 0x63, 0x66, 0x21, 0xAD, 0x25, 0x85, 0x22, 0x4C, 0x50, 0xA7, 0x88, 0x49, 0x41, 0x27, 0x04, 0x32, 0x70, 0x48, 0xFE, 0xB2, 0x26, 0x31, 0x8A, 0x91, 0x45, 0x62, 0xA1, 0x86, 0xA6, 0x86, 0x08, 0xAD, 0x0F, 0x30, 0x4A, 0x5A, 0x1A, 0x56, 0x0C, 0x1B, 0x64, 0x2F, 0xD8, 0x20, 0x51, 0x94, 0x30, 0x8C, 0xA4, 0x20, 0xA4, 0x22, 0x3A, 0x47, 0xD5, 0x93, 0x56, 0x42, 0xE0, 0xA8, 0xC2, 0x60, 0xC6, 0x04, 0x10, 0x85, 0x15, 0x16, 0x32, 0x81, 0x59, 0xE1, 0x81, 0x4C, 0x51, 0x49, 0x41, 0x26, 0x18, 0xC5, 0x54, 0x94, 0x28, 0x26, 0x86, 0x8C, 0x16, 0x08, 0x72, 0xB1, 0xCA, 0xD0, 0x60, 0xB4, 0x26, 0x1A, 0x95, 0x15, 0xE2, 0xD8, 0x60, 0xF1, 0x88, 0xFE, 0x25, 0xE8, 0x88, 0x2D, 0x5F, 0x8A, 0xE0, 0x4E, 0x1E, 0x06, 0x94, 0x2B, 0x18, 0x94, 0x22, 0x3A, 0x0A, 0x06, 0x42, 0x34, 0x62, 0x10, 0x85, 0x25, 0x95, 0x29, 0xA8, 0x11, 0x61, 0xFD, 0x82, 0x87, 0x4C, 0xAA, 0xE3, 0x59, 0x4A, 0xB4, 0x11, 0xD9, 0x8B, 0x81, 0x4C, 0x64, 0xB6, 0x25, 0x2D, 0x2D, 0x18, 0x11, 0x95, 0x81, 0x22, 0x8C, 0xD3, 0x04, 0x52, 0x61, 0x89, 0x7A, 0x41, 0x04, 0x55, 0xA1, 0x95, 0x38, 0x51, 0x62, 0x62, 0x21, 0x48, 0xFD, 0x0D, 0xDC, 0xAB, 0xA8, 0xD2, 0x56, 0x25, 0x21, 0x29, 0xDA, 0x15, 0x50, 0xAF, 0x57, 0xE9, 0xE3, 0x54, 0x5A, 0x1A, 0x63, 0x07, 0x0F, 0x6A, 0x8A, 0xA8, 0xE7, 0x94, 0xDD, 0x47, 0x1F, 0x3B, 0x64, 0x55, 0x8D, 0x6B, 0x7F, 0x8E, 0x23, 0xE5, 0x94, 0xC1, 0x53, 0xB1, 0xFD, 0x4C, 0x98, 0x62, 0x1F, 0xAB, 0x41, 0x54, 0xE4, 0xA0, 0x5A, 0xCC, 0x12, 0xA7, 0xFF, 0x19, 0xFF, 0xBD, 0x38, 0x28, 0x32, 0x60, 0x49, 0x48, 0xA5, 0x81, 0xA8, 0x7A, 0x0F, 0xD0, 0xAD, 0x34, 0x52, 0x28, 0x2F, 0xB4, 0x32, 0x64, 0xAA, 0x93, 0x0A, 0xA0, 0x4D, 0xD4, 0x85, 0x85, 0x05, 0x16, 0x04, 0x94, 0x88, 0xA4, 0x3A, 0x14, 0xD1, 0x2D, 0x47, 0x0A, 0x18, 0x60, 0xA1, 0x89, 0xCB, 0x4A, 0x53, 0x25, 0x1A, 0x33, 0x96, 0x49, 0x81, 0x8A, 0x46, 0x30, 0x4C, 0xA8, 0xA6, 0x81, 0x8A, 0xA4, 0xA8, 0x85, 0xC5, 0x42, 0x2F, 0x4C, 0x68, 0x58, 0x22, 0x52, 0x1E, 0xAB, 0x21, 0x21, 0x42, 0x27, 0x0A, 0xC9, 0xA3, 0x26, 0x05, 0x8A, 0x9E, 0x1A, 0x2A, 0x08, 0x8B, 0x22, 0x2A, 0x87, 0xCC, 0x94, 0x10, 0xEA, 0x15, 0x59, 0x09, 0x21, 0x82, 0xDF, 0x82, 0x25, 0x14, 0x58, 0xC6, 0x85, 0xC2, 0x81, 0x46, 0x25, 0x25, 0x05, 0x35, 0x88, 0x61, 0xA3, 0x0B, 0x6C, 0x51, 0xA2, 0xB2, 0x90, 0x4A, 0x90, 0x8C, 0xE0, 0x93, 0x15, 0x04, 0x53, 0x28, 0x20, 0x46, 0xB5, 0x50, 0x4C, 0xE8, 0x68, 0xC2, 0x83, 0xC9, 0x78, 0xAC, 0xDE, 0x63, 0x5E, 0x2E, 0x81, 0x0C, 0x38, 0x97, 0x50, 0xDA, 0x4E, 0x94, 0xD1, 0x05, 0xAA, 0x21, 0x42, 0xB0, 0x8E, 0x36, 0x4E, 0x24, 0x62, 0x58, 0x2D, 0x52, 0x10, 0x53, 0x35, 0x88, 0x48, 0xD3, 0x0A, 0x22, 0x09, 0x21, 0x26, 0xA5, 0x29, 0x2D, 0x63, 0x35, 0x31, 0xB4, 0xAA, 0x18, 0xC5, 0x68, 0xC5, 0x31, 0x49, 0x8C, 0xD1, 0xA1, 0x51, 0x48, 0xA1, 0xA4, 0x63, 0x06, 0x55, 0xA2, 0x93, 0x65, 0x21, 0x42, 0x0E, 0x88, 0xAB, 0x8C, 0x13, 0x5A, 0x34, 0x53, 0x0D, 0x05, 0x29, 0x8C, 0x50, 0xA5, 0x21, 0x69, 0xE3, 0x81, 0xA0, 0xAB, 0x8C, 0x13, 0x18, 0xAA, 0x8E, 0x4A, 0x09, 0x47, 0x34, 0x4C, 0x33, 0xB6, 0xD4, 0x3E, 0x35, 0xA5, 0x56, 0x38, 0x80};
    uint8_t input[] = {0x55, 0xBB, 0x13, 0xC4, 0x5D, 0x74, 0xEA, 0x54, 0x1F, 0xBA, 0x86, 0x8E, 0x8A, 0xAE, 0x7F, 0x18, 0x8A, 0x39, 0xD5, 0x22, 0xD8, 0x8E, 0x63, 0x9D, 0x74, 0xD6, 0x43, 0xF4, 0xE0, 0x62, 0x98, 0xD2, 0x86, 0x8D, 0x5E, 0xCA, 0x66, 0x92, 0x12, 0x68, 0x68, 0xDB, 0x90, 0x75, 0x82, 0x59, 0xAB, 0x48, 0x98, 0xD2, 0xA1, 0xA1, 0xC8, 0x94, 0xB5, 0x8A, 0x93, 0x05, 0x62, 0x16, 0x31, 0x91, 0x63, 0x14, 0xC7, 0x85, 0x37, 0xA2, 0xF5, 0x8C, 0x14, 0x84, 0xB4, 0xD5, 0x64, 0x23, 0x07, 0x96, 0x81, 0x56, 0x82, 0x32, 0xA2, 0x60, 0x92, 0xAE, 0xFF, 0xCC, 0x13, 0x28, 0xCA, 0x2E, 0xAA, 0x6A, 0x5A, 0x21, 0x85, 0x46, 0x55, 0x88, 0x4D, 0x64, 0x55, 0x59, 0x98, 0x65, 0x29, 0x63, 0x82, 0xBF, 0xFD, 0x8C, 0x2A, 0xA3, 0xE5, 0x77, 0xD4, 0xDA, 0x42, 0x74, 0xA5, 0xE3, 0xC8, 0x21, 0x9D, 0x0E, 0x4A, 0xC5, 0x27, 0x5A, 0x2C, 0x65, 0x63, 0x9C, 0x4D, 0x0D, 0xC6, 0x96, 0x8E, 0x6A, 0x8B, 0xE4, 0x30, 0x2A, 0x08, 0x22, 0x84, 0xD5, 0x9A, 0x40, 0xEA, 0x1A, 0x0C, 0xA0, 0xC6, 0x0A, 0x6D, 0x87, 0x10, 0x84, 0x15, 0xA5, 0x36, 0x94, 0xA6, 0x28, 0xE1, 0x38, 0xA2, 0xB5, 0x3C, 0x60, 0xE0, 0x98, 0xCE, 0x10, 0xDF, 0xCC, 0x93, 0x23, 0xCA, 0x34, 0x67, 0x30, 0xAB, 0xD3, 0x0B, 0x41, 0x03, 0xFF, 0x26, 0x88, 0x3F, 0xD4, 0xB0, 0x5B, 0x50, 0x41, 0x70, 0xCC, 0x63, 0xAF, 0xA1, 0xFE, 0xB1, 0x3F, 0x31, 0x12, 0xB0, 0x31, 0x84, 0x4D, 0x6A, 0x4B, 0xE4, 0xC9, 0x92, 0x6F, 0x7F, 0xF7, 0xCC, 0x4D, 0x4A, 0xB4, 0x71, 0xD4, 0x32, 0x7D, 0x10};

    uint8_t *buffer = malloc(BUFFER_SIZE * 3);
    uint8_t *BUF_B = buffer + BUFFER_SIZE;
    uint8_t *BUF_C = buffer + 2 * BUFFER_SIZE;

    struct bit_buffer_t bit_ptr = {
        .data = input,
        .size = sizeof(input),
        .byte_index = 1,
        .bit_index = 6};

    struct sprite_t v_sprite;
    v_sprite.width = input[0] >> 4;
    v_sprite.height = input[0] & 0x0f;
    uint8_t primary_buffer = input[1] >> 7;

    if (primary_buffer == 0)
    {
        v_sprite.BP0 = BUF_B;
        v_sprite.BP1 = BUF_C;
    }
    else
    {
        v_sprite.BP0 = BUF_C;
        v_sprite.BP1 = BUF_B;
    }

    printf("Decoding %ux%u, primary buffer: %u\n", v_sprite.width, v_sprite.height, primary_buffer);

    decode(&bit_ptr, v_sprite.width, v_sprite.height, v_sprite.BP0);
    draw_bitplane(v_sprite.BP0, v_sprite.width, v_sprite.height, "buffer1.ppm");

    printf("Encoding bit: %02x  %lu %d\n", bit_ptr.data[bit_ptr.byte_index], bit_ptr.byte_index, bit_ptr.bit_index);

    uint8_t encoding_method = (bit_ptr.data[bit_ptr.byte_index] >> bit_ptr.bit_index) & 0x01;
    if (decrement_bit_index(&bit_ptr, 1) == ERROR)
    {
        return ERROR;
    }

    if (encoding_method != 0)
    {
        encoding_method = (encoding_method << 1) | ((bit_ptr.data[bit_ptr.byte_index] >> bit_ptr.bit_index) & 0x01);

        if (decrement_bit_index(&bit_ptr, 1) == ERROR)
        {
            return ERROR;
        }
    }
    printf("Encoding: %u\n", encoding_method);

    printf("Input: %02x  %lu %d\n", bit_ptr.data[bit_ptr.byte_index], bit_ptr.byte_index, bit_ptr.bit_index);
    decode(&bit_ptr, v_sprite.width, v_sprite.height, v_sprite.BP1);
    draw_bitplane(v_sprite.BP1, v_sprite.width, v_sprite.height, "buffer2.ppm");

    diff_buffer(v_sprite.width, v_sprite.height, v_sprite.BP0);
    draw_bitplane(v_sprite.BP0, v_sprite.width, v_sprite.height, "buffer1_diff.ppm");

    if (encoding_method != 2)
    {
        diff_buffer(v_sprite.width, v_sprite.height, v_sprite.BP1);
        draw_bitplane(v_sprite.BP1, v_sprite.width, v_sprite.height, "buffer2_diff.ppm");
    }
    if (encoding_method > 1)
    {
        for (size_t i = 0; i < v_sprite.width * TILE_WIDTH * v_sprite.height * TILE_HEIGHT; i++)
        {
            v_sprite.BP1[i] = v_sprite.BP1[i] ^ v_sprite.BP0[i];
        }
        draw_bitplane(v_sprite.BP1, v_sprite.width, v_sprite.height, "buffer2_xor.ppm");
    }

    free(buffer);
}