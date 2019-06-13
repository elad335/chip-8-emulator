#pragma once
#include "asmutils.h"
#include "../utils.h"

#include <initializer_list>

struct asm_insts
{
public:
	// Entry function
	static void(*entry)();
	static decltype(entry) build_entry();

	// Instruction builder type
	using build_t = void(asmjit::X86Assembler&);
	// Intruction function pointer type
	using func_t = std::uintptr_t;

	struct inst_entry
	{
		const u16 mask;
		const u16 opcode;
		const bool is_jump;
		std::add_pointer_t<build_t> builder;
	};

	static const std::initializer_list<inst_entry> all_ops;

	static void build_all(std::uintptr_t* table);

	static build_t RET;
	static build_t CLS;
	static build_t Compat;
	static build_t SCR;
	static build_t SCL;
	static build_t RESL;
	static build_t RESH;
	static build_t JP;
	static build_t CALL;
	static build_t SEi;
	static build_t SNEi;
	static build_t SE;
	static build_t WRI;
	static build_t ADDI;
	static build_t ASS;
	static build_t OR;
	static build_t AND;
	static build_t XOR;
	static build_t ADD;
	static build_t SUB;
	static build_t SHR;
	static build_t RSB;
	static build_t SHL;
	static build_t SNE;
	static build_t SetIndex;
	static build_t JPr;
	static build_t RND;
	static build_t DRW;
	static build_t XDRW;
	static build_t SKP;
	static build_t SKNP;
	static build_t GetD;
	static build_t GetK;
	static build_t SetD;
	static build_t SetS;
	static build_t AddIndex;
	static build_t SetCh;
	static build_t STD;
	static build_t STR;
	static build_t LDR;
	static build_t FSAVE;
	static build_t FRESTORE;
	static build_t UNK;
	static build_t guard;
};