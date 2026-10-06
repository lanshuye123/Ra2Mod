#include "Patch.h"
#include "Macro.h"

#include <HAres.h>

#include <cstdint>
#include <cstring>

// Returns the virtual size of the named PE section inside this DLL, and stores its
// virtual address in *ppVirtualAddress. Returns 0 when the section is absent.
static int GetSection(const char* pSectionName, void** ppVirtualAddress)
{
	const auto hInstance = HAres::hInstance;

	// DllMain may not have run yet (or the module handle was never stored); walking the
	// PE headers on a null base would fault at offset 0x3C.
	if (!hInstance)
		return 0;

	const auto pDosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(hInstance);
	const auto pHeader = reinterpret_cast<PIMAGE_NT_HEADERS>(
		reinterpret_cast<BYTE*>(hInstance) + pDosHeader->e_lfanew);

	for (int i = 0; i < pHeader->FileHeader.NumberOfSections; ++i)
	{
		const auto pSectionHeader = IMAGE_FIRST_SECTION(pHeader) + i;

		if (strncmp(pSectionName, reinterpret_cast<const char*>(pSectionHeader->Name), 8) == 0)
		{
			*ppVirtualAddress = reinterpret_cast<void*>(
				reinterpret_cast<DWORD>(hInstance) + pSectionHeader->VirtualAddress);
			return pSectionHeader->Misc.VirtualSize;
		}
	}

	return 0;
}

void Patch::ApplyStatic()
{
	void* pBuffer = nullptr;
	const int len = GetSection(PATCH_SECTION_NAME, &pBuffer);

	if (!pBuffer)
		return;

	for (int offset = 0; offset < len; offset += sizeof(Patch))
	{
		const auto pPatch = reinterpret_cast<Patch*>(reinterpret_cast<DWORD>(pBuffer) + offset);

		// The section is zero-padded up to VirtualSize; a zero offset marks the end.
		if (pPatch->offset == 0)
			return;

		pPatch->Apply();
	}
}

void Patch::Apply()
{
	void* pAddress = reinterpret_cast<void*>(static_cast<uintptr_t>(this->offset));

	DWORD protectFlag = 0;
	VirtualProtect(pAddress, this->size, PAGE_EXECUTE_READWRITE, &protectFlag);
	memcpy(pAddress, this->pData, this->size);
	VirtualProtect(pAddress, this->size, protectFlag, &protectFlag);

	// The instruction cache is coherent on x86; this is only here to conform to the docs.
	FlushInstructionCache(GetCurrentProcess(), pAddress, this->size);
}

void Patch::Apply_LJMP(DWORD offset, DWORD pointer)
{
	const _LJMP data(offset, pointer);
	Patch patch = { offset, sizeof(data), (byte*)&data };
	patch.Apply();
}

void Patch::Apply_CALL(DWORD offset, DWORD pointer)
{
	const _CALL data(offset, pointer);
	Patch patch = { offset, sizeof(data), (byte*)&data };
	patch.Apply();
}

void Patch::Apply_CALL6(DWORD offset, DWORD pointer)
{
	const _CALL6 data(offset, pointer);
	Patch patch = { offset, sizeof(data), (byte*)&data };
	patch.Apply();
}
