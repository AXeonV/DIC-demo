module Test (
    input a,
    input b,
    output c,
    output out_and,
    output out_or,
    output out_xor
  );

  assign out_and = a & b;
  assign out_or = a | b;
  assign out_xor = a ^ b;
  assign c = a & b;

endmodule
