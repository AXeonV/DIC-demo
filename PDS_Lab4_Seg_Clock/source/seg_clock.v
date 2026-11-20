`timescale 1ns / 1ns

module seg_clock
#(
    parameter CLK_FREQ = 50_000_000 // clock frequency(hz), 50 MHz, T = 1/50 = 0.02 us
) 
(
    input wire                   clk,              // clock input
	input wire                   rst_n,            // asynchronous reset input, low active 
    input wire [5:1]             keys,             // input key pins
    output [3:0]                 seg_sel,          // output digital led chip select
	output [7:0]                 seg_data          // output eight segment digital tube output, MSB is the decimal point
);

// local paramerts
parameter DELAY_1S = CLK_FREQ - 1;

reg [31:0] clk_cnt = 32'b0;
assign trigger1s = (clk_cnt == DELAY_1S);

reg [5:1] keys_prev;
wire [5:1] keys_stable;
wire [5:1] key_press_down;
assign key_press_down = keys_prev & (~keys_stable);

KeyDebounce #(
    .CLK_FREQ(CLK_FREQ),
    .KEY_CNT(5)
) u_key (
    .clk(clk),
    .keys(keys),
    .keys_stable(keys_stable)
);

parameter S_RUN    = 1'b1;
parameter S_STOP   = 1'b0;
reg current_state = S_STOP;
reg next_state = S_STOP;

reg [7:0] clock_mm = 8'd0;
reg [7:0] clock_ss = 8'd0;

reg [3:0] seg_num_0 = 4'd0;
reg [3:0] seg_num_1 = 4'd0;
reg [3:0] seg_num_2 = 4'd0;
reg [3:0] seg_num_3 = 4'd0;


seg_display #(
    .CLK_FREQ(CLK_FREQ)
) u_seg_display (
    .clk(clk),
    .rst_n(rst_n),
    .seg_num_0(seg_num_0),
    .seg_num_1(seg_num_1),
    .seg_num_2(seg_num_2),
    .seg_num_3(seg_num_3),
    .seg_sel(seg_sel),
    .seg_data(seg_data)
);

// timer for scaner counter, driven by clk & reset
always @(posedge clk) begin
    // reset 
    if (rst_n == 1'b0) begin
        clk_cnt <= 32'b0;
    end 
    else if (current_state == S_RUN) begin
        // clk counter
        if (clk_cnt == DELAY_1S) begin
            clk_cnt <= 32'b0;
        end 
        else begin
            clk_cnt <= clk_cnt + 32'b1;
        end
    end 
end

// key_prev, that value from delay 1 clock cycle, driven by clk & reset
always @(posedge clk) begin
    // reset 
    if (rst_n == 1'b0) begin
        keys_prev <= 5'b1_1111;
    end 
    else begin
        keys_prev <= keys_stable;
    end 
end


// 状态机, 1) 状态转移，时序逻辑
always @(posedge clk) begin
    if (rst_n == 1'b0) begin
        current_state <= S_STOP;
    end 
    else begin
        current_state <= next_state;
    end 
end 

// 状态机, 2) 状态切换，组合逻辑
always @(*) begin
    if (current_state == S_STOP && key_press_down[5]) begin
        next_state = S_RUN;
    end 
    else if (current_state == S_RUN && key_press_down[5]) begin
        next_state = S_STOP;
    end 
    else begin
        next_state = current_state;
    end 
end

always @(*) begin
    seg_num_0 = clock_mm / 10;
    seg_num_1 = clock_mm % 10;
end 

always @(*) begin
    seg_num_2 = clock_ss / 10;
    seg_num_3 = clock_ss % 10;
end 

// 状态机, 3) 输出 clock_mm, 时序逻辑
// logic to update clock_mm, driven by clk & reset
always @(posedge clk) begin 
    // reset 
    if (rst_n == 1'b0) begin
        clock_mm <= 8'd0;
    end 
    else if (current_state == S_STOP && key_press_down[3]) begin
        if (clock_mm == 59) clock_mm <= 0;
        else clock_mm <= clock_mm + 1;
    end 
    else if (current_state == S_STOP && key_press_down[4]) begin
        if (clock_mm == 0) clock_mm <= 59;
        else clock_mm <= clock_mm - 1;
    end 
    else if (current_state == S_RUN && trigger1s) begin
        if (clock_ss == 0 && clock_mm > 0) clock_mm <= clock_mm - 1;
    end 
end 

// 状态机, 3) 输出 clock_ss, 时序逻辑
// logic to update clock_ss, driven by clk & reset
always @(posedge clk) begin 
    // reset 
    if (rst_n == 1'b0) begin
        clock_ss <= 8'd0;
    end 
    else if (current_state == S_STOP && key_press_down[1]) begin
        if (clock_ss == 59) clock_ss <= 0;
        else clock_ss <= clock_ss + 1;
    end 
    else if (current_state == S_STOP && key_press_down[2]) begin
        if (clock_ss == 0) clock_ss <= 59;
        else clock_ss <= clock_ss - 1;
    end 
    else if (current_state == S_RUN && trigger1s) begin
        if (!(clock_mm == 0 && clock_ss == 0)) begin
            if (clock_ss == 0) clock_ss <= 59;
            else clock_ss <= clock_ss - 1;
        end
    end 
end  

endmodule
