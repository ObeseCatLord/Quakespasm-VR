/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2010-2014 QuakeSpasm developers
Copyright (C) 2016-2021 vkQuake developers

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

#ifndef QUAKE_VR_INPUT_H
#define QUAKE_VR_INPUT_H

#include "protocol.h"
#include "vr_openxr.h"

void VR_InputInit (void);
void VR_InputCommands (const vrxr_frame_t *frame);
void VR_InputMove (usercmd_t *pending);
void VR_InputApplyPending (usercmd_t *cmd);
void VR_InputInvalidateMotion (void);
void VR_InputClear (void);

#endif /* QUAKE_VR_INPUT_H */
