@echo off
setlocal
cd /d "%~dp0"
if not exist "arch\x86\boot\uefi\build\esp\EFI\BOOT\BOOTX64.EFI" (
    echo Loader UEFI mancante. Completare la preparazione descritta in README.md.
    goto failed
)
if not exist "kernel\build\meson-kernel\build.ninja" (
    meson setup kernel/build/meson-kernel kernel --vsenv -Dprofile=ordinary
    if errorlevel 1 goto failed
) else (
    meson configure kernel/build/meson-kernel -Dprofile=ordinary
    if errorlevel 1 goto failed
)
meson compile -C kernel/build/meson-kernel
if errorlevel 1 goto failed
if not exist "kernel\build\meson-qemu-runner\build.ninja" (
    meson setup kernel/build/meson-qemu-runner test/tooling/qemu-runner --vsenv
    if errorlevel 1 goto failed
)
meson compile -C kernel/build/meson-qemu-runner
if errorlevel 1 goto failed
kernel\build\meson-qemu-runner\qemu_runner.exe desktop %*
if errorlevel 1 goto failed
exit /b 0

:failed
echo Avvio interrotto. Per usare TCG eseguire avvia.bat --tcg.
pause
exit /b 1
