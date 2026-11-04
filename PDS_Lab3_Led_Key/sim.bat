
iverilog -o testbench.out source/*.v

if %errorlevel% neq 0 (
    rem echo "************** has bug! ***************"
    pause
    exit /b
)

vvp testbench.out

gtkwave testbench.wave

if %errorlevel% neq 0 (
    rem echo "************** has bug! ***************"
    pause
    exit /b
)
