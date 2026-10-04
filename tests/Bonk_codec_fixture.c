/* Production codec, existing golden bytes, maximum redundant payload and suffix framing. */
#define main prior_codec_main
#include "private_usercmd_fixture.c"
#undef main

int main (void)
{
	prior_codec_main ();
	byte bytes[DATAGRAM_MTU], plain[DATAGRAM_MTU];
	sizebuf_t buf, old;
	usercmd_t cmd, decoded;
	for (int trusted = 0; trusted < 2; ++trusted)
	{
		unsigned caps = trusted ? QSVR_MOVE_CAP_GORILLA_TRUSTED : QSVR_MOVE_CAP_GORILLA_RAW;
		set_full_command (&cmd, trusted);
		cmd.vr_contact.head_angles[0] = -179.75f;
		cmd.vr_contact.head_angles[1] = 123.25f;
		cmd.vr_contact.head_angles[2] = 17.5f;
		begin_write (&old, plain, sizeof plain);
		CL_WritePrivateUsercmd (&old, &cmd, PRFL_FLOATANGLE, caps);
		cmd.vr_contact.flags |= VR_WEAPON_CONTACT_HEAD_PRESENT;
		begin_write (&buf, bytes, sizeof bytes);
		CL_WritePrivateUsercmd (&buf, &cmd, PRFL_FLOATANGLE, caps);
		assert (buf.cursize == old.cursize + 12);
		begin_read (bytes, buf.cursize);
		assert (SV_ReadPrivateUsercmd (&decoded, 77, PRFL_FLOATANGLE, caps));
		assert_full_command (&decoded, &cmd, trusted);
		assert (!memcmp (decoded.vr_contact.head_angles, cmd.vr_contact.head_angles, 12));
		assert (msg_readcount == buf.cursize); // following Gorilla extension read exactly
		assert_all_prefixes_rejected (&cmd, PRFL_FLOATANGLE, caps);
		// Locate suffix independently by matching its unique three IEEE floats.
		int suffix = -1;
		for (int i = 0; i <= buf.cursize - 12; ++i)
			if (!memcmp (bytes + i, cmd.vr_contact.head_angles, 12)) suffix = i;
		assert (suffix >= 0);
		byte original[DATAGRAM_MTU];
		memcpy (original, bytes, buf.cursize);
		for (int axis = 0; axis < 3; ++axis)
		{
			const float invalid[] = {NAN, INFINITY, -INFINITY, 180, -180.25f};
			for (size_t k = 0; k < countof (invalid); ++k)
			{
				memcpy (bytes, original, buf.cursize);
				memcpy (bytes + suffix + 4 * axis, &invalid[k], 4);
				begin_read (bytes, buf.cursize);
				assert (!SV_ReadPrivateUsercmd (&decoded, 77, PRFL_FLOATANGLE, caps));
				assert (msg_badread);
			}
		}
		begin_write (&buf, bytes, sizeof bytes);
		for (int seq = 2; seq <= 4; ++seq)
		{
			MSG_WriteByte (&buf, clc_move);
			MSG_WriteShort (&buf, seq);
			CL_WritePrivateUsercmd (&buf, &cmd, PRFL_FLOATANGLE, caps);
		}
		int command_bytes = buf.cursize;
		MSG_WriteByte (&buf, clc_stringcmd);
		MSG_WriteString (&buf, "Bonk-following-extension");
		assert (buf.cursize <= DATAGRAM_MTU && !buf.overflowed);
		begin_read (bytes, buf.cursize);
		for (int seq = 2; seq <= 4; ++seq)
		{
			assert (MSG_ReadByte () == clc_move && MSG_ReadShort () == seq);
			assert (SV_ReadPrivateUsercmd (&decoded, seq, PRFL_FLOATANGLE, caps));
			assert (!memcmp (decoded.vr_contact.head_angles, cmd.vr_contact.head_angles, 12));
			assert (decoded.forwardmove == cmd.forwardmove && decoded.sidemove == cmd.sidemove && decoded.upmove == cmd.upmove);
		}
		assert (msg_readcount == command_bytes && MSG_ReadByte () == clc_stringcmd);
		assert (!strcmp (MSG_ReadString (), "Bonk-following-extension"));
		assert (msg_readcount == buf.cursize && !msg_badread);
		printf ("BONK_CODEC_%s: 3 maximal commands %d/%d bytes; following extension exact\n", trusted ? "TRUSTED" : "RAW", command_bytes, DATAGRAM_MTU);
	}
	return 0;
}
