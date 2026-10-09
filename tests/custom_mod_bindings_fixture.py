#!/usr/bin/env python3
"""Bounded end check of the production custom-command keys menu.

Run explicitly after implementation/integration. As in the particle menu
fixture, compile the actual menu section and selected production helpers;
assertions describe user-visible results rather than reimplementing policies.
The parser and registry lookups are production code over synthetic registries.

KEY/CMD OWNERS ARE STUBS: Key_Event only records calls, Key_SetBinding only
copies strings, and command execution/queueing only records attempted calls.
Passing this fixture proves menu behavior and call ordering, not native keyup,
alias execution, save/reload/profile precedence, gameplay, XR routing, or headset
usability. Native integration must qualify those separately. No game is launched
and no installed/user configuration is read or written.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CASES = ("rows", "known", "rejected", "controls", "limits", "capture", "payload", "lifecycle")


def definition(source, signature):
    """Same bounded top-level-definition seam as the particle menu fixture."""
    try:
        begin = source.index(signature)
        end = source.index("\n}", begin) + 2
    except ValueError as error:
        raise RuntimeError(f"Production seam missing: {signature}") from error
    return source[begin:end]


def functions(source, signatures):
    return "\n\n".join(definition(source, item) for item in signatures) + "\n"


def harness():
    menu = (ROOT / "Quake/menu.c").read_text()
    common = (ROOT / "Quake/common.c").read_text()
    cmd = (ROOT / "Quake/cmd.c").read_text()
    cvar = (ROOT / "Quake/cvar.c").read_text()
    common_header = (ROOT / "Quake/common.h").read_text()
    start = menu.index("/* KEYS MENU */")
    end = menu.index("/* HELP MENU */", start)
    keys = menu[start:end]
    # Extract the vector representation/macros, not the surrounding engine ABI.
    start = common_header.index("typedef struct vec_header_t")
    end = common_header.index("void Vec_Grow", start)
    vectors = common_header[start:end]
    parser = functions(common, (
        "const char *COM_ParseExBufferSpan (",
        "const char *COM_ParseExBuffer (",
        "const char *COM_ParseEx (",
        "const char *COM_Parse (",
        "qboolean COM_ParseLine (",
        "qboolean COM_ParseMutableLine (",
    ))
    registry = functions(cmd, (
        "qboolean Cmd_AliasExists (",
        "cmd_function_t *Cmd_FindCommand (",
        "qboolean Cmd_Exists (",
        "void Cmd_AddArg (",
        "int Cmd_Argc (",
        "const char *Cmd_Argv (",
        "static qboolean Cmd_TokenizeStringBuffer (",
        "void Cmd_TokenizeString (",
    )) + functions(cvar, (
        "cvar_t *Cvar_FindVar (",
        "cvar_t *Cvar_FindVarAfter (",
    ))
    routing = functions(menu, (
        "void M_MenuChanged (",
        "static qboolean M_Mouse_ClickValid (",
        "qboolean M_VRPointerCanClick (",
        "qboolean M_VRPointerRequiresHit (",
        "qboolean M_VRPointerBindingGrab (",
        "static qboolean M_IsMouseKey (",
        "void M_Keydown (",
        "void M_Charinput (",
        "qboolean M_TextEntry (",
        "qboolean M_WaitingForKeyBinding (",
    ))
    # Dispatch targets outside m_keys are inert, explicit boundary stubs. Keep
    # full production dispatchers so repeat/character/lifecycle gates execute.
    targets = sorted(set(re.findall(r"\b(M_\w+_(?:Key|Char|TextEntry))\s*\(", routing)))
    dispatch_stubs = "\n".join(
        (f"qboolean {name}(void) {{ return false; }}" if name.endswith("TextEntry")
         else f"void {name}(int key) {{ (void)key; }}")
        for name in targets if not name.startswith("M_Keys_"))
    return (PRELUDE + vectors + SUPPORT + parser + registry + dispatch_stubs +
            "\n" + keys + "\n" + routing + CHECKS)


PRELUDE = r'''
#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
typedef int qboolean;
typedef unsigned char byte;
typedef struct { int unused; } cb_context_t;
typedef struct { int width, height; } qpic_t;
struct cvar_s;
#include "keys.h"
#include "cmd.h"
#include "cvar.h"
#include "menu.h"
#include "mod_browser_keyboard.h"
#define q_snprintf snprintf
#define q_strcasecmp strcasecmp
#define CHARACTER_SIZE 8
#define q_isprint(c) isprint((unsigned char)(c))
#define countof(a) (sizeof(a) / sizeof((a)[0]))
#define q_min(a,b) ((a)<(b)?(a):(b))
#define q_max(a,b) ((a)>(b)?(a):(b))
#define CLAMP(lo,x,hi) q_min(q_max(lo,x),hi)
#define SAFE_FREE(p) do { free((void *)(p)); (p)=NULL; } while(0)
typedef enum { CPE_NOTRUNC, CPE_ALLOWTRUNC } cpe_mode;
typedef struct { const char *data; size_t len; } stringview_t;
static char com_token[4096];
static size_t q_strlcpy(char *out,const char *text,size_t size) {
    size_t length=strlen(text), copied=size?q_min(length,size-1):0;
    if(size) { memcpy(out,text,copied); out[copied]=0; }
    return length;
}
static size_t q_strlcat(char *out,const char *text,size_t size) {
    size_t old=strlen(out);
    if(old>=size) return size+strlen(text);
    return old+q_strlcpy(out+old,text,size-old);
}
static char *q_strdup(const char *s) {
    size_t n=strlen(s)+1; char *p=malloc(n); assert(p); memcpy(p,s,n); return p;
}
void Vec_Grow(void **p,size_t element_size,size_t extra);
void Vec_Clear(void **p);
void Vec_Free(void **p);
'''


SUPPORT = r'''
void Vec_Grow(void **p,size_t element_size,size_t extra) {
    vec_header_t *h=*p?((vec_header_t *)*p)-1:NULL;
    size_t size=h?h->size:0, capacity=h?h->capacity:0;
    if(size+extra<=capacity) return;
    capacity=q_max(size+extra,capacity?capacity*2:16);
    h=realloc(h,sizeof(*h)+capacity*element_size); assert(h);
    h->size=size; h->capacity=capacity; *p=h+1;
}
void Vec_Clear(void **p) { if(*p) VEC_HEADER(*p).size=0; }
void Vec_Free(void **p) { if(*p) free(((vec_header_t *)*p)-1); *p=NULL; }

/* Synthetic registry contents; production lookup/tokenization below. */
#define MAX_ALIAS_NAME 32
typedef struct cmdalias_s {
    struct cmdalias_s *next; char name[MAX_ALIAS_NAME]; char *value;
} cmdalias_t;
static cmdalias_t *cmd_alias;
static cmd_function_t *cmd_functions;
static cvar_t *cvar_vars;
#define MAX_ARGS 80
static int cmd_argc;
static char cmd_argv[MAX_ARGS][4096];
static char cmd_null_string[]="";
static const char *cmd_args;

/* STUB native key/command owners: spies, no dispatch or +/- semantics. */
char *keybindings[MAX_KEYS];
keydest_t key_dest;
enum m_state_e m_state;
static enum m_state_e m_keys_parent=m_options, m_mouse_hover_state, m_vr_pointer_state;
qboolean m_entersound;
static qboolean menu_changed, mods_keyboard, m_vr_pointer_valid, tracked;
static qboolean scrollbar_grab, slider_grab;
static int scrollbar_x, scrollbar_y, scrollbar_size;
static int *m_mouse_hover_cursor;
static int m_mouse_hover_value;
static float m_mouse_x, m_mouse_y;
static double realtime;
static qboolean hipnotic, mg3;
static cvar_t ui_mouse={.value=1};
static qboolean fixture_down[MAX_KEYS];
typedef struct { char kind; int key; } observation_t;
static observation_t observations[2048];
static char release_observed_binding[8192];
static int observation_count, queued_calls, execute_calls, default_clear_calls;
static int activate_calls, deactivate_calls;
static void observe(char kind,int key) {
    assert(observation_count<(int)countof(observations));
    observations[observation_count++]=(observation_t){kind,key};
}
void Key_Event(int key,qboolean down) {
    assert(key>=0 && key<MAX_KEYS);
    if(!down) q_strlcpy(release_observed_binding,keybindings[key]?keybindings[key]:"",sizeof(release_observed_binding));
    fixture_down[key]=down; observe(down?'D':'R',key);
}
void Key_SetBinding(int key,const char *binding) {
    assert(key>=0 && key<MAX_KEYS); observe(binding&&*binding?'B':'U',key);
    free(keybindings[key]); keybindings[key]=binding?q_strdup(binding):NULL;
}
const char *Key_KeynumToString(int key) {
    static char name[MAX_KEYS][16];
    if(key<0) return "";
    assert(key<MAX_KEYS); snprintf(name[key],sizeof(name[key]),"key%d",key); return name[key];
}
void Cbuf_InsertText(const char *text) { (void)text; ++queued_calls; }
void Cbuf_AddText(const char *text) { (void)text; ++queued_calls; }
void Cbuf_AddTextLen(const char *text,int n) { (void)text; (void)n; ++queued_calls; }
void Cbuf_Execute(void) { ++execute_calls; }
qboolean Cmd_ExecuteString(const char *text,cmd_source_t source) {
    (void)text; (void)source; ++execute_calls; return true;
}
static void VR_InputDefaultCommandCleared(const char *command) {
    (void)command; ++default_clear_calls;
}
static void IN_Activate(void) { ++activate_calls; }
static void IN_DeactivateForMenu(void) { ++deactivate_calls; }
static void S_LocalSound(const char *name) { (void)name; }
static void Con_DPrintf(const char *format,...) { (void)format; }
static void Con_Printf(const char *format,...) { (void)format; }
static qboolean V_TrackedSessionActive(void) { return tracked; }
static qboolean M_InScrollbar(void) { return false; }
static qboolean M_CancelPendingConnection(void) { return false; }
static const char *authored_file;
static void *COM_LoadFile(const char *path,void *unused) {
    (void)unused; assert(!strcmp(path,"bindlist.lst"));
    return authored_file?q_strdup(authored_file):NULL;
}
static void Mem_Free(void *p) { free(p); }
void M_MenuChanged(void);
void M_Menu_Options_f(void) { m_state=m_options; M_MenuChanged(); }
qboolean M_HandleScrollBarKeys(int key,int *cursor,int *first,int total,int page) {
    (void)key; (void)cursor; (void)first; (void)total; (void)page; return false;
}
void M_Mouse_UpdateCursor(int *cursor,int left,int right,int top,int height,int index) {
    (void)cursor; (void)left; (void)right; (void)top; (void)height; (void)index;
}
static qpic_t fixture_pic={32,8};
qpic_t *pic_up=&fixture_pic, *pic_down=&fixture_pic;
static qpic_t *Draw_CachePic(const char *path) { (void)path; return &fixture_pic; }
void M_DrawPic(cb_context_t *cbx,int x,int y,qpic_t *pic) {
    (void)cbx; (void)x; (void)y; (void)pic;
}
static void Draw_Character(cb_context_t *cbx,int x,int y,int character) {
    (void)cbx; (void)x; (void)y; (void)character;
}
static void Draw_Fill(cb_context_t *cbx,int x,int y,int w,int h,int color,float alpha) {
    (void)cbx; (void)x; (void)y; (void)w; (void)h; (void)color; (void)alpha;
}
typedef struct { int x,y; char text[8192]; } print_t;
static print_t printed[256];
static int printed_count;
void M_Print(cb_context_t *cbx,int x,int y,const char *text) {
    (void)cbx; assert(text); assert(printed_count<(int)countof(printed));
    print_t *p=&printed[printed_count++]; p->x=x; p->y=y;
    assert(strlen(text)<sizeof(p->text)); q_strlcpy(p->text,text,sizeof(p->text));
}
static void M_PrintWhite(cb_context_t *cbx,int x,int y,const char *text) { M_Print(cbx,x,y,text); }
static void M_PrintHighlighted(cb_context_t *cbx,int x,int y,const char *text) { M_Print(cbx,x,y,text); }
static void M_PrintScroll(cb_context_t *cbx,int x,int y,int width,const char *text,double position,qboolean bounce) {
    (void)width; (void)position; (void)bounce; M_Print(cbx,x,y,text);
}
static void M_DrawScrollbar(cb_context_t *cbx,int x,int y,float position,int size) {
    (void)cbx; (void)x; (void)y; (void)position; (void)size;
}
'''


CHECKS = r'''
/* Fixture setup writes directly so the owner spies see only menu side effects. */
static void seed(int key,const char *command) {
    free(keybindings[key]); keybindings[key]=command?q_strdup(command):NULL;
}
static void clear_observations(void) {
    observation_count=queued_calls=execute_calls=default_clear_calls=0;
    release_observed_binding[0]=0;
}
static void untouched(void) {
    assert(!observation_count && !queued_calls && !execute_calls && !default_clear_calls);
}
static int row(const char *command) {
    for(int i=0;i<(int)VEC_SIZE(bindnames);++i)
        if(bindnames[i].command && !strcmp(bindnames[i].command,command)) return i;
    return -1;
}
static int rows(const char *command) {
    int n=0;
    for(int i=0;i<(int)VEC_SIZE(bindnames);++i)
        if(bindnames[i].command && !strcmp(bindnames[i].command,command)) ++n;
    return n;
}
static void selected(const char *command) {
    assert(row(command)>=0); M_Keys_SelectCommand(command);
    assert(bindnames[keys_cursor].command && !strcmp(bindnames[keys_cursor].command,command));
}
static void open_editor(void) {
    M_Menu_Keys_f();
    int i;
    for(i=0;i<(int)VEC_SIZE(bindnames);++i) if(!bindnames[i].command) break;
    assert(i<(int)VEC_SIZE(bindnames)); keys_cursor=i;
    M_Keydown(K_ENTER,false);
    assert(M_TextEntry() && M_VRPointerRequiresHit() && !M_WaitingForKeyBinding());
}
static void type_text(const char *text) {
    for(const unsigned char *p=(const unsigned char *)text;*p;++p) M_Charinput(*p);
}
static void submit(void) {
    M_Keydown(K_ENTER,false);
}
static void assert_binding(int key,const char *command) {
    assert(keybindings[key] && !strcmp(keybindings[key],command));
}
static void registry_init(void) {
    static cmd_function_t commands[80];
    static cmdalias_t aliases[]={
        {.name="+hook",.value="impulse 24\n"},
        {.name="-hook",.value="impulse 25\n"},
        {.name="+lonely",.value="impulse 26\n"},
        {.name="modtoggle",.value="impulse 27\n"},
        {.name="stalealias",.value="impulse 28\n"},
    };
    size_t n=0;
    for(size_t i=0;i<countof(default_keybinds);++i) {
        const char *text=default_keybinds[i].command;
        if(!text[0] || !strcmp(text,"*")) continue;
        COM_Parse(text); assert(n<countof(commands));
        commands[n]=(cmd_function_t){.name=q_strdup(com_token),.srctype=src_command,.next=cmd_functions};
        cmd_functions=&commands[n++];
    }
    const char *extra[]={"echo","cmd","+fixturebutton","-fixturebutton","+unpaired"};
    for(size_t i=0;i<countof(extra);++i) {
        assert(n<countof(commands));
        commands[n]=(cmd_function_t){.name=q_strdup(extra[i]),.srctype=src_command,.next=cmd_functions};
        cmd_functions=&commands[n++];
    }
    for(size_t i=0;i<countof(aliases);++i) {
        aliases[i].next=cmd_alias; cmd_alias=&aliases[i];
    }
    static cvar_t fixture_cvar={.name="fixture_cvar",.string="0",.flags=CVAR_REGISTERED};
    cvar_vars=&fixture_cvar;
}
static void authored_and_saved(void) {
    authored_file="// authored labels\n\n\"impulse 1\" \"Mod blade\"\n"
        "\"+hook\" \"Grapple authored\"\n\"+forward\" \"Spoof movement\"\n"
        "\"never_registered\" \"Unsupported authored\"\n\"-\" \"\"\n";
    seed('a',"+forward"); seed('b',"impulse 1"); seed('c',"+hook");
    seed('d',"unsupported saved action"); seed('e',"unsupported saved action");
    seed('f',"UNSUPPORTED saved action"); seed('g',"stalealias");
    clear_observations(); M_Menu_Keys_f(); untouched();
    assert(!strcmp(bindnames[row("+forward")].description,"Move Forward"));
    assert(!strcmp(bindnames[row("impulse 1")].description,"Mod blade"));
    assert(!strcmp(bindnames[row("+hook")].description,"Grapple authored"));
    assert(rows("+forward")==1 && rows("impulse 1")==1 && rows("+hook")==1);
    assert(rows("unsupported saved action")==1 && rows("UNSUPPORTED saved action")==1);
    assert(row("never_registered")==-1 && row("modtoggle")==-1 && row("+lonely")==-1);
    assert(!strcmp(bindnames[row("unsupported saved action")].description,"unsupported saved action"));
    /* Remove the alias registry, approximating loss of support, not mod-switch execution. */
    cmdalias_t *saved=cmd_alias; cmd_alias=NULL;
    M_Menu_Keys_f(); untouched();
    assert(rows("stalealias")==1 && !strcmp(bindnames[row("stalealias")].description,"stalealias"));
    cmd_alias=saved;
    selected("unsupported saved action"); M_Keydown(K_BACKSPACE,false);
    assert(!keybindings['d'] || !keybindings['d'][0]);
    assert(!keybindings['e'] || !keybindings['e'][0]);
    assert_binding('f',"UNSUPPORTED saved action");
    authored_file=NULL;
    puts("CUSTOM_BINDINGS_AUTHORED_SAVED_ROWS_PASS");
}
static void validation_known(void) {
    const char *valid[]={"   +hook   ","+HOOK","+fixturebutton","modtoggle","MODTOGGLE",
        "ImPuLsE 24","fixture_cvar 1","cmd serveraction 7","echo hello/world","echo closing*/"};
    const char *expected[]={"+hook","+HOOK","+fixturebutton","modtoggle","MODTOGGLE",
        "ImPuLsE 24","fixture_cvar 1","cmd serveraction 7","echo hello/world","echo closing*/"};
    for(size_t i=0;i<countof(valid);++i) {
        clear_observations(); open_editor(); type_text(valid[i]); untouched(); submit(); untouched();
        if(!M_WaitingForKeyBinding())
            fprintf(stderr,"Expected accepted draft <%s>; production error: %s\n",valid[i],keys_command_error);
        assert(M_WaitingForKeyBinding() && !M_TextEntry() && !M_VRPointerRequiresHit());
        M_Keydown('z',false); assert_binding('z',expected[i]);
        assert(!queued_calls && !execute_calls);
    }
    puts("CUSTOM_BINDINGS_KNOWN_DRAFTS_PASS");
}
static void validation_rejected(void) {
    /* Native command/alias lookup ignores case, whereas the cvar owner uses
     * exact names. Only comment openers have a parse effect in a new draft;
     * a standalone closing delimiter remains an ordinary argument above. */
    const char *invalid[]={"","   ","never_registered","serveraction 7","+lonely","+unpaired","FIXTURE_CVAR 2",
        "+hook 7","+fixturebutton 7","echo a;quit","echo \"quoted\"","echo 'quoted'",
        "echo //comment","echo /*comment*/"};
    for(size_t i=0;i<countof(invalid);++i) {
        clear_observations(); open_editor(); type_text(invalid[i]); submit(); untouched();
        if(M_WaitingForKeyBinding())
            fprintf(stderr,"Expected rejected draft <%s>; production entered capture\n",invalid[i]);
        assert(!M_WaitingForKeyBinding() && M_TextEntry());
        assert(keys_command_error[0]);
        M_Keydown(K_ESCAPE,false); untouched(); assert(!M_TextEntry());
    }
    puts("CUSTOM_BINDINGS_REJECTED_DRAFTS_PASS");
}
static void validation_controls(void) {
    /* Character input must not install control bytes. Separately challenge the
     * actual submit validator with a draft containing each such byte. */
    const int controls[]={1,9,10,13,31,127};
    for(size_t i=0;i<countof(controls);++i) {
        clear_observations(); open_editor(); type_text("echo safe");
        M_Charinput(controls[i]); untouched();
        assert(!strcmp(keys_command_draft,"echo safe") && keys_command_error[0]);
        keys_command_draft[9]=(char)controls[i]; keys_command_draft[10]=0;
        submit(); untouched();
        assert(!M_WaitingForKeyBinding() && M_TextEntry() && keys_command_error[0]);
        M_Keydown(K_ESCAPE,false); untouched();
    }
    puts("CUSTOM_BINDINGS_CONTROL_INPUT_AND_VALIDATION_PASS");
}
static void validation_limits(void) {
    char limit[257]; memcpy(limit,"echo ",5); memset(limit+5,'x',250); limit[255]=0;
    clear_observations(); open_editor(); type_text(limit); submit(); untouched();
    assert(M_WaitingForKeyBinding()); M_Keydown('z',false); assert_binding('z',limit);
    char accepted[256]; memcpy(accepted,limit,sizeof(accepted));
    limit[255]='x'; limit[256]=0;
    clear_observations(); open_editor(); type_text(limit); submit(); untouched();
    assert(!M_WaitingForKeyBinding() && M_TextEntry() && keys_command_error[0]);
    assert_binding('z',accepted);
    printed_count=0; M_Keys_Draw(NULL); untouched();
    int visible_error=0;
    for(int i=0;i<printed_count;++i) if(!strcmp(printed[i].text,keys_command_error)) ++visible_error;
    assert(visible_error==1);
    M_Keydown(K_BACKSPACE,false); untouched();
    assert(strlen(keys_command_draft)==254); submit(); untouched();
    assert(M_WaitingForKeyBinding()); M_Keydown(K_ESCAPE,false); untouched();
    open_editor(); type_text(limit); submit(); untouched();
    M_Keydown(K_DEL,false); untouched(); assert(!keys_command_draft[0]);
    type_text("+hook"); submit(); untouched(); assert(M_WaitingForKeyBinding());
    M_Keydown(K_ESCAPE,false);
    puts("CUSTOM_BINDINGS_EDITOR_LIMIT_PASS");
}
static void cancellation_and_capture(void) {
    const char *commands[]={"+forward","+hook","unsupported capture action"};
    for(size_t c=0;c<countof(commands);++c) for(int old=2;old<=3;++old) {
        const char *command=commands[c];
        seed('j',command); seed('k',command); seed('l',old==3?command:NULL);
        seed('q',"capture owner before assignment");
        clear_observations(); M_Menu_Keys_f(); selected(command); M_Keydown(K_ENTER,false);
        untouched(); assert(M_WaitingForKeyBinding());
        M_Keydown(K_ESCAPE,false); untouched(); assert(!M_WaitingForKeyBinding());
        assert_binding('j',command); assert_binding('k',command);
        if(old==3) assert_binding('l',command);
        selected(command); M_Keydown(K_ENTER,false); M_Menu_Keys_f(); untouched();
        assert(!M_WaitingForKeyBinding()); assert_binding('j',command); assert_binding('k',command);
        selected(command); M_Keydown(K_ENTER,false); untouched();
        /* Exercise the copied pending payload across an actual row rebuild. */
        M_Keys_Populate(); untouched();
        fixture_down['q']=true; M_Keydown('q',true); untouched(); assert(M_WaitingForKeyBinding());
        M_Keydown('q',false); assert_binding('q',command);
        assert(!keybindings['j'] || !keybindings['j'][0]);
        assert(!keybindings['k'] || !keybindings['k'][0]);
        assert(!keybindings['l'] || !keybindings['l'][0]);
        int release=-1, first_set=-1;
        for(int i=0;i<observation_count;++i) {
            if(observations[i].kind=='R' && observations[i].key=='q') release=i;
            if(first_set<0 && (observations[i].kind=='B' || observations[i].kind=='U')) first_set=i;
        }
        assert(release>=0 && first_set>release && !fixture_down['q']);
        assert(!strcmp(release_observed_binding,"capture owner before assignment"));
        assert(!M_WaitingForKeyBinding() && !queued_calls && !execute_calls);
        seed('q',NULL);
    }
    puts("CUSTOM_BINDINGS_CAPTURE_CANCEL_RELEASE_ORDER_STUB_OWNER_PASS");
}
static void long_saved_payload(void) {
    char macro[700];
    strcpy(macro,"echo \"existing macro\"; ");
    size_t n=strlen(macro); memset(macro+n,'m',600-n); macro[600]=0;
    seed('v',macro); clear_observations(); M_Menu_Keys_f(); untouched();
    assert(rows(macro)==1); selected(macro);
    assert(!strcmp(bindnames[keys_cursor].command,macro));
    printed_count=0; M_Keys_Draw(NULL); untouched();
    int row_y=48+8*(keys_cursor-first_key), labels=0;
    for(int i=0;i<printed_count;++i) if(printed[i].x==10 && printed[i].y==row_y) {
        ++labels; assert(strlen(printed[i].text)>0 && strlen(printed[i].text)<strlen(macro));
    }
    assert(labels==1 && !strcmp(bindnames[keys_cursor].command,macro));
    M_Keydown(K_ENTER,false); untouched(); M_Keydown('w',false);
    assert_binding('w',macro); assert_binding('v',macro);
    assert(!queued_calls && !execute_calls);
    puts("CUSTOM_BINDINGS_IMPORTED_MACRO_EXACT_PAYLOAD_DRAW_PASS");
}
static void lifecycle(void) {
    seed('h',"+hook");
    clear_observations(); open_editor(); type_text("+hook");
    assert(M_TextEntry() && M_VRPointerRequiresHit());
    key_dest=key_game;
    assert(!M_TextEntry() && !M_VRPointerRequiresHit() && !M_WaitingForKeyBinding() && !M_VRPointerBindingGrab());
    key_dest=key_menu; m_state=m_options;
    assert(!M_TextEntry() && !M_VRPointerRequiresHit() && !M_WaitingForKeyBinding() && !M_VRPointerBindingGrab());
    M_Menu_Keys_f(); untouched();
    assert(!M_TextEntry() && !M_VRPointerRequiresHit() && !M_WaitingForKeyBinding());
    selected("+hook"); M_Keydown(K_ENTER,false); untouched(); assert(M_WaitingForKeyBinding());
    key_dest=key_game; assert(!M_WaitingForKeyBinding());
    assert(!M_VRPointerBindingGrab());
    key_dest=key_menu; m_state=m_options; assert(!M_WaitingForKeyBinding());
    assert(!M_VRPointerBindingGrab());
    M_Menu_Keys_f(); untouched(); assert(!M_WaitingForKeyBinding());
    mods_keyboard=true; m_state=m_mods; assert(M_VRPointerRequiresHit());
    mods_keyboard=false; assert(!M_VRPointerRequiresHit());
    M_Menu_Keys_f(); open_editor();
    printed_count=0; M_Keys_Draw(NULL); untouched();
    int plus=0;
    for(int i=0;i<printed_count;++i) if(!strcmp(printed[i].text,"+")) ++plus;
    assert(plus==1);
    /* Real pointer-hit and repeat gates, synthetic geometry/input observations. */
    keys_command_keyboard_cursor=39;
    m_mouse_hover_state=m_keys; m_mouse_x=-1; m_mouse_y=-1;
    M_Keydown(K_MOUSE1,false); untouched(); assert(!keys_command_draft[0]);
    const mod_browser_rect_t r=ModBrowser_KeyRect(39);
    m_mouse_x=r.x+1; m_mouse_y=r.y+1;
    M_Keydown(K_MOUSE1,false); untouched(); assert(!strcmp(keys_command_draft,"+"));
    M_Keydown(K_ABUTTON,true); untouched(); assert(!strcmp(keys_command_draft,"+"));
    M_Keydown(K_ESCAPE,false); untouched();
    puts("CUSTOM_BINDINGS_MENU_ROUTING_LIFECYCLE_PASS");
}
static void cleanup(void) {
    M_Keys_ResetCommand();
    for(int key=0;key<MAX_KEYS;++key) seed(key,NULL);
    for(size_t i=0;i<VEC_SIZE(bindnames);++i) {
        SAFE_FREE(bindnames[i].command); SAFE_FREE(bindnames[i].description);
    }
    for(size_t i=0;i<VEC_SIZE(custom_bindnames);++i) {
        SAFE_FREE(custom_bindnames[i].command); SAFE_FREE(custom_bindnames[i].description);
    }
    VEC_FREE(bindnames); VEC_FREE(custom_bindnames);
    for(cmd_function_t *p=cmd_functions;p;p=p->next) free((void *)p->name);
}
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IONBF,0);
    assert(argc==2);
    const struct { const char *name; void (*check)(void); } cases[]={
        {"rows",authored_and_saved}, {"known",validation_known},
        {"rejected",validation_rejected}, {"controls",validation_controls}, {"limits",validation_limits},
        {"capture",cancellation_and_capture}, {"payload",long_saved_payload}, {"lifecycle",lifecycle},
    };
    registry_init();
    for(size_t i=0;i<countof(cases);++i) if(!strcmp(argv[1],cases[i].name)) {
        cases[i].check(); cleanup(); return 0;
    }
    fprintf(stderr,"Unknown fixture case: %s\n",argv[1]); cleanup(); return 2;
}
'''


def main():
    source_text = harness()
    with tempfile.TemporaryDirectory(prefix="qsvr-custom-bindings-menu-") as directory:
        source = Path(directory) / "fixture.c"
        binary = Path(directory) / "fixture"
        source.write_text(source_text)
        compiler = shlex.split(os.environ.get("CC", "cc"))
        subprocess.run(compiler + [
            "-std=c11", "-O0", "-g", "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-function", "-Wno-sign-compare",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
            "-I", str(ROOT / "Quake"), str(source), "-o", str(binary),
        ], check=True)
        failed = []
        # Separate processes isolate setup and keep one assertion failure from
        # masking the rest of the bounded regression evidence.
        for case in CASES:
            result = subprocess.run([str(binary), case], check=False)
            if result.returncode:
                failed.append(case)
        if failed:
            raise SystemExit("CUSTOM_MOD_BINDINGS_MENU_FAILED: " + ", ".join(failed) +
                             " (native key/cmd owners are stubs)")
        print("CUSTOM_MOD_BINDINGS_MENU_PASS "
              "(native key/cmd owners stubbed; native integration required)")


if __name__ == "__main__":
    main()
