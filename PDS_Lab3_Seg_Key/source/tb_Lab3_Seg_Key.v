`timescale 1ns/1ps

module tb_Lab3_Seg_Key();
  reg [7:0]  keys;
  wire [3:0] seg_sel;
  wire [7:0] seg_data;

  Lab3_Seg_Key uut (.keys(keys), .seg_sel(seg_sel), .seg_data(seg_data));

  initial begin
    keys = 8'b110_11110; #100;
    keys = 8'b101_11101; #100;
    keys = 8'b100_11011; #100;
    keys = 8'b011_10111; #100;
    keys = 8'b101_01111; #100;
    keys = 8'b110_00000; #100;
    keys = 8'b100_10110; #100;
    keys = 8'b011_11001; #100;
    keys = 8'b110_00111; #100;
    keys = 8'b101_11100; #100;
    keys = 8'b011_00000; #100;
    keys = 8'b111_11110; #100;
    keys = 8'b010_10111; #100;
    keys = 8'b001_01111; #100;
    keys = 8'b000_11110; #100;
    keys = 8'b100_01010; #100;
  end
  
  initial begin
    $dumpfile("tb_Lab3_Seg_Key.vcd");
    $dumpvars;
    $display("save to tb_Lab3_Seg_Key.vcd");
  end

endmodule
