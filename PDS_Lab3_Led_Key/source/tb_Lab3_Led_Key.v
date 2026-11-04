`timescale 1ns/1ps
module tb_Lab3_Led_Key ();

  reg [3:0]  keys;
  wire [7:0] leds;

  Lab3_Led_Key uut (.keys(keys), .leds(leds));

  reg [4:0] i;

  initial begin
    for (i = 0; i < 16; i = i + 1) begin
      keys = i;
      #100;
    end
    $finish;
  end

  initial begin
    $dumpfile("tb_Lab3_Led_Key.vcd");
    $dumpvars;
    $display("save to tb_Lab3_Led_Key.vcd");
  end

endmodule
