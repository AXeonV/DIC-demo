module KeyDebounce #(
    parameter CLK_FREQ = 50_000_000,  // clock frequency(Mhz), 50 MHz
    parameter KEY_CNT = 8
  )
  (
    input                clk,         // clock input
    input  [KEY_CNT-1:0] keys,        // input key pins, raw input
    output [KEY_CNT-1:0] keys_stable  // output stable key status, 0 - press down
  );

  parameter WAITING_20ms_MAX = CLK_FREQ / 50 - 1;

  reg [KEY_CNT-1:0] keys_pre = {KEY_CNT{1'b1}};
  reg [KEY_CNT-1:0] keys_deb = {KEY_CNT{1'b1}};

  reg waiting = 0;
  reg [26:0] clk_cnt = 26'b0;

  wire trigger20ms;
  assign trigger20ms = (clk_cnt == WAITING_20ms_MAX) ? 1'b1 : 1'b0;

  always @(posedge clk) begin
    keys_pre <= keys;
    if (keys != keys_pre) begin
      waiting <= 1;
      clk_cnt <= 0;
    end
    else if (waiting) begin
      if (trigger20ms) begin
        keys_deb <= keys;
        waiting <= 0;
        clk_cnt <= 0;
      end
      else begin
        clk_cnt <= clk_cnt + 1;
      end
    end
  end

  assign keys_stable = keys_deb;

endmodule
