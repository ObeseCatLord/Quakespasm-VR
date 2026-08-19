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

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <mmsystem.h>
#include <tlhelp32.h>
#include <shlobj.h>

#ifdef _MSC_VER
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uuid.lib")
#endif

#include "quakedef.h"

#include <sys/types.h>
#include <errno.h>
#include <io.h>
#include <direct.h>

#if defined(SDL_FRAMEWORK) || defined(NO_SDL_CONFIG)
#if defined(USE_SDL2)
#include <SDL2/SDL.h>
#else
#include <SDL/SDL.h>
#endif
#else
#include "SDL.h"
#endif


qboolean		isDedicated;
qboolean	Win95, Win95old, WinNT, WinVista;
cvar_t		sys_throttle = {"sys_throttle", "0.02", CVAR_ARCHIVE};

static HANDLE		hinput, houtput;

#define	MAX_HANDLES		32	/* johnfitz -- was 10 */
static FILE		*sys_handles[MAX_HANDLES];


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

static qboolean UTF8ToWideString (const char *src, wchar_t *dst, size_t maxchars)
{
	if (MultiByteToWideChar (CP_UTF8, 0, src, -1, dst, (int) maxchars))
		return true;
	errno = GetLastError () == ERROR_INSUFFICIENT_BUFFER ? ENAMETOOLONG : EINVAL;
	return false;
}

FILE *Sys_fopen (const char *path, const char *mode)
{
	wchar_t	wpath[MAX_PATH];
	wchar_t	wmode[8];
	int		i;
	FILE	*f;

	for (i = 0; mode[i]; i++)
	{
		if (i == ((int) (sizeof (wmode) / sizeof (wmode[0])) - 1))
			Sys_Error ("Sys_fopen: invalid mode \"%s\"", mode);
		wmode[i] = mode[i];
	}
	wmode[i] = 0;

	if (!UTF8ToWideString (path, wpath, Q_COUNTOF (wpath)))
		return NULL;

	if (wpath[0] && strchr (mode, 'w'))
	{
		// create directory structure
		for (i = 1; wpath[i]; i++)
		{
			DWORD attr;
			wchar_t wc;
			if (wpath[i] != L'\\' && wpath[i] != L'/')
				continue;

			// keep the trailing slash
			wc = wpath[i + 1];
			wpath[i + 1] = L'\0';

			attr = GetFileAttributesW (wpath);
			if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
				return NULL;

			if (attr == INVALID_FILE_ATTRIBUTES && !CreateDirectoryW (wpath, NULL))
			{
				DWORD err = GetLastError ();
				if (err != ERROR_ALREADY_EXISTS)
					return NULL;
			}

			wpath[i + 1] = wc;
		}
	}

	f = _wfopen (wpath, wmode);

	return f;
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
	int	i, retval;

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

#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES	((DWORD)-1)
#endif
int Sys_FileType (const char *path)
{
	wchar_t wpath[MAX_OSPATH];
	DWORD result;

	if (!UTF8ToWideString(path, wpath, countof(wpath)))
		return FS_ENT_NONE;
	result = GetFileAttributesW(wpath);

	if (result == INVALID_FILE_ATTRIBUTES)
		return FS_ENT_NONE;
	if (result & FILE_ATTRIBUTE_DIRECTORY)
		return FS_ENT_DIRECTORY;

	return FS_ENT_FILE;
}

typedef struct procinfo_s
{
	DWORD id;
	DWORD parent_id;
	wchar_t	name[MAX_PATH];
} procinfo_t;

static procinfo_t *Sys_GetProc (DWORD id, procinfo_t *proc, DWORD *parent_id)
{
	HANDLE			hSnapshot;
	PROCESSENTRY32W	proc_entry;
	procinfo_t		*result = NULL;

	hSnapshot = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
	if (hSnapshot == INVALID_HANDLE_VALUE)
		return NULL;

	proc_entry.dwSize = sizeof (proc_entry);
	if (Process32FirstW (hSnapshot, &proc_entry))
	{
		do
		{
			if (proc_entry.th32ProcessID == id)
			{
				proc->id = proc_entry.th32ProcessID;
				proc->parent_id = proc_entry.th32ParentProcessID;
				wcsncpy (proc->name, proc_entry.szExeFile, Q_COUNTOF (proc->name));
				proc->name[Q_COUNTOF (proc->name) - 1] = 0;
				if (parent_id)
					*parent_id = proc->parent_id;
				result = proc;
				break;
			}
		} while (Process32NextW (hSnapshot, &proc_entry));
	}

	CloseHandle (hSnapshot);
	return result;
}

qboolean Sys_IsStartedFromMapEditor (void)
{
	qboolean		from_editor = false;
	procinfo_t		proc;
	procinfo_t		parent;
	DWORD			parent_id = 0;

	if (!Sys_GetProc (GetCurrentProcessId (), &proc, &parent_id))
		return false;

	while (parent_id)
	{
		if (!Sys_GetProc (parent_id, &parent, &proc.parent_id))
			break;

		if (_wcsicmp (parent.name, L"cmd.exe") == 0)
		{
			parent_id = parent.parent_id;
			continue;
		}

		#define PARENT_STARTS_WITH(prefix)	(_wcsnicmp (parent.name, L##prefix, wcslen (L##prefix)) == 0)
		if (PARENT_STARTS_WITH ("TrenchBroom") ||
			PARENT_STARTS_WITH ("NextBroom") ||
			PARENT_STARTS_WITH ("jack") ||
			PARENT_STARTS_WITH ("qrucible"))
			from_editor = true;
		#undef PARENT_STARTS_WITH
		break;
	}

	return from_editor;
}

static char	cwd[1024];

qboolean Sys_GetSteamDir (char *path, size_t pathsize)
{
	HKEY key;
	wchar_t wpath[MAX_PATH];
	DWORD type, bytes = sizeof(wpath) - sizeof(wpath[0]);

	if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ, &key) != ERROR_SUCCESS)
		return false;
	if (RegQueryValueExW(key, L"SteamPath", NULL, &type, (BYTE *)wpath, &bytes) != ERROR_SUCCESS ||
		type != REG_SZ || bytes < sizeof(wchar_t) || bytes > sizeof(wpath) - sizeof(wpath[0]))
	{
		RegCloseKey(key);
		return false;
	}
	RegCloseKey(key);
	/* Registry strings are allowed to omit their terminating NUL. */
	wpath[bytes / sizeof(wchar_t)] = 0;
	if (!WideCharToMultiByte(CP_UTF8, 0, wpath, -1, path, (int)pathsize, NULL, NULL))
	{
		if (pathsize) path[0] = 0;
		return false;
	}
	return Sys_FileType(va("%s/config/libraryfolders.vdf", path)) & FS_ENT_FILE;
}

static HRESULT Sys_InitCOM (void)
{
	HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	if (hr == RPC_E_CHANGED_MODE)
		hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	/* S_FALSE is successful but still requires a matching CoUninitialize. */
	return hr == S_FALSE ? S_OK : hr;
}

qboolean Sys_GetSteamQuakeContentDir (char *path, size_t pathsize, const char *library)
{
	PWSTR savedgames;
	HRESULT hr;
	qboolean result;

	(void)library;
	hr = Sys_InitCOM();
	if (FAILED(hr))
		return false;
	hr = SHGetKnownFolderPath(&FOLDERID_SavedGames, 0, NULL, &savedgames);
	if (FAILED(hr))
	{
		CoUninitialize();
		return false;
	}
	result = WideCharToMultiByte(CP_UTF8, 0, savedgames, -1, path,
		(int)pathsize, NULL, NULL) != 0;
	CoTaskMemFree(savedgames);
	CoUninitialize();
	if (!result || q_strlcat(path, "/Nightdive Studios/Quake", pathsize) >= pathsize)
		return false;
	return Sys_FileType(path) & FS_ENT_DIRECTORY;
}

static void Sys_GetBasedir (char *argv0, char *dst, size_t dstsize)
{
	wchar_t wdst[MAX_OSPATH];
	char *tmp;
	size_t rc;

	rc = GetCurrentDirectoryW(countof(wdst), wdst);
	if (rc == 0 || rc >= countof(wdst) ||
		!WideCharToMultiByte(CP_UTF8, 0, wdst, -1, dst, (int)dstsize, NULL, NULL))
		Sys_Error ("Couldn't determine current directory");

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
	OSVERSIONINFO	vinfo;

	Sys_SetTimerResolution ();
	Sys_SetDPIAware ();

	memset (cwd, 0, sizeof(cwd));
	Sys_GetBasedir(NULL, cwd, sizeof(cwd));
	host_parms->basedir = cwd;

	/* userdirs not really necessary for windows guys.
	 * can be done if necessary, though... */
	host_parms->userdir = host_parms->basedir; /* code elsewhere relies on this ! */

	vinfo.dwOSVersionInfoSize = sizeof(vinfo);

	if (!GetVersionEx (&vinfo))
		Sys_Error ("Couldn't get OS info");

	if ((vinfo.dwMajorVersion < 4) ||
		(vinfo.dwPlatformId == VER_PLATFORM_WIN32s))
	{
		Sys_Error (QUAKESPASM_PROJECT_NAME " requires at least Win95 or NT 4.0");
	}

	if (vinfo.dwPlatformId == VER_PLATFORM_WIN32_NT)
	{
		SYSTEM_INFO info;
		WinNT = true;
		if (vinfo.dwMajorVersion >= 6)
			WinVista = true;
		GetSystemInfo(&info);
		host_parms->numcpus = info.dwNumberOfProcessors;
		if (host_parms->numcpus < 1)
			host_parms->numcpus = 1;
	}
	else
	{
		WinNT = false; /* Win9x or WinME */
		host_parms->numcpus = 1;
		if ((vinfo.dwMajorVersion == 4) && (vinfo.dwMinorVersion == 0))
		{
			Win95 = true;
			/* Win95-gold or Win95A can't switch bpp automatically */
			if (vinfo.szCSDVersion[1] != 'C' && vinfo.szCSDVersion[1] != 'B')
				Win95old = true;
		}
	}
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
}

void Sys_mkdir (const char *path)
{
	wchar_t wpath[MAX_OSPATH];

	if (!UTF8ToWideString(path, wpath, countof(wpath)))
		Sys_Error("Unable to convert directory path %s", path);
	if (CreateDirectoryW(wpath, NULL) != 0)
		return;
	if (GetLastError() != ERROR_ALREADY_EXISTS)
		Sys_Error("Unable to create directory %s", path);
}

static const char errortxt1[] = "\nERROR-OUT BEGIN\n\n";
static const char errortxt2[] = "\nQUAKE ERROR: ";

void Sys_Error (const char *error, ...)
{
	va_list		argptr;
	char		text[1024];
	DWORD		dummy;

	host_parms->errstate++;

	va_start (argptr, error);
	q_vsnprintf (text, sizeof(text), error, argptr);
	va_end (argptr);

	if (isDedicated)
		WriteFile (houtput, errortxt1, strlen(errortxt1), &dummy, NULL);
	/* SDL will put these into its own stderr log,
	   so print to stderr even in graphical mode. */
	fputs (errortxt1, stderr);
	Host_Shutdown ();
	fputs (errortxt2, stderr);
	fputs (text, stderr);
	fputs ("\n\n", stderr);
	if (!isDedicated)
		PL_ErrorDialog(text);
	else
	{
		WriteFile (houtput, errortxt2, strlen(errortxt2), &dummy, NULL);
		WriteFile (houtput, text,      strlen(text),      &dummy, NULL);
		WriteFile (houtput, "\r\n",    2,		  &dummy, NULL);
		SDL_Delay (3000);	/* show the console 3 more seconds */
	}

	exit (1);
}

void Sys_Printf (const char *fmt, ...)
{
	va_list		argptr;
	char		text[1024];
	DWORD		dummy;

	va_start (argptr,fmt);
	q_vsnprintf (text, sizeof(text), fmt, argptr);
	va_end (argptr);

	if (isDedicated)
	{
		WriteFile(houtput, text, strlen(text), &dummy, NULL);
	}
	else
	{
	/* SDL will put these into its own stdout log,
	   so print to stdout even in graphical mode. */
		fputs (text, stdout);
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
	return SDL_GetTicks() / 1000.0;
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
