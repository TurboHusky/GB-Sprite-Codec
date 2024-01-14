#include "sprite.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Buffer settings for bitplane using 8x8 pixel tiles @ 1 bit per pixel
#define PX_PER_BYTE 8
#define BUFFER_WIDTH_IN_TILES 7
#define BUFFER_HEIGHT_IN_TILES 7
#define TILE_WIDTH 1
#define TILE_HEIGHT 8
#define BUFFER_SIZE (BUFFER_WIDTH_IN_TILES * TILE_WIDTH * BUFFER_HEIGHT_IN_TILES * TILE_HEIGHT)
#define RLE_MASK 0x0000000000000001

#if defined(DEBUG) && DEBUG > 0
 #define DEBUG_PRINT(fmt, ...) fprintf(stdout, "DEBUG: %s:%d:%s(): " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#else
 #define DEBUG_PRINT(fmt, ...)
#endif

enum rle_error_t
{
    NO_ERROR,
    UNEXPECTED_EOF,
    RUN_EOF,
    DATA_EOF
};

enum rle_data_t
{
    RUN = 0,
    DATA = 1
};

struct bit_buffer_t
{
    uint8_t *data;
    size_t size;
    size_t byte_index;
    int8_t bit_index;
};

void export_bitplane_to_ppm(const uint8_t *const data, const uint8_t width_in_tiles, const uint8_t height_in_tiles, const char *const filename)
{
    FILE *fp = fopen(filename, "wb");
    fprintf(fp, "P6\n%d %d\n255\n", width_in_tiles * TILE_WIDTH * 8, height_in_tiles * TILE_HEIGHT);
    uint8_t bw[] = {0x00, 0x00, 0x00, 0xff, 0xff, 0xff};

    for (int y = 0; y < height_in_tiles * TILE_HEIGHT; y++)
    {
        for (int x = 0; x < width_in_tiles * TILE_WIDTH; x++)
        {
            uint8_t byte = data[y + x * height_in_tiles * TILE_HEIGHT];
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

void export_sprite_to_ppm(const struct sprite_t *const sprite, const char *const filename)
{
    uint8_t width_offset_in_tiles = (BUFFER_WIDTH_IN_TILES - sprite->width + 1) >> 1;
    uint8_t height_offset_in_tiles = BUFFER_HEIGHT_IN_TILES - sprite->height;

    FILE *fp = fopen(filename, "wb");
    fprintf(fp, "P6\n%d %d\n255\n", sprite->width * TILE_WIDTH * PX_PER_BYTE, sprite->height * TILE_HEIGHT);
    uint8_t bw[] = {0xff, 0xff, 0xff, 0xaa, 0xaa, 0xaa, 0x55, 0x55, 0x55, 0x33, 0x33, 0x33};

    for (int y = height_offset_in_tiles * TILE_HEIGHT; y < BUFFER_HEIGHT_IN_TILES * TILE_HEIGHT; y++)
    {
        for (int x = width_offset_in_tiles * TILE_WIDTH; x < (width_offset_in_tiles + sprite->width) * TILE_WIDTH; x++)
        {
            uint16_t pixels = sprite->image[y + x * BUFFER_HEIGHT_IN_TILES * TILE_HEIGHT];
            for (int shift = 14; shift >= 0; shift -= 2)
            {
                uint16_t index = (pixels >> shift) & 0x03;
                fwrite(bw + index * 3, sizeof(uint8_t), 3, fp);
            }
        }
    }

    fclose(fp);
}

static inline void advance_bit_index(struct bit_buffer_t *const buffer, const int8_t offset)
{
    buffer->byte_index += (offset + 7 - buffer->bit_index) >> 3;
    buffer->bit_index = (buffer->bit_index - offset) & 0x07;
}

void write_buffer(const uint64_t source, int16_t bitcount, struct bit_buffer_t *const output)
{
    while (bitcount > output->bit_index && (output->byte_index < output->size))
    {
        int16_t shift = bitcount - (output->bit_index + 1);
        output->data[output->byte_index] |= source >> shift;

        bitcount -= (output->bit_index + 1);
        output->bit_index = 7;
        output->byte_index++;
    }
    if (output->byte_index >= output->size)
    {
        fprintf(stderr, "Write buffer full\n");
        return;
    }
    if (bitcount > 0)
    {
        int16_t shift = (output->bit_index + 1) - bitcount;
        output->data[output->byte_index] |= source << shift;
        output->bit_index -= bitcount;
    }
}

void write_run_length(const uint64_t run, struct bit_buffer_t *const outputstream)
{
    uint64_t N = (run + 1) >> 1;
    uint8_t bitcount = 0;

    while (N)
    {
        bitcount++;
        N >>= 1;
    }

    N = run + 1;
    uint64_t L = 1 << bitcount;
    uint64_t V = N - L;
    L -= 2;

    write_buffer(L, bitcount, outputstream);
    write_buffer(V, bitcount, outputstream);
}

void diff_decode_buffer(const uint8_t width_in_tiles, const uint8_t height_in_tiles, uint8_t *const buffer)
{
    for (uint8_t y = 0; y < height_in_tiles * TILE_HEIGHT; y++)
    {
        uint8_t last_bit = 0;
        for (uint8_t x = 0; x < width_in_tiles * TILE_WIDTH; x++)
        {
            uint8_t temp = buffer[x * height_in_tiles * TILE_HEIGHT + y];
            for (int8_t i = 7; i >= 0; i--)
            {
                temp ^= last_bit << i;
                last_bit = (temp >> i) & 0x01;
            }
            buffer[x * height_in_tiles * TILE_HEIGHT + y] = temp;
        }
    }
}

void diff_encode_buffer(const uint8_t width_in_tiles, const uint8_t height_in_tiles, uint8_t *const buffer)
{
    for (uint8_t y = 0; y < height_in_tiles * TILE_HEIGHT; y++)
    {
        uint8_t last_bit = 0;
        for (uint8_t x = 0; x < width_in_tiles * TILE_WIDTH; x++)
        {
            uint8_t temp = buffer[x * height_in_tiles * TILE_HEIGHT + y];
            temp = temp ^ (last_bit | (temp >> 1));
            last_bit = buffer[x * height_in_tiles * TILE_HEIGHT + y] << 7;
            buffer[x * height_in_tiles * TILE_HEIGHT + y] = temp;
        }
    }
}

enum rle_error_t rle_decode(struct bit_buffer_t *const inputstream, const uint8_t width_in_tiles, const uint8_t height_in_tiles, uint8_t *const output_buffer)
{
    uint16_t bitplane_size = width_in_tiles * TILE_WIDTH * height_in_tiles * TILE_HEIGHT * PX_PER_BYTE;
    uint16_t bits_read = 0;

    enum rle_data_t packet_type = (inputstream->data[inputstream->byte_index] >> inputstream->bit_index) & 0x01;
    advance_bit_index(inputstream, 1);

    if (inputstream->byte_index == inputstream->size)
    {
        fprintf(stderr, "Packet type occurs at end of data stream\n");
        return UNEXPECTED_EOF;
    }

    uint8_t x = 0;
    uint8_t y = 0;
    int8_t shift = 6;

    memset(output_buffer, 0, BUFFER_SIZE);

    while (bits_read < bitplane_size)
    {
        if (packet_type == RUN)
        {
            uint64_t L = 0;
            uint64_t V = 0;
            uint8_t bit_count = 0;

            do
            {
                L <<= 1;
                L |= (inputstream->data[inputstream->byte_index] >> inputstream->bit_index) & RLE_MASK;
                advance_bit_index(inputstream, 1);
                bit_count++;
            }
            while ((L & RLE_MASK) && (inputstream->byte_index < inputstream->size));

            while (bit_count && (inputstream->byte_index < inputstream->size))
            {
                V <<= 1;
                V |= (inputstream->data[inputstream->byte_index] >> inputstream->bit_index) & RLE_MASK;
                advance_bit_index(inputstream, 1);
                bit_count--;
            }

            if(bit_count)
            {
                fprintf(stderr, "Incomplete RUN data\n");
                return RUN_EOF;
            }

            uint64_t N = L + V + 1;
            bits_read += N << 1;

            if (bits_read > bitplane_size)
            {
                fprintf(stderr, "RUN data out of bounds\n");
                return RUN_EOF;
            }

            uint64_t delta_x = (y + N) / (height_in_tiles * TILE_HEIGHT);
            y = (y + N) % (height_in_tiles * TILE_HEIGHT);
            x += (delta_x - (shift >> 1) + 3) >> 2;
            shift = (shift - (delta_x << 1)) % 8;

            packet_type = DATA;
        }
        else
        {
            uint8_t bit_pair;

            if (inputstream->bit_index == 0)
            {
                if ((inputstream->byte_index + 1) >= inputstream->size)
                {
                    fprintf(stderr, "Incomplete DATA\n");
                    return DATA_EOF;
                }
                bit_pair = ((inputstream->data[inputstream->byte_index] << 1) & 0x02) | (inputstream->data[inputstream->byte_index + 1] >> 7);
            }
            else
            {
                bit_pair = (inputstream->data[inputstream->byte_index] >> (inputstream->bit_index - 1)) & 0x03;
            }
            advance_bit_index(inputstream, 2);

            if (bit_pair)
            {
                output_buffer[x * height_in_tiles * TILE_HEIGHT + y] |= (bit_pair << shift);
                y++;
                if (y >= height_in_tiles * TILE_HEIGHT)
                {
                    y = 0;
                    shift -= 2;
                    if (shift < 0)
                    {
                        shift += 8;
                        x++;
                    }
                }
                bits_read += 2;
            }
            else
            {
                packet_type = RUN;
            }
        }
    }
    return NO_ERROR;
}

void rle_encode(const uint8_t *const image, const uint8_t width_in_tiles, const uint8_t height_in_tiles, struct bit_buffer_t *const outputstream)
{
    uint8_t initial_packet = (*image & 0xC0) != 0x00;
    uint64_t run = 0;

    write_buffer(initial_packet, 1, outputstream);

    enum rle_data_t current_packet = initial_packet;
    for (int x = 0; x < width_in_tiles * TILE_WIDTH; x++)
    {
        size_t base_index = x * height_in_tiles * TILE_HEIGHT;
        for (int shift = 6; shift >= 0; shift -= 2)
        {
            for (int y = 0; y < height_in_tiles * TILE_HEIGHT; y++)
            {
                uint8_t input = (image[base_index + y] >> shift) & 0x03;
                if (current_packet == RUN)
                {
                    if (input == 0)
                    {
                        run++;
                    }
                    else
                    {
                        write_run_length(run, outputstream);
                        write_buffer(input, 2, outputstream);
                        current_packet = DATA;
                    }
                }
                else
                {
                    write_buffer(input, 2, outputstream);
                    if (input == 0)
                    {
                        run = 1;
                        current_packet = RUN;
                    }
                }
            }
        }
    }
    if (run > 0)
    {
        write_run_length(run, outputstream);
    }
}

void interleave_bitplanes(const uint8_t *const buffer_a, const uint8_t *const buffer_b, const size_t size, uint16_t *const output)
{
    for (int i = size - 1; i >= 0; i--)
    {
        uint16_t buf_a_interleaved = buffer_a[i];
        buf_a_interleaved = (buf_a_interleaved ^ (buf_a_interleaved << 4)) & 0x0f0f;
        buf_a_interleaved = (buf_a_interleaved ^ (buf_a_interleaved << 2)) & 0x3333;
        buf_a_interleaved = (buf_a_interleaved ^ (buf_a_interleaved << 1)) & 0x5555;

        uint16_t buf_b_interleaved = buffer_b[i];
        buf_b_interleaved = (buf_b_interleaved ^ (buf_b_interleaved << 4)) & 0x0f0f;
        buf_b_interleaved = (buf_b_interleaved ^ (buf_b_interleaved << 2)) & 0x3333;
        buf_b_interleaved = (buf_b_interleaved ^ (buf_b_interleaved << 1)) & 0x5555;

        output[i] = (buf_b_interleaved << 1) ^ buf_a_interleaved;
    }
}

void separate_bitplanes(const uint16_t *const image, const size_t image_size, uint8_t *const buffer_a, uint8_t *const buffer_b)
{
    for (size_t i = 0; i < image_size; i++)
    {
        uint16_t temp = image[i] & 0x5555;
        temp = (temp ^ (temp >> 1)) & 0x3333;
        temp = (temp ^ (temp >> 2)) & 0x0f0f;
        buffer_a[i] = temp ^ (temp >> 4);

        temp = (image[i] >> 1) & 0x5555;
        temp = (temp ^ (temp >> 1)) & 0x3333;
        temp = (temp ^ (temp >> 2)) & 0x0f0f;
        buffer_b[i] = temp ^ (temp >> 4);
    }
}

void apply_sprite_offset(uint8_t *const buffer, const uint8_t buffer_width, const uint8_t buffer_height, uint8_t *target, const uint8_t target_width, const uint8_t target_height)
{
    size_t width_offset_in_tiles = (buffer_width - target_width + 1) >> 1;
    size_t height_offset_in_tiles = buffer_height - target_height;  
    size_t index = (width_offset_in_tiles * buffer_height + height_offset_in_tiles) * TILE_HEIGHT;
    for (uint8_t c = 0; c < target_width * TILE_WIDTH; c++)
    {
        for (uint8_t r = 0; r < target_height * TILE_HEIGHT; r++)
        {
            target[index] = buffer[c * target_height * TILE_HEIGHT + r];
            index++;
        }
        index += (buffer_height - target_height) * TILE_HEIGHT;
    } 
}

void remove_sprite_offset(uint8_t *const buffer, const uint8_t buffer_width, const uint8_t buffer_height, uint8_t *target, const uint8_t target_width, const uint8_t target_height)
{
    size_t width_offset_in_tiles = (buffer_width - target_width + 1) >> 1;
    size_t height_offset_in_tiles = buffer_height - target_height;
    size_t index = 0;

    for (uint8_t c = 0; c < target_width * TILE_WIDTH; c++)
    {
        for (uint8_t r = 0; r < target_height * TILE_HEIGHT; r++)
        {
            target[index] = buffer[(( width_offset_in_tiles + c) * buffer_height + height_offset_in_tiles) * TILE_HEIGHT + r];
            index++;
        }
    }
}

struct sprite_t load_sprite(const char *const filename)
{
    struct sprite_t v_sprite = { .width=0, .height=0, .image=NULL };
    FILE *fp = fopen(filename, "rb");
    if(fp == NULL)
    {
        fprintf(stderr, "Unable to load file [%s]\n", filename);
        return v_sprite;
    }
    fseek(fp, 0L, SEEK_END);
    size_t filesize = ftell(fp);
    uint8_t *input = malloc(filesize);
    fseek(fp, 0L, SEEK_SET);
    size_t bytes_read = fread(input, sizeof(uint8_t), filesize, fp);
    if(ferror(fp))
    {
        fprintf(stderr, "File read failed\n");
        return v_sprite;
    }
    if(bytes_read < filesize)
    {
        fprintf(stderr, "Failed to read all file contents\n");
        return v_sprite;
    }
    if(feof(fp))
    {
        DEBUG_PRINT("%s", "End of file reached successfully\n");
    }
    fclose(fp);

    uint8_t *buffer = calloc(BUFFER_SIZE * 3, sizeof(uint8_t));
    uint8_t *BUF_A = buffer;
    uint8_t *BUF_B = buffer + BUFFER_SIZE;
    uint8_t *BUF_C = buffer + 2 * BUFFER_SIZE;
    uint8_t *BP0;
    uint8_t *BP1;

    v_sprite.width = input[0] >> 4;
    v_sprite.height = input[0] & 0x0f;
    v_sprite.primary_buffer = input[1] >> 7;
    size_t image_size = v_sprite.width * TILE_WIDTH * v_sprite.height * TILE_HEIGHT;
    struct bit_buffer_t bit_ptr =
    {
        .data = input,
        .size = filesize,
        .byte_index = 1,
        .bit_index = 6
    };

    if (v_sprite.primary_buffer == 0)
    {
        BP0 = BUF_B;
        BP1 = BUF_C;
    }
    else
    {
        BP0 = BUF_C;
        BP1 = BUF_B;
    }

    DEBUG_PRINT("Decoding %ux%u tile sprite.\n", v_sprite.width, v_sprite.height);
    DEBUG_PRINT("Primary buffer: %u\n", v_sprite.primary_buffer);
    if (rle_decode(&bit_ptr, v_sprite.width, v_sprite.height, BP0) != NO_ERROR)
    {
        return v_sprite;
    }
    v_sprite.encoding_method = (bit_ptr.data[bit_ptr.byte_index] >> bit_ptr.bit_index) & 0x01;
    advance_bit_index(&bit_ptr, 1);

    if (v_sprite.encoding_method != 0)
    {
        v_sprite.encoding_method = (v_sprite.encoding_method << 1) | ((bit_ptr.data[bit_ptr.byte_index] >> bit_ptr.bit_index) & 0x01);
        advance_bit_index(&bit_ptr, 1);
    }
    DEBUG_PRINT("Encoding mode: %u\n", v_sprite.encoding_method);

    if (rle_decode(&bit_ptr, v_sprite.width, v_sprite.height, BP1) != NO_ERROR)
    {
        return v_sprite;
    }

    diff_decode_buffer(v_sprite.width, v_sprite.height, BP0);
    if (v_sprite.encoding_method != 2)
    {
        diff_decode_buffer(v_sprite.width, v_sprite.height, BP1);
    }
    if (v_sprite.encoding_method > 1)
    {
        for (size_t i = 0; i < image_size; i++)
        {
            BP1[i] = BP1[i] ^ BP0[i];
        }
    }

    apply_sprite_offset(BUF_B, BUFFER_WIDTH_IN_TILES, BUFFER_HEIGHT_IN_TILES, BUF_A, v_sprite.width, v_sprite.height);
    memset(BUF_B, 0, BUFFER_SIZE);
    apply_sprite_offset(BUF_C, BUFFER_WIDTH_IN_TILES, BUFFER_HEIGHT_IN_TILES, BUF_B, v_sprite.width, v_sprite.height);

    interleave_bitplanes(BUF_A, BUF_B, BUFFER_SIZE, (uint16_t *)BUF_B);
    v_sprite.image = (uint16_t *)malloc(BUFFER_SIZE << 1);
    memcpy(v_sprite.image, BUF_B, BUFFER_SIZE << 1);

    free(input);
    free(buffer);

    return v_sprite;
}

void save_sprite(const struct sprite_t *const v_sprite, const uint8_t encoding_method, const uint8_t primary_buffer, const char *const filename)
{
    uint8_t *buffer = calloc(BUFFER_SIZE * 3, sizeof(uint8_t));
    uint8_t *BUF_A = buffer;
    uint8_t *BUF_B = buffer + BUFFER_SIZE;
    uint8_t *BUF_C = buffer + 2 * BUFFER_SIZE;
    uint8_t *BP0 = (primary_buffer) ? BUF_C : BUF_B;
    uint8_t *BP1 = (primary_buffer) ? BUF_B : BUF_C;

    separate_bitplanes(v_sprite->image, BUFFER_SIZE, BUF_A, BUF_B);
    remove_sprite_offset(BUF_B, BUFFER_WIDTH_IN_TILES, BUFFER_HEIGHT_IN_TILES, BUF_C, v_sprite->width, v_sprite->height);
    remove_sprite_offset(BUF_A, BUFFER_WIDTH_IN_TILES, BUFFER_HEIGHT_IN_TILES, BUF_B, v_sprite->width, v_sprite->height);

    if (encoding_method > 1)
    {
        for (size_t i = 0; i < v_sprite->width * TILE_WIDTH * v_sprite->height * TILE_HEIGHT; i++)
        {
            BP1[i] = BP0[i] ^ BP1[i];
        }
    }
    if (encoding_method != 2)
    {
        diff_encode_buffer(v_sprite->width, v_sprite->height, BP1);
    }
    diff_encode_buffer(v_sprite->width, v_sprite->height, BP0);

    struct bit_buffer_t compressedImage =
    {
        .data = calloc(BUFFER_SIZE * 2, sizeof(uint8_t)),
        .size = BUFFER_SIZE * 2
    };

    compressedImage.data[0] = v_sprite->width << 4 | v_sprite->height;
    compressedImage.data[1] = primary_buffer << 7;
    compressedImage.byte_index = 1;
    compressedImage.bit_index = 6;

    rle_encode(BP0, v_sprite->width, v_sprite->height, &compressedImage);
    int16_t count = (encoding_method == 0) ? 1 : 2;
    write_buffer(encoding_method, count, &compressedImage);
    rle_encode(BP1, v_sprite->width, v_sprite->height, &compressedImage);

    if (compressedImage.bit_index != 7)
    {
        compressedImage.byte_index++;
    }

    FILE *fp = fopen(filename, "wb");
    if(fp == NULL)
    {
        fprintf(stderr, "Unable to open file [%s] for writing\n", filename);
        return;
    }
    fwrite(compressedImage.data, sizeof(uint8_t), compressedImage.byte_index, fp);
    fclose(fp);

    free(compressedImage.data);
    free(buffer);
}

void free_sprite(struct sprite_t *const sprite)
{
    free(sprite->image);
    sprite->image = NULL;
    sprite->width = 0;
    sprite->height = 0;
}
