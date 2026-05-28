module top (
    // Clock and Reset
    input logic clock,
    input logic resetn, 
    // LEDs and switches
    output logic [15:0] leds,
    input logic [15:0] switches,
    input logic [4:0] buttons,
    output logic tx,
    // VGA Outputs (New)
    output logic [3:0] vga_red,
    output logic [3:0] vga_green,
    output logic [3:0] vga_blue,
    output logic hsync,
    output logic vsync
);

// Parameters
localparam AW = 32;
localparam DW = 32;
localparam NUM_PERIPHERALS = 64;
localparam NUM_REG_PERIPHERAL = 32;

// IO bus signals
logic [31:0] io_address;
logic io_addr_strobe;
logic [31:0] io_write_data;
logic io_write_strobe;
logic [3:0] io_byte_enable;
logic [31:0] io_read_data;
logic io_read_strobe;
logic io_ready;

logic reset;
assign reset = ~resetn;

// MicroBlaze instantiation
microblaze_mcs_0 your_instance_name (
  .Clk(clock),
  .Reset(reset),
  .IO_addr_strobe(io_addr_strobe),
  .IO_address(io_address),
  .IO_byte_enable(io_byte_enable),
  .IO_read_data(io_read_data),
  .IO_read_strobe(io_read_strobe),
  .IO_ready(io_ready),
  .IO_write_data(io_write_data),
  .IO_write_strobe(io_write_strobe)
);

// APB master signals
logic [AW-1:0] MpADDR;
logic [DW-1:0] MpWDATA;
logic MpSELx;
logic MpWRITE;
logic MpREADY;
logic MpENABLE;
logic MpSLVERR;
logic [DW-1:0] MpRDATA;

// APB Bridge
APB_Bridge #(
    .DW(DW),
    .AW(AW),
    .BRG_BASE(32'hC0000000)
) apb_bridge_inst (
    .CLK(clock),
    .RESETn(resetn),
    .io_address(io_address),
    .io_addr_strobe(io_addr_strobe),
    .io_write_data(io_write_data),
    .io_write_strobe(io_write_strobe),
    .io_byte_enable(io_byte_enable),
    .io_read_data(io_read_data),
    .io_read_strobe(io_read_strobe),
    .io_ready(io_ready),
    .pADDR(MpADDR),
    .pSELx(MpSELx),
    .pENABLE(MpENABLE),
    .pWRITE(MpWRITE),
    .pWDATA(MpWDATA),
    .pRDATA(MpRDATA),
    .pREADY(MpREADY),
    .pSLVERR(MpSLVERR)
);

// APB Interconnect signals
logic [AW-1:0] SpADDR [NUM_PERIPHERALS-1:0];
logic [NUM_PERIPHERALS-1:0] SpSEL;
logic [NUM_PERIPHERALS-1:0] SpENABLE;
logic [NUM_PERIPHERALS-1:0] SpWRITE;
logic [DW-1:0] SpWDATA [NUM_PERIPHERALS-1:0];
logic [DW-1:0] SpRDATA [NUM_PERIPHERALS-1:0];
logic [NUM_PERIPHERALS-1:0] SpREADY;
logic [NUM_PERIPHERALS-1:0] SpSLVERR;

APB_interconnect #(
    .DW(DW),
    .AW(AW),
    .NUM_PERIPHERALS(NUM_PERIPHERALS),
    .NUM_REG_PERIPHERAL(NUM_REG_PERIPHERAL)
) u_apb_interconnect (
    .MpADDR(MpADDR),
    .MpSELx(MpSELx),
    .MpENABLE(MpENABLE),
    .MpWRITE(MpWRITE),
    .MpWDATA(MpWDATA),
    .MpRDATA(MpRDATA),
    .MpREADY(MpREADY),
    .MpSLVERR(MpSLVERR),
    .SpADDR(SpADDR),
    .SpSEL(SpSEL),
    .SpENABLE(SpENABLE),
    .SpWRITE(SpWRITE),
    .SpWDATA(SpWDATA),
    .SpRDATA(SpRDATA),
    .SpREADY(SpREADY),
    .SpSLVERR(SpSLVERR)
);

// --- Peripheral Instantiations ---

// Slave 0: GPIO (Base: 0xC0000000)
APB_gpio u_apb_gpio (
    .pCLK(clock),
    .pRESETn(resetn),
    .pADDR(SpADDR[0]),
    .pSEL(SpSEL[0]),
    .pENABLE(SpENABLE[0]),
    .pWRITE(SpWRITE[0]),
    .pWDATA(SpWDATA[0]),
    .pRDATA(SpRDATA[0]),
    .pREADY(SpREADY[0]),
    .pSLVERR(SpSLVERR[0]),
    .switch(switches),
    .button(buttons),
    .led(leds)
);

// Slave 1: Timer (Base: 0xC0000080)
APB_timer u_apb_timer (
    .pCLK(clock),
    .pRESETn(resetn),
    .pADDR(SpADDR[1]),
    .pSEL(SpSEL[1]),
    .pENABLE(SpENABLE[1]),
    .pWRITE(SpWRITE[1]),
    .pWDATA(SpWDATA[1]),
    .pRDATA(SpRDATA[1]),
    .pREADY(SpREADY[1]),
    .pSLVERR(SpSLVERR[1])
);

// Slave 2: UART (Base: 0xC0000100)
APB_uart u_apb_uart (
    .pCLK(clock),
    .pRESETn(resetn),
    .pADDR(SpADDR[2]),
    .pSEL(SpSEL[2]),
    .pENABLE(SpENABLE[2]),
    .pWRITE(SpWRITE[2]),
    .pWDATA(SpWDATA[2]),
    .pRDATA(SpRDATA[2]),
    .pREADY(SpREADY[2]),
    .pSLVERR(SpSLVERR[2]),
    .tx(tx)
);

// Slave 3: Video Subsystem (Base: 0xC0000180)
APB_video_subsystem u_apb_video (
    .pCLK(clock),
    .pRESETn(resetn),
    .pADDR(SpADDR[3]),
    .pSEL(SpSEL[3]),
    .pENABLE(SpENABLE[3]),
    .pWRITE(SpWRITE[3]),
    .pWDATA(SpWDATA[3]),
    .pRDATA(SpRDATA[3]),
    .pREADY(SpREADY[3]),
    .pSLVERR(SpSLVERR[3]),
    .vga_red(vga_red),
    .vga_green(vga_green),
    .vga_blue(vga_blue),
    .hsync(hsync),
    .vsync(vsync)
);

// Unused peripherals
genvar i;
for (i = 4; i < NUM_PERIPHERALS; i++) begin : gen_unused
    assign SpSLVERR[i] = 1;
    assign SpREADY[i] = 1; // Change to 1 so the bus doesn't hang if accessed
    assign SpRDATA[i] = 32'hFFFFFFFF;
end

endmodule