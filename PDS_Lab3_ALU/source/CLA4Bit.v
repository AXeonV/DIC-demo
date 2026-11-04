module CLA4Bit(
    input wire[3:0] a,
    input wire[3:0] b,
    input wire ci,
    output wire[3:0] s,
    output wire co
  );

  wire [3:0] p, g;
  wire [4:0] c;

  assign p = a ^ b;
  assign g = a & b;

  assign c[0] = ci;
  assign c[1] = g[0] | (p[0] & ci);
  assign c[2] = g[1] | (p[1] & g[0]) | (p[1] & p[0] & ci);
  assign c[3] = g[2] | (p[2] & g[1]) | (p[2] & p[1] & g[0]) | (p[2] & p[1] & p[0] & ci);
  assign c[4] = g[3] | (p[3] & g[2]) | (p[3] & p[2] & g[1]) | (p[3] & p[2] & p[1] & g[0]) | (p[3] & p[2] & p[1] & p[0] & ci);
  assign co = c[4];

  assign s = p ^ c;

endmodule
