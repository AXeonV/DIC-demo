module Lab3_Led_Key(
  input [3:0] keys,
  output [7:0] leds
);

assign leds[1:0] = ~keys[1:0];
assign leds[3:2] = keys[3:2];

reg [2:0] nums = 3'b000;

assign leds[6:4] = nums;
assign leds[7] = nums & 1;

always @(*) begin
  nums = keys[0] + keys[1] + keys[2] + keys[3];
end

endmodule