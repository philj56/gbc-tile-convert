/*
 * Copyright (C) 2017-2020 Philip Jones
 *
 * Licensed under the MIT License.
 * See either the LICENSE file, or:
 *
 * https://opensource.org/licenses/MIT
 *
 */

#include <errno.h>
#include <png.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TILES 1024
#define MAX_PALETTES 8
#define MAX(a, b) ((a) > (b) ? (a) : (b))

struct bitmap {
	uint32_t *data;
	uint16_t width;
	uint16_t height;
};

struct tile {
	unsigned int data_idx: 9;
	unsigned int palette_idx: 3;
	bool hflip: 1;
	bool vflip: 1;
};

static void hex_to_palette(uint32_t hex[4], uint8_t palette[8]);
static int colour_in_palette(uint32_t hex, uint8_t palette[8]);
static uint16_t hex_to_gb(uint32_t hex);
static int palette_in_list(uint8_t palette[8], int n_colours, uint8_t list[MAX_PALETTES][8], uint8_t colours_in_palettes[MAX_PALETTES]);
static int tile_in_list(uint8_t tile[16], uint8_t list[MAX_TILES * 16], int n_tiles);
static bool tiles_equal(uint8_t a[16], uint8_t b[16]);
static struct bitmap load_png(const char *filename);

int main(int argc, char *argv[])
{
	if (argc != 2) {
		fprintf(stderr, "Usage: gbctc input.png\n");
		exit(EXIT_FAILURE);
	}

	struct bitmap bitmap = load_png(argv[1]);

	printf("%s: %ux%u\n", argv[1], bitmap.width, bitmap.height);
	if (8 * (bitmap.width / 8) != bitmap.width
			|| 8 * (bitmap.height / 8) != bitmap.height) {
		fprintf(stderr, "Width and height must be multiples of 8.\n");
		exit(EXIT_FAILURE);
	}

	struct tile *tiles = calloc(MAX_TILES, sizeof(*tiles));
	uint8_t *tile_data = calloc(16 * MAX_TILES, sizeof(*tile_data));
	uint8_t palettes[MAX_PALETTES][8] = {0};
	uint8_t used_colours_in_palettes[MAX_PALETTES] = {0};

	int n_tiles = 0;
	int n_palettes = 0;

	for (uint8_t ty = 0; ty < bitmap.height / 8; ty++) {
		for (uint8_t tx = 0; tx < bitmap.width / 8; tx++) {
			uint32_t base_idx = 8 * ty * bitmap.width + 8 * tx;
			uint32_t colours[4] = {bitmap.data[base_idx]};
			uint8_t cur_data[16];
			int n_colours = 1;
			for (uint8_t y = 0; y < 8; y++) {
				for (uint8_t x = 0; x < 8; x++) {
					uint32_t idx = base_idx + y * bitmap.width + x;
					uint32_t px = bitmap.data[idx];
					int c_idx = -1;
					for (int i = 0; i < 4; i++) {
						if (px == colours[i]) {
							c_idx = i;
							break;
						}
					}
					if (c_idx < 0) {
						if (n_colours == 4) {
							fprintf(stderr, "Error: More than 4 colours in tile (%u, %u).\n", tx, ty);
							fprintf(stderr, "0: 0x%08X\n", colours[0]);
							fprintf(stderr, "1: 0x%08X\n", colours[1]);
							fprintf(stderr, "2: 0x%08X\n", colours[2]);
							fprintf(stderr, "3: 0x%08X\n", colours[3]);
							fprintf(stderr, "4: 0x%08X\n", px);
							exit(EXIT_FAILURE);
						}
						colours[n_colours] = px;
						n_colours++;
					}
				}
			}
			printf("\n");

			uint8_t cur_palette[8];
			hex_to_palette(colours, cur_palette);
			int p_idx = palette_in_list(cur_palette, n_colours, palettes, used_colours_in_palettes);
			tiles[32 * ty + tx].palette_idx = p_idx;
			n_palettes = MAX(n_palettes, p_idx + 1);
			for (uint8_t y = 0; y < 8; y++) {
				uint8_t upper = 0;
				uint8_t lower = 0;
				for (uint8_t x = 0; x < 8; x++) {
					uint32_t idx = base_idx + y * bitmap.width + x;
					uint32_t px = bitmap.data[idx];
					int c_idx = colour_in_palette(px, palettes[p_idx]);
					lower <<= 1;
					upper <<= 1;
					lower |= c_idx & 1;
					upper |= (c_idx & 2) >> 1;
				}
				cur_data[2 * y] = lower;
				cur_data[2 * y + 1] = upper;
			}
			int t_idx = tile_in_list(cur_data, tile_data, n_tiles);
			tiles[32 * ty + tx].data_idx = t_idx;
			n_tiles = MAX(n_tiles, t_idx + 1);
		}
	}
	for (int p_idx = 0; p_idx < n_palettes; p_idx++) {
		uint8_t *cur_palette = palettes[p_idx];
		printf("Palette%d:\n", p_idx);
		for (int i = 0; i < 4; i++) {
			printf("  db $%02X, $%02X\n", cur_palette[2 * i], cur_palette[2 * i+1]);
		}
	}
	printf("TileData:\n");
	for (int i = 0; i < n_tiles; i++) {
		printf("  db ");
		for (int j = 0; j < 15; j++) {
			printf("$%02X, ", tile_data[16 * i + j]);
		}
		printf("$%02X\n", tile_data[16 * i + 15]);
	}
	printf("Map:\n");
	for (uint8_t ty = 0; ty < 32; ty++) {
		printf("  db ");
		for (uint8_t tx = 0; tx < 31; tx++) {
			printf("$%02X, ", tiles[32 * ty + tx].data_idx);
		}
		printf("$%02X\n", tiles[32 * ty + 31].data_idx);
	}
	printf("Attributes:\n");
	for (uint8_t ty = 0; ty < 32; ty++) {
		printf("  db ");
		for (uint8_t tx = 0; tx < 31; tx++) {
			printf("$%02X, ", tiles[32 * ty + tx].palette_idx);
		}
		printf("$%02X\n", tiles[32 * ty + 31].palette_idx);
	}

	printf("Found %d tiles\n", n_tiles);
	free(tiles);
	free(tile_data);
}

void hex_to_palette(uint32_t hex[4], uint8_t palette[8])
{
	for (int c_idx = 0; c_idx < 4; c_idx++) {
		uint16_t colour = hex_to_gb(hex[c_idx]);

		palette[2 * c_idx] = colour & 0xFFu;
		palette[2 * c_idx + 1] = (colour >> 8u) & 0xFFu;
	}
}

uint16_t hex_to_gb(uint32_t hex)
{
	uint8_t r = (hex >> 3u) & 0x1Fu;
	uint8_t g = (hex >> 11u) & 0x1Fu;
	uint8_t b = (hex >> 19u) & 0x1Fu;

	return r | (g << 5u) | (b << 10u);
}

int colour_in_palette(uint32_t hex, uint8_t palette[8])
{
	uint16_t colour = hex_to_gb(hex);
	uint8_t lo = colour & 0xFFu;
	uint8_t hi = colour >> 8u;
	for (int i = 0; i < 4; i++) {
		if (lo == palette[2 * i] && hi == palette[2 * i + 1]) {
			return i;
		}
	}
	return -1;
}

int palette_in_list(uint8_t palette[8], int n_colours, uint8_t list[MAX_PALETTES][8], uint8_t colours_in_palettes[MAX_PALETTES])
{
	for (int i = 0; i < MAX_PALETTES; i++) {
		bool palettes_equal = true;
		for (int j = 0; j < 4 && j < n_colours; j++) {
			bool found = false;
			uint8_t c_in_p = colours_in_palettes[i];
			for (int k = 0; k < 4 && k < c_in_p; k++) {
				if (palette[2 * j] == list[i][2 * k] && palette[2 * j + 1] == list[i][2 * k + 1]) {
					found = true;
				}
			}
			if (!found) {
				if (c_in_p < 4) {
					list[i][2 * c_in_p] = palette[2 * j];
					list[i][2 * c_in_p + 1] = palette[2 * j + 1];
					colours_in_palettes[i]++;
				} else {
					palettes_equal = false;
					break;
				}
			}
		}
		if (palettes_equal) {
			return i;
		}
	}
	return -1;
}

int tile_in_list(uint8_t tile[16], uint8_t list[MAX_TILES * 16], int n_tiles)
{
	for (int i = 0; i < n_tiles; i++) {
		if (tiles_equal(tile, &list[16 * i])) {
			return i;
		}
	}
	memcpy(&list[16 * n_tiles], tile, 16);
	return n_tiles;
}

bool tiles_equal(uint8_t a[16], uint8_t b[16])
{
	for (int i = 0; i < 16; i++) {
		if (a[i] != b[i]) {
			return false;
		}
	}
	return true;
}

#define HEADER_BYTES 8

struct bitmap load_png(const char *filename)
{
	FILE *fp = fopen(filename, "rb");
	uint8_t header[HEADER_BYTES];
	struct bitmap bitmap = {0};
	if (!fp) {
		fprintf(stderr, "Couldn't open %s: %s\n", filename, strerror(errno));
		return bitmap;
	}
	if (fread(header, 1, HEADER_BYTES, fp) == 0) {
		fprintf(stderr, "Failed to read fontmap data: %s\n", filename);
		fclose(fp);
		return bitmap;
	}
	if (png_sig_cmp(header, 0, HEADER_BYTES)) {
		fprintf(stderr, "Not a PNG file: %s\n", filename);
		fclose(fp);
		return bitmap;
	}

	png_structp png_ptr = png_create_read_struct(
			PNG_LIBPNG_VER_STRING,
			NULL, NULL, NULL);
	if (!png_ptr) {
		fprintf(stderr, "Couldn't create PNG read struct.\n");
		fclose(fp);
		return bitmap;
	}

	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr) {
		png_destroy_read_struct(&png_ptr, NULL, NULL);
		fclose(fp);
		fprintf(stderr, "Couldn't create PNG info struct.\n");
		return bitmap;
	}

	if (setjmp(png_jmpbuf(png_ptr)) != 0) {
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		fclose(fp);
		fprintf(stderr, "Couldn't setjmp for libpng.\n");
		return bitmap;
	}

	png_init_io(png_ptr, fp);
	png_set_sig_bytes(png_ptr, HEADER_BYTES);
	png_read_info(png_ptr, info_ptr);

	bitmap.width = png_get_image_width(png_ptr, info_ptr);
	bitmap.height = png_get_image_height(png_ptr, info_ptr);
	uint32_t bit_depth = png_get_bit_depth(png_ptr, info_ptr);
	uint32_t colour_type = png_get_color_type(png_ptr, info_ptr);
	
	bitmap.data = calloc(bitmap.width * bitmap.height, sizeof(*bitmap.data));

	png_bytepp row_pointers = calloc(bitmap.height, sizeof(png_bytep));
	for (uint32_t y = 0; y < bitmap.height; y++) {
		row_pointers[y] = (unsigned char *)&bitmap.data[y * bitmap.width];
	}

	if (bit_depth < 8) {
		png_set_packing(png_ptr);
	}
	if (colour_type == PNG_COLOR_TYPE_RGB) {
		png_set_filler(png_ptr, 0xFFu, PNG_FILLER_AFTER);
	}
	png_read_image(png_ptr, row_pointers);
	png_read_end(png_ptr, NULL);

	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
	free(row_pointers);
	fclose(fp);
	return bitmap;
}
