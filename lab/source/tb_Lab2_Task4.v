`timescale 1ns/1ps

module tb_Lab2_Task4(

  );

  reg [1:0] mode;
  reg signed [7:0] current_temperature;
  reg signed [7:0] target_temperature;
  reg [1:0] air_condition;
  reg [1:0] heater;

  Lab2_Task4 uut (
               .mode                	(mode                 ),
               .current_temperature 	(current_temperature  ),
               .target_temperature  	(target_temperature   ),
               .air_condition       	(air_condition        ),
               .heater              	(heater               )
             );

  initial begin
    // 1. All off mode
    mode = 2'd0;
    current_temperature = 8'sd25;
    target_temperature  = 8'sd20;
    #100;

    // 2. Heating: high power boundary
    mode = 2'd1;
    current_temperature = 8'sd15;
    target_temperature  = 8'sd20;
    #100;

    // 3. Heating: low power boundary
    mode = 2'd1;
    current_temperature = 8'sd16;
    target_temperature  = 8'sd20;
    #100;

    // 4. Heating: equal temperature
    mode = 2'd1;
    current_temperature = 8'sd20;
    target_temperature  = 8'sd20;
    #100;

    // 5. Heating: current > target
    mode = 2'd1;
    current_temperature = 8'sd25;
    target_temperature  = 8'sd20;
    #100;

    // 6. Cooling: high power boundary
    mode = 2'd2;
    current_temperature = 8'sd25;
    target_temperature  = 8'sd20;
    #100;

    // 7. Cooling: low power boundary
    mode = 2'd2;
    current_temperature = 8'sd24;
    target_temperature  = 8'sd20;
    #100;

    // 8. Cooling: equal temperature
    mode = 2'd2;
    current_temperature = 8'sd20;
    target_temperature  = 8'sd20;
    #100;

    // 9. Cooling: current < target
    mode = 2'd2;
    current_temperature = 8'sd15;
    target_temperature  = 8'sd20;
    #100;

    // 10. Automatic: equal
    mode = 2'd3;
    current_temperature = 8'sd20;
    target_temperature  = 8'sd20;
    #100;

    // 11. Automatic: +1, AC low
    current_temperature = 8'sd21;
    target_temperature  = 8'sd20;
    #100;

    // 12. Automatic: +4, AC low
    current_temperature = 8'sd24;
    target_temperature  = 8'sd20;
    #100;

    // 13. Automatic: +5, AC high
    current_temperature = 8'sd25;
    target_temperature  = 8'sd20;
    #100;

    // 14. Automatic: -1, heater low
    current_temperature = 8'sd19;
    target_temperature  = 8'sd20;
    #100;

    // 15. Automatic: -4, heater low
    current_temperature = 8'sd16;
    target_temperature  = 8'sd20;
    #100;

    // 16. Automatic: -5, heater high
    current_temperature = 8'sd15;
    target_temperature  = 8'sd20;
    #100;

    // 17. Negative temperature: heater high
    mode = 2'd3;
    current_temperature = -8'sd15;
    target_temperature  = -8'sd10;
    #100;

    // 18. Negative temperature: AC high
    current_temperature = -8'sd5;
    target_temperature  = -8'sd10;
    #100;

    $finish;
  end

  initial begin
    $dumpfile("tb_Lab2_Task4.vcd");
    $dumpvars;
  end

endmodule
