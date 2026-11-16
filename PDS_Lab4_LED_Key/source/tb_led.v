`timescale 1us / 1us

module tb_led;

  reg clk = 1'b0;
  reg [7:0] keys = 8'b1111_1111;
  wire [7:0] led;

  led #(
        .CLK_FREQ(5_000)
      ) uut (
        .clk(clk),
        .keys(keys),
        .led(led)
      );

  always #1 clk = ~clk;

  initial begin
    // Observe automatic blinking and rotation for over 8 seconds
    #85000;

    // Key 0: press with jitter, then release with jitter
    keys[0] = 0;
    #10 keys[0] = 1;
    #10 keys[0] = 0;
    #15 keys[0] = 1;
    #15 keys[0] = 0;
    #500;
    keys[0] = 1;
    #10 keys[0] = 0;
    #10 keys[0] = 1;
    #500;

    // Key 1, 2, 3: toggle corresponding LEDs
    keys[1] = 0;
    #500 keys[1] = 1;
    #500;
    keys[2] = 0;
    #500 keys[2] = 1;
    #500;
    keys[3] = 0;
    #500 keys[3] = 1;
    #500;

    // Key 0: short press (less than debounce delay)
    keys[0] = 0;
    #50 keys[0] = 1;
    #600;

    // Keys 4-7: select LED, then continue rotation
    keys[4] = 0;
    #500 keys[4] = 1;
    #10500;

    keys[5] = 0;
    #500 keys[5] = 1;
    #10500;

    keys[6] = 0;
    #500 keys[6] = 1;
    #10500;

    keys[7] = 0;
    #500 keys[7] = 1;
    #10500;

    // Key 4 and Key 6: multiple key input
    keys[4] = 0;
    #40 keys[6] = 0;
    #700;
    keys[4] = 1;
    keys[6] = 1;
    #11000;

    $finish;
  end

  initial begin
    $dumpfile("tb_Lab4_LED.vcd");
    $dumpvars;
    $display("save to tb_Lab4_LED.vcd");
  end

endmodule
