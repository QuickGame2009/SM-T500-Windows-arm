#!/usr/bin/env python3
# Diagnostico: antes de arrancar una aplicacion EFI (bootmgfw.efi) imprime el estado de la CPU y del GIC,
# y envuelve Stall / GetTime / SetWatchdogTimer / GetNextMonotonicCount para registrar sus llamadas.
# Uso: python3 patch.py <ruta a MdeModulePkg/Core/Dxe/Image/Image.c>
import re, sys
p = sys.argv[1]
raw = open(p, "rb").read().decode("utf-8")
nl = "\r\n" if "\r\n" in raw else "\n"
assert "DG state" not in raw, "ya parcheado"

helper = r'''
#if defined (MDE_CPU_AARCH64)
STATIC EFI_STALL                    mDgStall;
STATIC EFI_GET_TIME                 mDgGetTime;
STATIC EFI_SET_WATCHDOG_TIMER       mDgWatchdog;
STATIC EFI_GET_NEXT_MONOTONIC_COUNT mDgMono;
STATIC UINT64                       mDgCnt[4];
STATIC BOOLEAN                      mDgBusy;
STATIC BOOLEAN                      mDgInstalled;

STATIC
BOOLEAN
DgLog (
  IN UINTN  Index
  )
{
  mDgCnt[Index]++;
  if (mDgBusy) {
    return FALSE;
  }

  return (BOOLEAN)((mDgCnt[Index] <= 20) || ((mDgCnt[Index] % 20000) == 0));
}

STATIC
EFI_STATUS
EFIAPI
DgStall (
  IN UINTN  Microseconds
  )
{
  if (DgLog (0)) {
    mDgBusy = TRUE;
    DEBUG ((DEBUG_ERROR, "DG Stall(%lu) #%lu\n", (UINT64)Microseconds, mDgCnt[0]));
    mDgBusy = FALSE;
  }

  return mDgStall (Microseconds);
}

STATIC
EFI_STATUS
EFIAPI
DgGetTime (
  OUT EFI_TIME               *Time,
  OUT EFI_TIME_CAPABILITIES  *Capabilities OPTIONAL
  )
{
  EFI_STATUS  Status;

  Status = mDgGetTime (Time, Capabilities);
  if (DgLog (1)) {
    mDgBusy = TRUE;
    DEBUG ((DEBUG_ERROR, "DG GetTime #%lu -> %r %u-%u-%u %u:%u:%u\n", mDgCnt[1], Status, (UINT32)Time->Year, (UINT32)Time->Month, (UINT32)Time->Day, (UINT32)Time->Hour, (UINT32)Time->Minute, (UINT32)Time->Second));
    mDgBusy = FALSE;
  }

  return Status;
}

STATIC
EFI_STATUS
EFIAPI
DgWatchdog (
  IN UINTN   Timeout,
  IN UINT64  WatchdogCode,
  IN UINTN   DataSize,
  IN CHAR16  *WatchdogData OPTIONAL
  )
{
  if (DgLog (2)) {
    mDgBusy = TRUE;
    DEBUG ((DEBUG_ERROR, "DG SetWatchdogTimer(%lu) #%lu\n", (UINT64)Timeout, mDgCnt[2]));
    mDgBusy = FALSE;
  }

  return mDgWatchdog (Timeout, WatchdogCode, DataSize, WatchdogData);
}

STATIC
EFI_STATUS
EFIAPI
DgMono (
  OUT UINT64  *Count
  )
{
  if (DgLog (3)) {
    mDgBusy = TRUE;
    DEBUG ((DEBUG_ERROR, "DG GetNextMonotonicCount #%lu\n", mDgCnt[3]));
    mDgBusy = FALSE;
  }

  return mDgMono (Count);
}

STATIC
VOID
DgDump (
  VOID
  )
{
  UINT64  El, Daif, Sctlr, Vbar, Frq, Kctl, PCtl, PCval, PCt, Pmr, Grp1, Sre, Tcr, Mair;
  UINT32  Dctl, Rwake, Ren, Rpend, Ract, Rprio7;

  __asm__ volatile ("mrs %0, CurrentEL" : "=r" (El));
  El >>= 2;
  __asm__ volatile ("mrs %0, DAIF" : "=r" (Daif));
  if (El == 2) {
    __asm__ volatile ("mrs %0, sctlr_el2" : "=r" (Sctlr));
    __asm__ volatile ("mrs %0, vbar_el2" : "=r" (Vbar));
    __asm__ volatile ("mrs %0, tcr_el2" : "=r" (Tcr));
    __asm__ volatile ("mrs %0, mair_el2" : "=r" (Mair));
  } else {
    __asm__ volatile ("mrs %0, sctlr_el1" : "=r" (Sctlr));
    __asm__ volatile ("mrs %0, vbar_el1" : "=r" (Vbar));
    __asm__ volatile ("mrs %0, tcr_el1" : "=r" (Tcr));
    __asm__ volatile ("mrs %0, mair_el1" : "=r" (Mair));
  }

  __asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (Frq));
  __asm__ volatile ("mrs %0, cntp_ctl_el0" : "=r" (PCtl));
  __asm__ volatile ("mrs %0, cntp_cval_el0" : "=r" (PCval));
  __asm__ volatile ("mrs %0, cntpct_el0" : "=r" (PCt));
  Kctl = 0;
  if (El == 1) {
    __asm__ volatile ("mrs %0, cntkctl_el1" : "=r" (Kctl));
  }

  __asm__ volatile ("mrs %0, S3_0_C4_C6_0" : "=r" (Pmr));
  __asm__ volatile ("mrs %0, S3_0_C12_C12_7" : "=r" (Grp1));
  __asm__ volatile ("mrs %0, S3_0_C12_C12_5" : "=r" (Sre));

  Dctl    = (*(volatile UINT32 *)(UINTN)(0x0F200000));
  Rwake   = (*(volatile UINT32 *)(UINTN)(0x0F300014));
  Ren     = (*(volatile UINT32 *)(UINTN)(0x0F310100));
  Rpend   = (*(volatile UINT32 *)(UINTN)(0x0F310200));
  Ract    = (*(volatile UINT32 *)(UINTN)(0x0F310300));
  Rprio7  = (*(volatile UINT32 *)(UINTN)(0x0F310400 + 28));

  DEBUG ((DEBUG_ERROR, "DG state EL=%lu DAIF=%lx SCTLR=%lx VBAR=%lx TCR=%lx MAIR=%lx\n", El, Daif, Sctlr, Vbar, Tcr, Mair));
  DEBUG ((DEBUG_ERROR, "DG timer CNTFRQ=%lu CNTKCTL=%lx CNTP_CTL=%lx CNTP_CVAL=%lx CNTPCT=%lx\n", Frq, Kctl, PCtl, PCval, PCt));
  DEBUG ((DEBUG_ERROR, "DG gic ICC_PMR=%lx IGRPEN1=%lx SRE=%lx GICD_CTLR=%x GICR_WAKER=%x ISEN0=%x ISPEND0=%x ISACT0=%x PRIO30=%x\n", Pmr, Grp1, Sre, Dctl, Rwake, Ren, Rpend, Ract, Rprio7));

  if (!mDgInstalled) {
    mDgInstalled         = TRUE;
    mDgStall             = gBS->Stall;
    mDgGetTime           = gDxeCoreRT->GetTime;
    mDgWatchdog          = gBS->SetWatchdogTimer;
    mDgMono              = gBS->GetNextMonotonicCount;
    gBS->Stall                    = DgStall;
    gBS->SetWatchdogTimer         = DgWatchdog;
    gBS->GetNextMonotonicCount    = DgMono;
    gDxeCoreRT->GetTime           = DgGetTime;
    gBS->Hdr.CRC32 = 0;
    gBS->CalculateCrc32 (gBS, gBS->Hdr.HeaderSize, &gBS->Hdr.CRC32);
    gDxeCoreRT->Hdr.CRC32 = 0;
    gBS->CalculateCrc32 (gDxeCoreRT, gDxeCoreRT->Hdr.HeaderSize, &gDxeCoreRT->Hdr.CRC32);
  }
}
#endif

'''.replace("\r\n", "\n").replace("\n", nl)

# 1) helper antes de CoreStartImage
m = re.search(r"(\r?\n)(/\*\*\r?\n  Transfer control to a loaded image's entry point\.)", raw)
if not m:
    m = re.search(r"(\r?\n)(EFI_STATUS\r?\nEFIAPI\r?\nCoreStartImage \()", raw)
assert m, "ancla de CoreStartImage no encontrada"
raw = raw[:m.start(2)] + helper.lstrip("\r\n") + nl + raw[m.start(2):]

# 2) llamada antes del punto de entrada
anc = re.compile(r"(    Image->Started = TRUE;\r?\n)(    Image->Status  = Image->EntryPoint \(ImageHandle, Image->Info\.SystemTable\);)")
assert len(anc.findall(raw)) == 1
call = nl.join([
    "#if defined (MDE_CPU_AARCH64)",
    "    if (Image->ImageContext.ImageType == EFI_IMAGE_SUBSYSTEM_EFI_APPLICATION) {",
    "      DgDump ();",
    "    }",
    "",
    "#endif",
    ""])
raw = anc.sub(lambda mm: mm.group(1) + call + mm.group(2), raw, count=1)

# 3) includes
open(p, "wb").write(raw.encode("utf-8"))
print("parche aplicado a", p)
