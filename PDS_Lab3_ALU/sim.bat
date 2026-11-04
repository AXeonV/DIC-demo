
iverilog -o testbench.out source/tb_alu.v source/alu.v source/CLA*.v

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
