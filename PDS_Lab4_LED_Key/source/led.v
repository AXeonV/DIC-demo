module led #(
    parameter CLK_FREQ = 50_000_000
  )
  (
    input clk,
    input [7:0] keys,
    output [7:0] led
  );

  parameter CLK_1s_MAX = CLK_FREQ - 1;

  wire [7:0] keys_stable;
  KeyDebounce #(
                .CLK_FREQ (CLK_FREQ)
              ) u_keydeb (
                .clk(clk),
                .keys(keys),
                .keys_stable(keys_stable)
              );

  reg [26:0] clk_cnt = 26'b0;
  always @(posedge clk) begin
    if (clk_cnt >= CLK_1s_MAX) begin
      clk_cnt <= 26'b0;
    end
    else begin
      clk_cnt <= clk_cnt + 26'b1;
    end
  end

  wire trigger1s;
  assign trigger1s = (clk_cnt == CLK_1s_MAX) ? 1'b1 : 1'b0;

  reg [3:0] time_state = 4'b0000;
  always @(posedge clk) begin
    if (trigger1s) begin
      time_state <= time_state + 4'b1;
    end
  end

  reg [7:0] keys_pre = 8'b1111_1111;
  always @(posedge clk) begin
    keys_pre <= keys_stable;
  end

  wire [7:0] key_pressed;
  assign key_pressed = keys_pre & ~keys_stable;

  reg [7:0] led_state = 8'b0001_0000;

  reg [7:0] led_nxt1 = 8'b0001_0000;
  always @(*) begin // 1st control: key press
    led_nxt1 = led_state;
    if (key_pressed[0]) led_nxt1[0] = ~led_state[0];
    if (key_pressed[1]) led_nxt1[1] = ~led_state[1];
    if (key_pressed[2]) led_nxt1[2] = ~led_state[2];
    if (key_pressed[3]) led_nxt1[3] = ~led_state[3];
    if (key_pressed[4]) led_nxt1[7:4] = 4'b0001;
    if (key_pressed[5]) led_nxt1[7:4] = 4'b0010;
    if (key_pressed[6]) led_nxt1[7:4] = 4'b0100;
    if (key_pressed[7]) led_nxt1[7:4] = 4'b1000;
  end

  reg [7:0] led_nxt2 = 8'b0001_0000;
  always @(*) begin // 2nd control: time state
    led_nxt2 = led_nxt1;
    if (trigger1s) begin
      led_nxt2[0] = ~led_nxt1[0];
      if ((time_state & 1) == 1) led_nxt2[1] = ~led_nxt1[1];
      if ((time_state & 3) == 3) led_nxt2[2] = ~led_nxt1[2];
      if ((time_state & 7) == 7) led_nxt2[3] = ~led_nxt1[3];
      led_nxt2[7:4] = {led_nxt1[6:4], led_nxt1[7]};
    end
  end

  always @(posedge clk) begin
    led_state <= led_nxt2;
  end

  assign led = led_state;

endmodule
