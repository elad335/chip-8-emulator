#include "asmutils.h"

namespace asmjit
{
	JitRuntime& get_global_runtime()
	{
		static JitRuntime g_rt;
		return g_rt;
	}
}