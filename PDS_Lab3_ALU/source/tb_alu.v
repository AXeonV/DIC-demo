`timescale 1ns / 1ps

module tb_alu;

  // Inputs
  reg [4:0] f;
  reg [7:0] a;
  reg [7:0] b;

  // Outputs
  wire [7:0] s;
  wire err;

  // Expected true result
  reg [7:0] expected_s;
  reg expected_err;

  wire pass;
  assign pass = (expected_err === err) && (expected_s === s);

  // Instantiate the Unit Under Test (UUT)
  alu uut (
        .f(f),
        .a(a),
        .b(b),
        .s(s),
        .err(err)
      );

  initial begin

    // ADD
    // case 1: Two negative numbers, no overflow
    f = 5'b00000;
    a = 8'b10011100;
    b = 8'b11110110;
    expected_s = 8'b10010010;
    expected_err = 1'b0;
    #100;

    // case 2: Zero plus zero
    f = 5'b00000;
    a = 8'b00000000;
    b = 8'b00000000;
    expected_s = 8'b00000000;
    expected_err = 1'b0;
    #100;

    // case 3: Maximum positive number plus zero
    f = 5'b00000;
    a = 8'b01111111;
    b = 8'b00000000;
    expected_s = 8'b01111111;
    expected_err = 1'b0;
    #100;

    // case 4: Minimum negative number plus zero
    f = 5'b00000;
    a = 8'b10000000;
    b = 8'b00000000;
    expected_s = 8'b10000000;
    expected_err = 1'b0;
    #100;

    // case 5: Positive overflow
    f = 5'b00000;
    a = 8'b01111111;
    b = 8'b00000001;
    expected_s = 8'b00000000;
    expected_err = 1'b1;
    #100;

    // case 6: Negative overflow
    f = 5'b00000;
    a = 8'b10000000;
    b = 8'b11111111;
    expected_s = 8'b00000000;
    expected_err = 1'b1;
    #100;

    // case 7: Sum equals 127
    f = 5'b00000;
    a = 8'b01100100;
    b = 8'b00011011;
    expected_s = 8'b01111111;
    expected_err = 1'b0;
    #100;

    // case 8: Sum equals -128
    f = 5'b00000;
    a = 8'b10011100;
    b = 8'b11100100;
    expected_s = 8'b10000000;
    expected_err = 1'b0;
    #100;

    // case 9: Mixed signs
    f = 5'b00000;
    a = 8'b10000000;
    b = 8'b01111111;
    expected_s = 8'b11111111;
    expected_err = 1'b0;
    #100;

    // case 10: Sum equals zero
    f = 5'b00000;
    a = 8'b11111111;
    b = 8'b00000001;
    expected_s = 8'b00000000;
    expected_err = 1'b0;
    #100;

    // SUB
    // case 11: Zero minus zero
    f = 5'b00001;
    a = 8'b00000000;
    b = 8'b00000000;
    expected_s = 8'b00000000;
    expected_err = 1'b0;
    #100;
    // case 12: Positive minus positive
    f = 5'b00001;
    a = 8'b00000111;
    b = 8'b00000011;
    expected_s = 8'b00000100;
    expected_err = 1'b0;
    #100;

    // case 13: Negative minus negative
    f = 5'b00001;
    a = 8'b11111100;
    b = 8'b11110111;
    expected_s = 8'b00000101;
    expected_err = 1'b0;
    #100;

    // case 14: Positive overflow
    f = 5'b00001;
    a = 8'b01111111;
    b = 8'b11111111;
    expected_s = 8'b00000000;
    expected_err = 1'b1;
    #100;

    // case 15: Negative overflow
    f = 5'b00001;
    a = 8'b10000000;
    b = 8'b00000001;
    expected_s = 8'b00000000;
    expected_err = 1'b1;
    #100;

    // case 16: -128 minus -1
    f = 5'b00001;
    a = 8'b10000000;
    b = 8'b11111111;
    expected_s = 8'b10000001;
    expected_err = 1'b0;
    #100;

    // case 17: Equal operands
    f = 5'b00001;
    a = 8'b01111111;
    b = 8'b01111111;
    expected_s = 8'b00000000;
    expected_err = 1'b0;
    #100;

    // case 18: Result equals -128
    f = 5'b00001;
    a = 8'b11111111;
    b = 8'b01111111;
    expected_s = 8'b10000000;
    expected_err = 1'b0;
    #100;

    // case 19: Zero minus -128
    f = 5'b00001;
    a = 8'b00000000;
    b = 8'b10000000;
    expected_s = 8'b00000000;
    expected_err = 1'b1;
    #100;

    // case 20: Result equals 127
    f = 5'b00001;
    a = 8'b00110010;
    b = 8'b10110011;
    expected_s = 8'b01111111;
    expected_err = 1'b0;
    #100;

    // MUL
    // case 21: Zero times negative
    f = 5'b00010;
    a = 8'b00000000;
    b = 8'b10000000;
    expected_s = 8'b00000000;
    expected_err = 1'b0;
    #100;

    // case 22: Multiply by one
    f = 5'b00010;
    a = 8'b00000001;
    b = 8'b01111111;
    expected_s = 8'b01111111;
    expected_err = 1'b0;
    #100;

    // case 23: -128 times one
    f = 5'b00010;
    a = 8'b10000000;
    b = 8'b00000001;
    expected_s = 8'b10000000;
    expected_err = 1'b0;
    #100;

    // case 24: -128 times -1, overflow
    f = 5'b00010;
    a = 8'b10000000;
    b = 8'b11111111;
    expected_s = 8'b00000000;
    expected_err = 1'b1;
    #100;

    // case 25: Positive multiplication
    f = 5'b00010;
    a = 8'b00001100;
    b = 8'b00001010;
    expected_s = 8'b01111000;
    expected_err = 1'b0;
    #100;

    // case 26: Positive multiplication overflow
    f = 5'b00010;
    a = 8'b00010000;
    b = 8'b00001000;
    expected_s = 8'b00000000;
    expected_err = 1'b1;
    #100;

    // case 27: Multiplication result equals -128
    f = 5'b00010;
    a = 8'b11110000;
    b = 8'b00001000;
    expected_s = 8'b10000000;
    expected_err = 1'b0;
    #100;

    // case 28: Negative multiplication overflow
    f = 5'b00010;
    a = 8'b11101100;
    b = 8'b00001010;
    expected_s = 8'b00000000;
    expected_err = 1'b1;
    #100;

    // case 29: Negative times negative
    f = 5'b00010;
    a = 8'b11110110;
    b = 8'b11110100;
    expected_s = 8'b01111000;
    expected_err = 1'b0;
    #100;

    // case 30: Negative times negative overflow
    f = 5'b00010;
    a = 8'b11110101;
    b = 8'b11110100;
    expected_s = 8'b00000000;
    expected_err = 1'b1;
    #100;

    // case 31: Positive times negative
    f = 5'b00010;
    a = 8'b00000010;
    b = 8'b11111101;
    expected_s = 8'b11111010;
    expected_err = 1'b0;
    #100;

    // case 32: Large multiplication overflow
    f = 5'b00010;
    a = 8'b01111111;
    b = 8'b01111111;
    expected_s = 8'b00000000;
    expected_err = 1'b1;
    #100;

    // NEG
    // case 33: Negate zero
    f = 5'b10000;
    a = 8'b00000000;
    b = 8'b10101010;
    expected_s = 8'b00000000;
    expected_err = 1'b0;
    #100;

    // case 34: Negate positive one
    f = 5'b10000;
    a = 8'b00000001;
    b = 8'b00000000;
    expected_s = 8'b11111111;
    expected_err = 1'b0;
    #100;

    // case 35: Negate negative one
    f = 5'b10000;
    a = 8'b11111111;
    b = 8'b00000000;
    expected_s = 8'b00000001;
    expected_err = 1'b0;
    #100;

    // case 36: Negate maximum positive number
    f = 5'b10000;
    a = 8'b01111111;
    b = 8'b00000000;
    expected_s = 8'b10000001;
    expected_err = 1'b0;
    #100;

    // case 37: Negate -127
    f = 5'b10000;
    a = 8'b10000001;
    b = 8'b00010010;
    expected_s = 8'b01111111;
    expected_err = 1'b0;
    #100;

    // case 38: Negate -128, overflow
    f = 5'b10000;
    a = 8'b10000000;
    b = 8'b11111111;
    expected_s = 8'b00000000;
    expected_err = 1'b1;
    #100;

    // SHL
    // case 39: Shift left by zero
    f = 5'b00011;
    a = 8'b00000001;
    b = 8'b00000000;
    expected_s = 8'b00000001;
    expected_err = 1'b0;
    #100;

    // case 40: Shift left by one
    f = 5'b00011;
    a = 8'b00000001;
    b = 8'b00000001;
    expected_s = 8'b00000010;
    expected_err = 1'b0;
    #100;

    // case 41: Discard the highest bit
    f = 5'b00011;
    a = 8'b10000001;
    b = 8'b00000001;
    expected_s = 8'b00000010;
    expected_err = 1'b0;
    #100;

    // case 42: Shift left by four
    f = 5'b00011;
    a = 8'b00001111;
    b = 8'b00000100;
    expected_s = 8'b11110000;
    expected_err = 1'b0;
    #100;

    // case 43: Shift left by seven
    f = 5'b00011;
    a = 8'b11111111;
    b = 8'b00000111;
    expected_s = 8'b10000000;
    expected_err = 1'b0;
    #100;

    // case 44: Shift left by eight
    f = 5'b00011;
    a = 8'b11111111;
    b = 8'b00001000;
    expected_s = 8'b00000000;
    expected_err = 1'b0;
    #100;

    // case 45: Shift left by 255
    f = 5'b00011;
    a = 8'b00000001;
    b = 8'b11111111;
    expected_s = 8'b00000000;
    expected_err = 1'b0;
    #100;

    // SHR
    // case 46: Shift right by zero
    f = 5'b00100;
    a = 8'b10000000;
    b = 8'b00000000;
    expected_s = 8'b10000000;
    expected_err = 1'b0;
    #100;

    // case 47: Logical shift right
    f = 5'b00100;
    a = 8'b10000000;
    b = 8'b00000001;
    expected_s = 8'b01000000;
    expected_err = 1'b0;
    #100;

    // case 48: Shift right by four
    f = 5'b00100;
    a = 8'b11111111;
    b = 8'b00000100;
    expected_s = 8'b00001111;
    expected_err = 1'b0;
    #100;

    // case 49: Shift out the lowest bit
    f = 5'b00100;
    a = 8'b00000001;
    b = 8'b00000001;
    expected_s = 8'b00000000;
    expected_err = 1'b0;
    #100;

    // case 50: Shift right by seven
    f = 5'b00100;
    a = 8'b11111111;
    b = 8'b00000111;
    expected_s = 8'b00000001;
    expected_err = 1'b0;
    #100;

    // case 51: Shift right by eight
    f = 5'b00100;
    a = 8'b11111111;
    b = 8'b00001000;
    expected_s = 8'b00000000;
    expected_err = 1'b0;
    #100;

    // case 52: Shift right by 255
    f = 5'b00100;
    a = 8'b11111111;
    b = 8'b11111111;
    expected_s = 8'b00000000;
    expected_err = 1'b0;
    #100;

    // BAND
    // case 53: Bitwise AND
    f = 5'b00101;
    a = 8'b10100101;
    b = 8'b00111100;
    expected_s = 8'b00100100;
    expected_err = 1'b0;
    #100;

    // case 54: Bitwise AND with all ones
    f = 5'b00101;
    a = 8'b11111111;
    b = 8'b00110111;
    expected_s = 8'b00110111;
    expected_err = 1'b0;
    #100;

    // case 55: Bitwise AND with zero
    f = 5'b00101;
    a = 8'b00000000;
    b = 8'b11111111;
    expected_s = 8'b00000000;
    expected_err = 1'b0;
    #100;

    // BOR
    // case 56: Bitwise OR
    f = 5'b00110;
    a = 8'b10100101;
    b = 8'b00111100;
    expected_s = 8'b10111101;
    expected_err = 1'b0;
    #100;

    // case 57: Bitwise OR with zero
    f = 5'b00110;
    a = 8'b00000000;
    b = 8'b10000000;
    expected_s = 8'b10000000;
    expected_err = 1'b0;
    #100;

    // case 58: Bitwise OR with all ones
    f = 5'b00110;
    a = 8'b11111111;
    b = 8'b00000001;
    expected_s = 8'b11111111;
    expected_err = 1'b0;
    #100;

    // BXOR
    // case 59: Bitwise XOR
    f = 5'b00111;
    a = 8'b10100101;
    b = 8'b00111100;
    expected_s = 8'b10011001;
    expected_err = 1'b0;
    #100;

    // case 60: XOR with identical operands
    f = 5'b00111;
    a = 8'b11111111;
    b = 8'b11111111;
    expected_s = 8'b00000000;
    expected_err = 1'b0;
    #100;

    // case 61: XOR with complementary operands
    f = 5'b00111;
    a = 8'b01010101;
    b = 8'b10101010;
    expected_s = 8'b11111111;
    expected_err = 1'b0;
    #100;

    // BNOT
    // case 62: Bitwise NOT of zero
    f = 5'b10001;
    a = 8'b00000000;
    b = 8'b00110101;
    expected_s = 8'b11111111;
    expected_err = 1'b0;
    #100;

    // case 63: Bitwise NOT of all ones
    f = 5'b10001;
    a = 8'b11111111;
    b = 8'b00000001;
    expected_s = 8'b00000000;
    expected_err = 1'b0;
    #100;

    // case 64: Bitwise NOT of mixed pattern
    f = 5'b10001;
    a = 8'b10100101;
    b = 8'b00000000;
    expected_s = 8'b01011010;
    expected_err = 1'b0;
    #100;

    // AND
    // case 65: Both operands are zero
    f = 5'b01000;
    a = 8'b00000000;
    b = 8'b00000000;
    expected_s = 8'b00000000;
    expected_err = 1'b0;
    #100;

    // case 66: Only b is nonzero
    f = 5'b01000;
    a = 8'b00000000;
    b = 8'b00001000;
    expected_s = 8'b00000000;
    expected_err = 1'b0;
    #100;

    // case 67: Only a is nonzero
    f = 5'b01000;
    a = 8'b00001000;
    b = 8'b00000000;
    expected_s = 8'b00000000;
    expected_err = 1'b0;
    #100;

    // case 68: Both operands are nonzero
    f = 5'b01000;
    a = 8'b10000000;
    b = 8'b00000010;
    expected_s = 8'b00000001;
    expected_err = 1'b0;
    #100;

    // OR
    // case 69: Both operands are zero
    f = 5'b01001;
    a = 8'b00000000;
    b = 8'b00000000;
    expected_s = 8'b00000000;
    expected_err = 1'b0;
    #100;

    // case 70: Only a is nonzero
    f = 5'b01001;
    a = 8'b00000001;
    b = 8'b00000000;
    expected_s = 8'b00000001;
    expected_err = 1'b0;
    #100;

    // case 71: Only b is nonzero
    f = 5'b01001;
    a = 8'b00000000;
    b = 8'b10000000;
    expected_s = 8'b00000001;
    expected_err = 1'b0;
    #100;

    // case 72: Both operands are nonzero
    f = 5'b01001;
    a = 8'b10000000;
    b = 8'b11111111;
    expected_s = 8'b00000001;
    expected_err = 1'b0;
    #100;

    // NOT
    // case 73: Logical NOT of zero
    f = 5'b10010;
    a = 8'b00000000;
    b = 8'b11111111;
    expected_s = 8'b00000001;
    expected_err = 1'b0;
    #100;

    // case 74: Logical NOT of one
    f = 5'b10010;
    a = 8'b00000001;
    b = 8'b00000000;
    expected_s = 8'b00000000;
    expected_err = 1'b0;
    #100;

    // case 75: Logical NOT of nonzero value
    f = 5'b10010;
    a = 8'b10000000;
    b = 8'b00000000;
    expected_s = 8'b00000000;
    expected_err = 1'b0;
    #100;

    // EQ
    // case 76: Equal operands
    f = 5'b01010;
    a = 8'b10100101;
    b = 8'b10100101;
    expected_s = 8'b00000001;
    expected_err = 1'b0;
    #100;

    // case 77: Unequal operands
    f = 5'b01010;
    a = 8'b01111111;
    b = 8'b10000000;
    expected_s = 8'b00000000;
    expected_err = 1'b0;
    #100;

    // NE
    // case 78: Equal operands
    f = 5'b01011;
    a = 8'b11111111;
    b = 8'b11111111;
    expected_s = 8'b00000000;
    expected_err = 1'b0;
    #100;

    // case 79: Unequal operands
    f = 5'b01011;
    a = 8'b11111111;
    b = 8'b00000000;
    expected_s = 8'b00000001;
    expected_err = 1'b0;
    #100;

    // GE
    // case 80: Unsigned greater than or equal
    f = 5'b01100;
    a = 8'b10000000;
    b = 8'b01111111;
    expected_s = 8'b00000001;
    expected_err = 1'b0;
    #100;

    // case 81: Unsigned greater than or equal, false
    f = 5'b01100;
    a = 8'b01111111;
    b = 8'b10000000;
    expected_s = 8'b00000000;
    expected_err = 1'b0;
    #100;

    // case 82: Equal boundary for GE
    f = 5'b01100;
    a = 8'b10000000;
    b = 8'b10000000;
    expected_s = 8'b00000001;
    expected_err = 1'b0;
    #100;

    // LE
    // case 83: Unsigned less than or equal
    f = 5'b01101;
    a = 8'b01111111;
    b = 8'b10000000;
    expected_s = 8'b00000001;
    expected_err = 1'b0;
    #100;

    // case 84: Unsigned less than or equal, false
    f = 5'b01101;
    a = 8'b10000000;
    b = 8'b01111111;
    expected_s = 8'b00000000;
    expected_err = 1'b0;
    #100;

    // case 85: Equal boundary for LE
    f = 5'b01101;
    a = 8'b10000000;
    b = 8'b10000000;
    expected_s = 8'b00000001;
    expected_err = 1'b0;
    #100;

    // GT
    // case 86: Unsigned greater than
    f = 5'b01110;
    a = 8'b10000000;
    b = 8'b01111111;
    expected_s = 8'b00000001;
    expected_err = 1'b0;
    #100;

    // case 87: Unsigned greater than, false
    f = 5'b01110;
    a = 8'b01111111;
    b = 8'b10000000;
    expected_s = 8'b00000000;
    expected_err = 1'b0;
    #100;

    // case 88: Equal operands for GT
    f = 5'b01110;
    a = 8'b10000000;
    b = 8'b10000000;
    expected_s = 8'b00000000;
    expected_err = 1'b0;
    #100;

    // LT
    // case 89: Unsigned less than
    f = 5'b01111;
    a = 8'b01111111;
    b = 8'b10000000;
    expected_s = 8'b00000001;
    expected_err = 1'b0;
    #100;

    // case 90: Unsigned less than, false
    f = 5'b01111;
    a = 8'b10000000;
    b = 8'b01111111;
    expected_s = 8'b00000000;
    expected_err = 1'b0;
    #100;

    // case 91: Equal operands for LT
    f = 5'b01111;
    a = 8'b10000000;
    b = 8'b10000000;
    expected_s = 8'b00000000;
    expected_err = 1'b0;
    #100;

    // INVALID FUNCTION CODES
    // case 92: First invalid operation code
    f = 5'b10011;
    a = 8'b00010011;
    b = 8'b01001111;
    expected_s = 8'b00000000;
    expected_err = 1'b1;
    #100;

    // case 93: Invalid code with zero inputs
    f = 5'b10100;
    a = 8'b00000000;
    b = 8'b00000000;
    expected_s = 8'b00000000;
    expected_err = 1'b1;
    #100;

    // case 94: Invalid code with boundary values
    f = 5'b10111;
    a = 8'b11111111;
    b = 8'b01111111;
    expected_s = 8'b00000000;
    expected_err = 1'b1;
    #100;

    // case 95: Invalid code with negative values
    f = 5'b11011;
    a = 8'b10001111;
    b = 8'b10000000;
    expected_s = 8'b00000000;
    expected_err = 1'b1;
    #100;

    // case 96: Invalid code with complementary patterns
    f = 5'b11110;
    a = 8'b01010101;
    b = 8'b10101010;
    expected_s = 8'b00000000;
    expected_err = 1'b1;
    #100;

    // case 97: Maximum invalid operation code
    f = 5'b11111;
    a = 8'b01111111;
    b = 8'b11111111;
    expected_s = 8'b00000000;
    expected_err = 1'b1;
    #100;

    #100 $finish;
  end

  // waveform output, DO NOT modify below codes
  initial begin
    $dumpfile("tb_Lab3_ALU.vcd");
    $dumpvars;
    $display("save to tb_Lab3_ALU.vcd");
  end

endmodule
