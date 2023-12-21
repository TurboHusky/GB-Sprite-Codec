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
    fprintf(fp, "P6\n%d %d\n255\n", BUFFER_WIDTH_IN_TILES * TILE_WIDTH * 8, BUFFER_HEIGHT_IN_TILES * TILE_HEIGHT);
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
    if (decrement_bit_index(inputstream, 1) == ERROR)
    {
        return;
    }

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
        *(target + i) = (buf_b_interleaved << 1) ^ buf_a_interleaved;
#else
        *(target + i) = (buf_a_interleaved << 1) ^ buf_b_interleaved;
#endif
    }
}

void read_bitplanes(const uint16_t *const image, const size_t image_size, uint8_t *buffer_a, uint8_t *buffer_b)
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

int main(int argc, char **argv)
{
    (void)argc;
    FILE *fp = fopen(argv[1], "rb");
    fseek(fp, 0L, SEEK_END);
    size_t filesize = ftell(fp);
    uint8_t *input = malloc(filesize);
    fseek(fp, 0L, SEEK_SET);
    fread(input, sizeof(uint8_t), filesize, fp);
    fclose(fp);

    uint8_t *buffer = calloc(BUFFER_SIZE * 3, sizeof(uint8_t));
    uint8_t *BUF_A = buffer;
    uint8_t *BUF_B = buffer + BUFFER_SIZE;
    uint8_t *BUF_C = buffer + 2 * BUFFER_SIZE;

    struct bit_buffer_t bit_ptr = {
        .data = input,
        .size = filesize,
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

    size_t image_size = v_sprite.width * TILE_WIDTH * v_sprite.height * TILE_HEIGHT;

    rle_decode(&bit_ptr, v_sprite.width, v_sprite.height, v_sprite.BP0);
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
    rle_decode(&bit_ptr, v_sprite.width, v_sprite.height, v_sprite.BP1);
    draw_bitplane(v_sprite.BP1, v_sprite.width, v_sprite.height, "buffer2.ppm");

    diff_decode_buffer(v_sprite.width, v_sprite.height, v_sprite.BP0);
    draw_bitplane(v_sprite.BP0, v_sprite.width, v_sprite.height, "buffer1_diff.ppm");

    if (encoding_method != 2)
    {
        diff_decode_buffer(v_sprite.width, v_sprite.height, v_sprite.BP1);
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

    uint16_t *image = calloc(image_size, sizeof(uint16_t));
    interleave_bitplanes(BUF_B, BUF_C, image_size, image);
    // draw_sprite(image, "test.ppm");

    size_t width_offset_in_tiles = (BUFFER_WIDTH_IN_TILES - v_sprite.width + 1) >> 1;
    size_t height_offset_in_tiles = BUFFER_HEIGHT_IN_TILES - v_sprite.height;
    size_t index = (width_offset_in_tiles * BUFFER_HEIGHT_IN_TILES + height_offset_in_tiles) * TILE_HEIGHT;

    for (uint8_t c = 0; c < v_sprite.width * TILE_WIDTH; c++)
    {
        for (uint8_t r = 0; r < v_sprite.height * TILE_HEIGHT; r++)
        {
            BUF_A[index] = BUF_B[c * v_sprite.height * TILE_HEIGHT + r];
            index++;
        }
        index += (BUFFER_HEIGHT_IN_TILES - v_sprite.height) * TILE_HEIGHT;
    }
    draw_bitplane(BUF_A, BUFFER_WIDTH_IN_TILES, BUFFER_HEIGHT_IN_TILES, "buffer_a.ppm");

    memset(BUF_B, 0, BUFFER_SIZE);
    index = (width_offset_in_tiles * BUFFER_HEIGHT_IN_TILES + height_offset_in_tiles) * TILE_HEIGHT;
    for (uint8_t c = 0; c < v_sprite.width * TILE_WIDTH; c++)
    {
        for (uint8_t r = 0; r < v_sprite.height * TILE_HEIGHT; r++)
        {
            BUF_B[index] = BUF_C[c * v_sprite.height * TILE_HEIGHT + r];
            index++;
        }
        index += (BUFFER_HEIGHT_IN_TILES - v_sprite.height) * TILE_HEIGHT;
    }
    draw_bitplane(BUF_B, BUFFER_WIDTH_IN_TILES, BUFFER_HEIGHT_IN_TILES, "buffer_b.ppm");

    interleave_bitplanes(BUF_A, BUF_B, BUFFER_SIZE, (uint16_t *)BUF_B);

    draw_sprite((uint16_t *)BUF_B, "sprite.ppm");

    // Encode
    read_bitplanes(image, image_size, BUF_B, BUF_C);
    draw_bitplane(BUF_B, v_sprite.width, v_sprite.height, "BUF_A.ppm");
    draw_bitplane(BUF_C, v_sprite.width, v_sprite.height, "BUF_B.ppm");

    if (encoding_method > 1)
    {
        for (size_t i = 0; i < v_sprite.width * TILE_WIDTH * v_sprite.height * TILE_HEIGHT; i++)
        {
            v_sprite.BP1[i] = v_sprite.BP0[i] ^ v_sprite.BP1[i];
        }
        draw_bitplane(v_sprite.BP1, v_sprite.width, v_sprite.height, "BP1_XOR.ppm");
    }
    if (encoding_method != 2)
    {
        diff_encode_buffer(v_sprite.width, v_sprite.height, v_sprite.BP1);
        draw_bitplane(v_sprite.BP1, v_sprite.width, v_sprite.height, "BP1_DELTA.ppm");
    }
    diff_encode_buffer(v_sprite.width, v_sprite.height, v_sprite.BP0);
    draw_bitplane(v_sprite.BP0, v_sprite.width, v_sprite.height, "BP0_DELTA.ppm");

    struct bit_buffer_t compressedImage = {
        .data = calloc(BUFFER_SIZE * 2, sizeof(uint8_t)),
        .size = BUFFER_SIZE * 2};

    compressedImage.data[0] = v_sprite.width << 4 | v_sprite.height;
    compressedImage.data[1] = primary_buffer << 7;
    compressedImage.byte_index = 1;
    compressedImage.bit_index = 6;

    rle_encode(v_sprite.BP0, v_sprite.width, v_sprite.height, &compressedImage);
    int16_t count = (encoding_method == 0) ? 1 : 2;
    write_buffer(encoding_method, count, &compressedImage);
    rle_encode(v_sprite.BP1, v_sprite.width, v_sprite.height, &compressedImage);

    if(compressedImage.bit_index != 7)
    {
        compressedImage.byte_index++;
    }

    FILE *rfp = fopen("recompressed.bin", "wb");
    fwrite(compressedImage.data, sizeof(uint8_t), compressedImage.byte_index, rfp);
    fclose(rfp);

    free(input);
    free(buffer);
    free(image);
    free(compressedImage.data);
}