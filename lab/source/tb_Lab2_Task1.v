`timescale 1ns/1ps

module tb_Lab2_Task1 (
  );

  reg [2:0] i;
  wire [1:0] o;

  Lab2_Task1 uut (
               .in(i),
               .out(o)
             );

  initial begin
    // initialize inputs
    // #100;
    i = 3'b000;
    // wait 100ns
    #100;
    // update inputs
    i = 3'b001;
    #100;
    // update inputs
    i = 3'b010;
    #100;
    // update inputs
    i = 3'b011;
    #100;
    // update inputs
    i = 3'b100;
    #100;
    // update inputs
    i = 3'b101;
    #100;
    // update inputs
    i = 3'b110;
    #100;
    // update inputs
    i = 3'b111;
    #100;
    $finish;
  end

  // output wave file
  initial begin
    $dumpfile("tb_Lab2_Task1.vcd");
    $dumpvars;
  end

endmodule
