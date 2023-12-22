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
    uint8_t primary_buffer;
    uint8_t encoding_method;
    uint8_t *BP0;
    uint8_t *BP1;
    uint16_t *image;
};

// Buffer for bitplane using 8x8px tiles @ 1bpp
#define PX_PER_BYTE 8
#define BUFFER_WIDTH_IN_TILES 7
#define BUFFER_HEIGHT_IN_TILES 7
#define TILE_WIDTH 1
#define TILE_HEIGHT 8
#define BUFFER_SIZE (BUFFER_WIDTH_IN_TILES * TILE_WIDTH * BUFFER_HEIGHT_IN_TILES * TILE_HEIGHT)
#define RLE_MASK 0x0000000000000001

void write_buffer(const uint64_t source, int16_t bitcount, struct bit_buffer_t *output)
{
    uint64_t bits = source & 0xffffffffffffffff >> (64 - bitcount);

    int16_t shift = (bitcount - 1) - output->bit_index;
    while (shift >= 0)
    {
        output->data[output->byte_index] |= bits >> shift;
        bitcount -= (output->bit_index + 1);

        output->bit_index = 7;
        output->byte_index++;
        shift = (bitcount - 1) - output->bit_index;
    }
    if (bitcount > 0)
    {
        shift = output->bit_index - (bitcount - 1);
        output->data[output->byte_index] |= bits << shift;
        output->bit_index -= bitcount;
    }
}

void draw_bitplane(const uint8_t *const data, const uint8_t width_in_tiles, const uint8_t height_in_tiles, const char *const filename)
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

void draw_sprite(const uint16_t *const data, const char *const filename)
{
    FILE *fp = fopen(filename, "wb");
    fprintf(fp, "P6\n%d %d\n255\n", BUFFER_WIDTH_IN_TILES * TILE_WIDTH * PX_PER_BYTE, BUFFER_HEIGHT_IN_TILES * TILE_HEIGHT);
    uint8_t bw[] = {0xff, 0xff, 0xff, 0xaa, 0xaa, 0xaa, 0x55, 0x55, 0x55, 0x33, 0x33, 0x33};

    for (int y = 0; y < BUFFER_HEIGHT_IN_TILES * TILE_HEIGHT; y++)
    {
        for (int x = 0; x < BUFFER_WIDTH_IN_TILES * TILE_WIDTH; x++)
        {
            uint16_t pixels = data[y + x * BUFFER_HEIGHT_IN_TILES * TILE_HEIGHT];
            for (int shift = 14; shift >= 0; shift -= 2)
            {
                uint16_t index = (pixels >> shift) & 0x03;
                fwrite(bw + index * 3, sizeof(uint8_t), 3, fp);
            }
        }
    }

    fclose(fp);
}

static inline void decrement_bit_index(struct bit_buffer_t *buffer, int8_t offset)
{
    buffer->byte_index += (offset - buffer->bit_index + 7) >> 3;
    buffer->bit_index = (buffer->bit_index - offset) & 0x07;
}

void diff_decode_buffer(const uint8_t width_in_tiles, const uint8_t height_in_tiles, uint8_t *buffer)
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

void diff_encode_buffer(const uint8_t width_in_tiles, const uint8_t height_in_tiles, uint8_t *buffer)
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

void rle_decode(struct bit_buffer_t *inputstream, const uint8_t width_in_tiles, const uint8_t height_in_tiles, uint8_t *output_buffer)
{
    uint16_t bitplane_size = width_in_tiles * TILE_WIDTH * height_in_tiles * TILE_HEIGHT * PX_PER_BYTE;
    uint16_t bitplane_count = 0;

    enum rle_data_t packet_type = (inputstream->data[inputstream->byte_index] >> inputstream->bit_index) & 0x01;
    decrement_bit_index(inputstream, 1);

    uint8_t x = 0;
    uint8_t y = 0;
    int8_t shift = 6;

    memset(output_buffer, 0, BUFFER_SIZE);

    while (bitplane_count < bitplane_size)
    {
        if (packet_type == RUN)
        {
            uint64_t L = 0;
            uint64_t V = 0;
            uint8_t bit_count = 0;

            do
            {
                L <<= 1;
                L |= (inputstream->data[inputstream->byte_index] >> inputstream->bit_index) & 0x01;
                decrement_bit_index(inputstream, 1);
                bit_count++;
            } while (inputstream->byte_index < inputstream->size && L & RLE_MASK);

            for (size_t i = 0; i < bit_count; i++)
            {

                V <<= 1;
                V |= (inputstream->data[inputstream->byte_index] >> inputstream->bit_index) & 0x01;
                decrement_bit_index(inputstream, 1);
                if(inputstream->byte_index >= inputstream->size)
                {
                    return;
                }
            }

            uint64_t N = L + V + 1;
            bitplane_count += N << 1;

            uint64_t delta_x = (y + N) / (height_in_tiles * TILE_HEIGHT);
            y = (y + N) % (height_in_tiles * TILE_HEIGHT);
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

                bitplane_count += 2;
            }
            else
            {
                packet_type = RUN;
            }

            decrement_bit_index(inputstream, 2);
        }
    }
}

void rle_encode(uint8_t *image, const uint8_t width_in_tiles, const uint8_t height_in_tiles, struct bit_buffer_t *outputstream)
{
    (void)outputstream;
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
                        uint64_t N = run + 1;
                        uint8_t bitcount = 0;
                        run = N;
                        while (run)
                        {
                            bitcount++;
                            run >>= 1;
                        }
                        uint64_t L = (1 << (bitcount - 1));
                        uint64_t V = N - L;
                        L -= 2;

                        write_buffer(L, bitcount - 1, outputstream);
                        write_buffer(V, bitcount - 1, outputstream);
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
        uint64_t N = run + 1;
        uint8_t bitcount = 0;
        run = N;
        while (run)
        {
            bitcount++;
            run >>= 1;
        }
        uint64_t L = (1 << (bitcount - 1));
        uint64_t V = N - L;
        L -= 2;

        write_buffer(L, bitcount - 1, outputstream);
        write_buffer(V, bitcount - 1, outputstream);
    }
}

void interleave_bitplanes(const uint8_t *buffer_a, const uint8_t *buffer_b, const size_t size, uint16_t *target)
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

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        target[i] = (buf_b_interleaved << 1) ^ buf_a_interleaved;
#else
        target[i] = (buf_a_interleaved << 1) ^ buf_b_interleaved;
#endif
    }
}

void separate_bitplanes(const uint16_t *const image, const size_t image_size, uint8_t *buffer_a, uint8_t *buffer_b)
{
    for (size_t i = 0; i < image_size; i++)
    {
        uint16_t temp = image[i] & 0x5555;
        temp = (temp ^ (temp >> 1)) & 0x3333;
        temp = (temp ^ (temp >> 2)) & 0x0f0f;
        temp = (temp ^ (temp >> 4)) & 0x00ff;
        buffer_a[i] = temp;

        temp = (image[i] >> 1) & 0x5555;
        temp = (temp ^ (temp >> 1)) & 0x3333;
        temp = (temp ^ (temp >> 2)) & 0x0f0f;
        temp = (temp ^ (temp >> 4)) & 0x00ff;
        buffer_b[i] = temp;
    }
}

void apply_sprite_offset(uint8_t *buffer, uint8_t width, uint8_t height)
{
    uint8_t *BUF_A = buffer;
    uint8_t *BUF_B = buffer + BUFFER_SIZE;
    uint8_t *BUF_C = buffer + (BUFFER_SIZE << 1);

    size_t width_offset_in_tiles = (BUFFER_WIDTH_IN_TILES - width + 1) >> 1;
    size_t height_offset_in_tiles = BUFFER_HEIGHT_IN_TILES - height;
    size_t index = (width_offset_in_tiles * BUFFER_HEIGHT_IN_TILES + height_offset_in_tiles) * TILE_HEIGHT;

    for (uint8_t c = 0; c < width * TILE_WIDTH; c++)
    {
        for (uint8_t r = 0; r < height * TILE_HEIGHT; r++)
        {
            BUF_A[index] = BUF_B[c * height * TILE_HEIGHT + r];
            index++;
        }
        index += (BUFFER_HEIGHT_IN_TILES - height) * TILE_HEIGHT;
    }

    memset(BUF_B, 0, BUFFER_SIZE);
    index = (width_offset_in_tiles * BUFFER_HEIGHT_IN_TILES + height_offset_in_tiles) * TILE_HEIGHT;
    for (uint8_t c = 0; c < width * TILE_WIDTH; c++)
    {
        for (uint8_t r = 0; r < height * TILE_HEIGHT; r++)
        {
            BUF_B[index] = BUF_C[c * height * TILE_HEIGHT + r];
            index++;
        }
        index += (BUFFER_HEIGHT_IN_TILES - height) * TILE_HEIGHT;
    }
}

void remove_sprite_offset(uint8_t *buffer, uint8_t width, uint8_t height)
{
    uint8_t *BUF_A = buffer;
    uint8_t *BUF_B = buffer + BUFFER_SIZE;
    uint8_t *BUF_C = buffer + (BUFFER_SIZE << 1);

    size_t width_offset_in_tiles = (BUFFER_WIDTH_IN_TILES - width + 1) >> 1;
    size_t height_offset_in_tiles = BUFFER_HEIGHT_IN_TILES - height;
    size_t index = 0;

    for (uint8_t c = 0; c < width * TILE_WIDTH; c++)
    {
        for (uint8_t r = 0; r < height * TILE_HEIGHT; r++)
        {
            BUF_C[index] = BUF_B[(( width_offset_in_tiles + c) * BUFFER_HEIGHT_IN_TILES + height_offset_in_tiles) * TILE_HEIGHT + r];
            index++;
        }
    }
    memset(BUF_B, 0, BUFFER_SIZE);
    index = 0;
    for (uint8_t c = 0; c < width * TILE_WIDTH; c++)
    {
        for (uint8_t r = 0; r < height * TILE_HEIGHT; r++)
        {
            BUF_B[index] = BUF_A[(( width_offset_in_tiles + c) * BUFFER_HEIGHT_IN_TILES + height_offset_in_tiles) * TILE_HEIGHT + r];
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
        printf("Unable to load file [%s]", filename);
        return v_sprite;
    }
    fseek(fp, 0L, SEEK_END);
    size_t filesize = ftell(fp);
    uint8_t *input = malloc(filesize);
    fseek(fp, 0L, SEEK_SET);
    fread(input, sizeof(uint8_t), filesize, fp);
    if(ferror(fp))
    {
        printf("File read failed.\n");
        return v_sprite;
    }
    if(feof(fp))
    {
        printf("End of file reached successfully.\n");
    }
    fclose(fp);

    uint8_t *buffer = calloc(BUFFER_SIZE * 3, sizeof(uint8_t));
    uint8_t *BUF_A = buffer;
    uint8_t *BUF_B = buffer + BUFFER_SIZE;
    uint8_t *BUF_C = buffer + 2 * BUFFER_SIZE;

    v_sprite.width = input[0] >> 4;
    v_sprite.height = input[0] & 0x0f;
    v_sprite.primary_buffer = input[1] >> 7;
    size_t image_size = v_sprite.width * TILE_WIDTH * v_sprite.height * TILE_HEIGHT;
    struct bit_buffer_t bit_ptr = {
        .data = input,
        .size = filesize,
        .byte_index = 1,
        .bit_index = 6};

    if (v_sprite.primary_buffer == 0)
    {
        v_sprite.BP0 = BUF_B;
        v_sprite.BP1 = BUF_C;
    }
    else
    {
        v_sprite.BP0 = BUF_C;
        v_sprite.BP1 = BUF_B;
    }

    printf("Decoding %ux%u tile sprite.\nPrimary buffer: %u\n", v_sprite.width, v_sprite.height, v_sprite.primary_buffer);
    rle_decode(&bit_ptr, v_sprite.width, v_sprite.height, v_sprite.BP0);
    v_sprite.encoding_method = (bit_ptr.data[bit_ptr.byte_index] >> bit_ptr.bit_index) & 0x01;
    decrement_bit_index(&bit_ptr, 1);

    if (v_sprite.encoding_method != 0)
    {
        v_sprite.encoding_method = (v_sprite.encoding_method << 1) | ((bit_ptr.data[bit_ptr.byte_index] >> bit_ptr.bit_index) & 0x01);
        decrement_bit_index(&bit_ptr, 1);
    }
    printf("Encoding mode: %u\n", v_sprite.encoding_method);

    rle_decode(&bit_ptr, v_sprite.width, v_sprite.height, v_sprite.BP1);

    diff_decode_buffer(v_sprite.width, v_sprite.height, v_sprite.BP0);
    if (v_sprite.encoding_method != 2)
    {
        diff_decode_buffer(v_sprite.width, v_sprite.height, v_sprite.BP1);
    }
    if (v_sprite.encoding_method > 1)
    {
        for (size_t i = 0; i < image_size; i++)
        {
            v_sprite.BP1[i] = v_sprite.BP1[i] ^ v_sprite.BP0[i];
        }
    }

    apply_sprite_offset(buffer, v_sprite.width, v_sprite.height);

    interleave_bitplanes(BUF_A, BUF_B, BUFFER_SIZE, (uint16_t *)BUF_B);
    v_sprite.image = (uint16_t *)malloc(BUFFER_SIZE << 1);
    memcpy(v_sprite.image, BUF_B, BUFFER_SIZE << 1);

    free(buffer);
    free(input);

    return v_sprite;
}

void save_sprite(const struct sprite_t const *v_sprite, const uint8_t encoding_method, const uint8_t primary_buffer, const char *filename)
{
    size_t image_size = v_sprite->width * TILE_WIDTH * v_sprite->height * TILE_HEIGHT;
    uint8_t *buffer = calloc(BUFFER_SIZE * 3, sizeof(uint8_t));
    uint8_t *BUF_A = buffer;
    uint8_t *BUF_B = buffer + BUFFER_SIZE;
    uint8_t *BUF_C = buffer + 2 * BUFFER_SIZE;
    uint8_t *BP0 = (primary_buffer) ? BUF_C : BUF_B;
    uint8_t *BP1 = (primary_buffer) ? BUF_B : BUF_C;

    separate_bitplanes(v_sprite->image, BUFFER_SIZE, BUF_A, BUF_B);
    remove_sprite_offset(buffer, v_sprite->width, v_sprite->height);

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

    struct bit_buffer_t compressedImage = {
        .data = calloc(BUFFER_SIZE * 2, sizeof(uint8_t)),
        .size = BUFFER_SIZE * 2};

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

    FILE *rfp = fopen(filename, "wb");
    fwrite(compressedImage.data, sizeof(uint8_t), compressedImage.byte_index, rfp);
    fclose(rfp);

    free(compressedImage.data);
    free(buffer);
}

int main(int argc, char **argv)
{
    struct sprite_t sprite = load_sprite(argv[1]);
    draw_sprite(sprite.image, "sprite.ppm");
    save_sprite(&sprite, sprite.encoding_method, sprite.primary_buffer, "resave.bin");
    free(sprite.image);
}
