module Lab2_Task3(
    input [7:0] a,
    input [7:0] b,
    input [7:0] c,
    input [7:0] d,
    output reg [7:0] min,
    output reg [7:0] max
  );

  reg [7:0] min1, min2, max1, max2;

  always @(*) begin
    min1 = a < b ? a : b;
    min2 = c < d ? c : d;
    max1 = a > b ? a : b;
    max2 = c > d ? c : d;
    min = min1 < min2 ? min1 : min2;
    max = max1 > max2 ? max1 : max2;
  end

endmodule
