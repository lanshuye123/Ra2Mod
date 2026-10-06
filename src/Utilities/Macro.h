#pragma once
#include <Helpers/Macro.h>
#include <ASMMacros.h>
#include "Patch.h"

// Helpers that YRpp does not provide.
//
// GET / GET_STACK / LEA_STACK / REF_STACK / STACK_OFFSET come from YRpp's
// Helpers\Macro.h and are re-exported through the include above.

// Export / calling-convention macros for public C ABI functions
// Usage:
//   DEFINE_EXPORT(return_type, func_name, arglist...)
#define DEFINE_EXPORT(ret, name, ...) extern "C" __declspec(dllexport) ret __stdcall name(__VA_ARGS__)

#define GET_REGISTER_STATIC_TYPE(type, dst, reg) static type dst; _asm { mov dst, reg }

template<typename T>
__forceinline T& Make_Global(const uintptr_t address)
{
	return *reinterpret_cast<T*>(address);
}

template<typename T>
__forceinline T* Make_Pointer(const uintptr_t address)
{
	return reinterpret_cast<T*>(address);
}

#define NAKED __declspec(naked)

#pragma region Patch Macros

#pragma pack(push, 1)
#pragma warning(push)
#pragma warning( disable : 4324)

#define LJMP_OPCODE 0xE9
#define CALL_OPCODE 0xE8
#define NOP_OPCODE  0x90

typedef void JumpType;

typedef JumpType LJMP;
struct _LJMP
{
	byte opcode;
	DWORD pointer;

	constexpr _LJMP(DWORD offset, DWORD pointer) :
		opcode(LJMP_OPCODE),
		pointer(pointer - offset - 5)
	{ };
};

typedef JumpType CALL;
struct _CALL
{
	byte opcode;
	DWORD pointer;

	constexpr _CALL(DWORD offset, DWORD pointer) :
		opcode(CALL_OPCODE),
		pointer(pointer - offset - 5)
	{ };
};

typedef JumpType CALL6;
struct _CALL6
{
	byte opcode;
	DWORD pointer;
	byte nop;

	constexpr _CALL6(DWORD offset, DWORD pointer) :
		opcode(CALL_OPCODE),
		pointer(pointer - offset - 5),
		nop(NOP_OPCODE)
	{ };
};

typedef JumpType VTABLE;
struct _VTABLE
{
	DWORD pointer;

	constexpr _VTABLE(DWORD offset, DWORD pointer) :
		pointer(pointer)
	{ };
};

typedef JumpType OFFSET;
typedef _VTABLE _OFFSET;

#pragma warning(pop)
#pragma pack(pop)
#pragma endregion Patch Structs

#pragma region Static Patch
// A static patch is a Patch record placed in the .patch PE section at compile time.
// Patch::ApplyStatic() walks that section at runtime and applies each record, so the
// patch list does not have to be maintained by hand. The anchor keeps the linker from
// discarding the record when /OPT:REF is on.
#define _ALLOCATE_STATIC_PATCH(offset, size, data)                \
	namespace STATIC_PATCH##offset                                \
	{                                                             \
		__declspec(allocate(PATCH_SECTION_NAME))                  \
		Patch patch = {offset, size, (byte*)data};                \
	}                                                             \
	_YR_DEFINE_INCLUDE_ANCHOR(                                    \
		_YR_PP_CAT(YrKeepPatch_, offset),                         \
		&STATIC_PATCH##offset::patch                              \
	)

#define DEFINE_PATCH_TYPED(type, offset, ...)                     \
	namespace STATIC_PATCH##offset                                \
	{                                                             \
		const type data[] = {__VA_ARGS__};                        \
	}                                                             \
	_ALLOCATE_STATIC_PATCH(offset, sizeof(data), data);

#define DEFINE_PATCH(offset, ...)                                 \
	DEFINE_PATCH_TYPED(byte, offset, __VA_ARGS__);

#define DEFINE_JUMP(jumpType, offset, pointer)                    \
	namespace STATIC_PATCH##offset                                \
	{                                                             \
		const _##jumpType data (offset, pointer);                 \
	}                                                             \
	_ALLOCATE_STATIC_PATCH(offset, sizeof(data), &data);

#define DEFINE_NAKED_HOOK(hook, funcname)                         \
	void funcname();                                              \
	DEFINE_FUNCTION_JUMP(LJMP, hook, funcname)                    \
	void NAKED funcname()

#pragma endregion Static Patch

#pragma region Thiscall Patch
#define _GET_FUNCTION_ADDRESS(function, getterName)               \
	static constexpr __forceinline uintptr_t getterName()         \
	{                                                             \
		uintptr_t addr;                                           \
		{ _asm mov eax, function }                                \
		{ _asm mov addr, eax }                                    \
		return addr;                                              \
	}

#define DEFINE_FUNCTION_JUMP(jumpType, offset, function)          \
	namespace NAMESPACE_THISCALL_JUMP##offset                     \
	{                                                             \
		_GET_FUNCTION_ADDRESS(function, GetAddr)                  \
		DEFINE_JUMP(jumpType, offset, GetAddr())                  \
	}
#pragma endregion

#pragma endregion Patch Macros
