/*
	* BytePatch
	* The world's shittiest runtime memory editor.

	* Copyright (c) 2026 Lily

	* This program is free software; licensed under the MIT license.
	* You should have received a copy of the license along with this program.
	* If not, see <https://opensource.org/licenses/MIT>.
*/

#include "../include/BytePatch.hpp"	// header, obviously
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <cstdint>				// uint8_t
#include <utility>				// std::move, std::pair
#include <vector>					// std::vector
#include <unordered_map>	// std::unordered_map
#include <cstring>				// std::memcpy (why is it in cstring that's such a big fucking header jesus christ)
#include <memoryapi.h>		// VirtualProtect

template <typename T>
using patchSignature = std::pair<std::vector<T>, std::vector<T>>;
using patchSignatureU8 = patchSignature<uint8_t>; // You try writing that out a thousand times, I don't want to jfc

enum class PATCH_STATUS {
  DISABLED,			// Not enabled yet
  ENABLED,			// Already active
  QUEUE_ENABLE,	// Not active, but queued to enable
  QUEUE_DISABLE	// Active, but queued to remove
};

struct PatchData {
	void* address = nullptr;
	patchSignatureU8 patchBytes;
	std::vector<uint8_t> originalBytes;
	PATCH_STATUS status = PATCH_STATUS::DISABLED;
};

// patch repo stored here
static std::unordered_map<void*, PatchData> g_patches;

// blatantly stolen from Pattern16, because my patchSignature syntax is the same
// if you're wondering: faster than converting using stoi
// by a LOT
alignas(64) inline constexpr const int8_t hexLookup[128] = {
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,-1,-1,-1,-1,-1,-1,
	-1,10,11,12,13,14,15,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,91,-1,93,-1,-1,
	-1,10,11,12,13,14,15,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
};

// convert a string into a patchSignature
// blatantly stealing p16's homework again thanks Dasaav
static patchSignatureU8 strToPatchSignature(const std::string& patchSignatureString) {
	patchSignatureU8 patchSignatureBytes{};
	auto& [bytes, mask] = patchSignatureBytes;
	std::string byteStr;
	auto bit = -1;
	auto counter = 0;
	for (auto chr : patchSignatureString) {
		if (chr == ' ' || chr < 0) continue;
		if (chr == '[') { 
			bit = 0;		// switch to bit mode
			continue;
		}
		if (chr == ']') {
			 bit = -1;	// switch to byte mode
			 continue;
		}
		auto val = hexLookup[chr];		// hex nibble LUT by ascii value
		if (bit < 0) {	// byte mode
			if ((++counter &= 1)) {
				bytes.push_back(0);
				mask.push_back(0);
			}
			if (val >= 0) {
				bytes.back() |= val << (counter << 2);
				mask.back() |= 0xF << (counter << 2);
			}
		} else {
			if (--bit < 0) { // bit mode
				bytes.push_back(0);
				mask.push_back(0);
				counter = 0;
				bit &= 7;
			}
			if (val >= 0) {
				bytes.back() |= (val & 1) << bit;
				mask.back() |= 1 << bit;
			}
		}
	}
	return patchSignatureBytes;
}

// merges the original and patched bytes into a single vector for writing, based on the calculated bitmask
static std::vector<uint8_t> bitwiseMerge(const std::vector<uint8_t>& originalBytes, const patchSignatureU8& patchBytes) {
	const auto& newData = patchBytes.first;
	const auto& bitmask = patchBytes.second;
	std::vector<uint8_t> result(originalBytes.size());
	for (size_t i = 0; i < originalBytes.size(); ++i) {
		result[i] = (originalBytes[i] & ~bitmask[i]) | (newData[i] & bitmask[i]);
	}
	return result;
}

// Finds a patch by its target pointer
static PatchData* findPatch(void* address) {
	auto it = g_patches.find(address);
	if (it == g_patches.end()) return nullptr;
	return &(it->second);
}


// Set the bytes at the target pointer
static BP_STATUS ApplyPatch(void* address, const std::vector<uint8_t>& bytes) {
	DWORD oldProtect, dummy;
	if (!VirtualProtect(address, bytes.size(), PAGE_EXECUTE_READWRITE, &oldProtect)) {
		return BP_ERROR_MEMORY_PROTECT; // VirtualProtect exploded, run away!
	}
	std::memcpy(address, bytes.data(), bytes.size());
	VirtualProtect(address, bytes.size(), oldProtect, &dummy);	
	FlushInstructionCache(GetCurrentProcess(), address, bytes.size());
	return BP_OK;
}

// Apply a patch by PatchData, used internally
static BP_STATUS EnablePatch(PatchData* patch) {
	// already enabled, do nothing
	if (patch->status == PATCH_STATUS::ENABLED) return BP_ERROR_ENABLED;
	BP_STATUS result = BP_OK;
	// no need to apply any changes to memory if the patch is in QUEUE_DISABLE
	if (patch->status != PATCH_STATUS::QUEUE_DISABLE) {
		std::vector<uint8_t> bytes = bitwiseMerge(patch->originalBytes, patch->patchBytes);
		result = ApplyPatch(patch->address, bytes);
	}
	if (result == BP_OK) {
		patch->status = PATCH_STATUS::ENABLED;
	}
	return result;
}

// Remove a patch by PatchData, used internally
static BP_STATUS DisablePatch(PatchData* patch) {
	// already disabled, do nothing
	if (patch->status == PATCH_STATUS::DISABLED) return BP_ERROR_DISABLED;
	BP_STATUS result = BP_OK;
	// no need to apply any changes to memory if the patch is in QUEUE_ENABLE
	if (patch->status != PATCH_STATUS::QUEUE_ENABLE) {
		result = ApplyPatch(patch->address, patch->originalBytes);
	}
	if (result == BP_OK) {
		patch->status = PATCH_STATUS::DISABLED;
	}
	return result;
}

// Create a patch in the disabled state
BP_STATUS BP_CreatePatch(void* address, const std::string& bytePatchAOB) {
	// check if user is dumb and attempted to modify a null pointer
	if (address == nullptr) return BP_ERROR_NULL;
	// check if user is dumb and attempted to CreatePatch at the same address twice
	if (findPatch(address) != nullptr) return BP_ERROR_ALREADY_EXISTS;
	
	// check if user is dumb and passed some random shit as the signature
	patchSignatureU8 signature = strToPatchSignature(bytePatchAOB);
	size_t patchSize = signature.first.size();
	if (patchSize == 0) return BP_ERROR_INVALID_SIGNATURE;

	// grab the original bytes, for RemovePatch
	std::vector<uint8_t> originalBytes(patchSize);
	DWORD oldProtect, dummy;
	if (!VirtualProtect(address, originalBytes.size(), PAGE_EXECUTE_READWRITE, &oldProtect)) {
		return BP_ERROR_MEMORY_PROTECT; // VirtualProtect exploded, run away!
	}
	std::memcpy(originalBytes.data(), address, patchSize);
	VirtualProtect(address, originalBytes.size(), oldProtect, &dummy);

	// construct new patch
	PatchData newPatch;
	newPatch.address = address;
	newPatch.patchBytes = std::move(signature);
	newPatch.originalBytes = std::move(originalBytes);
	newPatch.status = PATCH_STATUS::DISABLED;

	// store new patch
	g_patches[address] = std::move(newPatch);
	return BP_OK;
}

// Delete an existing patch entirely. Disables if it's enabled.
BP_STATUS BP_RemovePatch(void* address) {
	auto patch = findPatch(address);
	if (patch == nullptr) return BP_ERROR_NOT_FOUND;
	if (patch->status == PATCH_STATUS::ENABLED || patch->status == PATCH_STATUS::QUEUE_DISABLE) {
		BP_STATUS result = DisablePatch(patch);
		if (result != BP_OK) return result;
	}
	g_patches.erase(address);
	return BP_OK;
}

// Enable a created, but disabled patch.
BP_STATUS BP_EnablePatch(void* address) {
	if (address == BP_ALL_PATCHES) {
		if (g_patches.empty()) return BP_ERROR_NOT_FOUND;
		BP_STATUS f_res = BP_OK;
		for (auto& [target, patch] : g_patches) {
			if (patch.status == PATCH_STATUS::ENABLED) continue;
			BP_STATUS l_res = BP_OK;
			l_res = EnablePatch(&patch);
			if (l_res != BP_OK) // one of the patches failed
			{
				f_res = l_res;
			}
		}
		return f_res;
	}
	PatchData* patch = findPatch(address);
	if (patch == nullptr) return BP_ERROR_NOT_FOUND;
	return EnablePatch(patch);
}

// Disable an enabled patch.
BP_STATUS BP_DisablePatch(void* address) {
	if (address == BP_ALL_PATCHES) {
		if (g_patches.empty()) return BP_ERROR_NOT_FOUND;
		BP_STATUS f_res = BP_OK;
		for (auto& [target, patch] : g_patches) {
			if (patch.status == PATCH_STATUS::DISABLED) continue;
			BP_STATUS l_res = BP_OK;
			l_res = DisablePatch(&patch);
			if (l_res != BP_OK) // one of the patches failed
			{
				f_res = l_res;
			}
		}
		return f_res;
	}
	PatchData* patch = findPatch(address);
	if (patch == nullptr) return BP_ERROR_NOT_FOUND;
	return DisablePatch(patch);
}

// Queue a disabled patch to be enabled.
BP_STATUS BP_QueueEnablePatch(void* address) {
	if (address == BP_ALL_PATCHES) {
		if (g_patches.empty()) return BP_ERROR_NOT_FOUND;
		for (auto& [target, patch] : g_patches) {
			if (patch.status == PATCH_STATUS::ENABLED || patch.status == PATCH_STATUS::QUEUE_ENABLE) continue;
			if (patch.status == PATCH_STATUS::DISABLED) patch.status = PATCH_STATUS::QUEUE_ENABLE;
			if (patch.status == PATCH_STATUS::QUEUE_DISABLE) patch.status = PATCH_STATUS::ENABLED;
		}
		return BP_OK;
	}
	PatchData* patch = findPatch(address);
	if (patch == nullptr) return BP_ERROR_NOT_FOUND;
	if (patch->status == PATCH_STATUS::ENABLED) return BP_ERROR_ENABLED;
	if (patch->status == PATCH_STATUS::QUEUE_ENABLE) return BP_ERROR_QUEUED;
	if (patch->status == PATCH_STATUS::DISABLED) patch->status = PATCH_STATUS::QUEUE_ENABLE;
	if (patch->status == PATCH_STATUS::QUEUE_DISABLE) patch->status = PATCH_STATUS::ENABLED;
	return BP_OK;
}

// Queue an enabled patch to be disabled.
BP_STATUS BP_QueueDisablePatch(void* address) {
	if (address == BP_ALL_PATCHES) {
		if (g_patches.empty()) return BP_ERROR_NOT_FOUND;
		for (auto& [target, patch] : g_patches) {
			if (patch.status == PATCH_STATUS::DISABLED || patch.status == PATCH_STATUS::QUEUE_DISABLE) continue;
			if (patch.status == PATCH_STATUS::ENABLED) patch.status = PATCH_STATUS::QUEUE_DISABLE;
			if (patch.status == PATCH_STATUS::QUEUE_ENABLE) patch.status = PATCH_STATUS::DISABLED;
		}
		return BP_OK;
	}
	PatchData* patch = findPatch(address);
	if (patch == nullptr) return BP_ERROR_NOT_FOUND;
	if (patch->status == PATCH_STATUS::DISABLED) return BP_ERROR_DISABLED;
	if (patch->status == PATCH_STATUS::QUEUE_DISABLE) return BP_ERROR_QUEUED;
	if (patch->status == PATCH_STATUS::ENABLED) patch->status = PATCH_STATUS::QUEUE_DISABLE;
	if (patch->status == PATCH_STATUS::QUEUE_ENABLE) patch->status = PATCH_STATUS::DISABLED;
	return BP_OK;
}

// Apply all queued changes.
BP_STATUS BP_ApplyQueued() {
	if (g_patches.empty()) return BP_OK; // nothing to do
	BP_STATUS f_res = BP_OK;
	for (auto& [target, patch] : g_patches) {
		BP_STATUS l_res = BP_OK;
		if (patch.status == PATCH_STATUS::QUEUE_ENABLE) {
			l_res = EnablePatch(&patch);
		}
		else if (patch.status == PATCH_STATUS::QUEUE_DISABLE) {
			l_res = DisablePatch(&patch);
		}
		if (l_res != BP_OK) f_res = l_res;
	}
	return f_res;
}