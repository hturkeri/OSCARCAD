"""Dağıtım klasöründeki tüm PE dosyalarının import ettiği DLL'leri tarar,
klasörde olmayan ve Windows sistem DLL'i olmayanları raporlar."""
import os, re, shutil, subprocess, sys
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass
root = sys.argv[1]
OBJDUMP = os.environ.get('OBJDUMP') or next(
    (t for t in ('x86_64-w64-mingw32-objdump', 'llvm-objdump', 'objdump') if shutil.which(t)), None)
if not OBJDUMP:
    sys.exit('objdump bulunamadi (OBJDUMP ortam degiskeni ile verin)')
SYSTEM = {x.lower() for x in """
kernel32.dll user32.dll gdi32.dll advapi32.dll shell32.dll ole32.dll oleaut32.dll
ws2_32.dll crypt32.dll bcrypt.dll ncrypt.dll secur32.dll shlwapi.dll comdlg32.dll
comctl32.dll winmm.dll imm32.dll version.dll setupapi.dll cfgmgr32.dll dwmapi.dll
uxtheme.dll opengl32.dll glu32.dll d3d9.dll d3d11.dll d3d12.dll dxgi.dll dxva2.dll
dwrite.dll d2d1.dll mf.dll mfplat.dll mfreadwrite.dll mfuuid.dll evr.dll strmiids.dll
iphlpapi.dll dnsapi.dll netapi32.dll userenv.dll wtsapi32.dll authz.dll mpr.dll
winhttp.dll wininet.dll urlmon.dll normaliz.dll wldap32.dll psapi.dll dbghelp.dll
ntdll.dll rpcrt4.dll shcore.dll usp10.dll msimg32.dll winspool.drv powrprof.dll
hid.dll avrt.dll ksuser.dll propsys.dll api-ms-win-core-synch-l1-2-0.dll
msvcrt.dll ucrtbase.dll cabinet.dll wintrust.dll imagehlp.dll vfw32.dll msacm32.dll
mscms.dll d3dcompiler_47.dll dcomp.dll windowscodecs.dll mfcore.dll ncrypt.dll
bcryptprimitives.dll sspicli.dll wsock32.dll credui.dll netutils.dll dhcpcsvc.dll
""".split()}
files = {}
for dp, _, fs in os.walk(root):
    for f in fs:
        if f.lower().endswith(('.exe', '.dll')):
            files[f.lower()] = os.path.join(dp, f)
present = set(files)
missing = {}
for name, path in sorted(files.items()):
    out = subprocess.run([OBJDUMP, '-p', path],
                         capture_output=True, text=True).stdout
    if not re.search(r'pei?-x86-64|coff-x86-64', out):
        print('UYARI: 64-bit PE degil:', path)
    for dll in re.findall(r'DLL Name: (\S+)', out, re.I):
        d = dll.lower()
        if d in present or d in SYSTEM or d.startswith(('api-ms-win-', 'ext-ms-')):
            continue
        missing.setdefault(d, []).append(name)
print(f'{len(files)} PE dosyası tarandı.')
if missing:
    print('EKSİK DLL:')
    for d, users in sorted(missing.items()):
        print(f'  {d}  <- {", ".join(users[:5])}')
    sys.exit(1)
print('Eksik DLL yok.')
