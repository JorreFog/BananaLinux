/* gbastress: GBA registers and helpers (own code, from GBATEK's register list). GPL-3.0 like BananaLinux. */
#pragma once
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef int8_t s8; typedef int16_t s16; typedef int32_t s32;
#define REG16(a) (*(volatile u16*)(a))
#define REG32(a) (*(volatile u32*)(a))
#define DISPCNT REG16(0x4000000)
#define DISPSTAT REG16(0x4000004)
#define VCOUNT REG16(0x4000006)
#define BGCNT(n) REG16(0x4000008 + 2 * (n))
#define BGHOFS(n) REG16(0x4000010 + 4 * (n))
#define BGVOFS(n) REG16(0x4000012 + 4 * (n))
#define BG2PA REG16(0x4000020)
#define BG2PB REG16(0x4000022)
#define BG2PC REG16(0x4000024)
#define BG2PD REG16(0x4000026)
#define BG2X REG32(0x4000028)
#define BG2Y REG32(0x400002C)
#define WIN0H REG16(0x4000040)
#define WIN1H REG16(0x4000042)
#define WIN0V REG16(0x4000044)
#define WIN1V REG16(0x4000046)
#define WININ REG16(0x4000048)
#define WINOUT REG16(0x400004A)
#define MOSAIC REG16(0x400004C)
#define BLDCNT REG16(0x4000050)
#define BLDALPHA REG16(0x4000052)
#define BLDY REG16(0x4000054)
#define SND1SWEEP REG16(0x4000060)
#define SND1CNT REG16(0x4000062)
#define SND1FREQ REG16(0x4000064)
#define SND2CNT REG16(0x4000068)
#define SND2FREQ REG16(0x400006C)
#define SND4CNT REG16(0x4000078)
#define SND4FREQ REG16(0x400007C)
#define SNDCNT_L REG16(0x4000080)
#define SNDCNT_H REG16(0x4000082)
#define SNDCNT_X REG16(0x4000084)
#define FIFO_A 0x40000A0
#define FIFO_B 0x40000A4
#define DMASAD(n) REG32(0x40000B0 + 12 * (n))
#define DMADAD(n) REG32(0x40000B4 + 12 * (n))
#define DMACNT_L(n) REG16(0x40000B8 + 12 * (n))
#define DMACNT_H(n) REG16(0x40000BA + 12 * (n))
#define TMCNT_L(n) REG16(0x4000100 + 4 * (n))
#define TMCNT_H(n) REG16(0x4000102 + 4 * (n))
#define KEYINPUT REG16(0x4000130)
#define IE REG16(0x4000200)
#define IF REG16(0x4000202)
#define WAITCNT REG16(0x4000204)
#define IME REG16(0x4000208)
#define BIOS_IF (*(volatile u16*)0x03007FF8)
#define IRQ_VECTOR (*(volatile u32*)0x03007FFC)
#define IRQ_VBLANK 1
#define IRQ_HBLANK 2
#define IRQ_VCOUNT 4
#define IRQ_TIMER1 0x10
#define PAL_BG ((volatile u16*)0x05000000)
#define PAL_OBJ ((volatile u16*)0x05000200)
#define VRAM ((volatile u8*)0x06000000)
#define OAM ((volatile u16*)0x07000000)
#define SRAM ((volatile u8*)0x0E000000)
#define CHARBLOCK(n) (0x06000000 + 0x4000 * (n))
#define SCREENBLOCK(n) (0x06000000 + 0x800 * (n))
#define IWRAM_CODE __attribute__((section(".iwram"), target("arm"), noinline))
#define EWRAM_BSS __attribute__((section(".ewram_bss")))
#define RGB(r, g, b) ((u16)((r) | ((g) << 5) | ((b) << 10)))

static inline void dma3_copy32(const void* src, volatile void* dst, u32 words)
{
	DMASAD(3) = (u32)src; DMADAD(3) = (u32)dst; DMACNT_L(3) = (u16)words; DMACNT_H(3) = 0x8400;
}
static inline void dma3_copy16(const void* src, volatile void* dst, u32 halves)
{
	DMASAD(3) = (u32)src; DMADAD(3) = (u32)dst; DMACNT_L(3) = (u16)halves; DMACNT_H(3) = 0x8000;
}
static inline void dma3_fill32(const u32* val, volatile void* dst, u32 words)
{
	DMASAD(3) = (u32)val; DMADAD(3) = (u32)dst; DMACNT_L(3) = (u16)words; DMACNT_H(3) = 0x8500;
}
/* BIOS calls (Thumb SWI numbers), as games use them */
static inline void bios_vblank_wait(void) { __asm__ volatile("swi 5" ::: "r0", "r1", "r2", "r3", "memory"); }
static inline s32 bios_div(s32 n, s32 d)
{
	register s32 r0 __asm__("r0") = n; register s32 r1 __asm__("r1") = d;
	__asm__ volatile("swi 6" : "+r"(r0), "+r"(r1) :: "r3", "memory");
	return r0;
}
static inline void bios_lz77_vram(const void* src, volatile void* dst)
{
	register u32 r0 __asm__("r0") = (u32)src; register u32 r1 __asm__("r1") = (u32)dst;
	__asm__ volatile("swi 0x12" : "+r"(r0), "+r"(r1) :: "r2", "r3", "memory");
}
static inline void bios_cpufastset(const void* src, volatile void* dst, u32 mode)
{
	register u32 r0 __asm__("r0") = (u32)src; register u32 r1 __asm__("r1") = (u32)dst; register u32 r2 __asm__("r2") = mode;
	__asm__ volatile("swi 0xC" : "+r"(r0), "+r"(r1), "+r"(r2) :: "r3", "memory");
}
