#include "render.h"
#include "../emucore.h"
#include "../input.h"
#include "AsmInterpreter.h"

using namespace asmjit;

#define DECLARE(...) decltype(__VA_ARGS__) __VA_ARGS__

DECLARE(asm_insts::all_ops) =
{
	{0x0000, 0x0000, true , &asm_insts::UNK}, // Fill all the table with UNK first
	{0xFFFF, 0x00E0, false, &asm_insts::CLS},
	{0xFFFF, 0x00EE, true , &asm_insts::RET},
	{0xFFFF, 0x00FA, false, &asm_insts::Compat},
	{0xFFFF, 0x00FB, false, &asm_insts::SCR},
	{0xFFFF, 0x00FC, false, &asm_insts::SCL},
	{0xFFFF, 0x00FE, false, &asm_insts::RESL},
	{0xFFFF, 0x00FF, false, &asm_insts::RESH},
	{0xF000, 0x1000, true , &asm_insts::JP},
	{0xF000, 0x2000, true , &asm_insts::CALL},
	{0xF000, 0x3000, true , &asm_insts::SEi},
	{0xF000, 0x4000, true , &asm_insts::SNEi},
	{0xF00F, 0x5000, true , &asm_insts::SE},
	{0xF000, 0x6000, false, &asm_insts::WRI},
	{0xF000, 0x7000, false, &asm_insts::ADDI},
	{0xF00F, 0x8000, false, &asm_insts::ASS},
	{0xF00F, 0x8001, false, &asm_insts::OR},
	{0xF00F, 0x8002, false, &asm_insts::AND},
	{0xF00F, 0x8003, false, &asm_insts::XOR},
	{0xF00F, 0x8004, false, &asm_insts::ADD},
	{0xF00F, 0x8005, false, &asm_insts::SUB},
	{0xF00F, 0x8006, false, &asm_insts::SHR},
	{0xF00F, 0x8007, false, &asm_insts::RSB},
	{0xF00F, 0x800E, false, &asm_insts::SHL},
	{0xF00F, 0x9000, true , &asm_insts::SNE},
	{0xF000, 0xA000, false, &asm_insts::SetIndex},
	{0xF000, 0xB000, true , &asm_insts::JPr},
	{0xF000, 0xC000, false, &asm_insts::RND},
	{0xF000, 0xD000, false, &asm_insts::DRW},
	{0xF00F, 0xD000, false, &asm_insts::XDRW},
	{0xF0FF, 0xE09E, true , &asm_insts::SKP},
	{0xF0FF, 0xE0A1, true , &asm_insts::SKNP},
	{0xF0FF, 0xF007, false, &asm_insts::GetD},
	{0xF0FF, 0xF00A, false, &asm_insts::GetK},
	{0xF0FF, 0xF015, false, &asm_insts::SetD},
	{0xF0FF, 0xF018, false, &asm_insts::SetS},
	{0xF0FF, 0xF01E, false, &asm_insts::AddIndex},
	{0xF0FF, 0xF029, false, &asm_insts::SetCh},
	{0xF0FF, 0xF033, false, &asm_insts::STD},
	{0xF0FF, 0xF055, false, &asm_insts::STR},
	{0xF0FF, 0xF065, false, &asm_insts::LDR},
	//{0xF0FF, 0xF075, false, &asm_insts::FSAVE},
	//{0xF0FF, 0xF085, false, &asm_insts::FRESTORE},
	{0xFFFF, 0xFFFF, true , &asm_insts::guard}
};

// Shared instruction handlers opcodes (TODO: use more opcodes?)
enum s_ops : uptr
{
	CLS = 0x00E0u,
	UNK = 0xFFFFu,
};

//
static const X86Gp& state = x86::rcx;
static const X86Gp& opcode = x86::rdx;
static const X86Gp& pc = x86::rbp;

// Function arguments on x86-64 Windows
static std::array<const X86Gp, 4> args = 
{
	x86::rcx,
	x86::rdx,
	x86::r8,
	x86::r9
};

// Default return register on x86-64 Windows
static const X86Gp& retn = x86::rax;

// Temporaries
//std::array<X86Gp, 7> tr = 
//{
//	x86::r8,
//	x86::r9,
//	x86::r10,
//	x86::r11,
//	x86::r12, // non-volatile 
//	x86::r13, // non-volatile
//	x86::r14  // non-volatile
//};

// Optional code emitting after the end of the current instruction
static std::function<void(X86Assembler&)> from_end{};

#define STATE_OFFS(member) ::offset32(&emu_state::member)
#define STACK_RESERVE (0x28)

// Addressing helpers:
// Get offset shift by type (size must be 1, 2, 4, or 8)
#define GET_SHIFT(x) (::flog2<sizeof(x)>())
#define GET_ELEM_SIZE(x) sizeof(std::remove_extent_t<decltype(x)>)
#define GET_SIZE_MEM(x) GET_ELEM_SIZE(emu_state::##x)
#define GET_SHIFT_ARR(x) (::flog2<GET_ELEM_SIZE(x)>())
#define GET_SHIFT_MEMBER(x) (GET_SHIFT_ARR(emu_state::##x)) 
#define ARR_SUBSCRIPT(x) GET_SHIFT_ARR(emu_state::##x), STATE_OFFS(x)
#define lea_ptr x86::qword_ptr
//#define get_u256 x86::yword_ptr
//#define get_u128 x86::oword_ptr
//#define get_u64 x86::qword_ptr
//#define get_u32 x86::dword_ptr
//#define get_u16 x86::word_ptr
//#define get_u8 x86::byte_ptr

// TODO (add a setting for it)
static const bool g_sleep_supported = true;

template <u32 _index, bool is_be = false>
static void getField(X86Assembler& c, const X86Gp& reg, const X86Gp& opr = opcode)
{
	// Byteswap fields if specified
	constexpr u32 index = _index ^ (is_be ? 2 : 0);

	// Optimize if self modify
	if (reg != opr)
	{
		c.mov(reg.r32(), opr.r32());
	}

	if constexpr (index != 0)
	{
		c.shr(reg.r32(), index * 4);
	}

	if constexpr (index != 3)
	{
		c.and_(reg.r32(), 0xF);
	}
};

// Fallback to cpp interpreter for debugging
static void fallback(X86Assembler& c)
{
	// Wrapper to member function
	static const auto call_interpreter = [](emu_state* _state)
	{
		_state->OpcodeFallback();
	};

	c.mov(x86::dword_ptr(state, STATE_OFFS(pc)), pc.r32());
	c.call(imm_ptr<void(*)(emu_state*)>(call_interpreter));
	c.mov(pc.r32(), x86::dword_ptr(state, STATE_OFFS(pc)));
}

// Try to emit a loop instruction
// loop instruction uses Imm8 for relative so it may fail if distance is too long
// If that happens use dec ecx + jne instead
static void try_loop(X86Assembler& c, Label& label)
{
	if (c.getLastError() == ErrorCode::kErrorOk
		&& c.loop(label) != ErrorCode::kErrorOk)
	{
		c.resetLastError(); // Must reset
		c.dec(x86::ecx);
		c.jne(label);
	}
}

static asmjit::X86Mem refVF()
{
	// The 15 register
	return x86::byte_ptr(state, STATE_OFFS(gpr) + 0xf);
};

template <typename F>
static asm_insts::func_t build_instruction(const F& func, const bool jump)
{
	return build_function_asm<asm_insts::func_t>([&](X86Assembler& c)
	{
		std::invoke(func, std::ref(c));

		if (!jump)
		{
			c.add(pc.r32(), 2);
		}

		if (g_sleep_supported)
		{
			c.mov(args[0], 1);
			c.call(imm_ptr(&::Sleep));
			c.mov(state, imm_ptr(&g_state));
		}

		if (::has_movbe())
		{
			if (g_sleep_supported)
			{
				// Clear upper bits of the register in case changed by Sleep
				c.movzx(args[1].r32(), args[1].r8());
			}

			c.movbe(args[1].r16(), x86::word_ptr(state, pc, 0, STATE_OFFS(memBase)));
		}
		else
		{
			c.movzx(args[1].r32(), x86::word_ptr(state, pc, 0, STATE_OFFS(memBase)));
			c.xchg(x86::dl, x86::dh); // Byteswap
		}

		// Jumptable
		c.jmp(x86::qword_ptr(state, args[1], ARR_SUBSCRIPT(ops)));

		// Emit optional code
		if (auto builder = std::move(from_end))
		{
			builder(std::ref(c));
		}

		// Verify success
		assert(c.getLastError() == ErrorCode::kErrorOk);
	});
}

void asm_insts::build_all(std::uintptr_t* table)
{
	for (const auto& entry : all_ops)
	{
		// Compile the instruction using the builder
		const std::remove_pointer_t<decltype(table)> func_ptr = build_instruction(entry.builder, entry.is_jump);

		// Get instruction pattern mask and pattern opcode 
		const u32 imask = u32(entry.mask);
		const u32 icode = u32(entry.opcode);

		// Scalers for each field
		static const auto m3 = [](const u32& val) -> u32 { return val * 0x1000; };
		static const auto m2 = [](const u32& val) -> u32 { return val * 0x100; };
		static const auto m1 = [](const u32& val) -> u32 { return val * 0x10; };
		static const auto m0 = [](const u32& val) -> u32 { return val * 0x1; };

		// Go through all possible opcodes which this instruction may fit in
		for (u32 op = (icode & imask); op < UINT16_MAX + 1;)
		{
			// Test if opocde is within specified bounds for each field
			if ((imask & m3(0xF)) && ((icode ^ op) & m3(0xF)) != 0)
			{
				// Fast skip until we get to the field we want
				op += m3(0x1);
				continue;
			}

			if ((imask & m2(0xF)) && ((icode ^ op) & m2(0xF)) != 0)
			{
				op += m2(0x1);
				continue;
			}

			if ((imask & m1(0xF)) && ((icode ^ op) & m1(0xF)) != 0)
			{
				op += m1(0x1);
				continue;
			}

			if ((imask & m0(0xF)) && ((icode ^ op) & m0(0xF)) != 0)
			{
				op += m0(0x1);
				continue;
			}

			table[op] = func_ptr;
			op += m0(0x1);
		}
	}

	// Build actual entry
	entry = build_entry();
}

decltype(asm_insts::entry) asm_insts::build_entry()
{
	return build_function_asm<decltype(asm_insts::entry)>([](X86Assembler& c)
	{
		Label is_exit = c.newLabel();
		c.mov(args[0], imm_ptr(&g_state));
		c.cmp(x86::byte_ptr(args[0], STATE_OFFS(emu_started)), (u8)true);
		c.je(is_exit);

		c.mov(x86::byte_ptr(args[0], STATE_OFFS(emu_started)), (u8)true);
		c.mov(x86::qword_ptr(x86::rsp, 0x8), pc); // Save non-volatile register on home-space
		c.mov(x86::qword_ptr(x86::rsp, 0x10), x86::r12);
		c.mov(x86::qword_ptr(x86::rsp, 0x18), x86::r13);
		c.mov(x86::qword_ptr(x86::rsp, 0x20), x86::r14);
		c.push(x86::r15);
		c.push(x86::rsi);
		c.push(x86::rdi);
		c.push(x86::rbx);
		c.sub(x86::rsp, STACK_RESERVE); // Allocate min stack frame
		c.mov(state, imm_ptr(&g_state));
		c.mov(pc.r32(), x86::dword_ptr(state, STATE_OFFS(pc))); // Load pc
		c.movzx(args[1].r32(), x86::word_ptr(state, pc, 0, STATE_OFFS(memBase)));
		c.xchg(x86::dl, x86::dh); // Byteswap
		c.jmp(x86::qword_ptr(state, args[1], ARR_SUBSCRIPT(ops)));

		c.bind(is_exit);
		c.mov(x86::dword_ptr(state, STATE_OFFS(pc)), pc.r32());
		c.add(x86::rsp, STACK_RESERVE);
		c.pop(x86::rbx);
		c.pop(x86::rdi);
		c.pop(x86::rsi);
		c.pop(x86::r15);
		c.mov(pc, x86::qword_ptr(x86::rsp, 0x8));
		c.mov(x86::r12, x86::qword_ptr(x86::rsp, 0x10));
		c.mov(x86::r13, x86::qword_ptr(x86::rsp, 0x18));
		c.mov(x86::r14, x86::qword_ptr(x86::rsp, 0x20));
		c.ret();
	});
}

void asm_insts::CLS(X86Assembler& c)
{
	Label extended_mode = c.newLabel();
	Label end = c.newLabel();

	if (::has_avx())
	{
		// Try to use AVX if possible
		c.vxorps(x86::ymm0, x86::ymm0, x86::ymm0);
		c.vmovaps(x86::ymm1, x86::ymm0);
	}
	else
	{
		c.xorps(x86::xmm0, x86::xmm0);
		c.movaps(x86::xmm1, x86::xmm0);
	}

	c.lea(x86::r9, lea_ptr(state, STATE_OFFS(gfxMemory)));

	if (g_state.is_super)
	{
		c.cmp(x86::byte_ptr(state, STATE_OFFS(extended)), 0);
		c.jne(extended_mode);
	}

	// Simply generate the two modes handlers at once
	for (size_t extended = 0, y_size = emu_state::y_size;; extended = 1, y_size *= 2)
	{
		Label loop_ = c.newLabel();

		c.mov(x86::ecx, (emu_state::y_stride * y_size) / 256);
		c.bind(loop_);

		// Use out-of-order execution by using more than one register
		for (u32 i = 0; i < u32(!extended ? 4 : 2); i++)
		{
			if (::has_avx())
			{
				c.vmovaps(x86::yword_ptr(x86::r9, i * emu_state::y_stride + 0), x86::ymm0);
				c.vmovaps(x86::yword_ptr(x86::r9, i * emu_state::y_stride + 32), x86::ymm1);

				if (extended)
				{
					c.vmovaps(x86::yword_ptr(x86::r9, i * emu_state::y_stride + 64), x86::ymm0);
					c.vmovaps(x86::yword_ptr(x86::r9, i * emu_state::y_stride + 96), x86::ymm1);
				}
			}
			else
			{
				c.movaps(x86::oword_ptr(x86::r9, i * emu_state::y_stride + 0), x86::xmm0);
				c.movaps(x86::oword_ptr(x86::r9, i * emu_state::y_stride + 16), x86::xmm1);
				c.movaps(x86::oword_ptr(x86::r9, i * emu_state::y_stride + 32), x86::xmm0);
				c.movaps(x86::oword_ptr(x86::r9, i * emu_state::y_stride + 48), x86::xmm1);

				if (extended)
				{
					c.movaps(x86::oword_ptr(x86::r9, i * emu_state::y_stride + 64), x86::xmm0);
					c.movaps(x86::oword_ptr(x86::r9, i * emu_state::y_stride + 80), x86::xmm1);
					c.movaps(x86::oword_ptr(x86::r9, i * emu_state::y_stride + 96), x86::xmm0);
					c.movaps(x86::oword_ptr(x86::r9, i * emu_state::y_stride + 112), x86::xmm1);
				}
			}
		}

		c.add(x86::r9, 256); // Fill 256 bytes each loop
		try_loop(c, loop_);

		c.mov(args[0], (std::uintptr_t)&g_state + (u64)STATE_OFFS(gfxMemory)); // Set gfxMemory* as argument
		c.call(imm_ptr(std::addressof(extended ? ::KickSChip8Framebuffer : ::KickChip8Framebuffer)));

		if (extended != 0 || !g_state.is_super)
		{
			// Don't generate extended mode handler for non-super
			break;
		}

		// Bind extended mode label at the middle, skip if non-extended
		c.jmp(end);
		c.align(kAlignData, 16);
		c.bind(extended_mode);
	}

	c.bind(end);
	c.mov(state, imm_ptr(&g_state));
}

void asm_insts::RET(X86Assembler& c)
{
	c.mov(x86::r8d, x86::dword_ptr(state, STATE_OFFS(sp)));
	c.sub(x86::r8d, 1);

	// Check stack underflow
	Label ok = c.newLabel();
	c.jns(ok);
	c.mov(x86::r8, imm_ptr("RET stack underflow"));
	c.mov(x86::qword_ptr(state, STATE_OFFS(last_error)), x86::r8);
	c.mov(x86::r8, imm_ptr(&asm_insts::entry));
	c.mov(x86::r8, x86::qword_ptr(x86::r8));
	c.jmp(x86::r8);;
	c.bind(ok);

	c.mov(x86::dword_ptr(state, STATE_OFFS(sp)), x86::r8d);
	c.mov(pc.r32(), x86::dword_ptr(state, x86::r8, ARR_SUBSCRIPT(stack)));
}

void asm_insts::Compat(X86Assembler& c)
{
	if (!g_state.is_super)
	{
		c.jmp(x86::qword_ptr(state, STATE_OFFS(ops) + s_ops::UNK * GET_SIZE_MEM(ops)));
	}

	c.mov(x86::byte_ptr(state, STATE_OFFS(compatibilty)), 0u - 1u);
}

// Builder for SCR and SCL
template<bool is_SCR>
static void form_SCRL(X86Assembler& c)
{
	if (!g_state.is_super)
	{
		c.jmp(x86::qword_ptr(state, STATE_OFFS(ops) + s_ops::UNK * GET_SIZE_MEM(ops)));
	}

	Label extended_mode = c.newLabel();
	Label end = c.newLabel();

	const X86Gp& source = is_SCR ? x86::rsi : x86::rdi;
	const X86Gp& dest = is_SCR ? x86::rdi : x86::rsi;
	c.lea(source, lea_ptr(state, STATE_OFFS(gfxMemory)));
	c.lea(dest, lea_ptr(state, STATE_OFFS(gfxMemory) + sizeof(u32))); // Offset 4 pixels destintion
	c.cmp(x86::byte_ptr(state, STATE_OFFS(extended)), (u8)true);
	c.je(extended_mode);

	// Simply generate the two modes handlers at once
	for (size_t extended = 0, y_size = emu_state::y_size, x_size = emu_state::x_size;; extended = 1, y_size *= 2, x_size *= 2)
	{
		Label loop_ = c.newLabel();

		c.mov(x86::r8d, y_size);
		c.bind(loop_);
		c.mov(x86::ecx, (x_size - 4) / sizeof(u32)); // move bytes in dwords
		c.rep().movsd();
		c.mov(x86::dword_ptr(dest, is_SCR ? 0 - s32(y_size) : s32(-4)), 0); // Place zeroes on the edge of the screen
		c.add(source, emu_state::y_stride - (x_size - 4));
		c.add(dest, emu_state::y_stride - (x_size - 4));
		c.dec(x86::r8d);
		c.jne(loop_);

		c.mov(args[0], (std::uintptr_t)&g_state + (u64)STATE_OFFS(gfxMemory)); // Set gfxMemory* as argument
		c.call(imm_ptr(std::addressof(extended ? ::KickSChip8Framebuffer : ::KickChip8Framebuffer)));

		if (extended != 0)
		{
			break;
		}

		// Bind extended mode label
		c.jmp(end);
		c.align(kAlignCode, 16);
		c.bind(extended_mode);
	}

	c.bind(end);
	c.mov(state, imm_ptr(&g_state));
}

void asm_insts::SCR(X86Assembler& c)
{
	form_SCRL<true>(c);
}
void asm_insts::SCL(X86Assembler& c)
{
	form_SCRL<false>(c);
}

void asm_insts::RESL(X86Assembler& c)
{
	if (!g_state.is_super)
	{
		c.jmp(x86::qword_ptr(state, STATE_OFFS(ops) + s_ops::UNK * GET_SIZE_MEM(ops)));
	}

	c.mov(x86::byte_ptr(state, STATE_OFFS(extended)), (u8)false);

	// TODO: is the screen cleared even when resolution didnt change?
	c.jmp(x86::qword_ptr(state, STATE_OFFS(ops) + s_ops::CLS * GET_SIZE_MEM(ops)));
}

void asm_insts::RESH(X86Assembler& c)
{
	if (!g_state.is_super)
	{
		c.jmp(x86::qword_ptr(state, STATE_OFFS(ops) + s_ops::UNK * GET_SIZE_MEM(ops)));
	}

	c.mov(x86::byte_ptr(state, STATE_OFFS(extended)), (u8)true);

	// TODO: is the screen cleared even when resolution didnt change?
	c.jmp(x86::qword_ptr(state, STATE_OFFS(ops) + s_ops::CLS * GET_SIZE_MEM(ops)));
}

void asm_insts::JP(X86Assembler& c)
{
	c.and_(opcode.r32(), 0xFFF); // Extract addr
	c.mov(pc.r32(), opcode.r32());
}

void asm_insts::CALL(X86Assembler& c)
{
	c.and_(opcode.r32(), 0xFFF); // Extract addr
	c.mov(x86::r8d, x86::dword_ptr(state, STATE_OFFS(sp)));

	// Check stack overflow
	Label ok = c.newLabel();
	c.cmp(x86::r8d, 16);
	c.jne(ok);
	c.mov(x86::r8, imm_ptr("CALL stack overflow"));
	c.mov(x86::qword_ptr(state, STATE_OFFS(last_error)), x86::r8);
	c.mov(x86::r8, imm_ptr(&asm_insts::entry));
	c.mov(x86::r8, x86::qword_ptr(x86::r8));
	c.jmp(x86::r8);
	c.bind(ok);

	c.add(pc.r32(), 2);
	c.mov(x86::dword_ptr(state, x86::r8, ARR_SUBSCRIPT(stack)), pc.r32());
	c.inc(x86::r8d);
	c.mov(x86::dword_ptr(state, STATE_OFFS(sp)), x86::r8d);
	c.mov(pc.r32(), opcode.r32());
}

void asm_insts::SEi(X86Assembler& c)
{
	c.mov(x86::r8b, opcode.r8());
	getField<2>(c, opcode);
	c.cmp(x86::r8b, x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)));
	c.sete(x86::dl);
	c.lea(pc, lea_ptr(pc, x86::rdx, 1, 2));
}

void asm_insts::SNEi(X86Assembler& c)
{
	c.mov(x86::r8b, opcode.r8());
	getField<2>(c, opcode);
	c.cmp(x86::r8b, x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)));
	c.setne(x86::dl);
	c.lea(pc, lea_ptr(pc, x86::rdx, 1, 2));
}

void asm_insts::SE(X86Assembler& c)
{
	getField<1>(c, x86::r8);
	getField<2>(c, opcode);
	c.mov(x86::r8b, x86::byte_ptr(state, x86::r8, 0, STATE_OFFS(gpr)));
	c.cmp(x86::r8b, x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)));
	c.sete(x86::dl);
	c.lea(pc, lea_ptr(pc, x86::rdx, 1, 2)); // pc += (equal ? 2 : 0) + 2
}

void asm_insts::WRI(X86Assembler& c)
{
	getField<2>(c, x86::r8);
	c.mov(x86::byte_ptr(state, x86::r8, 0, STATE_OFFS(gpr)), opcode.r8());
}

void asm_insts::ADDI(X86Assembler& c)
{
	getField<2>(c, x86::r8);
	c.add(x86::byte_ptr(state, x86::r8, 0, STATE_OFFS(gpr)), opcode.r8());
}

void asm_insts::ASS(X86Assembler& c)
{
	getField<1>(c, x86::r8);
	getField<2>(c, opcode);
	c.mov(x86::r8b, x86::byte_ptr(state, x86::r8, 0, STATE_OFFS(gpr)));
	c.mov(x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)), x86::r8b);
}

void asm_insts::OR(X86Assembler& c)
{
	getField<1>(c, x86::r8);
	getField<2>(c, opcode);
	c.mov(x86::r8b, x86::byte_ptr(state, x86::r8, 0, STATE_OFFS(gpr)));
	c.or_(x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)), x86::r8b);
}

void asm_insts::AND(X86Assembler& c)
{
	getField<1>(c, x86::r8);
	getField<2>(c, opcode);
	c.mov(x86::r8b, x86::byte_ptr(state, x86::r8, 0, STATE_OFFS(gpr)));
	c.and_(x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)), x86::r8b);
}

void asm_insts::XOR(X86Assembler& c)
{
	getField<1>(c, x86::r8);
	getField<2>(c, opcode);
	c.mov(x86::r8b, x86::byte_ptr(state, x86::r8, 0, STATE_OFFS(gpr)));
	c.xor_(x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)), x86::r8b);
}

void asm_insts::ADD(X86Assembler& c)
{
	getField<2>(c, x86::r8);
	getField<1>(c, opcode);
	c.mov(x86::r8b, x86::byte_ptr(state, x86::r8, 0, STATE_OFFS(gpr)));
	c.movzx(x86::r9d, x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)));
	c.add(x86::r8d, x86::r9d);
	c.mov(x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)), x86::r8b);
	c.shr(x86::r8d, 8);
	c.mov(refVF(), x86::r8b);
}

void asm_insts::SUB(X86Assembler& c)
{
	getField<1>(c, x86::r8);
	getField<2>(c, opcode);
	c.mov(x86::r8b, x86::byte_ptr(state, x86::r8, 0, STATE_OFFS(gpr)));
	c.movzx(x86::r9d, x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)));
	c.sub(x86::r8d, x86::r9d);
	c.setns(refVF()); // TODO: Check order
	c.mov(x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)), x86::r8b);
}

void asm_insts::SHR(X86Assembler& c)
{
	getField<2>(c, opcode);
	c.shr(x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)), 1);
	c.setc(refVF());
}

void asm_insts::RSB(X86Assembler& c)
{
	getField<1>(c, x86::r8);
	getField<2>(c, opcode);
	c.mov(x86::r8b, x86::byte_ptr(state, x86::r8, 0, STATE_OFFS(gpr)));
	c.movzx(x86::r9d, x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)));
	c.sub(x86::r9d, x86::r8d);
	c.setns(refVF()); // TODO: Check order
	c.mov(x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)), x86::r9b);
}

void asm_insts::SHL(X86Assembler& c)
{
	getField<2>(c, opcode);
	c.shl(x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)), 1);
	c.setc(refVF());
}

void asm_insts::SNE(X86Assembler& c)
{
	getField<1>(c, x86::r8);
	getField<2>(c, opcode);
	c.mov(x86::r8b, x86::byte_ptr(state, x86::r8, 0, STATE_OFFS(gpr)));
	c.cmp(x86::r8b, x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)));
	c.setne(x86::dl);
	c.lea(pc, lea_ptr(pc, x86::rdx, 1, 2)); // pc += (nequal ? 2 : 0) + 2
}

void asm_insts::SetIndex(X86Assembler& c)
{
	c.and_(opcode.r32(), 0xFFF); // Extract index
	c.mov(x86::dword_ptr(state, STATE_OFFS(index)), opcode.r32());
}

void asm_insts::JPr(X86Assembler& c)
{
	c.and_(opcode.r32(), 0xFFF);
	c.movzx(x86::r8d, x86::byte_ptr(state, STATE_OFFS(gpr) + 0));
	c.lea(pc.r32(), lea_ptr(opcode, x86::r8d));
}

void asm_insts::RND(X86Assembler& c)
{
	c.mov(x86::r8d, opcode.r32()); // Save rdx
	c.rdtsc();
	c.and_(x86::al, x86::r8b); // Mask timestamp
	getField<2>(c, x86::r8, x86::r8);
	c.mov(x86::byte_ptr(state, x86::r8, 0, STATE_OFFS(gpr)), x86::al);
}

template<bool is_XDRW>
static void form_DRW(X86Assembler& c)
{
	Label main_loop = c.newLabel();
	Label skip_size0 = c.newLabel();
	Label table_lookup = c.newLabel();
	Label needs_wrapping = c.newLabel();

	// Ram pointer
	c.mov(x86::r8d, x86::dword_ptr(state, STATE_OFFS(index)));
	c.lea(x86::r8, lea_ptr(state, x86::r8, 0, STATE_OFFS(memBase)));

	const bool is_super = g_state.is_super;
	if (is_super)
	{
		// Load is_extended value
		c.movzx(x86::ebx, x86::byte_ptr(state, STATE_OFFS(extended)));
		c.mov(x86::r11b, x86::bl);
		c.shl(x86::r11b, clog2<0x3f>()); // Shift it to the bit exactly after the end of 0x1f
		c.or_(x86::r11b, 0x3f); // Now we OR it (to get 0x7f on extended mode)
	}

	getField<2>(c, x86::r10);
	c.mov(x86::r10b, x86::byte_ptr(state, x86::r10, 0, STATE_OFFS(gpr)));

	// Apply x mask
	if (is_super) 
	{
		c.and_(x86::r10d, x86::r11d);
		c.shr(x86::r11d, 1); // In all modes x lines is twice as much as y lines
	}
	else
	{
		c.and_(x86::r10b, 0x3f);
	}

	getField<1>(c, x86::r9);
	c.mov(x86::r9b, x86::byte_ptr(state, x86::r9, 0, STATE_OFFS(gpr)));

	if (is_super) // Apply y mask
		c.and_(x86::r9d, x86::r11d);
	else
		c.and_(x86::r9d, 0x1f);

	// Constant value unchanged with modes
	c.shl(x86::r9d, flog2<emu_state::y_stride>());

	// Vram offset
	c.add(x86::r9d, x86::r10d);

	// VF setup
	c.xor_(x86::r11d, x86::r11d);
	c.mov(x86::r14d, 1);

	// Get lines amount
	if (!is_XDRW)
	{
		getField<0>(c, opcode);
		c.je(skip_size0); // Flags set at getField
	}
	else
	{
		c.mov(opcode.r32(), 16);
	}

	c.mov(x86::r12d, opcode.r32());
	c.shl(x86::r12d, flog2<emu_state::y_stride>());
	c.lea(x86::r12, lea_ptr(x86::r9, x86::r12, 0, !is_XDRW ? 7 : 15));

	if (is_super)
	{
		c.mov(x86::r15d, x86::ebx);
		c.neg(x86::r15d); // = extended ? 0xFFFFFFFF : 0
		c.and_(x86::r15d, (~(u32)emu_state::xy_mask) & emu_state::xy_mask_ex); // Get different bits in mask between modes and mask them
		c.or_(x86::r15d, emu_state::xy_mask); // Get the actual mask
		c.not_(x86::r15d);
		c.test(x86::r12d, x86::r15d);
	}
	else
	{
		c.test(x86::r12d, ~((u32)emu_state::xy_mask));
	}

	c.jne(needs_wrapping);

	// Get max ram address
	c.lea(x86::rdx, lea_ptr(x86::r8, x86::rdx, is_XDRW ? 1 : 0));
	c.bind(main_loop);

	// XDRW consumes 2 bytes at a time
	for (u32 i = 0; i < (is_XDRW ? 2 : 1); i++)
	{
		// Load pixel value and decode it
		c.movzx(x86::r10d, x86::byte_ptr(x86::r8, i));
		c.mov(x86::r10, x86::qword_ptr(state, x86::r10, ARR_SUBSCRIPT(DRWtable)));

		// Load previous qword pixels state and test VF
		c.mov(x86::r12, x86::qword_ptr(state, x86::r9, 0, STATE_OFFS(gfxMemory) + i * sizeof(u64)));
		c.test(x86::r10, x86::r12);
		c.cmovne(x86::r11d, x86::r14d);
		c.xor_(x86::r10, x86::r12);
		c.mov(x86::qword_ptr(state, x86::r9, 0, STATE_OFFS(gfxMemory) + i * sizeof(u64)), x86::r10);
	}

	!is_XDRW ? c.inc(x86::r8) : c.lea(x86::r8, lea_ptr(x86::r8, 2));

	c.add(x86::r9, emu_state::y_stride);
	c.cmp(x86::r8, x86::rdx);
	c.jne(main_loop);

	c.bind(skip_size0);
	c.mov(refVF(), x86::r11b);
	c.lea(args[0], lea_ptr(state, STATE_OFFS(gfxMemory)));

	if (is_super)
	{
		c.mov(opcode, imm_ptr(&::KickChip8Framebuffer));
		c.test(x86::bl, 0x1);
		c.mov(x86::rbx, imm_ptr(&::KickSChip8Framebuffer));
		c.cmovne(opcode, x86::rbx);
		c.call(opcode);
	}
	else
	{
		c.call(imm_ptr(&::KickChip8Framebuffer));
	}

	c.mov(state, imm_ptr(&g_state));

	// Specilization for wrapping (slow but accurate)
	from_end = [=](X86Assembler& c)
	{
		Label wrap_loop = c.newLabel();
		Label wrap_skip = c.newLabel();
		c.align(kAlignCode, 16);
		c.bind(needs_wrapping);

		// Get max ram address
		c.lea(x86::rdx, lea_ptr(x86::r8, x86::rdx));
		c.bind(wrap_loop);

		for (u32 p = 0; p < (is_XDRW ? 2 : 1); p++)
		{
			// Load pixel value
			c.movzx(x86::r10d, x86::byte_ptr(x86::r8, p));
			//c.test(x86::r10d, x86::r10d);
			//c.je(wrap_skip);

			// Temp offset for clamping
			c.mov(x86::r12d, x86::r9d);

			// Unpack bits into pixels (unrolled loop)
			for (u32 i = 0; i < 8; i++)
			{
				Label next = c.newLabel();
				c.bt(x86::r10d, 7 - i);
				c.jnc(next);

				if (is_super)
				{
					if (g_state.DRW_wrapping)
					{
						c.and_(x86::r12d, x86::r15d);
					}
					else
					{
						c.test(x86::r12d, x86::r15d);
						c.jne(next);
					}

				}
				else
				{
					if (g_state.DRW_wrapping)
					{
						c.and_(x86::r12d, emu_state::xy_mask);
					}
					else
					{
						c.test(x86::r12d, ~((u32)emu_state::xy_mask));
						c.jne(next);
					}
				}

				c.xor_(x86::byte_ptr(state, x86::r12, 0, STATE_OFFS(gfxMemory) + p * sizeof(u64)), 0xff);
				c.cmove(x86::r11d, x86::r14d); // Set VF
				c.bind(next);
				c.inc(x86::r12d);
			}
		}

		c.bind(wrap_skip);
		!is_XDRW ? c.inc(x86::r8) : c.lea(x86::r8, lea_ptr(x86::r8, 2));
		c.add(x86::r9, emu_state::y_stride);
		c.cmp(x86::r8, x86::rdx);
		c.jne(wrap_loop);
		c.jmp(skip_size0); // Return to normal instruction epilouge 
	};
}

void asm_insts::DRW(X86Assembler& c)
{
	form_DRW<false>(c);
}

void asm_insts::XDRW(X86Assembler& c)
{
	if (!g_state.is_super)
	{
		// ???
		c.jmp(x86::qword_ptr(state, STATE_OFFS(ops) + s_ops::UNK * GET_SIZE_MEM(ops)));
	}

	form_DRW<true>(c);
}

void asm_insts::SKP(X86Assembler& c)
{
	getField<2>(c, opcode);
	c.mov(x86::dl, x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)));
	c.and_(x86::dl, 0xf);
	c.mov(x86::r8, imm_ptr(&input::keyIDs));
	c.movzx(args[0].r32(), x86::byte_ptr(x86::r8, x86::rdx, GET_SHIFT_ARR(input::keyIDs)));
	c.call(imm_ptr(&::GetKeyState));
	c.mov(state, imm_ptr(&g_state));
	c.shr(retn.r16(), 16 - 1); // If pressed, contains 1 otherwise 0
	c.movzx(retn.r32(), retn.r8());
	c.lea(pc, lea_ptr(pc, retn, 1, 2));
}

void asm_insts::SKNP(X86Assembler& c)
{
	getField<2>(c, opcode);
	c.mov(x86::dl, x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)));
	c.and_(x86::dl, 0xf);
	c.mov(x86::r8, imm_ptr(&input::keyIDs));
	c.movzx(args[0].r32(), x86::byte_ptr(x86::r8, x86::rdx, GET_SHIFT_ARR(input::keyIDs)));
	c.call(imm_ptr(&::GetKeyState));
	c.mov(state, imm_ptr(&g_state));
	c.shr(retn.r16(), 16 - 1);
	c.xor_(retn.r8(), 1);
	c.movzx(retn.r32(), retn.r8());
	c.lea(pc, lea_ptr(pc, retn, 1, 2));
}

void asm_insts::GetD(X86Assembler& c)
{
	c.mov(x86::r8b, x86::byte_ptr(state, STATE_OFFS(timers) + ::offset32(&decltype(emu_state::timers)::delay)));
	getField<2>(c, opcode);
	c.mov(x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)), x86::r8b);
}

void asm_insts::GetK(X86Assembler& c)
{
	c.mov(x86::dword_ptr(state, STATE_OFFS(pc)), pc.r32());
	c.mov(pc, opcode); // Save rdx
	c.call(imm_ptr(&input::WaitForPress));
	c.mov(state, imm_ptr(&g_state));
	getField<2>(c, pc, pc);
	c.mov(x86::byte_ptr(state, pc, 0, STATE_OFFS(gpr)), retn.r8());
	c.mov(pc.r32(), x86::dword_ptr(state, STATE_OFFS(pc)));
}

void asm_insts::SetD(X86Assembler& c)
{
	getField<2>(c, opcode);
	c.mov(x86::r8b, x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)));
	c.mov(x86::byte_ptr(state, STATE_OFFS(timers) + ::offset32(&decltype(emu_state::timers)::delay)), x86::r8b);
}

void asm_insts::SetS(X86Assembler& c)
{
	getField<2>(c, opcode);
	c.mov(x86::r8b, x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)));
	c.mov(x86::byte_ptr(state, STATE_OFFS(timers) + ::offset32(&decltype(emu_state::timers)::sound)), x86::r8b);
}

void asm_insts::AddIndex(X86Assembler& c)
{
	getField<2>(c, opcode);
	c.mov(x86::dl, x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)));
	c.add(x86::edx, x86::dword_ptr(state, STATE_OFFS(index)));
	c.mov(x86::dword_ptr(state, STATE_OFFS(index)), x86::edx);
}

void asm_insts::SetCh(X86Assembler& c)
{
	getField<2>(c, opcode);
	c.mov(x86::dl, x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)));
	c.and_(x86::dl, 0xf);
	c.mov(x86::eax, 5);
	c.mul(x86::edx); // This can also be done with: index = (gpr << 2) + gpr
	c.mov(x86::dword_ptr(state, STATE_OFFS(index)), x86::eax);
}

void asm_insts::STD(X86Assembler& c)
{
	// div instruction produces both quotient and reminder
	// Let's make a good use of it
	getField<2>(c, opcode);
	c.movzx(x86::eax, x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)));
	c.mov(x86::dl, 100);
	c.div(x86::dl);
	c.mov(x86::r8d, x86::dword_ptr(state, STATE_OFFS(index)));
	c.lea(x86::r8, lea_ptr(state, x86::r8, 0, STATE_OFFS(memBase)));
	c.mov(x86::byte_ptr(x86::r8, 0), x86::al);
	c.shr(x86::eax, 8); // Move back reminder
	c.mov(x86::dl, 10);
	c.div(x86::dl);
	c.mov(x86::byte_ptr(x86::r8, 1), x86::al);
	c.shr(x86::eax, 8);
	c.mov(x86::byte_ptr(x86::r8, 2), x86::al);
}

void asm_insts::STR(X86Assembler& c)
{
	getField<2>(c, opcode);
	c.inc(opcode.r8());
	c.mov(x86::r8, state); // Save state
	c.mov(x86::ecx, opcode.r32());
	c.mov(x86::edi, x86::dword_ptr(x86::r8, STATE_OFFS(index)));
	c.lea(x86::rdi, lea_ptr(x86::r8, x86::rdi, 0, STATE_OFFS(memBase)));
	c.lea(x86::rsi, lea_ptr(x86::r8, STATE_OFFS(gpr)));
	c.rep().movsb();
	c.mov(state, x86::r8);
	if (g_state.is_super)
		c.and_(opcode.r32(), x86::dword_ptr(state, STATE_OFFS(compatibilty))); // Zero out if compat flag is false
	c.add(x86::dword_ptr(state, STATE_OFFS(index)), opcode.r32());
}

void asm_insts::LDR(X86Assembler& c)
{
	getField<2>(c, opcode);
	c.inc(opcode.r8());
	c.mov(x86::r8, state);
	c.mov(x86::ecx, opcode.r32());
	c.mov(x86::esi, x86::dword_ptr(x86::r8, STATE_OFFS(index)));
	c.lea(x86::rsi, lea_ptr(x86::r8, x86::rsi, 0, STATE_OFFS(memBase)));
	c.lea(x86::rdi, lea_ptr(x86::r8, STATE_OFFS(gpr)));
	c.rep().movsb();
	c.mov(state, x86::r8);
	if (g_state.is_super)
		c.and_(opcode.r32(), x86::dword_ptr(state, STATE_OFFS(compatibilty))); // Zero out if compat flag is false
	c.add(x86::dword_ptr(state, STATE_OFFS(index)), opcode.r32());
}

void asm_insts::UNK(X86Assembler& c)
{
	c.mov(x86::r8, imm_ptr("Unknown instruction"));
	c.mov(x86::qword_ptr(state, STATE_OFFS(last_error)), x86::r8);
	c.mov(x86::r8, imm_ptr(&asm_insts::entry));
	c.mov(x86::r8, x86::qword_ptr(x86::r8));
	c.jmp(x86::r8);
}

void asm_insts::guard(X86Assembler& c)
{
	UNK(c);
}

DECLARE(asm_insts::entry);

#undef lea_ptr
#undef DECLARE
#undef STATE_OFFS
#undef DEBUG_INSTS
#undef STACK_RESERVE
#undef GET_SHIFT
#undef GET_SHIFT_ARR
#undef GET_ARR_SIZE
#undef GET_SIZE_MEM
#undef GET_SHIFT_MEMBER
#undef ARR_SUBSCRIPT
