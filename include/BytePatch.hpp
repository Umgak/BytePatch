
/*
	* BytePatch
	* The world's shittiest runtime memory editor.

	* Copyright (c) 2026 Lily

	* This program is free software; licensed under the MIT license.
	* You should have received a copy of the license along with this program.
	* If not, see <https://opensource.org/licenses/MIT>.
*/
#pragma once
#ifndef BYTE_PATCH_H
#define BYTE_PATCH_H
#include <string> //sorry folks, I do need this one actually. the rest are all in the cpp

// Since we're doing Minhook-like syntax, this will allow to toggle all patches in one go
#define BP_ALL_PATCHES NULL

typedef enum BP_STATUS {
	// Should never be returned
	BP_UNKNOWN = -1,

	// Success
	BP_OK = 0,

	// Don't try to make me install hooks on null
	BP_ERROR_NULL,

	// BytePatch already exists at that address - if attempting to create two BytePatch with same base address
	BP_ERROR_ALREADY_EXISTS,

	// BytePatchAOB passed to BP_CreatePatch was not understood
	BP_ERROR_INVALID_SIGNATURE,
	
	// BytePatch does not exist - if attempting to modify a BytePatch that does not exist
	BP_ERROR_NOT_FOUND,

	// BytePatch is already applied - if attempting to EnablePatch() when it's already ENABLED
	BP_ERROR_ENABLED,

	// BytePatch is not enabled yet - if attempting to DisablePatch() when it's already DISABLED
	BP_ERROR_DISABLED,

	// Bytepatch is already queued - if attempting to QueueEnable/Disable when it's already in one of the QUEUED states.
	BP_ERROR_QUEUED,

	// Failed to change memory protections
	BP_ERROR_MEMORY_PROTECT
} BP_STATUS;

// Create a patch in the disabled state
BP_STATUS BP_CreatePatch(void* address, const std::string& bytePatchAOB);
// Delete an existing patch entirely. Disables if it's enabled.
BP_STATUS BP_RemovePatch(void* address);
// Enable a created, but disabled patch.
BP_STATUS BP_EnablePatch(void* address);
// Disable an enabled patch.
BP_STATUS BP_DisablePatch(void* address);
// Queue a disabled patch to be enabled.
BP_STATUS BP_QueueEnablePatch(void* address);
// Queue an enabled patch to be disabled.
BP_STATUS BP_QueueDisablePatch(void* address);
// Apply all queued changes.
BP_STATUS BP_ApplyQueued();
#endif