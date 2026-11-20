`timescale 1ns / 1ns

module seg_display
#(
    parameter CLK_FREQ = 50_000_000 // clock frequency(hz), 50 MHz, T = 1/50 = 0.02 us
) 
(
    input wire                   clk,              // clock input
	input wire                   rst_n,            // asynchronous reset input, low active 
    input wire [3:0]             seg_num_0,        // display number for segment digital tube #0 
	input wire [3:0]             seg_num_1,        // display number for segment digital tube #1
    input wire [3:0]             seg_num_2,        // display number for segment digital tube #2 
    input wire [3:0]             seg_num_3,        // display number for segment digital tube #3 
    output reg [3:0]             seg_sel = 0,      // digital led chip select
	output reg [7:0]             seg_data = 0      // eight segment digital tube output, MSB is the decimal point
);

// define state code for seg_sel
parameter SEG_SEL_NULL  = 4'b0000 ;
parameter SEG_SEL_0     = 4'b0001 ;
parameter SEG_SEL_1     = 4'b0010 ;
parameter SEG_SEL_2     = 4'b0100 ;
parameter SEG_SEL_3     = 4'b1000 ;

// 计数器, 触发 scan_next 信号
parameter SCAN_CLK_CNT = CLK_FREQ / 1000 - 1;

reg [31:0] clk_cnt = 32'b0;
assign scan_next = (clk_cnt == SCAN_CLK_CNT);

always @(posedge clk) begin
    // reset 
    if (rst_n == 1'b0) begin
        clk_cnt <= 32'b0;
    end 
    else begin
        // clk counter
        if (clk_cnt == SCAN_CLK_CNT) begin
            clk_cnt <= 32'b0;
        end 
        else begin
            clk_cnt <= clk_cnt + 32'b1;
        end
    end 
end


// 状态机 for seg_sel
reg [3:0] next_seg_sel = SEG_SEL_NULL;

// 状态机 for seg_sel, 1) 状态转移，时序逻辑
always @(posedge clk) begin
    if (rst_n == 1'b0) begin
        seg_sel <= SEG_SEL_NULL;
    end 
    else begin
        seg_sel <= next_seg_sel;
    end 
end 

// 状态机 for seg_sel, 2) 状态切换，组合逻辑
always @(*) begin
    if (scan_next) begin
        next_seg_sel = (seg_sel == SEG_SEL_3 || seg_sel == SEG_SEL_NULL) ? SEG_SEL_0 : seg_sel << 1;
    end 
    else begin
        next_seg_sel = seg_sel;
    end 
end

// 状态机 for seg_sel, 3) 输出 sel_num & seg_data
reg [3:0] sel_num = 4'b0;

always @(*) begin
    case (seg_sel)
        SEG_SEL_0: sel_num = seg_num_0;
        SEG_SEL_1: sel_num = seg_num_1;
        SEG_SEL_2: sel_num = seg_num_2;
        SEG_SEL_3: sel_num = seg_num_3;
        default: sel_num = 4'b0000;
    endcase
end

// combination logic, output: seg_data, based on sel_num
always @(*) begin
	case (sel_num)
		4'd0: seg_data = 8'b1100_0000;
		4'd1: seg_data = 8'b1111_1001;
		4'd2: seg_data = 8'b1010_0100;
		4'd3: seg_data = 8'b1011_0000;
		4'd4: seg_data = 8'b1001_1001;
		4'd5: seg_data = 8'b1001_0010;
		4'd6: seg_data = 8'b1000_0010;
		4'd7: seg_data = 8'b1111_1000;
		4'd8: seg_data = 8'b1000_0000;
		4'd9: seg_data = 8'b1001_0000;
		4'ha: seg_data = 8'b1000_1000;
		4'hb: seg_data = 8'b1000_0011;
		4'hc: seg_data = 8'b1100_0110;
		4'hd: seg_data = 8'b1010_0001;
		4'he: seg_data = 8'b1000_0110;
		4'hf: seg_data = 8'b1000_1110;
		default:seg_data = 8'b1100_0000;
	endcase
end

endmodule
