
#pragma once
#define ASMJIT_EMBED
//#define ASMJIT_DEBUG

#include "../../asmjit/src/asmjit/asmjit.h"
#include "../utils.h"

namespace asmjit
{
	JitRuntime& get_global_runtime();
};

// Build runtime function with asmjit::X86Assembler
template <typename FT, typename F>
static FT build_function_asm(F&& builder)
{
	using namespace asmjit;

	auto& g_rt = get_global_runtime();

	CodeHolder code;
	code.init(g_rt.getCodeInfo());

	// Code alignment optimization enabled
	code._globalHints = asmjit::CodeEmitter::kHintOptimizedAlign;

	X86Assembler compiler(&code);
	builder(std::ref(compiler));

	FT result;

	if (g_rt.add(&result, &code))
	{
		return nullptr;
	}

	return result;
};
