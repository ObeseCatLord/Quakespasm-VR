/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2005 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#ifndef MICROSOFT_WINDOWS_WINBASE_H_DEFINE_INTERLOCKED_CPLUSPLUS_OVERLOADS
#define MICROSOFT_WINDOWS_WINBASE_H_DEFINE_INTERLOCKED_CPLUSPLUS_OVERLOADS 0
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#include <winreg.h>
#include <versionhelpers.h>

#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4091) // 'typedef ': ignored on left of 'tagGPFIDL_FLAGS' when no variable is declared
#endif
#include <shlobj.h>
#ifdef _MSC_VER
#pragma warning (pop)
#endif

#include "quakedef.h"

#include <sys/types.h>
#include <errno.h>
#include <io.h>
#include <direct.h>

#if defined(SDL_FRAMEWORK) || defined(NO_SDL_CONFIG)
#include <SDL2/SDL.h>
#else
#include "SDL.h"
#endif


qboolean		isDedicated;

static HANDLE		hinput, houtput;

#define	MAX_HANDLES		32	/* johnfitz -- was 10 */
static FILE		*sys_handles[MAX_HANDLES];

static double counter_freq;

static int findhandle (void)
{
	int i;

	for (i = 1; i < MAX_HANDLES; i++)
	{
		if (!sys_handles[i])
			return i;
	}
	Sys_Error ("out of handles");
	return -1;
}

static void UTF8ToWideString (const char *src, wchar_t *dst, size_t maxchars)
{
	if (!MultiByteToWideChar (CP_UTF8, 0, src, -1, dst, maxchars))
		Sys_Error ("MultiByteToWideChar failed: %lu", GetLastError ());
}

static void WideStringToUTF8 (const wchar_t *src, char *dst, size_t maxbytes)
{
	if (!WideCharToMultiByte (CP_UTF8, 0, src, -1, dst, maxbytes, NULL, NULL))
		Sys_Error ("WideCharToMultiByte failed: %lu", GetLastError ());
}

FILE *Sys_fopen (const char *path, const char *mode)
{
	wchar_t	wpath[MAX_PATH];
	wchar_t	wmode[8];
	int		i;
	FILE	*f;

	for (i = 0; mode[i]; i++)
	{
		if (i == countof (wmode) - 1)
			Sys_Error ("Sys_fopen: invalid mode \"%s\"", mode);
		wmode[i] = mode[i];
	}
	wmode[i] = 0;

	UTF8ToWideString (path, wpath, countof (wpath));
	f = _wfopen (wpath, wmode);

	return f;
}

int Sys_remove (const char *path)
{
	wchar_t	wpath[MAX_PATH];
	UTF8ToWideString (path, wpath, countof (wpath));
	return _wremove (wpath);
}

long Sys_filelength (FILE *f)
{
	long		pos, end;

	pos = ftell (f);
	fseek (f, 0, SEEK_END);
	end = ftell (f);
	fseek (f, pos, SEEK_SET);

	return end;
}

int Sys_FileOpenRead (const char *path, int *hndl)
{
	FILE	*f;
	int		i, retval;

	i = findhandle ();
	f = Sys_fopen (path, "rb");

	if (!f)
	{
		*hndl = -1;
		retval = -1;
	}
	else
	{
		sys_handles[i] = f;
		*hndl = i;
		retval = Sys_filelength(f);
	}

	return retval;
}

int Sys_FileOpenWrite (const char *path)
{
	FILE	*f;
	int		i;

	i = findhandle ();
	f = Sys_fopen (path, "wb");

	if (!f)
		Sys_Error ("Error opening %s: %s", path, strerror(errno));

	sys_handles[i] = f;
	return i;
}

void Sys_FileClose (int handle)
{
	fclose (sys_handles[handle]);
	sys_handles[handle] = NULL;
}

void Sys_FileSeek (int handle, int position)
{
	fseek (sys_handles[handle], position, SEEK_SET);
}

int Sys_FileRead (int handle, void *dest, int count)
{
	return fread (dest, 1, count, sys_handles[handle]);
}

int Sys_FileWrite (int handle, const void *data, int count)
{
	return fwrite (data, 1, count, sys_handles[handle]);
}

int Sys_FileTime (const char *path)
{
	FILE	*f;

	f = Sys_fopen (path, "rb");

	if (f)
	{
		fclose(f);
		return 1;
	}

	return -1;
}

qboolean Sys_GetSteamDir (char *path, size_t pathsize)
{
	LSTATUS		err;
	HKEY		key;
	WCHAR		wpath[MAX_PATH + 1];
	DWORD		size, type;

	err = RegOpenKeyExW (HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ, &key);
	if (err != ERROR_SUCCESS)
		return false;

	// Note: string might not contain a terminating null character
	// https://docs.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryvalueexw#remarks

	err = RegQueryValueExW (key, L"SteamPath", NULL, &type, NULL, &size);
	if (err != ERROR_SUCCESS || type != REG_SZ || size > sizeof (wpath) - sizeof (wpath[0]))
	{
		RegCloseKey (key);
		return false;
	}

	err = RegQueryValueExW (key, L"SteamPath", NULL, &type, (BYTE *)wpath, &size);
	RegCloseKey (key);
	if (err != ERROR_SUCCESS || type != REG_SZ)
		return false;

	wpath[size / sizeof (wpath[0])] = 0;

	return WideCharToMultiByte (CP_UTF8, 0, wpath, -1, path, pathsize, NULL, NULL) != 0;
}

// https://github.com/libsdl-org/SDL/blob/120c76c84bbce4c1bfed4e9eb74e10678bd83120/src/core/windows/SDL_windows.c#L88-L99
static HRESULT Sys_InitCOM (void)
{
	HRESULT hr = CoInitializeEx (NULL, COINIT_APARTMENTTHREADED);
	if (hr == RPC_E_CHANGED_MODE)
		hr = CoInitializeEx (NULL, COINIT_MULTITHREADED);

	/* S_FALSE means success, but someone else already initialized. */
	/* You still need to call CoUninitialize in this case! */
	if (hr == S_FALSE)
		return S_OK;

	return hr;
}

qboolean Sys_GetSteamQuakeUserDir (char *path, size_t pathsize, const char *library)
{
	const char SUBDIR[] = "\\Nightdive Studios\\Quake";
	PWSTR wpath;
	HRESULT hr;
	qboolean ret;

	hr = Sys_InitCOM ();
	if (FAILED (hr))
		return false;

	hr = SHGetKnownFolderPath (&FOLDERID_SavedGames, 0, NULL, &wpath);
	if (FAILED (hr))
	{
		CoUninitialize ();
		return false;
	}

	ret = WideCharToMultiByte (CP_UTF8, 0, wpath, -1, path, pathsize, NULL, NULL) != 0;
	CoTaskMemFree (wpath);
	CoUninitialize ();

	return ret && (size_t) q_strlcat (path, SUBDIR, pathsize) < pathsize;
}

static char	cwd[1024];

static void Sys_GetBasedir (char *argv0, char *dst, size_t dstsize)
{
	char *tmp;
	size_t rc;
	wchar_t wpath[MAX_PATH];

	rc = GetCurrentDirectoryW (0, NULL);
	if (rc == 0)
		Sys_Error ("Couldn't determine current directory name length (error %lu)", GetLastError ());
	if (rc >= countof (wpath))
		Sys_Error ("Current directory name too long (%lu)", (DWORD)rc);
	if (!GetCurrentDirectoryW (rc, wpath))
		Sys_Error ("Couldn't determine current directory (error %lu)", GetLastError ());

	WideStringToUTF8 (wpath, dst, dstsize);

	tmp = dst;
	while (*tmp != 0)
		tmp++;
	while (*tmp == 0 && tmp != dst)
	{
		--tmp;
		if (tmp != dst && (*tmp == '/' || *tmp == '\\'))
			*tmp = 0;
	}
}

static char exedir[1024];

static const char *Sys_GetExeDir (void)
{
	wchar_t wpath[MAX_PATH];
	char *p, *slash;

	if (!GetModuleFileNameW (NULL, wpath, countof (wpath)))
		return NULL;

	if (!WideCharToMultiByte (CP_UTF8, 0, wpath, -1, exedir, sizeof (exedir), NULL, NULL))
		return NULL;

	for (p = exedir, slash = NULL; *p; p++)
		if (*p == '/' || *p == '\\')
			slash = p;
	if (slash)
		*slash = 0;

	return exedir;
}

typedef struct winfindfile_s {
	findfile_t			base;
	WIN32_FIND_DATAW	data;
	HANDLE				handle;
} winfindfile_t;

static void Sys_FillFindData (winfindfile_t *find)
{
	WideStringToUTF8 (find->data.cFileName, find->base.name, countof (find->base.name));
	find->base.attribs = 0;
	if (find->data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		find->base.attribs |= FA_DIRECTORY;
}

findfile_t *Sys_FindFirst (const char *dir, const char *ext)
{
	winfindfile_t		*ret;
	char				pattern[MAX_PATH];
	wchar_t				wpattern[MAX_PATH];
	HANDLE				handle;
	WIN32_FIND_DATAW	data;

	if (!ext)
		ext = "*";
	else if (*ext == '.')
		++ext;
	q_snprintf (pattern, sizeof (pattern), "%s/*.%s", dir, ext);

	UTF8ToWideString (pattern, wpattern, countof (wpattern));
	handle = FindFirstFileW (wpattern, &data);

	if (handle == INVALID_HANDLE_VALUE)
		return NULL;

	ret = (winfindfile_t *) calloc (1, sizeof (winfindfile_t));
	ret->handle = handle;
	ret->data = data;
	Sys_FillFindData (ret);

	return (findfile_t *) ret;
}

findfile_t *Sys_FindNext (findfile_t *find)
{
	winfindfile_t *wfind = (winfindfile_t *) find;
	if (!FindNextFileW (wfind->handle, &wfind->data))
	{
		Sys_FindClose (find);
		return NULL;
	}
	Sys_FillFindData (wfind);
	return find;
}

void Sys_FindClose (findfile_t *find)
{
	if (find)
	{
		winfindfile_t *wfind = (winfindfile_t *) find;
		FindClose (wfind->handle);
		free (wfind);
	}
}

typedef enum { dpi_unaware = 0, dpi_system_aware = 1, dpi_monitor_aware = 2 } dpi_awareness;
typedef BOOL (WINAPI *SetProcessDPIAwareFunc)();
typedef HRESULT (WINAPI *SetProcessDPIAwarenessFunc)(dpi_awareness value);

static void Sys_SetDPIAware (void)
{
	HMODULE hUser32, hShcore;
	SetProcessDPIAwarenessFunc setDPIAwareness;
	SetProcessDPIAwareFunc setDPIAware;

	/* Neither SDL 1.2 nor SDL 2.0.3 can handle the OS scaling our window.
	  (e.g. https://bugzilla.libsdl.org/show_bug.cgi?id=2713)
	  Call SetProcessDpiAwareness/SetProcessDPIAware to opt out of scaling.
	*/

	hShcore = LoadLibraryA ("Shcore.dll");
	hUser32 = LoadLibraryA ("user32.dll");
	setDPIAwareness = (SetProcessDPIAwarenessFunc) (hShcore ? GetProcAddress (hShcore, "SetProcessDpiAwareness") : NULL);
	setDPIAware = (SetProcessDPIAwareFunc) (hUser32 ? GetProcAddress (hUser32, "SetProcessDPIAware") : NULL);

	if (setDPIAwareness) /* Windows 8.1+ */
		setDPIAwareness (dpi_monitor_aware);
	else if (setDPIAware) /* Windows Vista-8.0 */
		setDPIAware ();

	if (hShcore)
		FreeLibrary (hShcore);
	if (hUser32)
		FreeLibrary (hUser32);
}

static void Sys_SetTimerResolution(void)
{
	/* Set OS timer resolution to 1ms.
	   Works around buffer underruns with directsound and SDL2, but also
	   will make Sleep()/SDL_Dleay() accurate to 1ms which should help framerate
	   stability.
	*/
	timeBeginPeriod (1);
}

void Sys_Init (void)
{
	SYSTEM_INFO info;

	Sys_SetTimerResolution ();
	Sys_SetDPIAware ();

	memset (cwd, 0, sizeof(cwd));
	Sys_GetBasedir(NULL, cwd, sizeof(cwd));
	host_parms->basedir = cwd;

	host_parms->exedir = Sys_GetExeDir ();

	/* userdirs not really necessary for windows guys.
	 * can be done if necessary, though... */
	host_parms->userdir = host_parms->basedir; /* code elsewhere relies on this ! */

	if (!IsWindowsXPOrGreater ())
		Sys_Error ("This engine requires Windows XP or newer");

	GetSystemInfo(&info);
	host_parms->numcpus = info.dwNumberOfProcessors;
	if (host_parms->numcpus < 1)
		host_parms->numcpus = 1;
	Sys_Printf("Detected %d CPUs.\n", host_parms->numcpus);

	if (isDedicated)
	{
		if (!AllocConsole ())
		{
			isDedicated = false;	/* so that we have a graphical error dialog */
			Sys_Error ("Couldn't create dedicated server console");
		}

		hinput = GetStdHandle (STD_INPUT_HANDLE);
		houtput = GetStdHandle (STD_OUTPUT_HANDLE);
	}

	counter_freq = (double)SDL_GetPerformanceFrequency();
}

void Sys_mkdir (const char *path)
{
	wchar_t wpath[MAX_PATH];
	BOOL result;
	DWORD err;

	UTF8ToWideString (path, wpath, countof (wpath));
	result = CreateDirectoryW (wpath, NULL);
	if (result)
		return;

	err = GetLastError ();
	if (err != ERROR_ALREADY_EXISTS)
		Sys_Error ("Unable to create directory %s (error %lu)", path, err);
}

static const wchar_t errortxt1[] = L"\nERROR-OUT BEGIN\n\n";
static const wchar_t errortxt2[] = L"\nQUAKE ERROR: ";

void Sys_Error (const char *error, ...)
{
	va_list		argptr;
	char		text[1024];
	wchar_t		wtext[1024];

	host_parms->errstate++;

	va_start (argptr, error);
	q_vsnprintf (text, sizeof(text), error, argptr);
	va_end (argptr);

	PR_SwitchQCVM(NULL);

	if (!MultiByteToWideChar (CP_UTF8, 0, text, -1, wtext, countof (wtext)))
		wcscpy (wtext, L"An unknown error has occurred");

	if (isDedicated)
		WriteConsoleW (houtput, errortxt1, wcslen(errortxt1), NULL, NULL);
	/* SDL will put these into its own stderr log,
	   so print to stderr even in graphical mode. */
	fputws (errortxt1, stderr);
	Host_Shutdown ();
	fputws (errortxt2, stderr);
	fputws (wtext, stderr);
	fputws (L"\n\n", stderr);
	if (!isDedicated)
		PL_ErrorDialog (text);
	else
	{
		WriteConsoleW (houtput, errortxt2, wcslen(errortxt2), NULL, NULL);
		WriteConsoleW (houtput, wtext,     wcslen(wtext),     NULL, NULL);
		WriteConsoleW (houtput, L"\r\n",   2,		          NULL, NULL);
		SDL_Delay (3000);	/* show the console 3 more seconds */
	}

	if (IsDebuggerPresent ())
		DebugBreak ();

	exit (1);
}

void Sys_Printf (const char *fmt, ...)
{
	va_list		argptr;
	char		text[1024];
	wchar_t		wtext[1024];
	int			len;

	va_start (argptr,fmt);
	q_vsnprintf (text, sizeof(text), fmt, argptr);
	va_end (argptr);

	len = MultiByteToWideChar (CP_UTF8, 0, text, -1, wtext, countof (wtext));
	if (!len)
		return;

	if (isDedicated)
	{
		WriteConsoleW (houtput, wtext, len, NULL, NULL);
	}
	else
	{
	/* SDL will put these into its own stdout log,
	   so print to stdout even in graphical mode. */
		fputws (wtext, stdout);
		OutputDebugStringW (wtext);
	}
}

void Sys_Quit (void)
{
	Host_Shutdown();

	if (isDedicated)
		FreeConsole ();

	exit (0);
}

double Sys_DoubleTime (void)
{
	return (double)SDL_GetPerformanceCounter() / counter_freq;
}

const char *Sys_ConsoleInput (void)
{
	static char	con_text[256];
	static int	textlen;
	INPUT_RECORD	recs[1024];
	int		ch;
	DWORD		dummy, numread, numevents;

	for ( ;; )
	{
		if (GetNumberOfConsoleInputEvents(hinput, &numevents) == 0)
			Sys_Error ("Error getting # of console events");

		if (! numevents)
			break;

		if (ReadConsoleInput(hinput, recs, 1, &numread) == 0)
			Sys_Error ("Error reading console input");

		if (numread != 1)
			Sys_Error ("Couldn't read console input");

		if (recs[0].EventType == KEY_EVENT)
		{
		    if (recs[0].Event.KeyEvent.bKeyDown == FALSE)
		    {
			ch = recs[0].Event.KeyEvent.uChar.AsciiChar;

			switch (ch)
			{
			case '\r':
				WriteFile(houtput, "\r\n", 2, &dummy, NULL);

				if (textlen != 0)
				{
					con_text[textlen] = 0;
					textlen = 0;
					return con_text;
				}

				break;

			case '\b':
				WriteFile(houtput, "\b \b", 3, &dummy, NULL);
				if (textlen != 0)
					textlen--;

				break;

			default:
				if (ch >= ' ')
				{
					WriteFile(houtput, &ch, 1, &dummy, NULL);
					con_text[textlen] = ch;
					textlen = (textlen + 1) & 0xff;
				}

				break;
			}
		    }
		}
	}

	return NULL;
}

void Sys_Sleep (unsigned long msecs)
{
/*	Sleep (msecs);*/
	SDL_Delay (msecs);
}

void Sys_SendKeyEvents (void)
{
	IN_Commands();		//ericw -- allow joysticks to add keys so they can be used to confirm SCR_ModalMessage
	IN_SendKeyEvents();
}
