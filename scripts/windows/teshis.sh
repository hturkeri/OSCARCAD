#!/usr/bin/env bash
export PATH=$MINGW_PREFIX/bin:$PATH
pacman -S --needed --noconfirm $MINGW_PACKAGE_PREFIX-gdb > logs/teshis_pacman.log 2>&1
echo "=== gdb pc_mkapp ===" > logs/teshis_gdb.log
gdb -batch -ex run -ex bt -ex "info threads" --args build-$(echo $MSYSTEM | tr A-Z a-z)/bin/pc_mkapp.exe samples/bracket.pcad build-$(echo $MSYSTEM | tr A-Z a-z)/bin/persistcad_runner.exe "$TEMP/pc_teshis_app" >> logs/teshis_gdb.log 2>&1
echo "=== ctest (GUI hariç) ===" > logs/teshis_ctest.log
ctest --test-dir build-$(echo $MSYSTEM | tr A-Z a-z) -E "gui_harness" -j6 --output-on-failure --timeout 300 >> logs/teshis_ctest.log 2>&1
echo BITTI >> logs/teshis_ctest.log
