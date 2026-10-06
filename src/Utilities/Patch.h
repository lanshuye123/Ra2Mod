#pragma once
#include <windows.h>
#include <initializer_list>

// no more than 8 characters: PE section names are limited to 8 bytes.
#define PATCH_SECTION_NAME ".patch"
#pragma section(PATCH_SECTION_NAME, read)

#pragma pack(push, 1)
#pragma warning(push)
#pragma warning(disable : 4324)

// One raw memory patch: write Size bytes from pData to Offset in the game image.
struct __declspec(novtable) Patch
{
	DWORD offset;
	DWORD size;
	byte* pData;

	void Apply();

	// Applies every Patch record found in the .patch PE section. Called once at ExeRun.
	static void ApplyStatic();

	// --- runtime patching -------------------------------------------------
	// Use these when the decision to patch depends on runtime state (a config
	// value, the presence of another DLL, a detected game version).
	template <typename T>
	static void Apply_TYPED(DWORD offset, std::initializer_list<T> data)
	{
		Patch patch = { offset, static_cast<DWORD>(data.size() * sizeof(T)),
			const_cast<byte*>(reinterpret_cast<const byte*>(data.begin())) };
		patch.Apply();
	};

	template <size_t Size>
	static inline void Apply_RAW(DWORD offset, const char(&str)[Size])
	{
		Patch patch = { offset, static_cast<DWORD>(Size), (byte*)str };
		patch.Apply();
	};

	static inline void Apply_RAW(DWORD offset, std::initializer_list<byte> data)
	{
		Apply_TYPED<byte>(offset, data);
	};

	static void Apply_LJMP(DWORD offset, DWORD pointer);
	static inline void Apply_LJMP(DWORD offset, void* pointer)
	{
		Apply_LJMP(offset, reinterpret_cast<DWORD>(pointer));
	};

	static void Apply_CALL(DWORD offset, DWORD pointer);
	static inline void Apply_CALL(DWORD offset, void* pointer)
	{
		Apply_CALL(offset, reinterpret_cast<DWORD>(pointer));
	};

	static void Apply_CALL6(DWORD offset, DWORD pointer);
	static inline void Apply_CALL6(DWORD offset, void* pointer)
	{
		Apply_CALL6(offset, reinterpret_cast<DWORD>(pointer));
	};

	static inline void Apply_VTABLE(DWORD offset, DWORD pointer)
	{
		Apply_TYPED<DWORD>(offset, { pointer });
	};

	static inline void Apply_VTABLE(DWORD offset, void* pointer)
	{
		Apply_VTABLE(offset, reinterpret_cast<DWORD>(pointer));
	};

	static inline void Apply_OFFSET(DWORD offset, DWORD pointer)
	{
		Apply_VTABLE(offset, pointer);
	};

	static inline void Apply_OFFSET(DWORD offset, void* pointer)
	{
		Apply_VTABLE(offset, pointer);
	};
};

#pragma warning(pop)
#pragma pack(pop)
