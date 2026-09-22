module Lab2_Task4(
    input [1:0] mode,
    input signed [7:0] current_temperature,
    input signed [7:0] target_temperature,
    output reg [1:0] air_condition,
    output reg [1:0] heater
  );

  reg signed [7:0] diff_temperature;
  reg [7:0] abs_diff_temperature;

  always @(*) begin
    diff_temperature = current_temperature - target_temperature;
    case (mode)
      0: begin
        air_condition = 0;
        heater = 0;
      end
      1: begin
        air_condition = 0;
        heater = (diff_temperature <= - 5) ? 2 : 1;
      end
      2: begin
        heater = 0;
        air_condition = (diff_temperature >= 5) ? 2 : 1;
      end
      3: begin
        abs_diff_temperature = diff_temperature < 0 ? ~diff_temperature + 1 : diff_temperature;
        if (abs_diff_temperature < 1) begin
          air_condition = 0;
          heater = 0;
        end
        else if (diff_temperature >= 1) begin
          heater = 0;
          air_condition = (diff_temperature >= 5) ? 2 : 1;
        end
        else if (diff_temperature <= -1) begin
          air_condition = 0;
          heater = (diff_temperature <= - 5) ? 2 : 1;
        end
        else begin
          air_condition = 0;
          heater = 0;
        end
      end
      default: begin
        air_condition = 0;
        heater = 0;
      end
    endcase
  end

endmodule
