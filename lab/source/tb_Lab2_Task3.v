`timescale 1ns/1ps

module tb_Lab2_Task3(

  );

  reg [7:0] a, b, c, d;
  reg [7:0] min, max;

  Lab2_Task3 uut (
               .a   	(a    ),
               .b   	(b    ),
               .c   	(c    ),
               .d   	(d    ),
               .min 	(min  ),
               .max 	(max  )
             );

  initial begin
    // Test 1: increasing order
    a = 8'd10;
    b = 8'd20;
    c = 8'd30;
    d = 8'd40;
    #100;

    // Test 2: decreasing order
    a = 8'd200;
    b = 8'd150;
    c = 8'd100;
    d = 8'd50;
    #100;

    // Test 3: all equal
    a = 8'd88;
    b = 8'd88;
    c = 8'd88;
    d = 8'd88;
    #100;

    // Test 4: boundary values
    a = 8'd0;
    b = 8'd255;
    c = 8'd127;
    d = 8'd128;
    #100;

    // Test 5: random order
    a = 8'd73;
    b = 8'd12;
    c = 8'd201;
    d = 8'd99;
    #100;

    $finish;
  end

  initial begin
    $dumpfile("tb_Lab2_Task3.vcd");
    $dumpvars;
  end

endmodule
