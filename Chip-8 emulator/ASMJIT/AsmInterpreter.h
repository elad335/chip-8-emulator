#pragma once
#include "asmutils.h"
#include "../utils.h"
#include "../emucore.h"

struct asm_insts
{
public:
	friend struct emu_state_t;

	// Entry function
	static void(*entry)(emu_state_t* /*state*/);

	using func_t = void(*)(emu_state_t* /*state*/, u16 /*opcode*/);

	static func_t RET;
	static func_t CLS;
	static func_t JP;
	static func_t CALL;
	static func_t SEi;
	static func_t SNEi;
	static func_t SE;
	static func_t WRI;
	static func_t ADDI;
	static func_t ASS;
	static func_t OR;
	static func_t AND;
	static func_t XOR;
	static func_t ADD;
	static func_t SUB;
	static func_t SHR;
	static func_t RSB;
	static func_t SHL;
	static func_t SNE;
	static func_t SetIndex;
	static func_t JPr;
	static func_t RND;
	static func_t DRW;
	static func_t SKP;
	static func_t SKNP;
	static func_t GetD;
	static func_t GetK;
	static func_t SetD;
	static func_t SetS;
	static func_t AddIndex;
	static func_t SetCh;
	static func_t STD;
	static func_t STR;
	static func_t LDR;
	static func_t UNK;
	static func_t guard;
};

static_assert(sizeof(asm_insts::func_t) == 8);