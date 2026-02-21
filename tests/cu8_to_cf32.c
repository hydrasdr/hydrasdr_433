/** @file
    CU8-to-CF32 sample converter for decoder regression tests.

    Reads unsigned 8-bit IQ (CU8) recordings and writes float32 IQ (CF32).
    Conversion: float_val = (byte - 127.4f) / 127.4f

    Copyright (C) 2025

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <windows.h>
#else
#include <dirent.h>
#endif

#define CU8_CENTER 127.4f
#define CHUNK_SIZE 8192

static int convert_file(const char *in_path, const char *out_path)
{
	FILE *fin;
	FILE *fout;
	unsigned char inbuf[CHUNK_SIZE];
	float outbuf[CHUNK_SIZE];
	size_t n;

	fin = fopen(in_path, "rb");
	if (!fin) {
		fprintf(stderr, "Error: cannot open input '%s'\n", in_path);
		return 1;
	}

	fout = fopen(out_path, "wb");
	if (!fout) {
		fprintf(stderr, "Error: cannot open output '%s'\n", out_path);
		fclose(fin);
		return 1;
	}

	while ((n = fread(inbuf, 1, CHUNK_SIZE, fin)) > 0) {
		for (size_t i = 0; i < n; i++)
			outbuf[i] = ((float)inbuf[i] - CU8_CENTER) / CU8_CENTER;

		if (fwrite(outbuf, sizeof(float), n, fout) != n) {
			fprintf(stderr, "Error: write failed for '%s'\n", out_path);
			fclose(fin);
			fclose(fout);
			return 1;
		}
	}

	fclose(fin);
	fclose(fout);
	return 0;
}

/* Replace .cu8 extension with .cf32 in a new string */
static char *make_cf32_path(const char *cu8_path)
{
	size_t len = strlen(cu8_path);
	char *out;

	if (len < 4 || strcmp(cu8_path + len - 4, ".cu8") != 0) {
		fprintf(stderr, "Error: '%s' does not end in .cu8\n", cu8_path);
		return NULL;
	}

	/* .cf32 (5 chars) replaces .cu8 (4 chars), +1 for NUL */
	out = malloc(len + 2);
	if (!out) {
		fprintf(stderr, "Error: out of memory\n");
		return NULL;
	}

	snprintf(out, len + 2, "%.*s.cf32", (int)(len - 4), cu8_path);
	return out;
}

static void usage(const char *argv0)
{
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "  %s input.cu8 [output.cf32]\n", argv0);
	fprintf(stderr, "  %s --batch dir/\n", argv0);
	fprintf(stderr, "\nConverts CU8 (uint8 IQ) to CF32 (float32 IQ).\n");
	fprintf(stderr, "If output is omitted, replaces .cu8 with .cf32.\n");
	fprintf(stderr, "Batch mode converts all .cu8 files in directory.\n");
}

#ifdef _WIN32
static int batch_convert(const char *dir)
{
	WIN32_FIND_DATAA fd;
	HANDLE hFind;
	char pattern[MAX_PATH];
	char in_path[MAX_PATH];
	int total = 0, fail = 0;

	snprintf(pattern, sizeof(pattern), "%s\\*.cu8", dir);
	hFind = FindFirstFileA(pattern, &fd);
	if (hFind == INVALID_HANDLE_VALUE) {
		snprintf(pattern, sizeof(pattern), "%s/*.cu8", dir);
		hFind = FindFirstFileA(pattern, &fd);
	}
	if (hFind == INVALID_HANDLE_VALUE) {
		fprintf(stderr, "No .cu8 files found in '%s'\n", dir);
		return 1;
	}

	do {
		snprintf(in_path, sizeof(in_path), "%s/%s", dir, fd.cFileName);
		char *out_path = make_cf32_path(in_path);
		if (!out_path) {
			fail++;
			continue;
		}

		printf("Converting: %s\n", fd.cFileName);
		if (convert_file(in_path, out_path) != 0)
			fail++;
		else
			total++;
		free(out_path);
	} while (FindNextFileA(hFind, &fd));

	FindClose(hFind);
	printf("Converted %d files (%d failures)\n", total, fail);
	return fail > 0 ? 1 : 0;
}
#else
static int batch_convert(const char *dir)
{
	DIR *dp;
	struct dirent *ep;
	char in_path[4096];
	int total = 0, fail = 0;

	dp = opendir(dir);
	if (!dp) {
		fprintf(stderr, "Error: cannot open directory '%s'\n", dir);
		return 1;
	}

	while ((ep = readdir(dp)) != NULL) {
		size_t namelen = strlen(ep->d_name);
		if (namelen < 4 || strcmp(ep->d_name + namelen - 4, ".cu8") != 0)
			continue;

		snprintf(in_path, sizeof(in_path), "%s/%s", dir, ep->d_name);
		char *out_path = make_cf32_path(in_path);
		if (!out_path) {
			fail++;
			continue;
		}

		printf("Converting: %s\n", ep->d_name);
		if (convert_file(in_path, out_path) != 0)
			fail++;
		else
			total++;
		free(out_path);
	}

	closedir(dp);
	printf("Converted %d files (%d failures)\n", total, fail);
	return fail > 0 ? 1 : 0;
}
#endif

int main(int argc, char *argv[])
{
	if (argc < 2) {
		usage(argv[0]);
		return 1;
	}

	/* Batch mode */
	if (strcmp(argv[1], "--batch") == 0) {
		if (argc < 3) {
			fprintf(stderr, "Error: --batch requires a directory argument\n");
			return 1;
		}
		return batch_convert(argv[2]);
	}

	/* Single file mode */
	const char *in_path = argv[1];
	char *out_path;

	if (argc >= 3) {
		out_path = NULL; /* use argv[2] directly */
		printf("Converting: %s -> %s\n", in_path, argv[2]);
		return convert_file(in_path, argv[2]);
	}

	out_path = make_cf32_path(in_path);
	if (!out_path)
		return 1;

	printf("Converting: %s -> %s\n", in_path, out_path);
	int ret = convert_file(in_path, out_path);
	free(out_path);
	return ret;
}
