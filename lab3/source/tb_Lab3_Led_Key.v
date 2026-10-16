`timescale 1ns / 1ps
module tb_Lab3_Led_Key ();

  reg [3:0]  keys;
  wire [7:0] leds;

  Lab3_Led_Key uut (.keys(keys), .leds(leds));

  initial begin
    keys = 3'b000;
    #100;
    keys = 3'b001;
    #100;
    keys = 3'b010;
    #100;
    keys = 3'b011;
    #100;
    keys = 3'b100;
    #100;
    keys = 3'b101;
    #100;
    keys = 3'b110;
    #100;
    keys = 3'b111;
    #100;
    $finish;
  end

  initial begin
    $dumpfile("testbench.vcd");
    $dumpvars;
    $display("save to testbench.vcd");
  end
  
endmodule
