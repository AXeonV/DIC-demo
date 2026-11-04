`timescale 1ns / 1ps

module alu (
    input wire [4:0] f,
    input wire [7:0] a,
    input wire [7:0] b,
    output reg [7:0] s,
    output reg err
  );

  // define values of function f
  parameter ADD = 5'b00000;
  parameter SUB = 5'b00001;
  parameter MUL = 5'b00010;

  parameter SHL = 5'b00011;
  parameter SHR = 5'b00100;

  parameter BAND = 5'b00101;
  parameter BOR  = 5'b00110;
  parameter BXOR = 5'b00111;

  parameter AND = 5'b01000;
  parameter OR  = 5'b01001;

  parameter EQ = 5'b01010;
  parameter NE = 5'b01011;
  parameter GE = 5'b01100;
  parameter LE = 5'b01101;
  parameter GT = 5'b01110;
  parameter LT = 5'b01111;

  parameter NEG  = 5'b10000;
  parameter BNOT = 5'b10001;
  parameter NOT  = 5'b10010;

  // my code
  wire [7:0] add_res;
  wire [7:0] sub_res;
  CLA8Bit adder(.a(a), .b(b), .ci(1'b0), .s(add_res), .co());
  CLA8Bit suber(.a(a), .b(~b), .ci(1'b1), .s(sub_res), .co());

  wire signed [15:0] mul_res = $signed(a) * $signed(b);

  always @(*) begin
    err = 1'b0;

    case (f)
      ADD: begin
        s = add_res;
        err = (a[7] == b[7]) && (s[7] != a[7]);
      end
      SUB: begin
        s = sub_res;
        err = (a[7] != b[7]) && (s[7] != a[7]);
      end
      MUL: begin
        s = mul_res;
        err = (mul_res > 127) || (mul_res < -128);
      end
      NEG: begin
        s = -$signed(a);
        err = ($signed(a) == -128);
      end

      SHL: s = a << b;
      SHR: s = a >> b;

      BAND: s = a & b;
      BOR: s = a | b;
      BXOR: s = a ^ b;
      BNOT: s = ~a;

      AND: s = a && b;
      OR: s = a || b;
      NOT: s = !a;

      EQ: s = (a == b);
      NE: s = (a != b);
      GE: s = (a >= b);
      LE: s = (a <= b);
      GT: s = (a > b);
      LT: s = (a < b);

      default: err = 1'b1;
    endcase

    if (err) s = 8'b00000000;
  end

endmodule //alu
