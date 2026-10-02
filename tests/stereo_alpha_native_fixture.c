/* Native F05 alias/static fixture. Main supplies the two generated .mdl files. */
#ifdef NDEBUG
#error "Native alpha fixture requires assertions"
#endif
#include "../Quake/cl_parse.c"

#include <assert.h>
#include <string.h>

static const char *const fixture_alpha_paths[2] = {
	"progs/vr_alpha_wet.mdl",
	"progs/vr_alpha_dry.mdl"
};
static int fixture_alpha_models[2];
static int fixture_alpha_static_indices[2] = {-1, -1};
static int fixture_alpha_static_count;

static void Fixture_AlphaParseMessage (sizebuf_t *input, void (*parser) (void))
{
	sizebuf_t saved_message = net_message;
	int saved_readcount = msg_readcount;
	qboolean saved_badread = msg_badread;

	assert (input && input->data && input->maxsize > 0 &&
		input->cursize >= 0 && input->cursize <= input->maxsize && !input->overflowed);
	net_message = *input;
	MSG_BeginReading ();
	parser ();
	assert (msg_readcount == net_message.cursize && !msg_badread && !net_message.overflowed);
	net_message = saved_message;
	msg_readcount = saved_readcount;
	msg_badread = saved_badread;
}

static void Fixture_AlphaParsePrecache (void)
{
	CL_ParsePrecache ();
}

static void Fixture_AlphaParseStatic2 (void)
{
	CL_ParseStatic (2);
}

static void Fixture_AlphaPrecache (int index, const char *path)
{
	byte bytes[256];
	sizebuf_t input = {false, false, bytes, sizeof (bytes), 0};

	MSG_WriteShort (&input, index); /* type 0 (model), with a 14-bit index */
	MSG_WriteString (&input, path);
	Fixture_AlphaParseMessage (&input, Fixture_AlphaParsePrecache);
}

static void Fixture_AlphaStatic (int modelindex, const vec3_t origin)
{
	byte bytes[256];
	sizebuf_t input = {false, false, bytes, sizeof (bytes), 0};
	int axis;

	MSG_WriteByte (&input, B_LARGEMODEL | B_ALPHA);
	MSG_WriteShort (&input, modelindex);
	MSG_WriteByte (&input, 0); /* frame */
	MSG_WriteByte (&input, 0); /* colormap */
	MSG_WriteByte (&input, 0); /* skin */
	for (axis = 0; axis < 3; ++axis)
	{
		MSG_WriteCoord (&input, origin[axis], cl.protocolflags);
		MSG_WriteAngle (&input, 0, cl.protocolflags);
	}
	MSG_WriteByte (&input, 128); /* Fitz alpha decodes to exactly 0.5 */
	Fixture_AlphaParseMessage (&input, Fixture_AlphaParseStatic2);
}

void Fixture_AlphaSceneInit (void)
{
	static const vec3_t origins[2] = {{828, 850, -297}, {844, 850, -295}};
	int found = 0, i, first_static;

	assert (!Tasks_IsWorker ());
	assert (cls.signon == SIGNONS && cl.worldmodel &&
		!strcmp (cl.worldmodel->name, "maps/e1m1.bsp") &&
		cl.model_precache[1] == cl.worldmodel);
	assert (fixture_alpha_static_count == 0 && cl.num_statics >= 0);
	for (i = 2; i < MAX_MODELS && found < 2; ++i)
		if (!cl.model_precache[i])
		{
			assert (i <= 0x3fff);
			fixture_alpha_models[found++] = i;
		}
	assert (found == 2);

	for (i = 0; i < 2; ++i)
	{
		Fixture_AlphaPrecache (fixture_alpha_models[i], fixture_alpha_paths[i]);
		assert (cl.model_precache[fixture_alpha_models[i]] &&
			!strcmp (cl.model_precache[fixture_alpha_models[i]]->name, fixture_alpha_paths[i]) &&
			cl.model_precache[fixture_alpha_models[i]]->type == mod_alias);
	}

	first_static = cl.num_statics;
	Fixture_AlphaStatic (fixture_alpha_models[0], origins[0]);
	Fixture_AlphaStatic (fixture_alpha_models[1], origins[1]);
	assert (cl.num_statics == first_static + 2);
	fixture_alpha_static_indices[0] = first_static;
	fixture_alpha_static_indices[1] = first_static + 1;
	fixture_alpha_static_count = 2;
	for (i = 0; i < fixture_alpha_static_count; ++i)
	{
		entity_t *ent = cl.static_entities[fixture_alpha_static_indices[i]];
		qmodel_t *model = cl.model_precache[fixture_alpha_models[i]];
		assert (ent && ent->is_static && ent->model == model && model->type == mod_alias);
		assert (ent->baseline.modelindex == fixture_alpha_models[i] &&
			ent->netstate.modelindex == fixture_alpha_models[i] &&
			ent->alpha == 128 && ent->baseline.alpha == 128 && ent->netstate.alpha == 128);
	}
}

void Fixture_AlphaSceneOpacity (int enabled)
{
	unsigned char alpha = enabled ? 128 : ENTALPHA_ZERO;
	int i;

	assert ((enabled == 0 || enabled == 1) && !Tasks_IsWorker () && fixture_alpha_static_count == 2);
	for (i = 0; i < fixture_alpha_static_count; ++i)
	{
		int index = fixture_alpha_static_indices[i];
		entity_t *ent;
		assert (index >= 0 && index < cl.num_statics);
		ent = cl.static_entities[index];
		assert (ent && ent->is_static && ent->model == cl.model_precache[fixture_alpha_models[i]]);
		ent->alpha = alpha;
		ent->baseline.alpha = alpha;
		ent->netstate.alpha = alpha;
	}
}

static void Fixture_AlphaSceneOpacityCommand (void)
{
	assert (Cmd_Argc () == 2 && (!strcmp (Cmd_Argv (1), "0") || !strcmp (Cmd_Argv (1), "1")));
	Fixture_AlphaSceneOpacity (atoi (Cmd_Argv (1)));
}

void Fixture_AlphaSceneRegister (void)
{
	assert (!Tasks_IsWorker () && fixture_alpha_static_count == 0);
	Cmd_AddCommand ("fixture_alpha_init", Fixture_AlphaSceneInit);
	Cmd_AddCommand ("fixture_alpha_opacity", Fixture_AlphaSceneOpacityCommand);
}
