module Lab3_Seg_Key(
    input [7:0] keys,
    output [3:0] seg_sel,
    output [7:0] seg_data
  );

  reg [4:0] key1;
  reg [2:0] key2;
  reg [3:0] idx;
  reg [7:0] num;

  assign seg_sel = idx;
  assign seg_data = ~num;

  always @(*) begin
    key1 = ~keys;
    key2 = ~(keys >> 5);

    if (key2 >= 1 && key2 <= 4) idx = 4'b0001 << (key2 - 1);
    else idx = 4'b0000;

    if (idx && (key1 & 5'b10000)) num = 8'b01101101;
    else if (idx && (key1 & 5'b01000)) num = 8'b01100110;
    else if (idx && (key1 & 5'b00100)) num = 8'b01001111;
    else if (idx && (key1 & 5'b00010)) num = 8'b01011011;
    else if (idx && (key1 & 5'b00001)) num = 8'b00000110;
    else num = 8'b00000000;
  end

endmodule
