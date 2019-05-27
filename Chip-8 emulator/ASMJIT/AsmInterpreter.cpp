#include "render.h"
#include "../emucore.h"
#include "../input.h"
#include "AsmInterpreter.h"

using namespace asmjit;

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
//std::array<X86Gp, 4> tr = 
//{
//	x86::r8,
//	x86::r9,
//	x86::r10,
//	x86::r11
//};

#define DECLARE(...) decltype(__VA_ARGS__) __VA_ARGS__
#define STATE_OFFS(member) ::offset32(&emu_state_t::member)
#define STACK_RESERVE (0x28)

// Addressing helpers:
// Get offset shift by type (size must be 1, 2, 4, or 8)
#define GET_SHIFT(x) (::flog2<u32, sizeof(x)>())
#define GET_SHIFT_ARR(x) (::flog2<u32, sizeof(std::remove_extent_t<decltype(x)>)>())
#define GET_SHIFT_MEMBER(x) (GET_SHIFT_ARR(emu_state_t::##x)) 
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
	if (reg != opcode)
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
	static const auto call_interpreter = [](emu_state_t* state)
	{
		state->OpcodeFallback();
	};

	c.mov(x86::dword_ptr(state, STATE_OFFS(pc)), pc.r32());
	c.call(imm_ptr<void(*)(emu_state_t*)>(call_interpreter));
	c.mov(pc.r32(), x86::dword_ptr(state, STATE_OFFS(pc)));
}

static asmjit::X86Mem refVF()
{
	// The 15 register
	return x86::byte_ptr(state, STATE_OFFS(gpr) + 0xf);
};

template <bool jump = false, typename F>
static asm_insts::func_t build_instruction(F&& func)
{
	return build_function_asm<asm_insts::func_t>([&](X86Assembler& c)
	{
		std::invoke(std::forward<F>(func), std::ref(c));

		if constexpr (!jump)
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

		c.jmp(x86::qword_ptr(state, args[1], GET_SHIFT_MEMBER(ops), STATE_OFFS(ops)));
	});
}

DECLARE(asm_insts::entry) = build_function_asm<decltype(asm_insts::entry)>([](X86Assembler& c)
{
	c.mov(x86::qword_ptr(x86::rsp, 0x8), pc); // Save non-volatile register at home-space
	c.sub(x86::rsp, STACK_RESERVE); // Allocate min stack frame
	c.mov(state, imm_ptr(&g_state));
	c.mov(pc.r32(), x86::dword_ptr(state, STATE_OFFS(pc))); // Load pc
	c.movzx(args[1].r32(), x86::word_ptr(state, pc, 0, STATE_OFFS(memBase)));
	c.xchg(x86::dl, x86::dh); // Byteswap
	c.jmp(x86::qword_ptr(state, args[1], GET_SHIFT_MEMBER(ops), STATE_OFFS(ops)));
});

DECLARE(asm_insts::RET) = build_instruction<true>([](X86Assembler& c)
{
	c.mov(x86::r8d, x86::dword_ptr(state, STATE_OFFS(sp)));
	c.sub(x86::r8d, 1);

#ifdef  DEBUG_INSTS
	Label ok = c.newLabel();
	c.jns(ok);
	c.jmp(imm_ptr(&asm_insts::UNK));
	c.bind(ok);
#endif

	c.mov(x86::dword_ptr(state, STATE_OFFS(sp)), x86::r8d);
	c.mov(pc.r32(), x86::dword_ptr(state, x86::r8, GET_SHIFT_MEMBER(stack), STATE_OFFS(stack)));
});

DECLARE(asm_insts::CLS) = build_instruction([](X86Assembler& c)
{
	Label _loop = c.newLabel();
	const u32 size_ = ::has_avx() ? 256 : 128; // Use AVX if possible

	if (size_ == 256)
	{
		c.xorps(x86::xmm0, x86::xmm0);
		c.movaps(x86::xmm1, x86::xmm0);
	}
	else
	{
		c.vxorps(x86::ymm0, x86::ymm0, x86::ymm0);
		c.vmovaps(x86::ymm1, x86::ymm0);
	}

	c.lea(x86::r9, x86::ptr(state, STATE_OFFS(gfxMemory)));
	c.mov(x86::ecx, sizeof(emu_state_t::gfxMemory) / size_);
	c.bind(_loop);

	// Use out-of-order execution by using more than one register
	for (u32 i = 0; i < 4; i++)
	{
		if (size_ == 256)
		{
			c.vmovaps(x86::yword_ptr(x86::r9, i * emu_state_t::y_shift + 0), x86::ymm0);
			c.vmovaps(x86::yword_ptr(x86::r9, i * emu_state_t::y_shift + 32), x86::ymm1);
		}
		else
		{
			c.movaps(x86::oword_ptr(x86::r9, (((i & 0xfe) << 1) | i & 1) * (emu_state_t::y_shift / 2) + 0), x86::xmm0);
			c.movaps(x86::oword_ptr(x86::r9, (((i & 0xfe) << 1) | i & 1) * (emu_state_t::y_shift / 2) + 16), x86::xmm1);
		}
	}

	c.add(x86::r9, size_);
	c.loop(_loop);
	c.mov(args[0], x86::r9); // Set gfxMemory* as argument
	c.call(imm_ptr(&::KickChip8Framebuffer));
	c.mov(state, imm_ptr(&g_state));
});

DECLARE(asm_insts::JP) = build_instruction<true>([](X86Assembler& c)
{
	c.and_(opcode.r32(), 0xFFF); // Extract addr
	c.mov(pc.r32(), opcode.r32());
});

DECLARE(asm_insts::CALL) = build_instruction<true>([](X86Assembler& c)
{
	c.and_(opcode.r32(), 0xFFF); // Extract addr
	c.add(pc.r32(), 2);
	c.mov(x86::r8d, x86::dword_ptr(state, STATE_OFFS(sp)));

#ifdef  DEBUG_INSTS
	Label ok = c.newLabel();
	c.cmp(x86::r8d, 16);
	c.jne(ok);
	c.jmp(imm_ptr(&asm_insts::UNK));
	c.bind(ok);
#endif

	c.mov(x86::dword_ptr(state, x86::r8, GET_SHIFT_MEMBER(stack), STATE_OFFS(stack)), pc.r32());
	c.inc(x86::r8d);
	c.mov(x86::dword_ptr(state, STATE_OFFS(sp)), x86::r8d);
	c.mov(pc.r32(), opcode.r32());
});

DECLARE(asm_insts::SEi) = build_instruction<true>([](X86Assembler& c)
{
	c.mov(x86::r8b, opcode.r8());
	getField<2>(c, opcode);
	c.cmp(x86::r8b, x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)));
	c.sete(x86::dl);
	c.lea(pc, x86::ptr(pc, x86::rdx, 1, 2));
});

DECLARE(asm_insts::SNEi) = build_instruction<true>([](X86Assembler& c)
{
	c.mov(x86::r8b, opcode.r8());
	getField<2>(c, opcode);
	c.cmp(x86::r8b, x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)));
	c.setne(x86::dl);
	c.lea(pc, x86::ptr(pc, x86::rdx, 1, 2));
});

DECLARE(asm_insts::SE) = build_instruction<true>([](X86Assembler& c)
{
	getField<1>(c, x86::r8);
	getField<2>(c, opcode);
	c.mov(x86::r8b, x86::byte_ptr(state, x86::r8, 0, STATE_OFFS(gpr)));
	c.cmp(x86::r8b, x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)));
	c.sete(x86::dl);
	c.lea(pc, x86::ptr(pc, x86::rdx, 1, 2)); // pc += (equal ? 2 : 0) + 2
});

DECLARE(asm_insts::WRI) = build_instruction([](X86Assembler& c)
{
	getField<2>(c, x86::r8);
	c.mov(x86::byte_ptr(state, x86::r8, 0, STATE_OFFS(gpr)), opcode.r8());
});

DECLARE(asm_insts::ADDI) = build_instruction([](X86Assembler& c)
{
	getField<2>(c, x86::r8);
	c.add(x86::byte_ptr(state, x86::r8, 0, STATE_OFFS(gpr)), opcode.r8());
});

DECLARE(asm_insts::ASS) = build_instruction([](X86Assembler& c)
{
	getField<1>(c, x86::r8);
	getField<2>(c, opcode);
	c.mov(x86::r8b, x86::byte_ptr(state, x86::r8, 0, STATE_OFFS(gpr)));
	c.mov(x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)), x86::r8b);
});

DECLARE(asm_insts::OR) = build_instruction([](X86Assembler& c)
{
	getField<1>(c, x86::r8);
	getField<2>(c, opcode);
	c.mov(x86::r8b, x86::byte_ptr(state, x86::r8, 0, STATE_OFFS(gpr)));
	c.or_(x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)), x86::r8b);
});

DECLARE(asm_insts::AND) = build_instruction([](X86Assembler& c)
{
	getField<1>(c, x86::r8);
	getField<2>(c, opcode);
	c.mov(x86::r8b, x86::byte_ptr(state, x86::r8, 0, STATE_OFFS(gpr)));
	c.and_(x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)), x86::r8b);
});

DECLARE(asm_insts::XOR) = build_instruction([](X86Assembler& c)
{
	getField<1>(c, x86::r8);
	getField<2>(c, opcode);
	c.mov(x86::r8b, x86::byte_ptr(state, x86::r8, 0, STATE_OFFS(gpr)));
	c.xor_(x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)), x86::r8b);
});

DECLARE(asm_insts::ADD) = build_instruction([](X86Assembler& c)
{
	getField<2>(c, x86::r8);
	getField<1>(c, opcode);
	c.mov(x86::r8b, x86::byte_ptr(state, x86::r8, 0, STATE_OFFS(gpr)));
	c.movzx(x86::r9d, x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)));
	c.add(x86::r8d, x86::r9d);
	c.mov(x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)), x86::r8b);
	c.shr(x86::r8d, 8);
	c.mov(refVF(), x86::r8b);
});

DECLARE(asm_insts::SUB) = build_instruction([](X86Assembler& c)
{
	getField<1>(c, x86::r8);
	getField<2>(c, opcode);
	c.mov(x86::r8b, x86::byte_ptr(state, x86::r8, 0, STATE_OFFS(gpr)));
	c.movzx(x86::r9d, x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)));
	c.sub(x86::r8d, x86::r9d);
	c.mov(x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)), x86::r8b);
	c.shr(x86::r8w, 15);
	c.mov(refVF(), x86::r8b);
});

DECLARE(asm_insts::SHR) = build_instruction([](X86Assembler& c)
{
	getField<2>(c, opcode);
	c.shr(x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)), 1);
	c.setc(refVF());
});

DECLARE(asm_insts::RSB) = build_instruction([](X86Assembler& c)
{
	getField<1>(c, x86::r8);
	getField<2>(c, opcode);
	c.mov(x86::r8b, x86::byte_ptr(state, x86::r8, 0, STATE_OFFS(gpr)));
	c.movzx(x86::r9d, x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)));
	c.sub(x86::r9d, x86::r8d);
	c.mov(x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)), x86::r9b);
	c.setns(refVF());
});

DECLARE(asm_insts::SHL) = build_instruction([](X86Assembler& c)
{
	getField<2>(c, opcode);
	c.shl(x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)), 1);
	c.setc(refVF());
});

DECLARE(asm_insts::SNE) = build_instruction<true>([](X86Assembler& c)
{
	getField<1>(c, x86::r8);
	getField<2>(c, opcode);
	c.mov(x86::r8b, x86::byte_ptr(state, x86::r8, 0, STATE_OFFS(gpr)));
	c.cmp(x86::r8b, x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)));
	c.setne(x86::dl);
	c.lea(pc, x86::ptr(pc, x86::rdx, 1, 2)); // pc += (nequal ? 2 : 0) + 2
});

DECLARE(asm_insts::SetIndex) = build_instruction([](X86Assembler& c)
{
	c.and_(opcode.r32(), 0xFFF); // Extract index
	c.mov(x86::dword_ptr(state, STATE_OFFS(index)), opcode.r32());
});

DECLARE(asm_insts::JPr) = build_instruction<true>([](X86Assembler& c)
{
	c.movzx(opcode.r32(), opcode.r16());
	c.movzx(x86::r8d, x86::byte_ptr(state, STATE_OFFS(gpr) + 0));
	c.lea(pc.r32(), x86::ptr(opcode, x86::r8d));
});

DECLARE(asm_insts::RND) = build_instruction([](X86Assembler& c)
{
	c.mov(x86::r8d, opcode.r32()); // Save rdx
	c.rdtsc();
	c.and_(x86::al, x86::r8b); // Mask timestamp
	getField<2>(c, x86::r8, x86::r8);
	c.mov(x86::byte_ptr(state, x86::r8, 0, STATE_OFFS(gpr)), x86::al);
});

DECLARE(asm_insts::DRW) = build_instruction([](X86Assembler& c)
{
	Label skip = c.newLabel();
	Label main_loop = c.newLabel();
	Label skip_size0 = c.newLabel();

	// Ram pointer
	c.mov(x86::r8d, x86::dword_ptr(state, STATE_OFFS(index)));
	c.lea(x86::r8, x86::ptr(state, x86::r8, 0, STATE_OFFS(memBase)));

	getField<1>(c, x86::r9);
	c.mov(x86::r9b, x86::byte_ptr(state, x86::r9, 0, STATE_OFFS(gpr)));
	c.and_(x86::r9b, 0x1f);
	c.shl(x86::r9d, flog2<u32, emu_state_t::y_shift>());
	getField<2>(c, x86::r10);
	c.mov(x86::r10b, x86::byte_ptr(state, x86::r10, 0, STATE_OFFS(gpr)));
	c.and_(x86::r10b, 0x3f);

	// Vram offset
	c.add(x86::r9d, x86::r10d);

	c.mov(x86::dword_ptr(state, STATE_OFFS(pc)), pc.r32()); // Free pc
	const X86Gp temp = pc.r32();

	// VF setup
	c.xor_(x86::r11d, x86::r11d);
	c.mov(temp, 1);

	// Get lines amount
	getField<0>(c, opcode);
	c.je(skip_size0); // Flags set at getField, must check this

	// Get max ram address
	c.lea(x86::rdx, x86::ptr(x86::r8, x86::rdx));
	c.bind(main_loop);

	// Load pixel value
	c.movzx(x86::r10d, x86::byte_ptr(x86::r8));
	c.test(x86::r10d, x86::r10d);
	c.je(skip);

	// Unpack bits into pixels (unrolled loop)
	for (u32 i = 0; i < 8; i++)
	{
		Label next = c.newLabel();
		c.bt(x86::r10d, 7 - i);
		c.jnc(next);
		c.and_(x86::r9d, emu_state_t::xy_mask); // Wrap around y axis (TODO: x axis)
		c.xor_(x86::byte_ptr(state, x86::r9, 0, STATE_OFFS(gfxMemory) + i), 0xff);
		c.cmove(x86::r11d, temp); // Set VF
		c.bind(next);
	}

	c.bind(skip);
	c.add(x86::r8, emu_state_t::x_shift);
	c.add(x86::r9, emu_state_t::y_shift);
	c.cmp(x86::r8, x86::rdx);
	c.jne(main_loop);

	c.bind(skip_size0);
	c.mov(refVF(), x86::r11b);
	c.lea(args[0], x86::ptr(state, STATE_OFFS(gfxMemory)));
	c.call(imm_ptr(&::KickChip8Framebuffer));
	c.mov(state, imm_ptr(&g_state));
	c.mov(pc.r32(), x86::dword_ptr(state, STATE_OFFS(pc)));
});

DECLARE(asm_insts::SKP) = build_instruction<true>([](X86Assembler& c)
{
	getField<2>(c, opcode);
	c.mov(x86::dl, x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)));
	c.and_(x86::dl, 0xf);
	c.mov(x86::r8, imm_ptr(&input::keyIDs));
	c.movzx(args[0].r32(), x86::byte_ptr(x86::r8, x86::rdx, 0));
	c.call(imm_ptr(&::GetKeyState));
	c.mov(state, imm_ptr(&g_state));
	c.shr(retn.r16(), 16 - 1); // If pressed, contains 1 otherwise 0
	c.movzx(retn.r32(), retn.r8());
	c.lea(pc, x86::ptr(pc, retn, 1, 2));
});

DECLARE(asm_insts::SKNP) = build_instruction<true>([](X86Assembler& c)
{
	getField<2>(c, opcode);
	c.mov(x86::dl, x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)));
	c.and_(x86::dl, 0xf);
	c.mov(x86::r8, imm_ptr(&input::keyIDs));
	c.movzx(args[0].r32(), x86::byte_ptr(x86::r8, x86::rdx, 0));
	c.call(imm_ptr(&::GetKeyState));
	c.mov(state, imm_ptr(&g_state));
	c.shr(retn.r16(), 16 - 1);
	c.xor_(retn.r8(), 1);
	c.movzx(retn.r32(), retn.r8());
	c.lea(pc, x86::ptr(pc, retn, 1, 2));
});

DECLARE(asm_insts::GetD) = build_instruction([](X86Assembler& c)
{
	c.mov(x86::r8b, x86::byte_ptr(state, STATE_OFFS(timers) + ::offset32(&time_control_t::delay)));
	getField<2>(c, opcode);
	c.mov(x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)), x86::r8b);
});

DECLARE(asm_insts::GetK) = build_instruction([](X86Assembler& c)
{
	c.mov(x86::dword_ptr(state, STATE_OFFS(pc)), pc.r32());
	c.mov(pc, opcode); // Save rdx
	c.call(imm_ptr(&input::WaitForPress));
	c.mov(state, imm_ptr(&g_state));
	getField<2>(c, pc, pc);
	c.mov(x86::byte_ptr(state, pc, 0, STATE_OFFS(gpr)), retn.r8());
	c.mov(pc.r32(), x86::dword_ptr(state, STATE_OFFS(pc)));
});

DECLARE(asm_insts::SetD) = build_instruction([](X86Assembler& c)
{
	getField<2>(c, opcode);
	c.mov(x86::r8b, x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)));
	c.mov(x86::byte_ptr(state, STATE_OFFS(timers) + ::offset32(&time_control_t::delay)), x86::r8b);
});

DECLARE(asm_insts::SetS) = build_instruction([](X86Assembler& c)
{
	getField<2>(c, opcode);
	c.mov(x86::r8b, x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)));
	c.mov(x86::byte_ptr(state, STATE_OFFS(timers) + ::offset32(&time_control_t::sound)), x86::r8b);
});

DECLARE(asm_insts::AddIndex) = build_instruction([](X86Assembler& c)
{
	getField<2>(c, x86::r8);
	c.mov(x86::dl, x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)));
	c.add(x86::edx, x86::dword_ptr(state, STATE_OFFS(index)));
	c.and_(x86::edx, 0xfff);
	c.mov(x86::dword_ptr(state, STATE_OFFS(index)), x86::edx);
});

DECLARE(asm_insts::SetCh) = build_instruction([](X86Assembler& c)
{
	getField<2>(c, opcode);
	c.mov(x86::dl, x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)));
	c.and_(x86::dl, 0xf);
	c.mov(x86::eax, 5);
	c.mul(x86::edx); // This can also be done with: index = (gpr << 2) + gpr
	c.mov(x86::dword_ptr(state, STATE_OFFS(index)), x86::eax);
});

DECLARE(asm_insts::STD) = build_instruction([](X86Assembler& c)
{
	// div instruction produces both quotient and reminder
	// Let's make a good use of it
	getField<2>(c, opcode);
	c.movzx(x86::eax, x86::byte_ptr(state, x86::rdx, 0, STATE_OFFS(gpr)));
	c.mov(x86::dl, 100);
	c.div(x86::dl);
	c.mov(x86::r8d, x86::dword_ptr(state, STATE_OFFS(index)));
	c.lea(x86::r8, x86::ptr(state, x86::r8, 0, STATE_OFFS(memBase)));
	c.mov(x86::byte_ptr(x86::r8, 0), x86::al);
	c.shr(x86::eax, 8); // Move back reminder
	c.mov(x86::dl, 10);
	c.div(x86::dl);
	c.mov(x86::byte_ptr(x86::r8, 1), x86::al);
	c.shr(x86::eax, 8);
	c.mov(x86::byte_ptr(x86::r8, 2), x86::al);
});

DECLARE(asm_insts::STR) = build_instruction([](X86Assembler& c)
{
	getField<2>(c, opcode);
	c.inc(opcode.r8());
	c.mov(x86::r8, state); // Save state, rsi and rdi (non-volatile registers)
	c.mov(x86::r9, x86::rsi);
	c.mov(x86::r10, x86::rdi);
	c.mov(x86::ecx, opcode.r32());
	c.mov(x86::edi, x86::dword_ptr(x86::r8, STATE_OFFS(index)));
	c.lea(x86::rdi, x86::ptr(x86::r8, x86::rdi, 0, STATE_OFFS(memBase)));
	c.lea(x86::rsi, x86::ptr(x86::r8, STATE_OFFS(gpr)));
	c.rep().movsb();
	c.mov(state, x86::r8);
	c.mov(x86::rsi, x86::r9);
	c.mov(x86::rdi, x86::r10);
	c.add(x86::dword_ptr(state, STATE_OFFS(index)), opcode.r32());
});

DECLARE(asm_insts::LDR) = build_instruction([](X86Assembler& c)
{
	getField<2>(c, opcode);
	c.inc(opcode.r8());
	c.mov(x86::r8, state);
	c.mov(x86::r9, x86::rsi);
	c.mov(x86::r10, x86::rdi);
	c.mov(x86::ecx, opcode.r32());
	c.mov(x86::esi, x86::dword_ptr(x86::r8, STATE_OFFS(index)));
	c.lea(x86::rsi, x86::ptr(x86::r8, x86::rsi, 0, STATE_OFFS(memBase)));
	c.lea(x86::rdi, x86::ptr(x86::r8, STATE_OFFS(gpr)));
	c.rep().movsb();
	c.mov(state, x86::r8);
	c.mov(x86::rsi, x86::r9);
	c.mov(x86::rdi, x86::r10);
	c.add(x86::dword_ptr(state, STATE_OFFS(index)), opcode.r32());
});

DECLARE(asm_insts::UNK) = build_instruction<true>([](X86Assembler& c)
{
	c.mov(x86::dword_ptr(state, STATE_OFFS(pc)), pc.r32());

#ifdef DEBUG_INSTS
	c.int3();
#else
	c.ud2();
#endif
});

DECLARE(asm_insts::guard) = build_instruction<true>([](X86Assembler& c)
{
	Label ok = c.newLabel();
	c.cmp(pc.r16(), 0x1000);

#ifndef DEBUG_INSTS
	//TODO: logging of such event
	c.je(ok);
#endif
	c.jmp(imm_ptr(&asm_insts::UNK));

	// Reset pc
	c.bind(ok);
	c.xor_(pc.r32(), pc.r32());
});

#undef DECLARE
#undef STATE_OFFS
#undef DEBUG_INSTS
#undef STACK_RESERVE
#undef GET_SHIFT
#undef GET_SHIFT_ARR
#undef GET_SHIFT_MEMBER