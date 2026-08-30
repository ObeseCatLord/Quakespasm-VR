/*
 * Server PMove admission policy.
 *
 * Keep this independent of the VM and client structures so the exact
 * compatibility gates can be exercised without starting a server.
 */
#ifndef SV_PMOVE_POLICY_H
#define SV_PMOVE_POLICY_H

typedef enum
{
	SV_PMOVE_POLICY_AUTHORITY_LEGACY_FRAME = 0,
	SV_PMOVE_POLICY_AUTHORITY_ENGINE_COMPAT,
	SV_PMOVE_POLICY_AUTHORITY_QC_COMMAND
} sv_pmove_policy_authority_t;

typedef enum
{
	SV_PMOVE_POLICY_FALLBACK_ADMIN = 0,
	SV_PMOVE_POLICY_FALLBACK_CUSTOMPHYSICS,
	SV_PMOVE_POLICY_FALLBACK_UNSUPPORTED_STATE,
	SV_PMOVE_POLICY_FALLBACK_INVALID_STATE
} sv_pmove_policy_fallback_t;

typedef struct
{
	int requested_mode;
	int trusted_movement;
	int local_singleplayer;
	int client_spawned;
	int client_known_to_qc;
	int nq_player_physics;
	int legacy_prethink_mod;
	int has_qc_onladder_field;
	int has_sv_runclientcommand;
	int has_explicit_cmd_msec;
	int customphysics_active;
	int valid_state;
} sv_pmove_policy_input_t;

typedef struct
{
	int using_pmove;
	sv_pmove_policy_authority_t authority;
	sv_pmove_policy_fallback_t fallback;
} sv_pmove_policy_result_t;

/*
 * Header-only deliberately: sv_user is built by several platform makefiles,
 * while this pure policy must never introduce a platform-specific link input.
 */
static inline sv_pmove_policy_result_t
SV_PMovePolicyEvaluate(const sv_pmove_policy_input_t *input)
{
	sv_pmove_policy_result_t result;
	const int legacy_qc_ladder_mod = input->has_qc_onladder_field &&
		!input->has_sv_runclientcommand;

	result.using_pmove = input->requested_mode != 0 &&
		input->trusted_movement && !input->local_singleplayer &&
		input->client_spawned && input->client_known_to_qc &&
		!input->nq_player_physics && !input->legacy_prethink_mod &&
		!legacy_qc_ladder_mod && input->has_explicit_cmd_msec &&
		!input->customphysics_active && input->valid_state;
	result.authority = SV_PMOVE_POLICY_AUTHORITY_LEGACY_FRAME;
	result.fallback = SV_PMOVE_POLICY_FALLBACK_ADMIN;

	if (result.using_pmove)
	{
		if (input->requested_mode == 3)
		{
			if (input->has_sv_runclientcommand)
				result.authority = SV_PMOVE_POLICY_AUTHORITY_QC_COMMAND;
			else
			{
				result.using_pmove = 0;
				result.fallback = SV_PMOVE_POLICY_FALLBACK_UNSUPPORTED_STATE;
			}
		}
		else if (input->requested_mode == 2)
		{
			result.authority = SV_PMOVE_POLICY_AUTHORITY_ENGINE_COMPAT;
		}
		else
		{
			result.authority = input->has_sv_runclientcommand ?
				SV_PMOVE_POLICY_AUTHORITY_QC_COMMAND :
				SV_PMOVE_POLICY_AUTHORITY_ENGINE_COMPAT;
		}
	}
	else if (input->legacy_prethink_mod || legacy_qc_ladder_mod)
	{
		result.fallback = SV_PMOVE_POLICY_FALLBACK_UNSUPPORTED_STATE;
	}
	else if (input->customphysics_active)
	{
		result.fallback = SV_PMOVE_POLICY_FALLBACK_CUSTOMPHYSICS;
	}
	else if (input->client_spawned && input->client_known_to_qc &&
		!input->valid_state)
	{
		result.fallback = SV_PMOVE_POLICY_FALLBACK_INVALID_STATE;
	}
	else if (input->requested_mode != 0 && input->trusted_movement &&
		!input->nq_player_physics)
	{
		result.fallback = SV_PMOVE_POLICY_FALLBACK_UNSUPPORTED_STATE;
	}

	return result;
}

#endif /* SV_PMOVE_POLICY_H */
