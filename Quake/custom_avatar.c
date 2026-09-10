/* Immutable, local-only player avatar package registry. */
#include "custom_avatar.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define CA_MAX_CFG 8192u
#define CA_MAX_MESH (8u * 1024u * 1024u)

static custom_avatar_t ca_avatars[CUSTOM_AVATAR_MAX_PACKAGES];
static qboolean ca_failed[CUSTOM_AVATAR_MAX_PACKAGES];
static int ca_count;
static qboolean ca_initialized;

static const char *const ca_files[CUSTOM_AVATAR_FILE_COUNT] = {"avatar.cfg", "model.md5mesh",
															   "skin.tga", "skin_glow.tga"};
static const char *const ca_bone_names[19] = {
	"Hip",		  "Spine1", "Spine2",	  "Neck",		"Head",		  "Shoulder_L", "UpperArm_L",
	"LowerArm_L", "Hand_L", "Shoulder_R", "UpperArm_R", "LowerArm_R", "Hand_R",		"UpperLeg_L",
	"LowerLeg_L", "Foot_L", "UpperLeg_R", "LowerLeg_R", "Foot_R"};

typedef struct ca_sha256_s
{
	uint32_t h[8];
	uint64_t bits;
	byte block[64];
	size_t used;
} ca_sha256_t;

static uint32_t CA_Ror(uint32_t v, int n)
{
	return (v >> n) | (v << (32 - n));
}
static void CA_SHA256Block(ca_sha256_t *s, const byte *p)
{
	static const uint32_t k[64] = {
		0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
		0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
		0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
		0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
		0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
		0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
		0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
		0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
		0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
		0xc67178f2};
	uint32_t w[64], a, b, c, d, e, f, g, h, t1, t2;
	int i;
	for (i = 0; i < 16; ++i)
		w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
			   ((uint32_t)p[i * 4 + 2] << 8) | p[i * 4 + 3];
	for (; i < 64; ++i)
	{
		uint32_t x = w[i - 15], y = w[i - 2];
		w[i] = w[i - 16] + (CA_Ror(x, 7) ^ CA_Ror(x, 18) ^ (x >> 3)) + w[i - 7] +
			   (CA_Ror(y, 17) ^ CA_Ror(y, 19) ^ (y >> 10));
	}
	a = s->h[0];
	b = s->h[1];
	c = s->h[2];
	d = s->h[3];
	e = s->h[4];
	f = s->h[5];
	g = s->h[6];
	h = s->h[7];
	for (i = 0; i < 64; ++i)
	{
		t1 = h + (CA_Ror(e, 6) ^ CA_Ror(e, 11) ^ CA_Ror(e, 25)) + ((e & f) ^ ((~e) & g)) + k[i] +
			 w[i];
		t2 = (CA_Ror(a, 2) ^ CA_Ror(a, 13) ^ CA_Ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
		h = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}
	s->h[0] += a;
	s->h[1] += b;
	s->h[2] += c;
	s->h[3] += d;
	s->h[4] += e;
	s->h[5] += f;
	s->h[6] += g;
	s->h[7] += h;
}
static void CA_SHA256Init(ca_sha256_t *s)
{
	static const uint32_t v[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
								  0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
	memcpy(s->h, v, sizeof(v));
	s->bits = 0;
	s->used = 0;
}
static void CA_SHA256Update(ca_sha256_t *s, const void *data, size_t size)
{
	const byte *p = data;
	s->bits += (uint64_t)size * 8;
	while (size)
	{
		size_t n = q_min(size, 64 - s->used);
		memcpy(s->block + s->used, p, n);
		s->used += n;
		p += n;
		size -= n;
		if (s->used == 64)
		{
			CA_SHA256Block(s, s->block);
			s->used = 0;
		}
	}
}
static void CA_SHA256Final(ca_sha256_t *s, byte out[32])
{
	int i;
	s->block[s->used++] = 0x80;
	if (s->used > 56)
	{
		while (s->used < 64)
			s->block[s->used++] = 0;
		CA_SHA256Block(s, s->block);
		s->used = 0;
	}
	while (s->used < 56)
		s->block[s->used++] = 0;
	for (i = 7; i >= 0; --i)
		s->block[s->used++] = (byte)(s->bits >> (i * 8));
	CA_SHA256Block(s, s->block);
	for (i = 0; i < 8; ++i)
	{
		out[i * 4] = (byte)(s->h[i] >> 24);
		out[i * 4 + 1] = (byte)(s->h[i] >> 16);
		out[i * 4 + 2] = (byte)(s->h[i] >> 8);
		out[i * 4 + 3] = (byte)s->h[i];
	}
}

static int CA_ValidKey(const char *key)
{
	int i;
	if (!key || !key[0])
		return 0;
	for (i = 0; key[i]; ++i)
	{
		unsigned char c = (unsigned char)key[i];
		if (i >= 31 || !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
			return 0;
	}
	return PlayerAvatar_ValidCustomKey(key);
}

#ifdef _WIN32
static int CA_Wide(const char *in, wchar_t *out, size_t count)
{
	return in && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, in, -1, out, (int)count) != 0;
}
static int CA_SafeDirectory(const char *path)
{
	wchar_t w[MAX_OSPATH];
	DWORD a;
	return CA_Wide(path, w, Q_COUNTOF(w)) &&
		   (a = GetFileAttributesW(w)) != INVALID_FILE_ATTRIBUTES &&
		   (a & FILE_ATTRIBUTE_DIRECTORY) && !(a & FILE_ATTRIBUTE_REPARSE_POINT);
}
static int CA_SafeFile(const char *path)
{
	wchar_t w[MAX_OSPATH];
	DWORD a;
	return CA_Wide(path, w, Q_COUNTOF(w)) &&
		   (a = GetFileAttributesW(w)) != INVALID_FILE_ATTRIBUTES &&
		   !(a & FILE_ATTRIBUTE_DIRECTORY) && !(a & FILE_ATTRIBUTE_REPARSE_POINT);
}
static int CA_PathExists(const char *path)
{
	wchar_t w[MAX_OSPATH];
	return CA_Wide(path, w, Q_COUNTOF(w)) && GetFileAttributesW(w) != INVALID_FILE_ATTRIBUTES;
}
#else
static int CA_SafeDirectory(const char *path)
{
	struct stat s;
	return lstat(path, &s) == 0 && S_ISDIR(s.st_mode) && !S_ISLNK(s.st_mode);
}
static int CA_SafeFile(const char *path)
{
	struct stat s;
	return lstat(path, &s) == 0 && S_ISREG(s.st_mode) && !S_ISLNK(s.st_mode);
}
static int CA_PathExists(const char *path)
{
	struct stat s;
	return lstat(path, &s) == 0;
}
#endif
static int CA_Path(char *out, size_t size, const char *a, const char *b)
{
	int n = q_snprintf(out, size, "%s/%s", a, b);
	return n >= 0 && n < (int)size;
}
static void CA_FreeData(custom_avatar_data_t *data)
{
	int i;
	if (!data)
		return;
	for (i = 0; i < CUSTOM_AVATAR_FILE_COUNT; ++i)
	{
		free(data->bytes[i]);
		data->bytes[i] = NULL;
		data->sizes[i] = 0;
	}
}

static int CA_ReadFile(const char *path, size_t max, byte **out, size_t *outsize)
{
	byte *p;
	*out = NULL;
	*outsize = 0;
	if (!CA_SafeFile(path))
		return 0;
#ifdef _WIN32
	{
		wchar_t wpath[MAX_OSPATH];
		HANDLE handle;
		FILE_ATTRIBUTE_TAG_INFO tag;
		LARGE_INTEGER size;
		DWORD got;
		if (!CA_Wide(path, wpath, Q_COUNTOF(wpath)))
			return 0;
		handle = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
							 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
		if (handle == INVALID_HANDLE_VALUE ||
			!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag, sizeof(tag)) ||
			(tag.FileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
			!GetFileSizeEx(handle, &size) || size.QuadPart < 0 || (uint64_t)size.QuadPart > max)
		{
			if (handle != INVALID_HANDLE_VALUE)
				CloseHandle(handle);
			return 0;
		}
		p = (byte *)malloc((size_t)size.QuadPart + 1);
		if (!p)
		{
			CloseHandle(handle);
			return 0;
		}
		{
			qboolean ok =
				!size.QuadPart || (ReadFile(handle, p, (DWORD)size.QuadPart, &got, NULL) &&
								   got == (DWORD)size.QuadPart);
			if (!CloseHandle(handle))
				ok = false;
			if (!ok)
			{
				free(p);
				return 0;
			}
		}
		p[size.QuadPart] = 0;
		*out = p;
		*outsize = (size_t)size.QuadPart;
		return 1;
	}
#else
	{
		FILE *f;
		long size;
		{
			struct stat status;
			int fd = open(path, O_RDONLY | O_NOFOLLOW);
			if (fd < 0 || fstat(fd, &status) || !S_ISREG(status.st_mode) || !(f = fdopen(fd, "rb")))
			{
				if (fd >= 0)
					close(fd);
				return 0;
			}
		}
		if (fseek(f, 0, SEEK_END) || (size = ftell(f)) < 0 || (uint64_t)size > max ||
			fseek(f, 0, SEEK_SET))
		{
			fclose(f);
			return 0;
		}
		p = (byte *)malloc((size_t)size + 1);
		if (!p)
		{
			fclose(f);
			return 0;
		}
		{
			qboolean ok = !size || fread(p, 1, (size_t)size, f) == (size_t)size;
			if (fclose(f))
				ok = false;
			if (!ok)
			{
				free(p);
				return 0;
			}
		}
		p[size] = 0;
		*out = p;
		*outsize = (size_t)size;
		return 1;
	}
#endif
}
static int CA_ValidateTGA(const byte *p, size_t n, size_t *decoded)
{
	uint32_t w, h, bpp, offset, need;
	if (n < 18 || p[1] || p[2] != 2 || p[3] || p[4] || p[5] || p[6] || p[7] || p[8] || p[9] ||
		p[10] || p[11])
		return 0;
	w = (uint32_t)p[12] | ((uint32_t)p[13] << 8);
	h = (uint32_t)p[14] | ((uint32_t)p[15] << 8);
	bpp = p[16];
	if (!w || !h || w > CUSTOM_AVATAR_MAX_IMAGE_DIMENSION ||
		h > CUSTOM_AVATAR_MAX_IMAGE_DIMENSION || (bpp != 24 && bpp != 32))
		return 0;
	if ((p[17] & ~0x28) || (bpp == 24 && (p[17] & 15)) || (bpp == 32 && (p[17] & 15) != 8))
		return 0;
	offset = 18u + p[0];
	need = w * h * (bpp / 8u);
	if (offset > n || need > n - offset)
		return 0;
	/* Raw TGA exporters may append the standard v2 footer/extension area. */
	if (offset + need != n && (n - offset - need < 26 || n - offset - need > 4096 ||
							   memcmp(p + n - 18, "TRUEVISION-XFILE.\0", 18)))
		return 0;
	*decoded = (size_t)w * h * 4u;
	return 1;
}
static int CA_ByteText(const byte *p, size_t n)
{
	size_t i;
	for (i = 0; i < n; ++i)
		if (!p[i])
			return 0;
	return 1;
}
static int CA_Token(const byte *p, size_t n, size_t *at, char *out, size_t cap, int *quoted)
{
	size_t i = *at, o = 0;
	while (i < n && (p[i] == ' ' || p[i] == '\t' || p[i] == '\r' || p[i] == '\n'))
		++i;
	if (i == n)
		return 0;
	*quoted = p[i] == '"';
	if (*quoted)
		++i;
	while (i < n && ((*quoted && p[i] != '"') ||
					 (!*quoted && p[i] != ' ' && p[i] != '\t' && p[i] != '\r' && p[i] != '\n')))
	{
		if (p[i] < 0x20 || p[i] > 0x7e || p[i] == '"' || o + 1 >= cap)
			return -1;
		out[o++] = (char)p[i++];
	}
	if (*quoted)
	{
		if (i == n || p[i++] != '"' ||
			(i < n && p[i] != ' ' && p[i] != '\t' && p[i] != '\r' && p[i] != '\n'))
			return -1;
	}
	if (!o)
		return -1;
	out[o] = 0;
	*at = i;
	return 1;
}
static int CA_BoneSemantic(const char *name)
{
	int i;
	for (i = 0; i < 19; ++i)
		if (!strcmp(name, ca_bone_names[i]))
			return i;
	return -1;
}
static int CA_Scale(const char *s, float *out)
{
	double value = 0, fraction = .1;
	int dot = 0, digits = 0;
	if (!s || !*s)
		return 0;
	for (; *s; ++s)
	{
		if (*s == '.' && !dot)
		{
			dot = 1;
			continue;
		}
		if (*s < '0' || *s > '9')
			return 0;
		digits = 1;
		if (dot)
		{
			value += (*s - '0') * fraction;
			fraction *= .1;
		}
		else
			value = value * 10 + (*s - '0');
	}
	if (!digits || !isfinite(value) || value < .25 || value > 4.)
		return 0;
	*out = (float)value;
	return 1;
}
static int CA_ParseManifest(custom_avatar_t *a, const byte *p, size_t n)
{
	size_t at = 0;
	char word[64], value[64];
	int quoted, seen_name = 0, seen_scale = 0, seen_bones[19] = {0}, semantic, r, i;
	if (!CA_ByteText(p, n) || CA_Token(p, n, &at, word, sizeof(word), &quoted) != 1 || quoted ||
		strcmp(word, "version") || CA_Token(p, n, &at, value, sizeof(value), &quoted) != 1 ||
		quoted || strcmp(value, "1"))
		return 0;
	for (i = 0; i < 19; ++i)
		q_strlcpy(a->bones[i], ca_bone_names[i], sizeof(a->bones[i]));
	a->profile.display_scale = 1.0f;
	while ((r = CA_Token(p, n, &at, word, sizeof(word), &quoted)) > 0)
	{
		if (quoted)
			return 0;
		if (!strcmp(word, "name"))
		{
			if (seen_name++ || CA_Token(p, n, &at, value, sizeof(value), &quoted) != 1 || !quoted ||
				strlen(value) > 31)
				return 0;
			for (i = 0; value[i]; ++i)
				if ((unsigned char)value[i] < 0x20 || (unsigned char)value[i] > 0x7e)
					return 0;
			q_strlcpy(a->name, value, sizeof(a->name));
		}
		else if (!strcmp(word, "scale"))
		{
			if (seen_scale++ || CA_Token(p, n, &at, value, sizeof(value), &quoted) != 1 || quoted ||
				!CA_Scale(value, &a->profile.display_scale))
				return 0;
		}
		else if (!strcmp(word, "bone"))
		{
			if (CA_Token(p, n, &at, value, sizeof(value), &quoted) != 1 || quoted ||
				(semantic = CA_BoneSemantic(value)) < 0 || seen_bones[semantic]++ ||
				CA_Token(p, n, &at, value, sizeof(value), &quoted) != 1 || quoted || !value[0] ||
				strlen(value) > 31)
				return 0;
			q_strlcpy(a->bones[semantic], value, sizeof(a->bones[semantic]));
		}
		else
			return 0;
	}
	return r == 0 && seen_name;
}
static void CA_Hash(const custom_avatar_data_t *d, char out[65])
{
	ca_sha256_t s;
	byte digest[32];
	int i, j;
	CA_SHA256Init(&s);
	CA_SHA256Update(&s, "custom-avatar-v1", 16);
	for (i = 0; i < CUSTOM_AVATAR_FILE_COUNT; ++i)
	{
		uint64_t n = d->bytes[i] ? (uint64_t)d->sizes[i] : UINT64_MAX;
		CA_SHA256Update(&s, ca_files[i], strlen(ca_files[i]) + 1);
		for (j = 7; j >= 0; --j)
		{
			byte b = (byte)(n >> (j * 8));
			CA_SHA256Update(&s, &b, 1);
		}
		if (d->bytes[i])
			CA_SHA256Update(&s, d->bytes[i], d->sizes[i]);
	}
	CA_SHA256Final(&s, digest);
	for (i = 0; i < 32; ++i)
		q_snprintf(out + i * 2, 3, "%02x", digest[i]);
}
static int CA_ReadPackage(const char *directory, custom_avatar_data_t *d)
{
	char path[MAX_OSPATH];
	size_t skin, glow = 0;
	int i;
	memset(d, 0, sizeof(*d));
	if (!CA_SafeDirectory(directory))
		return 0;
	for (i = 0; i < CUSTOM_AVATAR_FILE_COUNT; ++i)
	{
		size_t max = i == CUSTOM_AVATAR_MANIFEST ? CA_MAX_CFG
					 : i == CUSTOM_AVATAR_MESH ? CA_MAX_MESH
											   : CUSTOM_AVATAR_MAX_IMAGE_BYTES + 18u + 255u + 4096u;
		if (!CA_Path(path, sizeof(path), directory, ca_files[i]))
			goto fail;
		if (i == CUSTOM_AVATAR_GLOW && !CA_SafeFile(path))
		{
			if (!CA_PathExists(path))
				continue;
			goto fail;
		}
		if (!CA_ReadFile(path, max, d->bytes + i, d->sizes + i))
			goto fail;
	}
	if (!CA_ByteText(d->bytes[CUSTOM_AVATAR_MANIFEST], d->sizes[CUSTOM_AVATAR_MANIFEST]) ||
		!CA_ByteText(d->bytes[CUSTOM_AVATAR_MESH], d->sizes[CUSTOM_AVATAR_MESH]) ||
		!CA_ValidateTGA(d->bytes[CUSTOM_AVATAR_SKIN], d->sizes[CUSTOM_AVATAR_SKIN], &skin) ||
		(d->bytes[CUSTOM_AVATAR_GLOW] &&
		 !CA_ValidateTGA(d->bytes[CUSTOM_AVATAR_GLOW], d->sizes[CUSTOM_AVATAR_GLOW], &glow)) ||
		skin + glow > CUSTOM_AVATAR_MAX_IMAGE_BYTES)
		goto fail;
	return 1;
fail:
	CA_FreeData(d);
	return 0;
}
static void CA_Profile(custom_avatar_t *a)
{
	float scale = a->profile.display_scale;
	int i;
	memset(&a->profile, 0, sizeof(a->profile));
	a->profile.id = (player_avatar_id_t)a->id;
	a->profile.key = a->key;
	a->profile.model_path = a->model_name;
	a->profile.family = R_AVATAR_FAMILY_HUMANOID;
	a->profile.capabilities = R_AVATAR_CAP_HEAD | R_AVATAR_CAP_ARMS | R_AVATAR_CAP_LEGS |
							  R_AVATAR_CAP_RETARGET | R_AVATAR_CAP_STANDARD_WEAPON;
	a->profile.display_scale = scale;
	a->profile.equipment_policy = R_AVATAR_EQUIPMENT_ATTACH_HAND;
	a->profile.basis_policy = R_AVATAR_BASIS_HUMANOID;
	a->profile.arm_pole_outward = 1.0f;
	a->profile.arm_pole_back = .35f;
	for (i = 0; i < 19; ++i)
		a->profile.joint[i].name = a->bones[i];
}
static int CA_Compare(const void *x, const void *y)
{
	return strcmp(((const custom_avatar_t *)x)->key, ((const custom_avatar_t *)y)->key);
}
static int CA_IndexKey(const char *key)
{
	int i;
	for (i = 0; i < ca_count; ++i)
		if (!strcmp(ca_avatars[i].key, key))
			return i;
	return -1;
}
static void CA_Consider(const char *root, const char *key)
{
	char base[MAX_OSPATH], directory[MAX_OSPATH];
	custom_avatar_t a;
	custom_avatar_data_t d;
	int old;
	if (!CA_ValidKey(key) || !CA_Path(base, sizeof(base), root, "player_models") ||
		!CA_SafeDirectory(base) || !CA_Path(directory, sizeof(directory), base, key) ||
		!CA_SafeDirectory(directory))
		return;
	old = CA_IndexKey(key);
	if (old >= 0)
	{
		ca_avatars[old] = ca_avatars[--ca_count];
		ca_failed[ca_count] = false;
	}
	if (ca_count >= CUSTOM_AVATAR_MAX_PACKAGES || !CA_ReadPackage(directory, &d))
		return;
	memset(&a, 0, sizeof(a));
	q_strlcpy(a.key, key, sizeof(a.key));
	q_strlcpy(a.directory, directory, sizeof(a.directory));
	if (!CA_ParseManifest(&a, d.bytes[CUSTOM_AVATAR_MANIFEST], d.sizes[CUSTOM_AVATAR_MANIFEST]))
	{
		CA_FreeData(&d);
		return;
	}
	CA_Hash(&d, a.digest);
	ca_avatars[ca_count++] = a;
	CA_FreeData(&d);
}
static void CA_ScanRoot(const char *root)
{
	char base[MAX_OSPATH];
	if (!root || !CA_Path(base, sizeof(base), root, "player_models") || !CA_SafeDirectory(base))
		return;
#ifdef _WIN32
	{
		wchar_t pattern[MAX_OSPATH];
		WIN32_FIND_DATAW e;
		HANDLE h;
		char key[32];
		if (!CA_Wide(base, pattern, Q_COUNTOF(pattern)) ||
			wcslen(pattern) + 3 >= Q_COUNTOF(pattern))
			return;
		wcscat(pattern, L"\\*");
		h = FindFirstFileW(pattern, &e);
		if (h == INVALID_HANDLE_VALUE)
			return;
		do
		{
			if ((e.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
				!(e.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
				WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, e.cFileName, -1, key,
									sizeof(key), NULL, NULL))
				CA_Consider(root, key);
		} while (FindNextFileW(h, &e));
		FindClose(h);
	}
#else
	{
		DIR *dir = opendir(base);
		struct dirent *e;
		if (!dir)
			return;
		while ((e = readdir(dir)) != NULL)
			CA_Consider(root, e->d_name);
		closedir(dir);
	}
#endif
}

static void CustomAvatar_List_f(void)
{
	int i;
	Con_Printf("Player models: select in Multiplayer > Setup, or cl_avatar <key>\n");
	for (i = 0; i < PLAYER_AVATAR_COUNT; ++i)
		Con_Printf("  %-16s %s (built in)\n", PlayerAvatar_KeyForId(i),
				   PlayerAvatar_DisplayNameForId(i));
	for (i = 0; i < ca_count; ++i)
		Con_Printf("  %-16s %s (%s)\n    %s\n", ca_avatars[i].key, ca_avatars[i].name,
				   ca_failed[i] ? "rejected this session" : "installed", ca_avatars[i].directory);
	Con_Printf(
		"%d custom packages. Install player_models/<key>/ beside id1 or in %s; restart to scan.\n",
		ca_count, COM_GetWriteRoot());
	Con_Printf(
		"Avatars require official rerelease player data; QBJ3/Enyo use their normal players.\n");
}

void CustomAvatar_Init(void)
{
	const char *roots[3];
	int count = 0, i;
	if (ca_initialized)
		return;
	ca_initialized = true;
	if (isDedicated)
		return;
	Cmd_AddCommand("player_models", CustomAvatar_List_f);
	if (host_parms && host_parms->basedir)
		roots[count++] = host_parms->basedir;
	if (com_basedir[0])
		roots[count++] = com_basedir;
	roots[count++] = COM_GetWriteRoot();
	for (i = 0; i < count; ++i)
	{
		int j, duplicate = 0;
		/* Keep the highest-priority occurrence when roots coincide. */
		for (j = i + 1; j < count; ++j)
			if (
#ifdef _WIN32
				!q_strcasecmp(roots[i], roots[j])
#else
				!strcmp(roots[i], roots[j])
#endif
			)
				duplicate = 1;
		if (!duplicate)
			CA_ScanRoot(roots[i]);
	}
	qsort(ca_avatars, ca_count, sizeof(ca_avatars[0]), CA_Compare);
	for (i = 0; i < ca_count; ++i)
	{
		ca_avatars[i].id = PLAYER_AVATAR_COUNT + i;
		q_snprintf(ca_avatars[i].model_name, sizeof(ca_avatars[i].model_name),
				   "progs/@custom_avatar_%d.md5mesh", ca_avatars[i].id);
		CA_Profile(&ca_avatars[i]);
	}
}
int CustomAvatar_TotalCount(void)
{
	return PLAYER_AVATAR_COUNT + ca_count;
}
int CustomAvatar_IdForKey(const char *key)
{
	int id, i;
	if (!key)
		return -1;
	id = PlayerAvatar_IdForKey(key);
	if (id >= 0)
		return id;
	for (i = 0; i < ca_count; ++i)
		if (!strcmp(key, ca_avatars[i].key))
			return ca_avatars[i].id;
	return -1;
}
const char *CustomAvatar_KeyForId(int id)
{
	return id < PLAYER_AVATAR_COUNT ? PlayerAvatar_KeyForId(id)
									: (CustomAvatar_Get(id) ? CustomAvatar_Get(id)->key : NULL);
}
const char *CustomAvatar_DisplayNameForId(int id)
{
	return id < PLAYER_AVATAR_COUNT ? PlayerAvatar_DisplayNameForId(id)
									: (CustomAvatar_Get(id) ? CustomAvatar_Get(id)->name : NULL);
}
const custom_avatar_t *CustomAvatar_Get(int id)
{
	int i;
	if (id < PLAYER_AVATAR_COUNT)
		return NULL;
	i = id - PLAYER_AVATAR_COUNT;
	return i < ca_count ? ca_avatars + i : NULL;
}
int CustomAvatar_Resolve(const char *key, const char *digest)
{
	int i;
	if (!key || !digest)
		return -1;
	for (i = 0; i < ca_count; ++i)
		if (!strcmp(key, ca_avatars[i].key) && !strcmp(digest, ca_avatars[i].digest))
			return ca_avatars[i].id;
	return -1;
}
int CustomAvatar_IdForModelName(const char *name)
{
	int i;
	if (!name)
		return -1;
	for (i = 0; i < ca_count; ++i)
		if (!strcmp(name, ca_avatars[i].model_name))
			return ca_avatars[i].id;
	return -1;
}
qboolean CustomAvatar_ReadData(int id, custom_avatar_data_t *data)
{
	const custom_avatar_t *a = CustomAvatar_Get(id);
	char digest[65];
	if (!data)
		return false;
	memset(data, 0, sizeof(*data));
	if (!a || ca_failed[id - PLAYER_AVATAR_COUNT] || !CA_ReadPackage(a->directory, data))
		return false;
	CA_Hash(data, digest);
	if (strcmp(digest, a->digest))
	{
		CA_FreeData(data);
		return false;
	}
	return true;
}
void CustomAvatar_FreeData(custom_avatar_data_t *data)
{
	CA_FreeData(data);
}
void CustomAvatar_MarkFailed(int id)
{
	if (CustomAvatar_Get(id))
		ca_failed[id - PLAYER_AVATAR_COUNT] = true;
}
qboolean CustomAvatar_HasFailed(int id)
{
	return CustomAvatar_Get(id) && ca_failed[id - PLAYER_AVATAR_COUNT];
}
