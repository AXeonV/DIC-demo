
iverilog -o testbench.out source/tb_led.v source/led.v 

if %errorlevel% neq 0 (
    echo "************** has bug! ***************"
    pause
    exit /b
)

vvp testbench.out

gtkwave testbench.wave

pause
