module Lab2_Task2(
    input [5:0] in,
    output reg [4:0] out_both,
    output reg [5:1] out_any,
    output reg [5:0] out_different,
    output reg [3:0] out_add,
    output reg [3:0] out_sub
  );

  always @(*) begin
    out_both = in & (in >> 1);
    out_any = (in | (in << 1)) >> 1;
    out_different = in ^ ((in >> 1) | ((in & 1) << 5));
    out_add = in[5:3] + in[2:0];
    out_sub = in[5:3] - in[2:0];
  end

endmodule
