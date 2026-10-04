#!/usr/bin/env python3
"""Exercise actual argv/+command producers at boundaries under ASan/UBSan."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def function(file, name, following):
    text = (ROOT / file).read_text()
    return text[text.index("void " + name):text.index(following, text.index("void " + name))]

source = r"""
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int qboolean;
#define true 1
#define false 0
#define CMDLINE_LENGTH 4096
#define MAX_NUM_ARGVS 50
typedef struct { char *string; } cvar_t;
cvar_t cmdline;
char com_cmdline[CMDLINE_LENGTH], *largv[MAX_NUM_ARGVS+1], **com_argv;
char argvdummy[]="";
int com_argc,safemode,rogue,hipnotic,standard_quake=1,warnings,insertions;
char captured[CMDLINE_LENGTH];
void Con_Printf(const char *s,...) {(void)s;}
void Con_Warning(const char *s,...) {(void)s;warnings++;}
int COM_CheckParm(const char *s) {for(int i=1;i<com_argc;i++)if(!strcmp(s,com_argv[i]))return i;return 0;}
void Cbuf_InsertText(const char *s) {assert(strlen(s)<sizeof(captured));strcpy(captured,s);insertions++;}
"""
source += function("Quake/common.c", "COM_InitArgv", "entity_state_t nullentitystate;")
source += function("Quake/cmd.c", "Cmd_StuffCmds_f", "void Cmd_Exec_f")
source += r"""
int main(void) {
 char longarg[8193];memset(longarg,'x',sizeof(longarg)-1);longarg[sizeof(longarg)-1]=0;
 char *a[]={"q","+map","start","-postcfg",longarg};
 for(size_t n=0;n<8192;n++) {
  longarg[n]=0; warnings=0;COM_InitArgv(5,a);
  size_t wanted=22+n;
  assert(com_argc==5 && com_argv[4]==longarg);
  if(wanted<=4095)assert(strlen(com_cmdline)==wanted && !warnings);
  else assert(!com_cmdline[0] && warnings==1);
  longarg[n]='x';
 }
 char *b[]={longarg};longarg[4095]=0;COM_InitArgv(1,b);assert(strlen(com_cmdline)==4095);
 longarg[4095]='x';longarg[4096]=0;COM_InitArgv(1,b);assert(!com_cmdline[0]);
 char *c[]={"q",longarg,"+map","late-map","-postcfg","profile.cfg"};
 longarg[600]=0;COM_InitArgv(6,c);cmdline.string=com_cmdline;insertions=0;
 Cmd_StuffCmds_f();assert(insertions==1 && strstr(captured,"map late-map")!=NULL);
 char manual[16384];
 for(size_t n=0;n<8192;n++) {
  manual[0]='+';memset(manual+1,'x',n);manual[n+1]=0;
  cmdline.string=manual;insertions=warnings=0;Cmd_StuffCmds_f();
  assert(n<=4095?(insertions==1 && !warnings && strlen(captured)==n):(!insertions && warnings==1));
 }
 /* Separators expand a ROM cmdline override: never insert a partial batch. */
 strcpy(manual,"+");memset(manual+1,'x',4094);strcpy(manual+4095,"+y");
 cmdline.string=manual;insertions=warnings=0;Cmd_StuffCmds_f();assert(!insertions && warnings==1);
 puts("COMMANDLINE_BOUNDS_PASSED argv_cases=8192 plus_cases=8192 sanitizers=ASan,UBSan");
}
"""
# Refuse accidental drift away from the production ABI constant.
header=(ROOT / "Quake/quakedef.h").read_text()
assert re.search(r"#define\s+CMDLINE_LENGTH\s+4096\b",header)
with tempfile.TemporaryDirectory(prefix="qsvr-commandline-bounds-") as temp:
    path=Path(temp);(path / "fixture.c").write_text(source)
    subprocess.run(["clang","-std=c11","-Wall","-Wextra","-Werror","-g",
                    "-fsanitize=address,undefined",str(path / "fixture.c"),"-o",str(path / "fixture")],check=True)
    subprocess.run([str(path / "fixture")],check=True)
