module Lab2_Task1 (
    input [2:0] in,
    output [1:0] out
  );

  wire [1:0] d_xor;
  wire [1:0] d_and;
  wire d_or;

  assign d_xor[0] = in[0] ^ in[1];
  assign d_xor[1] = d_xor[0] ^ in[2];
  assign d_and[0] = d_xor[0] & in[2];
  assign d_and[1] = in[0] & in[1];
  assign d_or = d_and[0] | d_and[1];

  assign out[0] = d_xor[1];
  assign out[1] = d_or;

endmodule
