`timescale 1us / 1us

module tb_seg_clock;

	// Internal signals
	reg clk = 1'b0;
	reg rst_n = 1'b0;
	reg  [5:1] keys = 5'h1f;
	wire [3:0] seg_sel;
	wire [7:0] seg_data;

	// Instantiate the Unit Under Test (UUT)
	seg_clock #(
		.CLK_FREQ(5000)
	) uut (
		.clk(clk), 
		.rst_n(rst_n),
		.keys(keys),
		.seg_sel(seg_sel),
		.seg_data(seg_data)
	);

	initial begin

	  // Initialize Inputs
	  rst_n = 1'b0;
	  #10;
	  rst_n = 1'b1;
	  #10;

	  // press down, key 3
		keys = 5'b1_1011;
		#5000;
		keys = 5'h1f;
		#5000;

	  // press down, key 5
		keys = 5'h0f;
		#5000;
		keys = 5'h1f;
		#5000;

		#1000000; // 20s
    $finish;
	end
	
	always #10 clk = ~clk;

  // 输出波形文件
  initial begin
    $dumpfile("tb_Lab4_Seg_Clock.vcd");
    $dumpvars;
    $display("save to tb_Lab4_Seg_Clock.vcd");
  end 
      
endmodule
