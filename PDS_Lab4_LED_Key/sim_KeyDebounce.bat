
iverilog -o testbench.out source/tb_KeyDebounce.v source/KeyDebounce.v 

if %errorlevel% neq 0 (
    echo "************** has bug! ***************"
    pause
    exit /b
)

vvp testbench.out

gtkwave testbench.wave

pause
