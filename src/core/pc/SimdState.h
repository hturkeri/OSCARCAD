#pragma once
// PersistCAD core — SIMD register hygiene for the numeric kernels (header-only).
//
// GUI libraries (Qt raster paint engine, GL drivers, font rasterisers) run AVX code paths and may return to
// the caller with the upper halves of the YMM registers "dirty". OpenCASCADE is compiled with legacy SSE
// encodings; on Intel CPUs every SSE instruction then pays an AVX->SSE state-transition / false-dependency
// penalty, which made regeneration inside the designer/runner 2.5-3x slower than in a console process
// (measured: 1.8 s vs 0.65 s for samples/enjektor_09DB01.pcad). VZEROUPPER clears that state.
//
// Safe on both the SysV and the Windows x64 ABI: the upper YMM halves are caller-saved (volatile) there.
// No-op on non-x86 targets, on CPUs without AVX and on compilers other than GCC/Clang.
namespace pc {

#if (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
namespace detail {
__attribute__((target("avx"), noinline)) inline void vzeroupperImpl() { __builtin_ia32_vzeroupper(); }
}  // namespace detail
inline void resetSimdUpperState() {
  static const bool hasAvx = (__builtin_cpu_init(), __builtin_cpu_supports("avx") != 0);
  if (hasAvx) detail::vzeroupperImpl();
}
#else
inline void resetSimdUpperState() {}
#endif

}  // namespace pc
