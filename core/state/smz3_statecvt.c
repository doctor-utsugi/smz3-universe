#include "smz3_statecvt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t u32;

/* Field tables: every struct below holds u32 slots, and a table lists the snapshot's fields in file order, big-endian.
 * An entry with size 0 repeats the next `group` entries `count` times, `off` bytes apart. */
typedef struct { uint16_t off, count; uint8_t size, group; } Fld;
#define F(T, f, sz)    { (uint16_t)offsetof(T, f), 1, sz, 0 }
#define A(T, f, sz, n) { (uint16_t)offsetof(T, f), n, sz, 0 }
#define G(T, arr, n, k) { (uint16_t)sizeof(((T *)0)->arr[0]), n, 0, k }
#define NF(t) ((int)(sizeof(t) / sizeof(t[0])))

static size_t fld_size(const Fld *t, int n)
{
    size_t len = 0;
    for (int i = 0; i < n; i++) {
        if (t[i].size == 0) {
            len += fld_size(t + i + 1, t[i].group) * t[i].count;
            i += t[i].group;
        } else
            len += (size_t)t[i].size * t[i].count;
    }
    return len;
}

/* Moves the fields between the struct at base and the bytes at p; returns the bytes used. */
static size_t fld_io(const Fld *t, int n, uint8_t *base, uint8_t *p, int put)
{
    uint8_t *start = p;
    for (int i = 0; i < n; i++) {
        if (t[i].size == 0) {
            for (int r = 0; r < t[i].count; r++) p += fld_io(t + i + 1, t[i].group, base + r * t[i].off, p, put);
            i += t[i].group;
            continue;
        }
        for (int k = 0; k < t[i].count; k++, p += t[i].size) {
            u32 *v = (u32 *)(base + t[i].off) + k;
            int sz = t[i].size;
            if (put)
                for (int b = 0; b < sz; b++) p[b] = (uint8_t)(*v >> (8 * (sz - 1 - b)));
            else {
                u32 x = 0;
                for (int b = 0; b < sz; b++) x = x << 8 | p[b];
                *v = x;
            }
        }
    }
    return (size_t)(p - start);
}

/* ---- the state, as a superset of both versions' fields ---- */

typedef struct {
    u32 Flags, BranchSkip, NMIActive, IRQActive, WaitingForInterrupt, WhichEvent, Cycles, PrevCycles, NextEvent, V_Counter;
    u32 MemSpeed, MemSpeedx2, FastROMSpeed, IRQPending, InDMA, InHDMA, InDMAorHDMA, InWRAMDMAorHDMA, HDMARanInDMA;
    u32 NMIPending, IRQLine, IRQTransition, IRQLastState, IRQExternal;
} Cpu;

typedef struct { u32 PB, DB, P, A, D, S, X, Y, PC; } Reg;

typedef struct { u32 SCBase, HOffset, VOffset, BGSize, NameBase, SCSize; } Bg;
typedef struct { u32 HPos, VPos, HFlip, VFlip, Name, Priority, Palette, Size; } Obj;
typedef struct { u32 Counts, OverlapLogic, W1Enable, W2Enable, W1Inside, W2Inside; } Win;

typedef struct {
    u32 VMA_High, VMA_Increment, VMA_Address, VMA_Mask1, VMA_FullGraphicCount, VMA_Shift, WRAM;
    Bg BG[4];
    u32 BGMode, BG3Priority, Brightness, CGFLIP, CGFLIPRead, CGADD, CGSavedByte, CGDATA[256];
    Obj OBJ[128];
    u32 OBJThroughMain, OBJThroughSub, OBJAddition, OBJNameBase, OBJNameSelect, OBJSizeSelect;
    u32 OAMAddr, SavedOAMAddr, OAMPriorityRotation, OAMFlip, OAMReadFlip, OAMTileAddress, OAMWriteRegister, OAMData[512 + 32];
    u32 FirstSprite, LastSprite, HTimerEnabled, VTimerEnabled, HTimerPosition, VTimerPosition, IRQHBeamPos, IRQVBeamPos;
    u32 HBeamFlip, VBeamFlip, HBeamPosLatched, VBeamPosLatched, GunHLatch, GunVLatch, HVBeamCounterLatched;
    u32 Mode7HFlip, Mode7VFlip, Mode7Repeat, MatrixA, MatrixB, MatrixC, MatrixD, CentreX, CentreY, M7HOFS, M7VOFS;
    u32 Mosaic, MosaicStart, BGMosaic[4], Window1Left, Window1Right, Window2Left, Window2Right, RecomputeClipWindows;
    Win Clip[6];
    u32 ForcedBlanking, FixedColourRed, FixedColourGreen, FixedColourBlue, ScreenHeight, Need16x8Mulitply;
    u32 BGnxOFSbyte, M7byte, HDMA, HDMAEnded, OpenBus1, OpenBus2, VRAMReadBuffer;
    u32 Joypad1ButtonReadPos, Joypad2ButtonReadPos, Joypad3ButtonReadPos, MouseSpeed[2];
} Ppu;

typedef struct {
    u32 ReverseTransfer, HDMAIndirectAddressing, UnusedBit43x0, AAddressFixed, AAddressDecrement, TransferMode, BAddress;
    u32 AAddress, ABank, Count, IndirectAddress, IndirectBank, Address, Repeat, LineCount, UnknownByte, DoTransfer;
} Dma;
typedef struct { Dma d[8]; } Dmas;

typedef struct {
    u32 Cycles, ShowROM, Flags, KeyedChannels, OutPorts[4], DSP[128], ExtraRAM[64];
    u32 Timer[3], TimerTarget[3], TimerEnabled[3], TimerValueWritten[3];
} Apu;

typedef struct { u32 P, YA, X, S, PC; } AReg;

typedef struct {
    u32 state, type, volume_left, volume_right, hertz, count, loop, envx, left_vol_level, right_vol_level, envx_target;
    u32 env_error, erate, direction, attack_rate, decay_rate, sustain_rate, release_rate, sustain_level, sample;
    u32 decoded[16], previous16[2], sample_number, last_block, needs_decode, block_pointer, sample_pointer, mode;
} Chan;

typedef struct {
    u32 master_volume_left, master_volume_right, echo_volume_left, echo_volume_right, echo_enable, echo_feedback;
    u32 echo_ptr, echo_buffer_size, echo_write_enabled, echo_channel_enable, pitch_mod, dummy[3];
    Chan ch[8];
} Sound;

typedef struct {
    u32 H_Max_Master, H_Max, V_Max_Master, V_Max, HBlankStart, HBlankEnd, HDMAInit, HDMAStart, NMITriggerPos;
    u32 WRAMRefreshPos, RenderPos, InterlaceField, DMACPUSync, NMIDMADelay, IRQFlagChanging, APUSpeedup;
    u32 IRQTriggerCycles, APUAllowTimeOverflow, NextIRQTimer;
} Tim;

typedef struct { u32 buf[12], interp_pos, brr_addr, env, hidden_env, buf_pos, brr_offset, kon_delay, env_mode, envx_out; } Voice;

/* The libretro SND block: the bsnes SMP, then blargg's SPC_DSP, then the CPU-side ports (little-endian). */
typedef struct {
    uint8_t ram[0x10000];
    u32 clock, opcode_number, opcode_cycle, pc, sp, a, x, y, flag[8], iplrom, dsp_addr, f8, f9, timer[3][5], tmp[6];
    uint8_t regs[128];
    Voice v[8];
    u32 echo_hist[8][2], every_other, kon, noise, counter, echo_offset, echo_length, phase, new_kon, endx_buf, envx_buf, outx_buf;
    u32 t_pmon, t_non, t_eon, t_dir, t_koff, t_brr_next_addr, t_adsr0, t_brr_header, t_brr_byte, t_srcn, t_esa, t_echo_enabled;
    u32 t_main_out[2], t_echo_out[2], t_echo_in[2], t_dir_addr, t_pitch, t_output, t_echo_ptr, t_looped;
    uint8_t ext_regs[128];
    u32 ref_time, remainder, dsp_clock;
    uint8_t cpu_ports[4];
} Snd;

#define SND_BLOCK (1024 * 65)

/* ---- field tables ---- */

static const Fld REG_T[] = {
    F(Reg, PB, 1), F(Reg, DB, 1), F(Reg, P, 2), F(Reg, A, 2), F(Reg, D, 2), F(Reg, S, 2), F(Reg, X, 2), F(Reg, Y, 2), F(Reg, PC, 2),
};

static const Fld CPU143[] = {
    F(Cpu, Flags, 4), F(Cpu, BranchSkip, 1), F(Cpu, NMIActive, 1), F(Cpu, IRQActive, 1), F(Cpu, WaitingForInterrupt, 1),
    F(Cpu, WhichEvent, 1), F(Cpu, Cycles, 4), F(Cpu, NextEvent, 4), F(Cpu, V_Counter, 4), F(Cpu, MemSpeed, 4),
    F(Cpu, MemSpeedx2, 4), F(Cpu, FastROMSpeed, 4),
};

static const Fld CPU14[] = {
    F(Cpu, Cycles, 4), F(Cpu, PrevCycles, 4), F(Cpu, V_Counter, 4), F(Cpu, Flags, 4), F(Cpu, IRQPending, 4), F(Cpu, MemSpeed, 4),
    F(Cpu, MemSpeedx2, 4), F(Cpu, FastROMSpeed, 4), F(Cpu, InDMA, 1), F(Cpu, InHDMA, 1), F(Cpu, InDMAorHDMA, 1),
    F(Cpu, InWRAMDMAorHDMA, 1), F(Cpu, HDMARanInDMA, 1), F(Cpu, WhichEvent, 1), F(Cpu, NextEvent, 4), F(Cpu, WaitingForInterrupt, 1),
    F(Cpu, NMIPending, 1), F(Cpu, IRQLine, 1), F(Cpu, IRQTransition, 1), F(Cpu, IRQLastState, 1), F(Cpu, IRQExternal, 1),
};

static const Fld PPU143[] = {
    F(Ppu, BGMode, 1), F(Ppu, BG3Priority, 1), F(Ppu, Brightness, 1), F(Ppu, VMA_High, 1), F(Ppu, VMA_Increment, 1),
    F(Ppu, VMA_Address, 2), F(Ppu, VMA_Mask1, 2), F(Ppu, VMA_FullGraphicCount, 2), F(Ppu, VMA_Shift, 2),
    G(Ppu, BG, 4, 6),
    F(Ppu, BG[0].SCBase, 2), F(Ppu, BG[0].VOffset, 2), F(Ppu, BG[0].HOffset, 2), F(Ppu, BG[0].BGSize, 1),
    F(Ppu, BG[0].NameBase, 2), F(Ppu, BG[0].SCSize, 2),
    F(Ppu, CGFLIP, 1), A(Ppu, CGDATA, 2, 256), F(Ppu, FirstSprite, 1),
    G(Ppu, OBJ, 128, 8),
    F(Ppu, OBJ[0].HPos, 2), F(Ppu, OBJ[0].VPos, 2), F(Ppu, OBJ[0].Name, 2), F(Ppu, OBJ[0].VFlip, 1), F(Ppu, OBJ[0].HFlip, 1),
    F(Ppu, OBJ[0].Priority, 1), F(Ppu, OBJ[0].Palette, 1), F(Ppu, OBJ[0].Size, 1),
    F(Ppu, OAMPriorityRotation, 1), F(Ppu, OAMAddr, 2), F(Ppu, OAMFlip, 1), F(Ppu, OAMTileAddress, 2), F(Ppu, IRQVBeamPos, 2),
    F(Ppu, IRQHBeamPos, 2), F(Ppu, VBeamPosLatched, 2), F(Ppu, HBeamPosLatched, 2), F(Ppu, HBeamFlip, 1), F(Ppu, VBeamFlip, 1),
    F(Ppu, HVBeamCounterLatched, 1), F(Ppu, MatrixA, 2), F(Ppu, MatrixB, 2), F(Ppu, MatrixC, 2), F(Ppu, MatrixD, 2),
    F(Ppu, CentreX, 2), F(Ppu, CentreY, 2), F(Ppu, Joypad1ButtonReadPos, 1), F(Ppu, Joypad2ButtonReadPos, 1),
    F(Ppu, Joypad3ButtonReadPos, 1), F(Ppu, CGADD, 1), F(Ppu, FixedColourRed, 1), F(Ppu, FixedColourGreen, 1),
    F(Ppu, FixedColourBlue, 1), F(Ppu, SavedOAMAddr, 2), F(Ppu, ScreenHeight, 2), F(Ppu, WRAM, 4), F(Ppu, ForcedBlanking, 1),
    F(Ppu, OBJNameSelect, 2), F(Ppu, OBJSizeSelect, 1), F(Ppu, OBJNameBase, 2), F(Ppu, OAMReadFlip, 1), F(Ppu, VTimerEnabled, 1),
    F(Ppu, HTimerEnabled, 1), F(Ppu, HTimerPosition, 2), F(Ppu, Mosaic, 1), F(Ppu, Mode7HFlip, 1), F(Ppu, Mode7VFlip, 1),
    F(Ppu, Mode7Repeat, 1), F(Ppu, Window1Left, 1), F(Ppu, Window1Right, 1), F(Ppu, Window2Left, 1), F(Ppu, Window2Right, 1),
    G(Ppu, Clip, 6, 5),
    F(Ppu, Clip[0].OverlapLogic, 1), F(Ppu, Clip[0].W1Enable, 1), F(Ppu, Clip[0].W2Enable, 1), F(Ppu, Clip[0].W1Inside, 1),
    F(Ppu, Clip[0].W2Inside, 1),
    F(Ppu, CGFLIPRead, 1), F(Ppu, Need16x8Mulitply, 1), A(Ppu, BGMosaic, 1, 4), A(Ppu, OAMData, 1, 512 + 32),
    F(Ppu, Need16x8Mulitply, 1), A(Ppu, MouseSpeed, 1, 2),
};

static const Fld PPU14[] = {
    F(Ppu, VMA_High, 1), F(Ppu, VMA_Increment, 1), F(Ppu, VMA_Address, 2), F(Ppu, VMA_Mask1, 2), F(Ppu, VMA_FullGraphicCount, 2),
    F(Ppu, VMA_Shift, 2), F(Ppu, WRAM, 4),
    G(Ppu, BG, 4, 6),
    F(Ppu, BG[0].SCBase, 2), F(Ppu, BG[0].HOffset, 2), F(Ppu, BG[0].VOffset, 2), F(Ppu, BG[0].BGSize, 1),
    F(Ppu, BG[0].NameBase, 2), F(Ppu, BG[0].SCSize, 2),
    F(Ppu, BGMode, 1), F(Ppu, BG3Priority, 1), F(Ppu, CGFLIP, 1), F(Ppu, CGFLIPRead, 1), F(Ppu, CGADD, 1), F(Ppu, CGSavedByte, 1),
    A(Ppu, CGDATA, 2, 256),
    G(Ppu, OBJ, 128, 8),
    F(Ppu, OBJ[0].HPos, 2), F(Ppu, OBJ[0].VPos, 2), F(Ppu, OBJ[0].HFlip, 1), F(Ppu, OBJ[0].VFlip, 1), F(Ppu, OBJ[0].Name, 2),
    F(Ppu, OBJ[0].Priority, 1), F(Ppu, OBJ[0].Palette, 1), F(Ppu, OBJ[0].Size, 1),
    F(Ppu, OBJThroughMain, 1), F(Ppu, OBJThroughSub, 1), F(Ppu, OBJAddition, 1), F(Ppu, OBJNameBase, 2), F(Ppu, OBJNameSelect, 2),
    F(Ppu, OBJSizeSelect, 1), F(Ppu, OAMAddr, 2), F(Ppu, SavedOAMAddr, 2), F(Ppu, OAMPriorityRotation, 1), F(Ppu, OAMFlip, 1),
    F(Ppu, OAMReadFlip, 1), F(Ppu, OAMTileAddress, 2), F(Ppu, OAMWriteRegister, 2), A(Ppu, OAMData, 1, 512 + 32),
    F(Ppu, FirstSprite, 1), F(Ppu, LastSprite, 1), F(Ppu, HTimerEnabled, 1), F(Ppu, VTimerEnabled, 1), F(Ppu, HTimerPosition, 2),
    F(Ppu, VTimerPosition, 2), F(Ppu, IRQHBeamPos, 2), F(Ppu, IRQVBeamPos, 2), F(Ppu, HBeamFlip, 1), F(Ppu, VBeamFlip, 1),
    F(Ppu, HBeamPosLatched, 2), F(Ppu, VBeamPosLatched, 2), F(Ppu, GunHLatch, 2), F(Ppu, GunVLatch, 2),
    F(Ppu, HVBeamCounterLatched, 1), F(Ppu, Mode7HFlip, 1), F(Ppu, Mode7VFlip, 1), F(Ppu, Mode7Repeat, 1), F(Ppu, MatrixA, 2),
    F(Ppu, MatrixB, 2), F(Ppu, MatrixC, 2), F(Ppu, MatrixD, 2), F(Ppu, CentreX, 2), F(Ppu, CentreY, 2), F(Ppu, M7HOFS, 2),
    F(Ppu, M7VOFS, 2), F(Ppu, Mosaic, 1), F(Ppu, MosaicStart, 1), A(Ppu, BGMosaic, 1, 4), F(Ppu, Window1Left, 1),
    F(Ppu, Window1Right, 1), F(Ppu, Window2Left, 1), F(Ppu, Window2Right, 1), F(Ppu, RecomputeClipWindows, 1),
    G(Ppu, Clip, 6, 6),
    F(Ppu, Clip[0].Counts, 1), F(Ppu, Clip[0].OverlapLogic, 1), F(Ppu, Clip[0].W1Enable, 1), F(Ppu, Clip[0].W2Enable, 1),
    F(Ppu, Clip[0].W1Inside, 1), F(Ppu, Clip[0].W2Inside, 1),
    F(Ppu, ForcedBlanking, 1), F(Ppu, FixedColourRed, 1), F(Ppu, FixedColourGreen, 1), F(Ppu, FixedColourBlue, 1),
    F(Ppu, Brightness, 1), F(Ppu, ScreenHeight, 2), F(Ppu, Need16x8Mulitply, 1), F(Ppu, BGnxOFSbyte, 1), F(Ppu, M7byte, 1),
    F(Ppu, HDMA, 1), F(Ppu, HDMAEnded, 1), F(Ppu, OpenBus1, 1), F(Ppu, OpenBus2, 1), F(Ppu, VRAMReadBuffer, 2),
};

static const Fld DMA143[] = {
    G(Dmas, d, 8, 15),
    F(Dmas, d[0].ReverseTransfer, 1), F(Dmas, d[0].AAddressFixed, 1), F(Dmas, d[0].AAddressDecrement, 1),
    F(Dmas, d[0].TransferMode, 1), F(Dmas, d[0].ABank, 1), F(Dmas, d[0].AAddress, 2), F(Dmas, d[0].Address, 2),
    F(Dmas, d[0].BAddress, 1), F(Dmas, d[0].Count, 2), F(Dmas, d[0].HDMAIndirectAddressing, 1), F(Dmas, d[0].IndirectAddress, 2),
    F(Dmas, d[0].IndirectBank, 1), F(Dmas, d[0].Repeat, 1), F(Dmas, d[0].LineCount, 1), F(Dmas, d[0].DoTransfer, 1),
};

static const Fld DMA14[] = {
    G(Dmas, d, 8, 16),
    F(Dmas, d[0].ReverseTransfer, 1), F(Dmas, d[0].HDMAIndirectAddressing, 1), F(Dmas, d[0].UnusedBit43x0, 1),
    F(Dmas, d[0].AAddressFixed, 1), F(Dmas, d[0].AAddressDecrement, 1), F(Dmas, d[0].TransferMode, 1), F(Dmas, d[0].BAddress, 1),
    F(Dmas, d[0].AAddress, 2), F(Dmas, d[0].ABank, 1), F(Dmas, d[0].Count, 2), F(Dmas, d[0].IndirectBank, 1),
    F(Dmas, d[0].Address, 2), F(Dmas, d[0].Repeat, 1), F(Dmas, d[0].LineCount, 1), F(Dmas, d[0].UnknownByte, 1),
    F(Dmas, d[0].DoTransfer, 1),
};

static const Fld APU143[] = {
    F(Apu, Cycles, 4), F(Apu, ShowROM, 1), F(Apu, Flags, 1), F(Apu, KeyedChannels, 1), A(Apu, OutPorts, 1, 4), A(Apu, DSP, 1, 128),
    A(Apu, ExtraRAM, 1, 64), A(Apu, Timer, 2, 3), A(Apu, TimerTarget, 2, 3), A(Apu, TimerEnabled, 1, 3),
    A(Apu, TimerValueWritten, 1, 3),
};

static const Fld AREG143[] = { F(AReg, P, 1), F(AReg, YA, 2), F(AReg, X, 1), F(AReg, S, 1), F(AReg, PC, 2) };

static const Fld SOU143[] = {
    F(Sound, master_volume_left, 2), F(Sound, master_volume_right, 2), F(Sound, echo_volume_left, 2),
    F(Sound, echo_volume_right, 2), F(Sound, echo_enable, 4), F(Sound, echo_feedback, 4), F(Sound, echo_ptr, 4),
    F(Sound, echo_buffer_size, 4), F(Sound, echo_write_enabled, 4), F(Sound, echo_channel_enable, 4), F(Sound, pitch_mod, 4),
    A(Sound, dummy, 4, 3),
    G(Sound, ch, 8, 28),
    F(Sound, ch[0].state, 4), F(Sound, ch[0].type, 4), F(Sound, ch[0].volume_left, 2), F(Sound, ch[0].volume_right, 2),
    F(Sound, ch[0].hertz, 4), F(Sound, ch[0].count, 4), F(Sound, ch[0].loop, 1), F(Sound, ch[0].envx, 4),
    F(Sound, ch[0].left_vol_level, 2), F(Sound, ch[0].right_vol_level, 2), F(Sound, ch[0].envx_target, 2),
    F(Sound, ch[0].env_error, 4), F(Sound, ch[0].erate, 4), F(Sound, ch[0].direction, 4), F(Sound, ch[0].attack_rate, 4),
    F(Sound, ch[0].decay_rate, 4), F(Sound, ch[0].sustain_rate, 4), F(Sound, ch[0].release_rate, 4),
    F(Sound, ch[0].sustain_level, 4), F(Sound, ch[0].sample, 2), A(Sound, ch[0].decoded, 2, 16), A(Sound, ch[0].previous16, 2, 2),
    F(Sound, ch[0].sample_number, 2), F(Sound, ch[0].last_block, 1), F(Sound, ch[0].needs_decode, 1),
    F(Sound, ch[0].block_pointer, 4), F(Sound, ch[0].sample_pointer, 4), F(Sound, ch[0].mode, 4),
};

static const Fld TIM14[] = {
    F(Tim, H_Max_Master, 4), F(Tim, H_Max, 4), F(Tim, V_Max_Master, 4), F(Tim, V_Max, 4), F(Tim, HBlankStart, 4),
    F(Tim, HBlankEnd, 4), F(Tim, HDMAInit, 4), F(Tim, HDMAStart, 4), F(Tim, NMITriggerPos, 4), F(Tim, WRAMRefreshPos, 4),
    F(Tim, RenderPos, 4), F(Tim, InterlaceField, 1), F(Tim, DMACPUSync, 4), F(Tim, NMIDMADelay, 4), F(Tim, IRQFlagChanging, 4),
    F(Tim, APUSpeedup, 4), F(Tim, IRQTriggerCycles, 4), F(Tim, APUAllowTimeOverflow, 1), F(Tim, NextIRQTimer, 4),
};

/* ---- the SND block, read and written by one walk ---- */

typedef struct { uint8_t *p, *end; int put, ok; } Io;

static void io_raw(Io *io, uint8_t *v, size_t n)
{
    if (!io->ok || (size_t)(io->end - io->p) < n) { io->ok = 0; return; }
    if (io->put) memcpy(io->p, v, n); else memcpy(v, io->p, n);
    io->p += n;
}

static void io_le(Io *io, u32 *v, int n)
{
    if (!io->ok || io->end - io->p < n) { io->ok = 0; return; }
    if (io->put)
        for (int b = 0; b < n; b++) io->p[b] = (uint8_t)(*v >> (8 * b));
    else {
        u32 x = 0;
        for (int b = n - 1; b >= 0; b--) x = x << 8 | io->p[b];
        *v = x;
    }
    io->p += n;
}

/* SPC_State_Copier::extra(): a count byte and that many bytes to skip, 0 when written. */
static void io_extra(Io *io)
{
    u32 n = 0;
    io_le(io, &n, 1);
    if (!io->put && io->ok) {
        if ((u32)(io->end - io->p) < n) io->ok = 0; else io->p += n;
    }
}

static int snd_io(Snd *s, uint8_t *blk, size_t len, int put)
{
    Io io = { blk, blk + len, put, 1 };
    io_raw(&io, s->ram, sizeof s->ram);
    u32 *smp[] = { &s->clock, &s->opcode_number, &s->opcode_cycle, &s->pc, &s->sp, &s->a, &s->x, &s->y };
    for (int i = 0; i < 8; i++) io_le(&io, smp[i], 4);
    for (int i = 0; i < 8; i++) io_le(&io, &s->flag[i], 4);
    io_le(&io, &s->iplrom, 4); io_le(&io, &s->dsp_addr, 4); io_le(&io, &s->f8, 4); io_le(&io, &s->f9, 4);
    for (int t = 0; t < 3; t++) for (int k = 0; k < 5; k++) io_le(&io, &s->timer[t][k], 4);
    for (int i = 0; i < 6; i++) io_le(&io, &s->tmp[i], 4);

    io_raw(&io, s->regs, 128);
    for (int i = 0; i < 8; i++) {
        Voice *v = &s->v[i];
        for (int k = 0; k < 12; k++) io_le(&io, &v->buf[k], 2);
        io_le(&io, &v->interp_pos, 2); io_le(&io, &v->brr_addr, 2); io_le(&io, &v->env, 2); io_le(&io, &v->hidden_env, 2);
        io_le(&io, &v->buf_pos, 1); io_le(&io, &v->brr_offset, 1); io_le(&io, &v->kon_delay, 1); io_le(&io, &v->env_mode, 1);
        io_le(&io, &v->envx_out, 1);
        io_extra(&io);
    }
    for (int i = 0; i < 8; i++) { io_le(&io, &s->echo_hist[i][0], 2); io_le(&io, &s->echo_hist[i][1], 2); }
    io_le(&io, &s->every_other, 1); io_le(&io, &s->kon, 1); io_le(&io, &s->noise, 2); io_le(&io, &s->counter, 2);
    io_le(&io, &s->echo_offset, 2); io_le(&io, &s->echo_length, 2); io_le(&io, &s->phase, 1);
    io_le(&io, &s->new_kon, 1); io_le(&io, &s->endx_buf, 1); io_le(&io, &s->envx_buf, 1); io_le(&io, &s->outx_buf, 1);
    io_le(&io, &s->t_pmon, 1); io_le(&io, &s->t_non, 1); io_le(&io, &s->t_eon, 1); io_le(&io, &s->t_dir, 1); io_le(&io, &s->t_koff, 1);
    io_le(&io, &s->t_brr_next_addr, 2); io_le(&io, &s->t_adsr0, 1); io_le(&io, &s->t_brr_header, 1); io_le(&io, &s->t_brr_byte, 1);
    io_le(&io, &s->t_srcn, 1); io_le(&io, &s->t_esa, 1); io_le(&io, &s->t_echo_enabled, 1);
    for (int i = 0; i < 2; i++) io_le(&io, &s->t_main_out[i], 2);
    for (int i = 0; i < 2; i++) io_le(&io, &s->t_echo_out[i], 2);
    for (int i = 0; i < 2; i++) io_le(&io, &s->t_echo_in[i], 2);
    io_le(&io, &s->t_dir_addr, 2); io_le(&io, &s->t_pitch, 2); io_le(&io, &s->t_output, 2); io_le(&io, &s->t_echo_ptr, 2);
    io_le(&io, &s->t_looped, 1);
    io_raw(&io, s->ext_regs, 128);
    io_extra(&io);

    io_le(&io, &s->ref_time, 4); io_le(&io, &s->remainder, 4); io_le(&io, &s->dsp_clock, 4);
    io_raw(&io, s->cpu_ports, 4);
    if (put && io.ok) memset(io.p, 0, (size_t)(io.end - io.p));
    return io.ok ? 0 : -1;
}

/* ---- blocks ---- */

typedef struct { char name[4]; const uint8_t *p; size_t len; } Blk;
typedef struct { Blk b[40]; int n; int version; } Blocks;

static int parse(const uint8_t *in, size_t n, const char *magic, Blocks *bs)
{
    size_t ml = strlen(magic);
    if (n < ml + 6 || memcmp(in, magic, ml) || in[ml] != ':' || in[ml + 5] != '\n') return -1;
    bs->version = atoi((const char *)in + ml + 1) % 10000;
    bs->n = 0;
    for (size_t i = ml + 6; i + 11 <= n && bs->n < (int)(sizeof bs->b / sizeof bs->b[0]);) {
        const uint8_t *h = in + i;
        if (h[3] != ':' || h[10] != ':') return -1;
        size_t len = 0;
        if (h[4] == '-')
            len = (size_t)h[6] << 24 | (size_t)h[7] << 16 | (size_t)h[8] << 8 | h[9];
        else
            for (int k = 4; k < 10; k++) {
                if (h[k] < '0' || h[k] > '9') return -1;
                len = len * 10 + (h[k] - '0');
            }
        if (len > n - i - 11) return -1;
        Blk *b = &bs->b[bs->n++];
        memcpy(b->name, h, 3); b->name[3] = 0;
        b->p = h + 11; b->len = len;
        i += 11 + len;
    }
    return 0;
}

static const Blk *find(const Blocks *bs, const char *name)
{
    for (int i = 0; i < bs->n; i++)
        if (!strcmp(bs->b[i].name, name)) return &bs->b[i];
    return NULL;
}

/* A struct block: shorter than its table means a state this code cannot read; longer is 1.43's padding. */
static int get_struct(const Blocks *bs, const char *name, const Fld *t, int nt, void *base)
{
    const Blk *b = find(bs, name);
    if (!b || b->len < fld_size(t, nt)) return -1;
    fld_io(t, nt, (uint8_t *)base, (uint8_t *)b->p, 0);
    return 0;
}

static uint8_t *put_head(uint8_t *o, const char *name, size_t len)
{
    char h[12];
    snprintf(h, sizeof h, "%.3s:%06u:", name, (unsigned)len);
    memcpy(o, h, 11);
    return o + 11;
}

static uint8_t *put_raw(uint8_t *o, const char *name, const void *p, size_t len)
{
    o = put_head(o, name, len);
    memcpy(o, p, len);
    return o + len;
}

static uint8_t *put_struct(uint8_t *o, const char *name, const Fld *t, int nt, void *base)
{
    size_t len = fld_size(t, nt);
    o = put_head(o, name, len);
    fld_io(t, nt, (uint8_t *)base, o, 1);
    return o + len;
}

int smz_state_format(const uint8_t *data, size_t n)
{
    if (!data || n < 14 || data[13] != '\n') return SMZ_STATE_UNKNOWN;
    if (!memcmp(data, "#!snes9x:", 9)) return atoi((const char *)data + 9) == 1 ? SMZ_STATE_143 : SMZ_STATE_UNKNOWN;
    if (!memcmp(data, "#!s9xsnp:", 9)) {
        int v = atoi((const char *)data + 9);
        return v >= 12 && v <= 14 ? SMZ_STATE_V14 : SMZ_STATE_UNKNOWN;
    }
    return SMZ_STATE_UNKNOWN;
}

/* ---- timing ---- */

/* 1.43 counts 1368 cycles a line (hblank at 1024), 1.63 1364 master cycles. */
#define H143 1368
#define HBLANK143 1024
#define HMAX14 1364
#define IRQ_TRIGGER14 14

enum { EV143_HBLANK_START, EV143_HBLANK_END, EV143_HTIMER_BEFORE, EV143_HTIMER_AFTER };
enum { EV14_HBLANK_START = 1, EV14_HDMA_START, EV14_HCOUNTER_MAX, EV14_HDMA_INIT, EV14_RENDER, EV14_WRAM_REFRESH };

#define NMI_FLAG143 (1u << 7)
#define IRQ_PENDING143 (1u << 11)
#define H_BEAM143 (1u << 0)
#define V_BEAM143 (1u << 1)

static int32_t s16(u32 v) { return (int16_t)(uint16_t)v; }

/* The next 1.43 h-event, as S9xReschedule picks it. */
static void event143(const Ppu *p, int32_t v, int32_t cyc, Cpu *c)
{
    int32_t max = cyc >= HBLANK143 ? H143 : HBLANK143;
    u32 which = cyc >= HBLANK143 ? EV143_HBLANK_END : EV143_HBLANK_START;
    int32_t ht = s16(p->HTimerPosition);
    if (p->HTimerEnabled && ht < max && ht > cyc && (!p->VTimerEnabled || v == (int32_t)p->IRQVBeamPos)) {
        which = ht < HBLANK143 ? EV143_HTIMER_BEFORE : EV143_HTIMER_AFTER;
        max = ht;
    }
    c->WhichEvent = which;
    c->NextEvent = (u32)max;
}

/* The next 1.63 h-event after hc, in S9xReschedule's order. */
static void event14(const Tim *t, int32_t hc, Cpu *c)
{
    static const u32 ev[] = { EV14_HDMA_INIT, EV14_RENDER, EV14_WRAM_REFRESH, EV14_HBLANK_START, EV14_HDMA_START, EV14_HCOUNTER_MAX };
    const int32_t pos[] = { (int32_t)t->HDMAInit, (int32_t)t->RenderPos, (int32_t)t->WRAMRefreshPos, (int32_t)t->HBlankStart,
                            (int32_t)t->HDMAStart, (int32_t)t->H_Max };
    int i = 0;
    while (i < 5 && hc >= pos[i]) i++;
    c->WhichEvent = ev[i];
    c->NextEvent = (u32)pos[i];
}

static int32_t until14(const Cpu *c, const Tim *t, int field, int32_t hc, int32_t vc)
{
    int32_t v = (int32_t)c->V_Counter, total = 0, hm = (int32_t)t->H_Max_Master;
    if (vc - v > 0) {
        total += (vc - v) * hm;
        if (v <= 240 && vc > 240 && field) total -= 4;
    } else {
        if (vc == v && hc > (int32_t)c->Cycles) return hc;
        total += ((int32_t)t->V_Max - v) * hm;
        if (v <= 240 && field) total -= 4;
        total += vc * hm;
        if (vc > 240 && !field) total -= 4;
    }
    return total + hc;
}

/* S9xUpdateIRQPositions(false) without interlace, which a fresh load does not restore either. */
static void irq14(Ppu *p, const Cpu *c, Tim *t, int field)
{
    int32_t hpos = (int32_t)p->IRQHBeamPos, vt = (int32_t)p->IRQVBeamPos, next;
    int32_t ht = hpos * 4 + IRQ_TRIGGER14 - (hpos ? 0 : 4) + (hpos > 322 ? 2 : 0) + (hpos > 326 ? 2 : 0);
    p->HTimerPosition = (uint16_t)ht;
    p->VTimerPosition = (u32)vt;
    if ((p->VTimerEnabled && vt >= (int32_t)t->V_Max) || (!p->HTimerEnabled && !p->VTimerEnabled))
        next = 0x0fffffff;
    else if (!p->VTimerEnabled) {
        int32_t v = (int32_t)c->V_Counter;
        next = ht;
        if ((int32_t)c->Cycles > next - IRQ_TRIGGER14) { next += (int32_t)t->H_Max; v++; }
        if (v == 240 && field) next -= (hpos <= 322 ? 2 : 0) + (hpos <= 326 ? 2 : 0);
    } else if (!p->HTimerEnabled)
        next = until14(c, t, field, IRQ_TRIGGER14 - 4, vt);
    else {
        int f = field;
        next = until14(c, t, field, ht, vt);
        if (vt < (int32_t)c->V_Counter || (vt == (int32_t)c->V_Counter && next > (int32_t)t->H_Max)) f = !f;
        if (vt == 240 && f) next -= (hpos <= 322 ? 2 : 0) + (hpos <= 326 ? 2 : 0);
    }
    t->NextIRQTimer = (u32)next;
}

/* ---- sound ---- */

static const uint8_t IPL_ROM[64] = {
    0xcd, 0xef, 0xbd, 0xe8, 0x00, 0xc6, 0x1d, 0xd0, 0xfc, 0x8f, 0xaa, 0xf4, 0x8f, 0xbb, 0xf5, 0x78,
    0xcc, 0xf4, 0xd0, 0xfb, 0x2f, 0x19, 0xeb, 0xf4, 0xd0, 0xfc, 0x7e, 0xf4, 0xd0, 0x0b, 0xe4, 0xf5,
    0xcb, 0xf4, 0xd7, 0x00, 0xfc, 0xd0, 0xf3, 0xab, 0x01, 0x10, 0xef, 0x7e, 0xf4, 0x10, 0xeb, 0xba,
    0xf6, 0xda, 0x00, 0xba, 0xf4, 0xc4, 0xf4, 0xdd, 0x5d, 0xd0, 0xdb, 0x1f, 0x00, 0x00, 0xc0, 0xff,
};

/* soundux.cpp's rate tables: 1.43 stores envelope rates in these old units and converts them on load. */
static const int32_t ENV_COUNTER[32] = {
    0x0000, 0x000F, 0x0014, 0x0018, 0x001E, 0x0028, 0x0030, 0x003C, 0x0050, 0x0060, 0x0078, 0x00A0, 0x00C0, 0x00F0, 0x0140, 0x0180,
    0x01E0, 0x0280, 0x0300, 0x03C0, 0x0500, 0x0600, 0x0780, 0x0A00, 0x0C00, 0x0F00, 0x1400, 0x1800, 0x1E00, 0x2800, 0x3C00, 0x7800,
};
static const u32 OLD_ATTACK[16] = { 4100, 2600, 1500, 1000, 640, 380, 260, 160, 96, 64, 40, 24, 16, 10, 6, 1 };
static const u32 OLD_DECAY[8] = { 1200, 740, 440, 290, 180, 110, 74, 37 };
static const u32 OLD_SUSTAIN[32] = {
    0xffffffffu, 38000, 28000, 24000, 19000, 14000, 12000, 9400, 7100, 5900, 4700, 3500, 2900, 2400, 1800, 1500,
    1200, 880, 740, 590, 440, 370, 290, 220, 180, 150, 110, 92, 74, 55, 37, 18,
};
#define ENV_COUNTER_MAX 0x7800
#define ENV_MAX 0x7FF

enum { S_SILENT, S_ATTACK, S_DECAY, S_SUSTAIN, S_RELEASE, S_GAIN, S_INC_LINEAR, S_INC_BENT, S_DEC_LINEAR, S_DEC_EXP };
enum { M_NONE = 0, M_ADSR = 1, M_RELEASE = 4 };   /* the GAIN modes share the S_GAIN.. numbers */
enum { ENV_RELEASE, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN };

#define DSP_MVOLL 0x0c
#define DSP_MVOLR 0x1c
#define DSP_EVOLL 0x2c
#define DSP_EVOLR 0x3c
#define DSP_KON 0x4c
#define DSP_KOFF 0x5c
#define DSP_FLG 0x6c
#define DSP_ENDX 0x7c
#define DSP_EFB 0x0d
#define DSP_PMON 0x2d
#define DSP_NON 0x3d
#define DSP_EON 0x4d
#define DSP_DIR 0x5d
#define DSP_ESA 0x6d

/* bsnes SMP opcodes that run over several op_step calls: the operand bytes read after each finished step. */
static const uint8_t SMP_MULTI[][5] = {
    {0x7e,1,0,0,0}, {0x8f,2,2,0,0}, {0xaa,2,0,0,0}, {0xaf,0,0,0,0}, {0xba,1,1,0,0}, {0xbf,0,0,0,0}, {0xc4,1,1,0,0}, {0xc5,1,2,2,0},
    {0xc6,0,0,0,0}, {0xc7,1,1,1,1}, {0xc9,1,2,2,0}, {0xca,2,2,0,0}, {0xcb,1,1,0,0}, {0xcc,1,2,2,0}, {0xd4,1,1,0,0}, {0xd5,2,2,0,0},
    {0xd6,2,2,0,0}, {0xd7,1,1,1,1}, {0xd8,1,1,0,0}, {0xd9,1,1,0,0}, {0xda,1,1,1,0}, {0xdb,1,1,0,0}, {0xe4,1,0,0,0}, {0xe5,1,2,0,0},
    {0xe6,0,0,0,0}, {0xe7,1,1,1,0}, {0xe9,2,0,0,0}, {0xeb,1,0,0,0}, {0xec,2,0,0,0}, {0xf4,1,0,0,0}, {0xf5,2,0,0,0}, {0xf6,2,0,0,0},
    {0xf7,1,1,1,0}, {0xf8,1,0,0,0}, {0xf9,1,0,0,0}, {0xfa,1,1,2,0}, {0xfb,1,0,0,0},
};

/* The PC of the SMP instruction under way: its steps before the last only fetch operands, so 1.43 can run it whole. */
static u32 smp_restart_pc(const Snd *s)
{
    if (!s->opcode_cycle || s->opcode_cycle > 4) return s->pc & 0xffff;
    for (size_t i = 0; i < sizeof SMP_MULTI / sizeof SMP_MULTI[0]; i++)
        if (SMP_MULTI[i][0] == s->opcode_number) return (s->pc - 1 - SMP_MULTI[i][s->opcode_cycle]) & 0xffff;
    return s->pc & 0xffff;
}

/* SPC_DSP voices from 1.43's channels: envelope level and phase, and the BRR block, restarted from its first sample. */
static void voices_from_143(const Sound *so, Snd *s)
{
    for (int i = 0; i < 8; i++) {
        const Chan *ch = &so->ch[i];
        Voice *v = &s->v[i];
        u32 env = ch->state == S_SILENT ? 0 : (ch->envx & 0x7f) << 4;
        memset(v, 0, sizeof *v);
        v->brr_addr = ch->block_pointer & 0xffff;
        v->env = v->hidden_env = env;
        v->brr_offset = 1;
        v->env_mode = ch->state == S_SILENT || ch->state == S_RELEASE ? ENV_RELEASE
                    : ch->state == S_DECAY ? ENV_DECAY : ch->state == S_SUSTAIN ? ENV_SUSTAIN : ENV_ATTACK;
        v->envx_out = env >> 4;
        s->regs[i * 16 + 8] = (uint8_t)(env >> 4);
        s->regs[i * 16 + 9] = 0;
    }
}

/* 1.43's channels from SPC_DSP voices, in the old units S9xConvertSoundOldValues reads back. */
static void channels_from_v14(const Snd *s, const uint8_t *regs, const uint8_t *aram, Sound *so, u32 *keyed)
{
    *keyed = 0;
    for (int i = 0; i < 8; i++) {
        const Voice *v = &s->v[i];
        const uint8_t *r = regs + i * 16;
        Chan *ch = &so->ch[i];
        u32 adsr1 = r[5], adsr2 = r[6], gain = r[7], ar = adsr1 & 15, dr = adsr1 >> 4 & 7, sl = adsr2 >> 5, sr = adsr2 & 31;
        int32_t env = (int32_t)(v->env & ENV_MAX), rate = 0, target = 0;
        u32 state, mode;
        if (v->env_mode == ENV_RELEASE) {
            state = env ? S_RELEASE : S_SILENT; mode = env ? M_RELEASE : M_NONE;
            rate = env ? ENV_COUNTER_MAX : 0;
        } else if (adsr1 & 0x80) {
            mode = M_ADSR;
            if (v->env_mode == ENV_ATTACK) { state = S_ATTACK; rate = ENV_COUNTER[ar * 2 + 1]; target = ENV_MAX; }
            else if (v->env_mode == ENV_DECAY) { state = S_DECAY; rate = ENV_COUNTER[dr * 2 + 0x10]; target = 256 * (int32_t)(sl + 1); }
            else { state = S_SUSTAIN; rate = ENV_COUNTER[sr]; }
        } else if (!(gain & 0x80)) {
            state = mode = S_GAIN;
            env = target = (int32_t)(gain & 0x7f) << 4;
        } else {
            state = mode = gain & 0x40 ? (gain & 0x20 ? S_INC_BENT : S_INC_LINEAR) : (gain & 0x20 ? S_DEC_EXP : S_DEC_LINEAR);
            rate = ENV_COUNTER[gain & 31];
            target = gain & 0x40 ? ENV_MAX : 0;
        }
        int32_t vl = (int8_t)r[0], vr = (int8_t)r[1];
        uint8_t header = aram[v->brr_addr & 0xffff];
        memset(ch, 0, sizeof *ch);
        ch->state = state;
        ch->mode = mode;
        ch->type = regs[DSP_NON] >> i & 1;
        ch->volume_left = (uint16_t)vl;
        ch->volume_right = (uint16_t)vr;
        ch->hertz = (u32)((r[2] | r[3] << 8) & 0x3fff) * 8;
        ch->loop = header >> 1 & 1;
        ch->last_block = header & 1;
        ch->envx = (u32)env >> 4;
        ch->envx_target = (u32)target >> 4;
        ch->left_vol_level = (uint16_t)((env * vl) >> 11);
        ch->right_vol_level = (uint16_t)((env * vr) >> 11);
        /* rounded up so that 1.43's truncating conversion lands back on rate */
        ch->erate = (u32)(((uint64_t)rate * 0x10000 + ENV_COUNTER_MAX - 1) / ENV_COUNTER_MAX);
        ch->attack_rate = OLD_ATTACK[ar];
        ch->decay_rate = OLD_DECAY[dr];
        ch->sustain_rate = OLD_SUSTAIN[sr];
        ch->release_rate = 8;
        ch->sustain_level = sl + 1;
        ch->sample_number = r[4];
        ch->needs_decode = 1;
        ch->block_pointer = v->brr_addr & 0xffff;
        if (state != S_SILENT) *keyed |= 1u << i;
    }
    so->master_volume_left = (uint16_t)(int8_t)regs[DSP_MVOLL];
    so->master_volume_right = (uint16_t)(int8_t)regs[DSP_MVOLR];
    so->echo_volume_left = (uint16_t)(int8_t)regs[DSP_EVOLL];
    so->echo_volume_right = (uint16_t)(int8_t)regs[DSP_EVOLR];
    so->echo_enable = so->echo_channel_enable = regs[DSP_EON];
    so->echo_feedback = (u32)(int32_t)(int8_t)regs[DSP_EFB];
    so->echo_write_enabled = !(regs[DSP_FLG] & 0x20);
    so->pitch_mod = regs[DSP_PMON] & ~1u;
}

/* ---- the conversions ---- */

typedef struct {
    Cpu cpu, out;
    Reg reg;
    Ppu ppu;
    Dmas dma;
    Apu apu;
    AReg areg;
    Sound sou;
    Tim tim;
    Snd snd;
} Ctx;

static const Blk *raw_block(const Blocks *bs, const char *name, size_t min)
{
    const Blk *b = find(bs, name);
    return b && b->len >= min ? b : NULL;
}

int smz_state_143_to_v14(const uint8_t *in, size_t n, uint8_t **out, size_t *outn)
{
    Blocks bs;
    if (!in || !out || !outn || parse(in, n, "#!snes9x", &bs) || bs.version != 1) return -1;
    const Blk *vra = raw_block(&bs, "VRA", 0x10000), *ram = raw_block(&bs, "RAM", 0x20000), *sra = raw_block(&bs, "SRA", 1);
    const Blk *fil = raw_block(&bs, "FIL", 0x8000), *ara = raw_block(&bs, "ARA", 0x10000);
    if (!vra || !ram || !sra || !fil || !ara) return -1;
    Ctx *c = (Ctx *)calloc(1, sizeof *c);
    if (!c) return -1;
    int rc = -1;
    uint8_t *o = NULL;
    if (get_struct(&bs, "CPU", CPU143, NF(CPU143), &c->cpu) || get_struct(&bs, "REG", REG_T, NF(REG_T), &c->reg)
        || get_struct(&bs, "PPU", PPU143, NF(PPU143), &c->ppu) || get_struct(&bs, "DMA", DMA143, NF(DMA143), &c->dma)
        || get_struct(&bs, "APU", APU143, NF(APU143), &c->apu) || get_struct(&bs, "ARE", AREG143, NF(AREG143), &c->areg)
        || get_struct(&bs, "SOU", SOU143, NF(SOU143), &c->sou))
        goto done;

    const uint8_t *F = fil->p;
    int field = F[0x213f] >> 7 & 1;
    Cpu *cp = &c->out;
    Ppu *p = &c->ppu;
    Tim *t = &c->tim;
    int32_t v = (int32_t)c->cpu.V_Counter, hc = (int32_t)c->cpu.Cycles * HMAX14 / H143;
    if (hc < 0) hc = 0;
    if (hc >= HMAX14) hc = HMAX14 - 1;

    t->H_Max_Master = HMAX14;
    t->H_Max = v == 240 && field ? HMAX14 - 4 : HMAX14;
    t->V_Max_Master = t->V_Max = F[0x213f] & 0x10 ? 312 : 262;
    t->HBlankStart = 1096; t->HBlankEnd = 4; t->HDMAInit = 20; t->HDMAStart = 1106;
    t->NMITriggerPos = 0xffff; t->WRAMRefreshPos = 538; t->RenderPos = 128 * 4; t->InterlaceField = (u32)field;
    t->DMACPUSync = 18; t->NMIDMADelay = 24; t->IRQTriggerCycles = IRQ_TRIGGER14;

    /* 1.43 saves at the top of the frame (V=0), after its NMI; it keeps no NMI or WAI in flight there. */
    cp->Cycles = cp->PrevCycles = (u32)hc;
    cp->V_Counter = (u32)v;
    cp->MemSpeed = c->cpu.MemSpeed; cp->MemSpeedx2 = c->cpu.MemSpeedx2; cp->FastROMSpeed = c->cpu.FastROMSpeed;
    cp->IRQLine = (c->cpu.IRQActive & (H_BEAM143 | V_BEAM143)) != 0;
    event14(t, hc, cp);

    p->CGSavedByte = p->CGFLIP ? p->CGDATA[p->CGADD & 0xff] & 0xff : 0;
    p->OBJThroughMain = p->OBJThroughSub = p->OBJAddition = 0;
    p->OAMWriteRegister = 0;
    p->LastSprite = 127;
    p->GunHLatch = 0; p->GunVLatch = 1000;
    p->M7HOFS = p->BG[0].HOffset; p->M7VOFS = p->BG[0].VOffset;
    p->MosaicStart = 0;
    p->RecomputeClipWindows = 1;
    for (int i = 0; i < 6; i++) p->Clip[i].Counts = 0;
    p->BGnxOFSbyte = p->M7byte = 0;
    p->HDMA = F[0x420c]; p->HDMAEnded = 0;
    p->OpenBus1 = p->OpenBus2 = 0;
    u32 va = (p->VMA_Address & 0x7fff) * 2;
    p->VRAMReadBuffer = vra->p[va] | vra->p[va + 1] << 8;
    irq14(p, cp, t, field);

    for (int i = 0; i < 8; i++) {
        Dma *d = &c->dma.d[i];
        d->UnusedBit43x0 = F[0x4300 + 16 * i] >> 5 & 1;
        d->UnknownByte = F[0x430b + 16 * i];
        if (F[0x420c] >> i & 1 && d->HDMAIndirectAddressing) d->Count = d->IndirectAddress;
    }

    /* the SMP: bsnes keeps the RAM under the IPL ROM in apuram, and the ports the CPU writes apart from it */
    Snd *s = &c->snd;
    const Apu *a = &c->apu;
    memcpy(s->ram, ara->p, 0x10000);
    for (int i = 0; i < 4; i++) { s->ram[0xf4 + i] = (uint8_t)a->OutPorts[i]; s->cpu_ports[i] = ara->p[0xf4 + i]; }
    if (a->ShowROM & 0xff)
        for (int i = 0; i < 64; i++) s->ram[0xffc0 + i] = (uint8_t)a->ExtraRAM[i];
    s->pc = c->areg.PC & 0xffff; s->sp = c->areg.S & 0xff; s->a = c->areg.YA & 0xff; s->y = c->areg.YA >> 8 & 0xff; s->x = c->areg.X & 0xff;
    for (int i = 0; i < 8; i++) s->flag[i] = c->areg.P >> (7 - i) & 1;
    s->iplrom = (a->ShowROM & 0xff) != 0;
    s->dsp_addr = ara->p[0xf2]; s->f8 = ara->p[0xf8]; s->f9 = ara->p[0xf9];
    for (int i = 0; i < 3; i++) {
        s->timer[i][0] = a->TimerEnabled[i] & 1;
        s->timer[i][1] = a->TimerTarget[i] & 0xff;
        s->timer[i][3] = a->Timer[i] & 0xff;
        s->timer[i][4] = ara->p[0xfd + i] & 15;
    }
    for (int i = 0; i < 128; i++) s->regs[i] = (uint8_t)a->DSP[i];
    voices_from_143(&c->sou, s);
    s->every_other = 1; s->noise = 0x4000; s->endx_buf = s->regs[DSP_ENDX];
    s->t_pmon = s->regs[DSP_PMON] & 0xfe; s->t_non = s->regs[DSP_NON]; s->t_eon = s->regs[DSP_EON]; s->t_dir = s->regs[DSP_DIR];
    s->t_koff = s->regs[DSP_KOFF]; s->t_esa = s->regs[DSP_ESA]; s->t_echo_enabled = ~s->regs[DSP_FLG] & 0x20u;
    memcpy(s->ext_regs, s->regs, 128);
    s->ref_time = (u32)hc;

    size_t cap = 14 + 19 + 4 * 11 + 48 + 16 + 3000 + 200 + 0x10000 + 0x20000 + sra->len + 0x8000 + 13 * 11 + SND_BLOCK + 91 + 70 + 100;
    o = (uint8_t *)malloc(cap);
    if (!o) goto done;
    uint8_t *w = o;
    memcpy(w, "#!s9xsnp:0014\n", 14); w += 14;
    w = put_raw(w, "NAM", "Removed", 8);
    w = put_struct(w, "CPU", CPU14, NF(CPU14), cp);
    w = put_struct(w, "REG", REG_T, NF(REG_T), &c->reg);
    w = put_struct(w, "PPU", PPU14, NF(PPU14), p);
    w = put_struct(w, "DMA", DMA14, NF(DMA14), &c->dma);
    w = put_raw(w, "VRA", vra->p, 0x10000);
    w = put_raw(w, "RAM", ram->p, 0x20000);
    w = put_raw(w, "SRA", sra->p, sra->len);
    w = put_raw(w, "FIL", F, 0x8000);
    w = put_head(w, "SND", SND_BLOCK);
    if (snd_io(s, w, SND_BLOCK, 1)) goto done;
    w += SND_BLOCK;
    /* controls: version 1 restores only the port read positions, which the next auto-read rewinds anyway */
    w = put_head(w, "CTL", 91);
    memset(w, 0, 91); w[0] = 1; w += 91;
    w = put_struct(w, "TIM", TIM14, NF(TIM14), t);
    /* MSU-1 stopped: a core with MSU-1 on refuses a state without this block, one without it ignores it */
    w = put_head(w, "MSU", 99);
    memset(w, 0, 99);
    w[3] = 2;                  /* MSU1_STATUS: revision 2 */
    memset(w + 23, 0xff, 4);   /* MSU1_AudioResumeTrack: none */
    w += 99;
    *out = o; *outn = (size_t)(w - o); o = NULL;
    rc = 0;
done:
    free(o);
    free(c);
    return rc;
}

int smz_state_v14_to_143(const uint8_t *in, size_t n, uint8_t **out, size_t *outn)
{
    Blocks bs;
    if (!in || !out || !outn || parse(in, n, "#!s9xsnp", &bs) || bs.version < 12 || bs.version > 14) return -1;
    const Blk *vra = raw_block(&bs, "VRA", 0x10000), *ram = raw_block(&bs, "RAM", 0x20000), *sra = raw_block(&bs, "SRA", 1);
    const Blk *fil = raw_block(&bs, "FIL", 0x8000), *snd = find(&bs, "SND");
    if (!vra || !ram || !sra || !fil || !snd) return -1;
    Ctx *c = (Ctx *)calloc(1, sizeof *c);
    if (!c) return -1;
    int rc = -1;
    uint8_t *o = NULL;
    if (get_struct(&bs, "CPU", CPU14, NF(CPU14), &c->cpu) || get_struct(&bs, "REG", REG_T, NF(REG_T), &c->reg)
        || get_struct(&bs, "PPU", PPU14, NF(PPU14), &c->ppu) || get_struct(&bs, "DMA", DMA14, NF(DMA14), &c->dma)
        || snd_io(&c->snd, (uint8_t *)snd->p, snd->len, 0))
        goto done;

    const uint8_t *F = fil->p;
    Cpu *cp = &c->out;
    Ppu *p = &c->ppu;
    Snd *s = &c->snd;
    int32_t v = (int32_t)c->cpu.V_Counter, cyc = (int32_t)c->cpu.Cycles * H143 / HMAX14;
    if (cyc < 0) cyc = 0;

    p->HTimerPosition = p->HTimerEnabled ? (u32)(p->IRQHBeamPos * H143 / 342) : H143 + 1;
    if (p->HTimerEnabled && (p->HTimerPosition == H143 || p->HTimerPosition == HBLANK143)) p->HTimerPosition--;
    if (p->BGMode == 7) { p->BG[0].HOffset = p->M7HOFS; p->BG[0].VOffset = p->M7VOFS; }
    if (p->CGFLIP) p->CGDATA[p->CGADD & 0xff] = (p->CGDATA[p->CGADD & 0xff] & 0x7f00) | (p->CGSavedByte & 0xff);
    p->Joypad1ButtonReadPos = p->Joypad2ButtonReadPos = p->Joypad3ButtonReadPos = 0;
    p->MouseSpeed[0] = p->MouseSpeed[1] = 0;

    /* 1.63 saves at the start of vblank with its NMI still to fire; 1.43 cannot hold a pending NMI in a state,
     * so it resumes at the end of the line before, where its own h-event raises that NMI */
    if (c->cpu.NMIPending) {
        v = v ? v - 1 : 261;
        cyc = H143;
        cp->WhichEvent = EV143_HBLANK_END;
        cp->NextEvent = H143;
    } else
        event143(p, v, cyc, cp);
    cp->Cycles = (u32)cyc;
    cp->V_Counter = (u32)v;
    cp->MemSpeed = c->cpu.MemSpeed; cp->MemSpeedx2 = c->cpu.MemSpeedx2; cp->FastROMSpeed = c->cpu.FastROMSpeed;
    if (c->cpu.IRQLine || c->cpu.IRQExternal) {
        cp->Flags = IRQ_PENDING143;
        cp->IRQActive = p->HTimerEnabled ? H_BEAM143 : V_BEAM143;
    }

    for (int i = 0; i < 8; i++) c->dma.d[i].IndirectAddress = c->dma.d[i].Count;

    /* the SMP: 1.43 keeps the CPU's ports in its RAM and the IPL ROM over the RAM it hides */
    Apu *a = &c->apu;
    uint8_t *aram = (uint8_t *)malloc(0x10000);
    if (!aram) goto done;
    memcpy(aram, s->ram, 0x10000);
    for (int i = 0; i < 4; i++) { a->OutPorts[i] = s->ram[0xf4 + i]; aram[0xf4 + i] = s->cpu_ports[i]; }
    for (int i = 0; i < 64; i++) a->ExtraRAM[i] = s->ram[0xffc0 + i];
    a->ShowROM = s->iplrom & 1;
    if (a->ShowROM) memcpy(aram + 0xffc0, IPL_ROM, 64);
    aram[0xf1] = (uint8_t)(a->ShowROM << 7 | (s->timer[2][0] & 1) << 2 | (s->timer[1][0] & 1) << 1 | (s->timer[0][0] & 1));
    aram[0xf2] = (uint8_t)s->dsp_addr; aram[0xf8] = (uint8_t)s->f8; aram[0xf9] = (uint8_t)s->f9;
    for (int i = 0; i < 3; i++) {
        a->TimerEnabled[i] = s->timer[i][0] & 1;
        a->TimerTarget[i] = (s->timer[i][1] & 0xff) ? (s->timer[i][1] & 0xff) : 0x100;
        a->Timer[i] = s->timer[i][3] & 0xff;
        aram[0xfa + i] = (uint8_t)s->timer[i][1];
        aram[0xfd + i] = (uint8_t)(s->timer[i][4] & 15);
    }
    a->Cycles = (u32)cyc;
    u32 keyed;
    channels_from_v14(s, s->regs, s->ram, &c->sou, &keyed);
    for (int i = 0; i < 128; i++) a->DSP[i] = s->regs[i];
    for (int i = 0; i < 8; i++) a->DSP[i * 16 + 9] = 0;
    a->DSP[DSP_KON] &= keyed;
    a->KeyedChannels = keyed;
    c->areg.PC = smp_restart_pc(s);
    c->areg.S = s->sp & 0xff; c->areg.X = s->x & 0xff; c->areg.YA = (s->y & 0xff) << 8 | (s->a & 0xff);
    c->areg.P = 0;
    for (int i = 0; i < 8; i++) c->areg.P |= (s->flag[i] & 1) << (7 - i);

    size_t cap = 14 + 11 + 8 + 12 * 11 + 64 + 16 + 3000 + 200 + 0x10000 + 0x20000 + sra->len + 0x8000 + 300 + 0x10000 + 1200 + 100;
    o = (uint8_t *)malloc(cap);
    if (!o) { free(aram); goto done; }
    uint8_t *w = o;
    memcpy(w, "#!snes9x:0001\n", 14); w += 14;
    w = put_raw(w, "NAM", "smz3", 5);
    w = put_struct(w, "CPU", CPU143, NF(CPU143), cp);
    w = put_struct(w, "REG", REG_T, NF(REG_T), &c->reg);
    w = put_struct(w, "PPU", PPU143, NF(PPU143), p);
    w = put_struct(w, "DMA", DMA143, NF(DMA143), &c->dma);
    w = put_raw(w, "VRA", vra->p, 0x10000);
    w = put_raw(w, "RAM", ram->p, 0x20000);
    w = put_raw(w, "SRA", sra->p, sra->len < 0x20000 ? sra->len : 0x20000);   /* all the SRAM 1.43 has */
    w = put_raw(w, "FIL", F, 0x8000);
    w = put_struct(w, "APU", APU143, NF(APU143), a);
    w = put_struct(w, "ARE", AREG143, NF(AREG143), &c->areg);
    w = put_raw(w, "ARA", aram, 0x10000);
    w = put_struct(w, "SOU", SOU143, NF(SOU143), &c->sou);
    free(aram);
    *out = o; *outn = (size_t)(w - o); o = NULL;
    rc = 0;
done:
    free(o);
    free(c);
    return rc;
}
