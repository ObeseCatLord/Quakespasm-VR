#ifndef VR_MDL_SPLIT_H
#define VR_MDL_SPLIT_H

/* Deterministic, allocation-owned splitter for pinned paired VR viewmodels. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define QBJ3_MDL_NAIL_SOURCE_SIZE ((size_t)1250396)
#define QBJ3_MDL_BERSERK_SOURCE_SIZE ((size_t)656804)
#define ENYO_MDL_SMG_SOURCE_SIZE ((size_t)322196)
#define DWELL_MDL_BERSERK_SOURCE_SIZE ((size_t)106284)

typedef enum {
	QBJ3_MDL_WEAPON_NAIL = 0,
	QBJ3_MDL_WEAPON_BERSERK = 1,
	ENYO_MDL_WEAPON_SMG = 2,
	DWELL_MDL_WEAPON_BERSERK = 3,
	VR_MDL_SPLIT_WEAPON_COUNT
} qbj3_mdl_weapon_t;

typedef enum {
	QBJ3_MDL_SIDE_LEFT = 0,
	QBJ3_MDL_SIDE_RIGHT = 1
} qbj3_mdl_side_t;

static size_t QBJ3_MDL_SourceSize (qbj3_mdl_weapon_t weapon)
{
	return weapon == QBJ3_MDL_WEAPON_NAIL ? QBJ3_MDL_NAIL_SOURCE_SIZE :
		weapon == QBJ3_MDL_WEAPON_BERSERK ? QBJ3_MDL_BERSERK_SOURCE_SIZE :
		weapon == ENYO_MDL_WEAPON_SMG ? ENYO_MDL_SMG_SOURCE_SIZE :
		weapon == DWELL_MDL_WEAPON_BERSERK ? DWELL_MDL_BERSERK_SOURCE_SIZE : 0;
}

/* Same reflected CRC-32 calculation used by the engine's asset guard. */
static uint32_t QBJ3_MDL_CRC32 (const unsigned char *data, size_t size)
{
	uint32_t crc = 0xffffffffu;
	int bit;
	while (size--)
	{
		crc ^= *data++;
		for (bit = 0; bit < 8; ++bit)
			crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int)(crc & 1u));
	}
	return ~crc;
}

static uint32_t QBJ3_MDL_ReadLE32 (const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
		((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void QBJ3_MDL_WriteLE32 (unsigned char *p, uint32_t v)
{
	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
	p[2] = (unsigned char)(v >> 16);
	p[3] = (unsigned char)(v >> 24);
}

static float QBJ3_MDL_ReadLEFloat (const unsigned char *p)
{
	uint32_t bits = QBJ3_MDL_ReadLE32 (p);
	float value;
	memcpy (&value, &bits, sizeof(value));
	return value;
}

static unsigned int QBJ3_MDL_Find (unsigned int *parent, unsigned int v)
{
	unsigned int root = v;
	while (parent[root] != root)
		root = parent[root];
	while (parent[v] != v)
	{
		unsigned int next = parent[v];
		parent[v] = root;
		v = next;
	}
	return root;
}

static void QBJ3_MDL_Union (unsigned int *parent, unsigned int a, unsigned int b)
{
	a = QBJ3_MDL_Find (parent, a);
	b = QBJ3_MDL_Find (parent, b);
	if (a != b)
		parent[b] = a;
}

static int QBJ3_MDL_Add (size_t a, size_t b, size_t *out)
{
	if (a > (size_t)-1 - b)
		return 0;
	*out = a + b;
	return 1;
}

static int QBJ3_MDL_Mul (size_t a, size_t b, size_t *out)
{
	if (a && b > (size_t)-1 / a)
		return 0;
	*out = a * b;
	return 1;
}

static int QBJ3_MDL_Range (size_t size, size_t offset, size_t bytes)
{
	return offset <= size && bytes <= size - offset;
}

static int QBJ3_MDL_TriangleDegenerate (const unsigned char *frame,
	unsigned int a, unsigned int b, unsigned int c)
{
	const unsigned char *pa = frame + 28 + (size_t)a * 4;
	const unsigned char *pb = frame + 28 + (size_t)b * 4;
	const unsigned char *pc = frame + 28 + (size_t)c * 4;
	int ax = (int)pb[0] - pa[0], ay = (int)pb[1] - pa[1], az = (int)pb[2] - pa[2];
	int bx = (int)pc[0] - pa[0], by = (int)pc[1] - pa[1], bz = (int)pc[2] - pa[2];
	int64_t x = (int64_t)ay * bz - (int64_t)az * by;
	int64_t y = (int64_t)az * bx - (int64_t)ax * bz;
	int64_t z = (int64_t)ax * by - (int64_t)ay * bx;
	return x == 0 && y == 0 && z == 0;
}

/* Returns 1 and transfers malloc ownership on success, 0 for incompatible
 * data, or -1 for a transient allocation failure. */
static int QBJ3_MDL_Split (const unsigned char *input, size_t input_size,
	qbj3_mdl_weapon_t weapon, qbj3_mdl_side_t side,
	unsigned char **out, size_t *out_size)
{
	unsigned int expected_verts, expected_tris, expected_frames;
	uint32_t expected_input_crc, expected_output_crc;
	size_t expected_input_size, expected_output_size;
	unsigned int numskins, width, height, numverts, numtris, numframes;
	size_t skinbytes, uv_offset, tri_offset, frame_offset, framebytes;
	unsigned int *parent = NULL, *selected = NULL;
	unsigned char *evidence = NULL, *result = NULL;
	int *remap = NULL;
	unsigned int i, f, selected_verts = 0, selected_tris = 0;
	float yscale, yorigin;
	size_t result_size, part, pos;
	int failure = 0;

	if (!out || !out_size)
		return 0;
	*out = NULL;
	*out_size = 0;
	if (!input || (int)side < 0 || side > QBJ3_MDL_SIDE_RIGHT)
		return 0;
	if (weapon == QBJ3_MDL_WEAPON_NAIL)
	{
		expected_input_size = QBJ3_MDL_NAIL_SOURCE_SIZE; expected_input_crc = 0x497d6bceu;
		expected_verts = 1968; expected_tris = 1751; expected_frames = 19;
		if (side == QBJ3_MDL_SIDE_LEFT)
		{
			expected_output_size = 1150156; expected_output_crc = 0xa05a268cu;
		}
		else { expected_output_size = 1149436; expected_output_crc = 0xd872476bu; }
	}
	else if (weapon == QBJ3_MDL_WEAPON_BERSERK)
	{
		expected_input_size = QBJ3_MDL_BERSERK_SOURCE_SIZE; expected_input_crc = 0xc3af3566u;
		expected_verts = 894; expected_tris = 1240; expected_frames = 101;
		if (side == QBJ3_MDL_SIDE_LEFT)
		{
			expected_output_size = 460932; expected_output_crc = 0x1e60e85bu;
		}
		else { expected_output_size = 460932; expected_output_crc = 0x7cf16657u; }
	}
	else if (weapon == ENYO_MDL_WEAPON_SMG)
	{
		expected_input_size = ENYO_MDL_SMG_SOURCE_SIZE; expected_input_crc = 0x14036902u;
		expected_verts = 984; expected_tris = 782; expected_frames = 17;
		expected_output_size = 276580;
		expected_output_crc = side == QBJ3_MDL_SIDE_LEFT ? 0xc71ef421u : 0x7d5cfb61u;
	}
	else if (weapon == DWELL_MDL_WEAPON_BERSERK)
	{
		expected_input_size = DWELL_MDL_BERSERK_SOURCE_SIZE; expected_input_crc = 0x69c2bf5eu;
		expected_verts = 304; expected_tris = 396; expected_frames = 51;
		expected_output_size = 70284;
		expected_output_crc = side == QBJ3_MDL_SIDE_LEFT ? 0x8e5fd44bu : 0xf3c035b6u;
	}
	else
		return 0;
	if (input_size != QBJ3_MDL_SourceSize(weapon) || input_size != expected_input_size ||
		QBJ3_MDL_CRC32(input, input_size) != expected_input_crc ||
		input_size < 84 || QBJ3_MDL_ReadLE32(input) != 0x4f504449u ||
		QBJ3_MDL_ReadLE32(input + 4) != 6)
		return 0;

	numskins = QBJ3_MDL_ReadLE32(input + 48);
	width = QBJ3_MDL_ReadLE32(input + 52);
	height = QBJ3_MDL_ReadLE32(input + 56);
	numverts = QBJ3_MDL_ReadLE32(input + 60);
	numtris = QBJ3_MDL_ReadLE32(input + 64);
	numframes = QBJ3_MDL_ReadLE32(input + 68);
	if (numskins != 1 || numverts != expected_verts || numtris != expected_tris ||
		numframes != expected_frames || !width || !height ||
		!QBJ3_MDL_Mul(width, height, &skinbytes) ||
		!QBJ3_MDL_Add(84, 4 + skinbytes, &uv_offset) ||
		!QBJ3_MDL_Mul(numverts, 12, &part) ||
		!QBJ3_MDL_Add(uv_offset, part, &tri_offset) ||
		!QBJ3_MDL_Mul(numtris, 16, &part) ||
		!QBJ3_MDL_Add(tri_offset, part, &frame_offset) ||
		!QBJ3_MDL_Mul(numverts, 4, &part) ||
		!QBJ3_MDL_Add(28, part, &framebytes) ||
		!QBJ3_MDL_Mul(numframes, framebytes, &part) ||
		!QBJ3_MDL_Add(frame_offset, part, &part) || part != input_size ||
		!QBJ3_MDL_Range(input_size, 84, 4 + skinbytes) ||
		QBJ3_MDL_ReadLE32(input + 84) != 0)
		return 0;
	for (f = 0; f < numframes; ++f)
		if (QBJ3_MDL_ReadLE32(input + frame_offset + (size_t)f * framebytes) != 0)
			return 0;
	for (i = 0; i < numtris; ++i)
	{
		const unsigned char *tri = input + tri_offset + (size_t)i * 16;
		if (QBJ3_MDL_ReadLE32(tri) > 1 || QBJ3_MDL_ReadLE32(tri + 4) >= numverts ||
			QBJ3_MDL_ReadLE32(tri + 8) >= numverts || QBJ3_MDL_ReadLE32(tri + 12) >= numverts)
			return 0;
	}

	parent = (unsigned int *)malloc((size_t)numverts * sizeof(*parent));
	evidence = (unsigned char *)calloc(numverts, sizeof(*evidence));
	selected = (unsigned int *)malloc((size_t)numverts * sizeof(*selected));
	remap = (int *)malloc((size_t)numverts * sizeof(*remap));
	if (!parent || !evidence || !selected || !remap)
	{
		failure = -1;
		goto fail;
	}
	for (i = 0; i < numverts; ++i) { parent[i] = i; remap[i] = -1; }
	for (i = 0; i < numtris; ++i)
	{
		const unsigned char *tri = input + tri_offset + (size_t)i * 16;
		unsigned int a = QBJ3_MDL_ReadLE32(tri + 4), b = QBJ3_MDL_ReadLE32(tri + 8), c = QBJ3_MDL_ReadLE32(tri + 12);
		QBJ3_MDL_Union(parent, a, b); QBJ3_MDL_Union(parent, a, c);
	}
	for (i = 0; i < numverts; ++i)
		parent[i] = QBJ3_MDL_Find(parent, i);
	yscale = QBJ3_MDL_ReadLEFloat(input + 12);
	yorigin = QBJ3_MDL_ReadLEFloat(input + 24);
	if (weapon == DWELL_MDL_WEAPON_BERSERK)
	{
		/* In this pinned source the right hand is 0..151, left 152..303.
		 * Punches cross the midline, so animation-side tests cannot classify
		 * them. Still require every topology component and triangle to stay
		 * inside exactly one partition before copying anything. */
		for (i = 0; i < numverts; ++i)
			evidence[parent[i]] |= i < 152 ? 2 : 1;
		for (i = 0; i < numverts; ++i)
			if (evidence[parent[i]] != 1 && evidence[parent[i]] != 2) goto fail;
	}
	else if (weapon == QBJ3_MDL_WEAPON_NAIL)
	{
		for (f = 0; f < numframes; ++f)
			for (i = 0; i < numtris; ++i)
			{
				const unsigned char *tri = input + tri_offset + (size_t)i * 16;
				unsigned int a = QBJ3_MDL_ReadLE32(tri + 4), b = QBJ3_MDL_ReadLE32(tri + 8), c = QBJ3_MDL_ReadLE32(tri + 12);
				const unsigned char *frame = input + frame_offset + (size_t)f * framebytes;
				unsigned int v[3] = {a, b, c}, j;
				if (QBJ3_MDL_TriangleDegenerate(frame, a, b, c)) continue;
				for (j = 0; j < 3; ++j)
				{
					float y = frame[28 + (size_t)v[j] * 4 + 1] * yscale + yorigin;
					if (y > 0.00001f) evidence[parent[v[j]]] |= 1;
					else if (y < -0.00001f) evidence[parent[v[j]]] |= 2;
				}
			}
		for (i = 0; i < numverts; ++i)
			if (evidence[parent[i]] != 1 && evidence[parent[i]] != 2) goto fail;
	}
	else
	{
		for (i = 0; i < numverts; ++i)
		{
			const unsigned char *v = input + frame_offset + 28 + (size_t)i * 4;
			float y = v[1] * yscale + yorigin;
			if (y > 0.0f) evidence[parent[i]] |= 1;
			else if (y < 0.0f) evidence[parent[i]] |= 2;
			else goto fail;
		}
		for (i = 0; i < numverts; ++i)
			if (evidence[parent[i]] != 1 && evidence[parent[i]] != 2) goto fail;
		for (f = weapon == ENYO_MDL_WEAPON_SMG ? 0 : 81; f < numframes; ++f)
			for (i = 0; i < numverts; ++i)
			{
				const unsigned char *v = input + frame_offset + (size_t)f * framebytes + 28 + (size_t)i * 4;
				float y = v[1] * yscale + yorigin;
				if ((evidence[parent[i]] == 1 && y <= 0.0f) || (evidence[parent[i]] == 2 && y >= 0.0f)) goto fail;
			}
	}
	for (i = 0; i < numverts; ++i)
		if ((evidence[parent[i]] == 1) == (side == QBJ3_MDL_SIDE_LEFT))
		{
			remap[i] = (int)selected_verts;
			selected[selected_verts++] = i;
		}
	for (i = 0; i < numtris; ++i)
	{
		const unsigned char *tri = input + tri_offset + (size_t)i * 16;
		unsigned int a = QBJ3_MDL_ReadLE32(tri + 4), b = QBJ3_MDL_ReadLE32(tri + 8), c = QBJ3_MDL_ReadLE32(tri + 12);
		if (evidence[parent[a]] != evidence[parent[b]] || evidence[parent[a]] != evidence[parent[c]]) goto fail;
		if ((evidence[parent[a]] == 1) == (side == QBJ3_MDL_SIDE_LEFT)) ++selected_tris;
	}
	if (!selected_verts || !selected_tris || !QBJ3_MDL_Mul(selected_verts, 12, &part) ||
		!QBJ3_MDL_Add(uv_offset, part, &result_size) ||
		!QBJ3_MDL_Mul(selected_tris, 16, &part) || !QBJ3_MDL_Add(result_size, part, &result_size) ||
		!QBJ3_MDL_Mul(selected_verts, 4, &part) || !QBJ3_MDL_Add(28, part, &part) ||
		!QBJ3_MDL_Mul(numframes, part, &part) || !QBJ3_MDL_Add(result_size, part, &result_size) ||
		result_size != expected_output_size) goto fail;
	result = (unsigned char *)malloc(result_size);
	if (!result) { failure = -1; goto fail; }
	memcpy(result, input, uv_offset);
	QBJ3_MDL_WriteLE32(result + 60, selected_verts);
	QBJ3_MDL_WriteLE32(result + 64, selected_tris);
	pos = uv_offset;
	for (i = 0; i < selected_verts; ++i, pos += 12)
		memcpy(result + pos, input + uv_offset + (size_t)selected[i] * 12, 12);
	for (i = 0; i < numtris; ++i)
	{
		const unsigned char *tri = input + tri_offset + (size_t)i * 16;
		unsigned int a = QBJ3_MDL_ReadLE32(tri + 4);
		if ((evidence[parent[a]] == 1) != (side == QBJ3_MDL_SIDE_LEFT)) continue;
		memcpy(result + pos, tri, 4);
		QBJ3_MDL_WriteLE32(result + pos + 4, (uint32_t)remap[a]);
		QBJ3_MDL_WriteLE32(result + pos + 8, (uint32_t)remap[QBJ3_MDL_ReadLE32(tri + 8)]);
		QBJ3_MDL_WriteLE32(result + pos + 12, (uint32_t)remap[QBJ3_MDL_ReadLE32(tri + 12)]);
		pos += 16;
	}
	for (f = 0; f < numframes; ++f)
	{
		const unsigned char *frame = input + frame_offset + (size_t)f * framebytes;
		unsigned char *dst = result + pos;
		unsigned int axis;
		memcpy(dst, frame, 28);
		/* Enyo's verified reference preserves the full-pair conservative
		 * bounds. QBJ3 references use recomputed tight per-half bounds. */
		for (axis = 0; weapon != ENYO_MDL_WEAPON_SMG && axis < 3; ++axis)
		{
			unsigned int min = 255, max = 0;
			for (i = 0; i < selected_verts; ++i)
			{
				unsigned int value = frame[28 + (size_t)selected[i] * 4 + axis];
				if (value < min) min = value;
				if (value > max) max = value;
			}
			dst[4 + axis] = (unsigned char)min;
			dst[8 + axis] = (unsigned char)max;
		}
		pos += 28;
		for (i = 0; i < selected_verts; ++i, pos += 4)
			memcpy(result + pos, frame + 28 + (size_t)selected[i] * 4, 4);
	}
	if (pos != result_size || QBJ3_MDL_CRC32(result, result_size) != expected_output_crc) goto fail;
	free(parent); free(evidence); free(selected); free(remap);
	*out = result; *out_size = result_size;
	return 1;
fail:
	free(parent); free(evidence); free(selected); free(remap); free(result);
	return failure;
}

#endif
