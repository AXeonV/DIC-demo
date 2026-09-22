`timescale 1ns/1ps

module tb_Test (
  );

  reg a;
  reg b;
  wire out_a;
  wire out_o;
  wire out_x;

  Test uut (
         .a (a),
         .b (b),
         .out_and (out_a),
         .out_or (out_o),
         .out_xor (out_x)
       );

  initial begin
    // initialize inputs
    #100;
    a = 0;
    b = 0;
    // wait 100ns
    #100;
    // update inputs
    a = 1;
    b = 0;
    #100;
    // update inputs
    a = 0;
    b = 1;
    #100;
    // update inputs
    a = 1;
    b = 1;
    #100;
    $finish;
  end

  // output wave file
  initial begin
    $dumpfile("tb_Test.vcd");
    $dumpvars;
  end

endmodule
