module CLA8Bit(
    input wire[7:0] a,
    input wire[7:0] b,
    input wire ci,
    output wire[7:0] s,
    output wire co
  );

  wire ct;

  CLA4Bit low_r (
            .a(a[3:0]),
            .b(b[3:0]),
            .ci(ci),
            .s(s[3:0]),
            .co(ct)
          );

  CLA4Bit high_r (
            .a(a[7:4]),
            .b(b[7:4]),
            .ci(ct),
            .s(s[7:4]),
            .co(co)
          );

endmodule
