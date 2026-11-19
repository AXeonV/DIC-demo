`timescale 1us / 1us

module tb_KeyDebounce;

  reg clk = 1'b0;
  reg [7:0] keys = 8'b1111_1111;
  wire [7:0] keys_stable;

  KeyDebounce #(
                .CLK_FREQ(500_000),
                .KEY_CNT(8)
              ) uut (
                .clk(clk),
                .keys(keys),
                .keys_stable(keys_stable)
              );

  always #1 clk = ~clk;

  initial begin
    // Key 0: press with bouncing
    #1000 keys[0] = 0;
    #100  keys[0] = 1;
    #100  keys[0] = 0;
    #150  keys[0] = 1;
    #150  keys[0] = 0;
    #25000;

    // Key 0: release with bouncing
    keys[0] = 1;
    #100  keys[0] = 0;
    #100  keys[0] = 1;
    #150  keys[0] = 0;
    #150  keys[0] = 1;
    #25000;

    // Key 1: late bouncing resets the 20 ms timer
    keys[1] = 0;
    #19000 keys[1] = 1;
    #500   keys[1] = 0;
    #25000;

    // Key 1: release
    keys[1] = 1;
    #25000;

    // Key 2: short press (less than 20 ms)
    keys[2] = 0;
    #5000 keys[2] = 1;
    #25000;

    // Keys 4 and 6: pressed 5 ms apart
    keys[4] = 0;
    #5000 keys[6] = 0;
    #25000;

    // Release Key 4 and Key 6
    keys[4] = 1;
    #25000;
    keys[6] = 1;
    #25000;

    // Keys 3 and 7: pressed simultaneously
    keys[3] = 0;
    keys[7] = 0;
    #25000;

    // Release simultaneously
    keys[3] = 1;
    keys[7] = 1;
    #25000;

    $finish;
  end

  initial begin
    $dumpfile("KeyDebounce.vcd");
    $dumpvars(0, tb_KeyDebounce);
    $display("save to KeyDebounce.vcd");
  end

endmodule
