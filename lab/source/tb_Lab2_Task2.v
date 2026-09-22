`timescale 1ns/1ps

module tb_Lab2_Task2(

  );

  reg [5:0] in;
  wire [4:0] out_both;
  wire [5:1] out_any;
  wire [5:0] out_different;
  wire [3:0] out_add;
  wire [3:0] out_sub;

  Lab2_Task2 uut (
               .in            	(in             ),
               .out_both      	(out_both       ),
               .out_any       	(out_any        ),
               .out_different 	(out_different  ),
               .out_add       	(out_add        ),
               .out_sub       	(out_sub        )
             );

  initial begin
    in = 6'b000000;
    #100;
    in = 6'b111111;
    #100;
    in = 6'b101010;
    #100;
    in = 6'b010101;
    #100;
    in = 6'b110001;
    #100;
    $finish;
  end

  initial begin
    $dumpfile("tb_Lab2_Task2.vcd");
    $dumpvars;
  end

endmodule
