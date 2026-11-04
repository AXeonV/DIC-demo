module top #(
    parameter CLK_FREQ = 32'd50_000_000
) (
    input clk,
    input rstn,
    input [3:0] sw,
    output reg [7:0] led
);

//==========================================================================
// wire and reg in the module
//==========================================================================

    wire    [4:0]  alu_function;
    wire    [7:0]  alu_data1;
    wire    [7:0]  alu_data2;

    wire    [7:0]  alu_result;
    wire    [7:0]  alu_err;

    wire    [7:0]  expected_result;
    wire           expected_err;

    wire           stop;
    wire           pass; 
    reg     [7:0]  passed_count = 8'd0;

//==========================================================================
// ALU instance
//==========================================================================

    alu u_alu (
        .f(alu_function),
        .a(alu_data1), 
        .b(alu_data2),
        .s(alu_result),
        .err(alu_err)
    );

//==========================================================================
// Inputs Generator & Outputs Checker
//==========================================================================

    assign pass = ((alu_result == expected_result) && (alu_err == expected_err)) ? 1'b1 : 1'b0;

    reg         rd_wait;
    wire [31:0] rd_data; 

    Testcase u_testcase (
        .addr(passed_count),  // input [7:0]
        .clk(clk),            // input
        .rst(~rstn),          // input
        .wr_data(32'b0),      // input [31:0]
        .wr_en(1'b0),        // input
        .rd_data(rd_data)     // output [31:0]
    );

    assign stop            = (rd_data[6:5] == 2'b11) || (passed_count == 8'd255) ? 1'b1 : 1'b0;

    assign alu_function    = rd_data[4:0];
    assign alu_data1       = rd_data[15:8];
    assign alu_data2       = rd_data[23:16];

    assign expected_err    = rd_data[7];
    assign expected_result = rd_data[31:24];

    always @(posedge clk) begin
        if (~rstn) begin 
            passed_count <= 8'd0;
            rd_wait <= 1'b1;
        end
        else if (rd_wait) begin
            rd_wait <= 1'b0;
        end
        else if (~stop & pass) begin
            passed_count <= passed_count + 8'd1;
            rd_wait <= 1'b1;
        end 
    end


//==========================================================================
// LED indicator 
//==========================================================================
    
    always @(*) begin
        case (sw)
            4'b0001: begin
                led = alu_result;
            end
            4'b0010: begin
                led = {alu_err, 2'b0, alu_function};
            end
            4'b0100: begin
                led = alu_data2;
            end
            4'b1000: begin
                led = alu_data1;
            end
            default: begin
                led = passed_count;
            end 
        endcase
    end

endmodule