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
#define QBJ3_MDL_WRENCH_SOURCE_SIZE ((size_t)798364)
#define ENYO_MDL_KATANA_SOURCE_SIZE ((size_t)403284)
#define BONK_MDL_DEFAULT_SOURCE_SIZE ((size_t)1325884)
#define BONK_MDL_ALKALINE_AXE_SOURCE_SIZE ((size_t)1542372)
#define BONK_MDL_SBLADE_SOURCE_SIZE ((size_t)1543404)
#define BONK_MDL_BUSTER_SWORD_SOURCE_SIZE ((size_t)1624380)
#define BONK_MDL_PICKAXE_SOURCE_SIZE ((size_t)1645708)
#define BONK_MDL_KATANA_SOURCE_SIZE ((size_t)1285356)
#define BONK_MDL_COPPER_AXE_SOURCE_SIZE ((size_t)1108748)
#define BONK_MDL_BASEBALL_SOURCE_SIZE ((size_t)1820532)
#define BONK_MDL_MOVING_PAST_IT_SOURCE_SIZE ((size_t)1325556)
#define BONK_MDL_MAILBOX_SOURCE_SIZE ((size_t)1357756)
#define BONK_MDL_HEAVY_ROCKET_SOURCE_SIZE ((size_t)2162924)
#define BONK_MDL_BURGER_SOURCE_SIZE ((size_t)2090844)
#define BONK_MDL_GUITAR_SOURCE_SIZE ((size_t)1451324)
#define BONK_MDL_DWARVEN_SOURCE_SIZE ((size_t)1693132)
#define BONK_MDL_JESTER_MALLET_SOURCE_SIZE ((size_t)1874876)
#define BONK_MDL_ERROR_SOURCE_SIZE ((size_t)2235116)
#define BONK_MDL_SAILOR_SCEPTRE_SOURCE_SIZE ((size_t)1715036)
#define BONK_MDL_FLOYD_SOURCE_SIZE ((size_t)1757156)
#define BONK_MDL_KEBBY_GEARS_SOURCE_SIZE ((size_t)2441980)
#define BONK_MDL_SQUEAKY_SOURCE_SIZE ((size_t)1493556)
#define BONK_MDL_SENTINEL_SOURCE_SIZE ((size_t)2039260)
#define BONK_MDL_PIRATE_SKULL_SOURCE_SIZE ((size_t)2268780)
#define BONK_MDL_STOP_SIGN_SOURCE_SIZE ((size_t)1219548)
#define BONK_MDL_BLOCKY_AXE_SOURCE_SIZE ((size_t)1941884)
#define BONK_MDL_BROWN_BRICK_SOURCE_SIZE ((size_t)1891484)
#define BONK_MDL_MACE_SOURCE_SIZE ((size_t)1499004)

typedef enum {
	QBJ3_MDL_WEAPON_NAIL = 0,
	QBJ3_MDL_WEAPON_BERSERK = 1,
	ENYO_MDL_WEAPON_SMG = 2,
	DWELL_MDL_WEAPON_BERSERK = 3,
	QBJ3_MDL_WEAPON_WRENCH_DOMINANT = 4,
	ENYO_MDL_WEAPON_KATANA_DOMINANT = 5,
	BONK_MDL_WEAPON_DEFAULT_DOMINANT = 6,
	BONK_MDL_WEAPON_DEFAULT_BLOODY_DOMINANT = 7,
	BONK_MDL_WEAPON_DEFAULT_GOLD_DOMINANT = 8,
	BONK_MDL_WEAPON_DEFAULT_GOLD_BLOODY_DOMINANT = 9,
	BONK_MDL_WEAPON_ALKALINE_AXE_DOMINANT = 10,
	BONK_MDL_WEAPON_SBLADE_DOMINANT = 11,
	BONK_MDL_WEAPON_BUSTER_SWORD_DOMINANT = 12,
	BONK_MDL_WEAPON_PICKAXE_DOMINANT = 13,
	BONK_MDL_WEAPON_KATANA_DOMINANT = 14,
	BONK_MDL_WEAPON_COPPER_AXE_DOMINANT = 15,
	BONK_MDL_WEAPON_BASEBALL_DOMINANT = 16,
	BONK_MDL_WEAPON_MOVING_PAST_IT_DOMINANT = 17,
	BONK_MDL_WEAPON_MAILBOX_DOMINANT = 18,
	BONK_MDL_WEAPON_HEAVY_ROCKET_DOMINANT = 19,
	BONK_MDL_WEAPON_BURGER_DOMINANT = 20,
	BONK_MDL_WEAPON_GUITAR_DOMINANT = 21,
	BONK_MDL_WEAPON_DWARVEN_DOMINANT = 22,
	BONK_MDL_WEAPON_JESTER_MALLET_DOMINANT = 23,
	BONK_MDL_WEAPON_ERROR_DOMINANT = 24,
	BONK_MDL_WEAPON_SAILOR_SCEPTRE_DOMINANT = 25,
	BONK_MDL_WEAPON_FLOYD_DOMINANT = 26,
	BONK_MDL_WEAPON_KEBBY_GEARS_DOMINANT = 27,
	BONK_MDL_WEAPON_SQUEAKY_DOMINANT = 28,
	BONK_MDL_WEAPON_SENTINEL_DOMINANT = 29,
	BONK_MDL_WEAPON_PIRATE_SKULL_DOMINANT = 30,
	BONK_MDL_WEAPON_STOP_SIGN_DOMINANT = 31,
	BONK_MDL_WEAPON_BLOCKY_AXE_DOMINANT = 32,
	BONK_MDL_WEAPON_BROWN_BRICK_DOMINANT = 33,
	BONK_MDL_WEAPON_MACE_DOMINANT = 34,
	VR_MDL_SPLIT_WEAPON_COUNT
} qbj3_mdl_weapon_t;

typedef enum {
	QBJ3_MDL_SIDE_LEFT = 0,
	QBJ3_MDL_SIDE_RIGHT = 1,
	QBJ3_MDL_SIDE_DOMINANT = 2
} qbj3_mdl_side_t;

static size_t QBJ3_MDL_SourceSize (qbj3_mdl_weapon_t weapon)
{
	return weapon == QBJ3_MDL_WEAPON_NAIL ? QBJ3_MDL_NAIL_SOURCE_SIZE :
		weapon == QBJ3_MDL_WEAPON_BERSERK ? QBJ3_MDL_BERSERK_SOURCE_SIZE :
		weapon == ENYO_MDL_WEAPON_SMG ? ENYO_MDL_SMG_SOURCE_SIZE :
		weapon == DWELL_MDL_WEAPON_BERSERK ? DWELL_MDL_BERSERK_SOURCE_SIZE :
		weapon == QBJ3_MDL_WEAPON_WRENCH_DOMINANT ? QBJ3_MDL_WRENCH_SOURCE_SIZE :
		weapon == ENYO_MDL_WEAPON_KATANA_DOMINANT ? ENYO_MDL_KATANA_SOURCE_SIZE :
		weapon >= BONK_MDL_WEAPON_DEFAULT_DOMINANT &&
		weapon <= BONK_MDL_WEAPON_DEFAULT_GOLD_BLOODY_DOMINANT ? BONK_MDL_DEFAULT_SOURCE_SIZE :
		weapon == BONK_MDL_WEAPON_ALKALINE_AXE_DOMINANT ? BONK_MDL_ALKALINE_AXE_SOURCE_SIZE :
		weapon == BONK_MDL_WEAPON_SBLADE_DOMINANT ? BONK_MDL_SBLADE_SOURCE_SIZE :
		weapon == BONK_MDL_WEAPON_BUSTER_SWORD_DOMINANT ? BONK_MDL_BUSTER_SWORD_SOURCE_SIZE :
		weapon == BONK_MDL_WEAPON_PICKAXE_DOMINANT ? BONK_MDL_PICKAXE_SOURCE_SIZE :
		weapon == BONK_MDL_WEAPON_KATANA_DOMINANT ? BONK_MDL_KATANA_SOURCE_SIZE :
		weapon == BONK_MDL_WEAPON_COPPER_AXE_DOMINANT ? BONK_MDL_COPPER_AXE_SOURCE_SIZE :
		weapon == BONK_MDL_WEAPON_BASEBALL_DOMINANT ? BONK_MDL_BASEBALL_SOURCE_SIZE :
		weapon == BONK_MDL_WEAPON_MOVING_PAST_IT_DOMINANT ? BONK_MDL_MOVING_PAST_IT_SOURCE_SIZE :
		weapon == BONK_MDL_WEAPON_MAILBOX_DOMINANT ? BONK_MDL_MAILBOX_SOURCE_SIZE :
		weapon == BONK_MDL_WEAPON_HEAVY_ROCKET_DOMINANT ? BONK_MDL_HEAVY_ROCKET_SOURCE_SIZE :
		weapon == BONK_MDL_WEAPON_BURGER_DOMINANT ? BONK_MDL_BURGER_SOURCE_SIZE :
		weapon == BONK_MDL_WEAPON_GUITAR_DOMINANT ? BONK_MDL_GUITAR_SOURCE_SIZE :
		weapon == BONK_MDL_WEAPON_DWARVEN_DOMINANT ? BONK_MDL_DWARVEN_SOURCE_SIZE :
		weapon == BONK_MDL_WEAPON_JESTER_MALLET_DOMINANT ? BONK_MDL_JESTER_MALLET_SOURCE_SIZE :
		weapon == BONK_MDL_WEAPON_ERROR_DOMINANT ? BONK_MDL_ERROR_SOURCE_SIZE :
		weapon == BONK_MDL_WEAPON_SAILOR_SCEPTRE_DOMINANT ? BONK_MDL_SAILOR_SCEPTRE_SOURCE_SIZE :
		weapon == BONK_MDL_WEAPON_FLOYD_DOMINANT ? BONK_MDL_FLOYD_SOURCE_SIZE :
		weapon == BONK_MDL_WEAPON_KEBBY_GEARS_DOMINANT ? BONK_MDL_KEBBY_GEARS_SOURCE_SIZE :
		weapon == BONK_MDL_WEAPON_SQUEAKY_DOMINANT ? BONK_MDL_SQUEAKY_SOURCE_SIZE :
		weapon == BONK_MDL_WEAPON_SENTINEL_DOMINANT ? BONK_MDL_SENTINEL_SOURCE_SIZE :
		weapon == BONK_MDL_WEAPON_PIRATE_SKULL_DOMINANT ? BONK_MDL_PIRATE_SKULL_SOURCE_SIZE :
		weapon == BONK_MDL_WEAPON_STOP_SIGN_DOMINANT ? BONK_MDL_STOP_SIGN_SOURCE_SIZE :
		weapon == BONK_MDL_WEAPON_BLOCKY_AXE_DOMINANT ? BONK_MDL_BLOCKY_AXE_SOURCE_SIZE :
		weapon == BONK_MDL_WEAPON_BROWN_BRICK_DOMINANT ? BONK_MDL_BROWN_BRICK_SOURCE_SIZE :
		weapon == BONK_MDL_WEAPON_MACE_DOMINANT ? BONK_MDL_MACE_SOURCE_SIZE : 0;
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

static int QBJ3_MDL_ComponentListed (const unsigned int *components,
	unsigned int count, unsigned int component)
{
	unsigned int i;
	for (i = 0; i < count; ++i)
		if (components[i] == component)
			return 1;
	return 0;
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
	unsigned int *parent = NULL, *selected = NULL, *component_size = NULL,
		*component_min = NULL;
	unsigned char *evidence = NULL, *result = NULL;
	int *remap = NULL;
	unsigned int i, f, selected_verts = 0, selected_tris = 0, component_count = 0;
	float yscale, yorigin;
	size_t result_size, part, pos;
	int failure = 0;
	int dominant_recipe = 0;
	const unsigned int *support_components = NULL;
	unsigned int support_component_count = 0, expected_components = 0;
	static const unsigned int wrench_support[] = {0, 2, 4, 6, 8, 9, 11, 13, 14, 19, 21};
	static const unsigned int katana_support[] = {4, 5, 10, 11, 12, 13, 14, 15, 16, 17, 26, 27, 28, 29, 39, 40, 50, 51, 52, 53, 54};
	static const unsigned int bonk_support[] = {1, 3, 5, 8, 15, 26, 27, 28, 34};
	static const unsigned int bonk_alkaline_axe_support[] = {1, 3, 9, 11, 14, 25, 26, 27, 32};
	static const unsigned int bonk_sblade_support[] = {1, 3, 5, 13, 16, 26, 27, 28, 30};
	static const unsigned int bonk_buster_sword_support[] = {1, 3, 5, 9, 13, 24, 25, 26, 34};
	static const unsigned int bonk_pickaxe_support[] = {1, 3, 5, 11, 13, 40, 41, 42, 55};
	static const unsigned int bonk_katana_support[] = {1, 3, 5, 8, 10, 24, 25, 26, 30};
	static const unsigned int bonk_copper_axe_support[] = {1, 3, 5, 7, 9, 18, 19, 20, 22};
	static const unsigned int bonk_baseball_support[] = {1, 3, 5, 29, 31, 39, 40, 41, 43};
	static const unsigned int bonk_moving_past_it_support[] = {1, 3, 5, 11, 13, 22, 23, 24, 28};
	static const unsigned int bonk_mailbox_support[] = {1, 3, 5, 7, 10, 22, 23, 24, 27};
	static const unsigned int bonk_heavy_rocket_support[] = {1, 3, 5, 17, 21, 50, 51, 52, 55};
	static const unsigned int bonk_burger_support[] = {1, 3, 5, 11, 13, 43, 44, 45, 47};
	static const unsigned int bonk_guitar_support[] = {1, 3, 5, 12, 15, 23, 24, 25, 29};
	static const unsigned int bonk_dwarven_support[] = {1, 3, 5, 11, 19, 26, 27, 28, 43};
	static const unsigned int bonk_jester_mallet_support[] = {1, 3, 5, 15, 17, 35, 36, 37, 39};
	static const unsigned int bonk_error_support[] = {1, 3, 5, 33, 37, 47, 48, 49, 55};
	static const unsigned int bonk_sailor_sceptre_support[] = {1, 3, 5, 11, 14, 33, 34, 35, 43};
	static const unsigned int bonk_floyd_support[] = {1, 3, 5, 12, 16, 33, 34, 35, 38};
	static const unsigned int bonk_kebby_gears_support[] = {1, 3, 5, 16, 18, 50, 51, 52, 56};
	static const unsigned int bonk_squeaky_support[] = {1, 3, 9, 11, 13, 26, 27, 28, 34};
	static const unsigned int bonk_sentinel_support[] = {1, 3, 5, 20, 32, 45, 46, 47, 56};
	static const unsigned int bonk_pirate_skull_support[] = {1, 3, 5, 16, 19, 42, 43, 44, 67};
	static const unsigned int bonk_stop_sign_support[] = {1, 3, 5, 7, 9, 21, 22, 23, 25};
	static const unsigned int bonk_blocky_axe_support[] = {12, 14, 20, 21, 22, 23, 31, 39};
	static const unsigned int bonk_brown_brick_support[] = {1, 3, 5, 6, 7, 8, 32, 40};
	static const unsigned int bonk_mace_support[] = {1, 3, 5, 12, 14, 26, 27, 28, 30};

	if (!out || !out_size)
		return 0;
	*out = NULL;
	*out_size = 0;
	if (!input || (int)side < 0 || side > QBJ3_MDL_SIDE_DOMINANT)
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
	else if (weapon == QBJ3_MDL_WEAPON_WRENCH_DOMINANT)
	{
		expected_input_size = QBJ3_MDL_WRENCH_SOURCE_SIZE; expected_input_crc = 0x4e2e4720u;
		expected_verts = 872; expected_tris = 868; expected_frames = 71;
		expected_output_size = 702244; expected_output_crc = 0x1bfff189u;
		dominant_recipe = 1; support_components = wrench_support;
		support_component_count = sizeof(wrench_support) / sizeof(wrench_support[0]); expected_components = 52;
	}
	else if (weapon == ENYO_MDL_WEAPON_KATANA_DOMINANT)
	{
		expected_input_size = ENYO_MDL_KATANA_SOURCE_SIZE; expected_input_crc = 0x6e300552u;
		expected_verts = 1021; expected_tris = 1039; expected_frames = 35;
		expected_output_size = 344020; expected_output_crc = 0xa707a071u;
		dominant_recipe = 1; support_components = katana_support;
		support_component_count = sizeof(katana_support) / sizeof(katana_support[0]); expected_components = 66;
	}
	else if (weapon >= BONK_MDL_WEAPON_DEFAULT_DOMINANT &&
		weapon <= BONK_MDL_WEAPON_MACE_DOMINANT)
	{
		static const size_t bonk_input_size[] = {1325884, 1325884, 1325884, 1325884, 1542372, 1543404, 1624380, 1645708, 1285356, 1108748, 1820532, 1325556, 1357756, 2162924, 2090844, 1451324, 1693132, 1874876, 2235116, 1715036, 1757156, 2441980, 1493556, 2039260, 2268780, 1219548, 1941884, 1891484, 1499004};
		static const uint32_t bonk_input_crc[] = {0xa9fc3c21u, 0xbd0cfa9cu, 0xba243d22u, 0x9dd24651u, 0x1453ad70u, 0x74e35eeeu, 0x241a3681u, 0x97ba1bd0u, 0x6cd3eb6du, 0xf0abaa6bu, 0xf82af541u, 0x38f48208u, 0x64cce50bu, 0xfad2bdb5u, 0x1c9c27e5u, 0xe337763fu, 0xa6e80991u, 0x0edec9d7u, 0x69e91a5au, 0x23c63ee9u, 0x0a8d1a29u, 0x31e69a1au, 0xa9974b68u, 0x86bafc3fu, 0x3ae8c931u, 0xf0ba2128u, 0x12a3a64fu, 0xd4674622u, 0xde96a173u};
		static const uint32_t bonk_output_crc[] = {0x6b015a17u, 0xa58d9385u, 0x2729b338u, 0x795c367au, 0x189776eeu, 0x9cb41fe1u, 0xe540f2d5u, 0xe8a90d78u, 0x9cc9287au, 0x4e1a43e1u, 0xa0ff1b47u, 0xf964f136u, 0xe9db2836u, 0xfce0615eu, 0x95ebd5f1u, 0x960e3f31u, 0x9ced4a20u, 0x75410d00u, 0xdacfb47eu, 0xa50ca8a4u, 0x14515f65u, 0x5949fe05u, 0x3aa7e63au, 0xc4586f63u, 0x3e849458u, 0x601873dfu, 0xba39619eu, 0x2eaea24au, 0x413cf48au};
		static const unsigned int bonk_verts[] = {1004, 1004, 1004, 1004, 1209, 1210, 1040, 1310, 966, 798, 1475, 1003, 1036, 1550, 1484, 1124, 1354, 1276, 1874, 1376, 1165, 1820, 1163, 1680, 1902, 904, 1592, 1300, 1168};
		static const unsigned int bonk_tris[] = {1274, 1274, 1274, 1274, 1582, 1582, 1224, 1526, 1192, 990, 1810, 1318, 1202, 1988, 1740, 1374, 1652, 1658, 1986, 1602, 1460, 2014, 1498, 2258, 2284, 1078, 1848, 1148, 1516};
		static const size_t bonk_output_size[] = {1020252, 1020252, 1020252, 1020252, 1236740, 1237772, 1318748, 1340076, 979724, 803116, 1514900, 1019924, 1052124, 1857292, 1785212, 1145692, 1387500, 1569244, 1929484, 1409404, 1451524, 2136348, 1187924, 1733628, 1963148, 913916, 1760468, 1710068, 1193372};
		static const unsigned int bonk_components[] = {65, 65, 65, 65, 74, 75, 72, 87, 65, 56, 92, 62, 77, 92, 105, 77, 84, 73, 90, 104, 73, 121, 68, 100, 128, 66, 149, 128, 77};
		static const unsigned int *const bonk_supports[] = {bonk_support, bonk_support, bonk_support, bonk_support, bonk_alkaline_axe_support, bonk_sblade_support, bonk_buster_sword_support, bonk_pickaxe_support, bonk_katana_support, bonk_copper_axe_support, bonk_baseball_support, bonk_moving_past_it_support, bonk_mailbox_support, bonk_heavy_rocket_support, bonk_burger_support, bonk_guitar_support, bonk_dwarven_support, bonk_jester_mallet_support, bonk_error_support, bonk_sailor_sceptre_support, bonk_floyd_support, bonk_kebby_gears_support, bonk_squeaky_support, bonk_sentinel_support, bonk_pirate_skull_support, bonk_stop_sign_support, bonk_blocky_axe_support, bonk_brown_brick_support, bonk_mace_support};
		static const unsigned int bonk_support_counts[] = {9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 8, 8, 9};
		unsigned int variant = (unsigned int)weapon - BONK_MDL_WEAPON_DEFAULT_DOMINANT;
		expected_input_size = bonk_input_size[variant]; expected_input_crc = bonk_input_crc[variant];
		expected_verts = bonk_verts[variant]; expected_tris = bonk_tris[variant]; expected_frames = 255;
		expected_output_size = bonk_output_size[variant]; expected_output_crc = bonk_output_crc[variant];
		dominant_recipe = 1; support_components = bonk_supports[variant];
		support_component_count = bonk_support_counts[variant]; expected_components = bonk_components[variant];
	}
	else
		return 0;
	if ((dominant_recipe && side != QBJ3_MDL_SIDE_DOMINANT) ||
		(!dominant_recipe && side == QBJ3_MDL_SIDE_DOMINANT))
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
		if (dominant_recipe)
		{
			for (f = 0; f < numframes; ++f)
			{
				const unsigned char *frame = input + frame_offset + (size_t)f * framebytes;
				if (!QBJ3_MDL_TriangleDegenerate(frame, a, b, c))
				{
					QBJ3_MDL_Union(parent, a, b); QBJ3_MDL_Union(parent, a, c);
					break;
				}
			}
		}
		else
		{
			QBJ3_MDL_Union(parent, a, b); QBJ3_MDL_Union(parent, a, c);
		}
	}
	for (i = 0; i < numverts; ++i)
		parent[i] = QBJ3_MDL_Find(parent, i);
	if (dominant_recipe)
	{
		component_size = (unsigned int *)calloc(numverts, sizeof(*component_size));
		component_min = (unsigned int *)malloc((size_t)numverts * sizeof(*component_min));
		if (!component_size || !component_min) { failure = -1; goto fail; }
		for (i = 0; i < numverts; ++i) component_min[i] = numverts;
		for (i = 0; i < numverts; ++i)
		{
			++component_size[parent[i]];
			if (i < component_min[parent[i]]) component_min[parent[i]] = i;
		}
		for (i = 0; i < numverts; ++i)
			if (component_size[i]) ++component_count;
		if (component_count != expected_components) goto fail;
		for (i = 0; i < numverts; ++i)
		{
			unsigned int rank = 0, root = parent[i], other;
			for (other = 0; other < numverts; ++other)
				if (component_size[other] &&
					(component_size[other] > component_size[root] ||
					 (component_size[other] == component_size[root] && component_min[other] < component_min[root])))
					++rank;
			if (!QBJ3_MDL_ComponentListed(support_components, support_component_count, rank))
			{
				remap[i] = (int)selected_verts;
				selected[selected_verts++] = i;
			}
		}
	}
	yscale = QBJ3_MDL_ReadLEFloat(input + 12);
	yorigin = QBJ3_MDL_ReadLEFloat(input + 24);
	if (dominant_recipe)
	{
		/* Every source triangle must wholly remain or wholly be removed. */
	}
	else if (weapon == DWELL_MDL_WEAPON_BERSERK)
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
	if (!dominant_recipe)
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
		if (dominant_recipe)
		{
			int keep_a = remap[a] >= 0, keep_b = remap[b] >= 0, keep_c = remap[c] >= 0;
			if (keep_a != keep_b || keep_a != keep_c) goto fail;
			if (keep_a) ++selected_tris;
		}
		else
		{
			if (evidence[parent[a]] != evidence[parent[b]] || evidence[parent[a]] != evidence[parent[c]]) goto fail;
			if ((evidence[parent[a]] == 1) == (side == QBJ3_MDL_SIDE_LEFT)) ++selected_tris;
		}
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
		if (dominant_recipe ? remap[a] < 0 :
			(evidence[parent[a]] == 1) != (side == QBJ3_MDL_SIDE_LEFT)) continue;
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
	free(component_size); free(component_min);
	*out = result; *out_size = result_size;
	return 1;
fail:
	free(parent); free(evidence); free(selected); free(remap);
	free(component_size); free(component_min); free(result);
	return failure;
}

#endif
